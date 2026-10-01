/**
 * Button Trigger - implementation (XCB + XInput2)
 */

#include "nerd_dictation/trigger/button_trigger.hpp"

#include <xcb/xcb.h>
#include <xcb/xinput.h>

#include <poll.h>

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <utility>
#include <vector>

namespace nerd_dictation {
namespace trigger {

namespace {

// X core button number -> Linux evdev code (linux/input-event-codes.h)
int linux_code_for_x_button(int button) {
    switch (button) {
        case 1: return 0x110;  // BTN_LEFT
        case 2: return 0x112;  // BTN_MIDDLE
        case 3: return 0x111;  // BTN_RIGHT
        case 8: return 0x113;  // BTN_SIDE
        case 9: return 0x114;  // BTN_EXTRA
        default: return -1;
    }
}

}  // namespace

bool device_has_button(const std::string& sysfs_input_root,
                       const std::string& device_node,
                       int linux_code) {
    if (linux_code < 0) {
        return false;
    }

    // "/dev/input/event14" -> "event14"
    const auto slash = device_node.rfind('/');
    const std::string node = device_node.substr(slash == std::string::npos ? 0 : slash + 1);
    if (node.rfind("event", 0) != 0) {
        return false;
    }

    std::ifstream f(sysfs_input_root + "/" + node + "/device/capabilities/key");
    if (!f) {
        return false;
    }

    std::vector<std::string> words;
    std::string w;
    while (f >> w) {
        words.push_back(w);
    }
    if (words.empty()) {
        return false;
    }

    // The kernel prints the bitmap as unsigned longs, most significant first.
    // This binary runs on the same machine as the kernel, so the widths match.
    constexpr int kBitsPerWord = sizeof(unsigned long) * 8;
    const size_t index_from_low = static_cast<size_t>(linux_code) / kBitsPerWord;
    if (index_from_low >= words.size()) {
        return false;
    }

    // The kernel prints bare lowercase hex. Anything else (sign, "0x",
    // overflow) is malformed — strtoul alone would accept "-1" as all ones.
    for (const std::string& word : words) {
        if (word.size() > kBitsPerWord / 4 ||
            word.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos) {
            return false;
        }
    }
    const std::string& word = words[words.size() - 1 - index_from_low];
    const unsigned long value = std::strtoul(word.c_str(), nullptr, 16);
    return (value >> (linux_code % kBitsPerWord)) & 1UL;
}

ButtonTrigger::ButtonTrigger(int button, std::string sysfs_input_root, int reprobe_ms)
    : button_(button), sysfs_root_(std::move(sysfs_input_root)), reprobe_ms_(reprobe_ms) {}

ButtonTrigger::~ButtonTrigger() {
    disconnect();
}

bool ButtonTrigger::connected() const {
    return conn_ != nullptr;
}

void ButtonTrigger::disconnect() {
    if (conn_) {
        xcb_disconnect(conn_);
        conn_ = nullptr;
    }
    devices_.clear();
}

bool ButtonTrigger::connect() {
    disconnect();
    error_.clear();

    if (linux_code_for_x_button(button_) < 0) {
        error_ = "unsupported button " + std::to_string(button_) +
                 " (supported: 1, 2, 3, 8, 9)";
        return false;
    }

    int screen_num = 0;
    conn_ = xcb_connect(nullptr, &screen_num);
    if (xcb_connection_has_error(conn_)) {
        const char* display = std::getenv("DISPLAY");
        error_ = std::string("cannot connect to X display ") + (display ? display : "(DISPLAY unset)");
        disconnect();
        return false;
    }

    const xcb_query_extension_reply_t* ext = xcb_get_extension_data(conn_, &xcb_input_id);
    if (!ext || !ext->present) {
        error_ = "X server has no XInputExtension";
        disconnect();
        return false;
    }
    xi_opcode_ = ext->major_opcode;

    auto* ver = xcb_input_xi_query_version_reply(
        conn_, xcb_input_xi_query_version(conn_, 2, 0), nullptr);
    const bool xi2 = ver && ver->major_version >= 2;
    free(ver);
    if (!xi2) {
        error_ = "X server does not support XInput 2";
        disconnect();
        return false;
    }

    // Only devices driven by libinput/evdev carry this property; a device
    // without it (e.g. XTEST) is never physical. Create the atom rather than
    // only look it up: if no driver had set it yet, a lookup would return
    // None for the whole connection and misclassify devices plugged later.
    static const char kDeviceNode[] = "Device Node";
    auto* atom = xcb_intern_atom_reply(
        conn_, xcb_intern_atom(conn_, 0, sizeof(kDeviceNode) - 1, kDeviceNode), nullptr);
    device_node_atom_ = atom ? atom->atom : static_cast<xcb_atom_t>(XCB_ATOM_NONE);
    free(atom);

    const xcb_setup_t* setup = xcb_get_setup(conn_);
    xcb_screen_iterator_t it = xcb_setup_roots_iterator(setup);
    for (int i = 0; i < screen_num && it.rem; ++i) {
        xcb_screen_next(&it);
    }
    if (!it.rem) {
        error_ = "X screen " + std::to_string(screen_num) + " not found";
        disconnect();
        return false;
    }
    const xcb_window_t root = it.data->root;
    root_ = root;

    // Re-classify devices when one is plugged or removed
    struct {
        xcb_input_event_mask_t head;
        uint32_t mask;
    } hierarchy = {{XCB_INPUT_DEVICE_ALL, 1}, XCB_INPUT_XI_EVENT_MASK_HIERARCHY};
    xcb_generic_error_t* err = xcb_request_check(
        conn_, xcb_input_xi_select_events_checked(conn_, root, 1, &hierarchy.head));
    if (err) {
        error_ = "XISelectEvents(hierarchy) failed, X error " + std::to_string(err->error_code);
        free(err);
        disconnect();
        return false;
    }

    // A foreign CORE passive grab (xbindkeys uses XGrabButton) does not
    // conflict with an XI2 grab: both would hold the button and the server
    // would deliver each press to whichever grab it happens to find first.
    // The core protocol fails GrabButton with AnyModifier (BadAccess) when
    // another client grabs this button with any modifiers, so probe with a
    // core grab and release it before taking the XI2 one.
    if (foreign_core_grab()) {
        disconnect();
        return false;
    }

    // Asynchronous grab: every press of the button is consumed, whatever the
    // source — synthesized clicks must reach neither dictation nor the app.
    const uint32_t mask = XCB_INPUT_XI_EVENT_MASK_BUTTON_PRESS |
                          XCB_INPUT_XI_EVENT_MASK_BUTTON_RELEASE;
    const uint32_t modifiers = XCB_INPUT_MODIFIER_MASK_ANY;
    auto* grab = xcb_input_xi_passive_grab_device_reply(
        conn_,
        xcb_input_xi_passive_grab_device(
            conn_, XCB_CURRENT_TIME, root, XCB_NONE, button_,
            XCB_INPUT_DEVICE_ALL_MASTER, 1, 1, XCB_INPUT_GRAB_TYPE_BUTTON,
            XCB_INPUT_GRAB_MODE_22_ASYNC, XCB_INPUT_GRAB_MODE_22_ASYNC,
            0 /* owner_events */, &mask, &modifiers),
        &err);
    if (!grab) {
        error_ = "XIGrabButton failed" +
                 (err ? ", X error " + std::to_string(err->error_code) : std::string());
        free(err);
        disconnect();
        return false;
    }
    // The reply lists the modifier combinations that could NOT be grabbed
    const bool refused = grab->num_modifiers > 0;
    free(grab);
    if (refused) {
        error_ = "button " + std::to_string(button_) +
                 " is already grabbed by another X client (xbindkeys?)";
        disconnect();
        return false;
    }

    if (!refresh_devices()) {
        disconnect();
        return false;
    }
    next_probe_ = std::chrono::steady_clock::now() + std::chrono::milliseconds(reprobe_ms_);
    return true;
}

bool ButtonTrigger::foreign_core_grab() {
    xcb_generic_error_t* err = xcb_request_check(
        conn_, xcb_grab_button_checked(conn_, 0, root_, XCB_EVENT_MASK_BUTTON_PRESS,
                                       XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC,
                                       XCB_NONE, XCB_NONE, button_, XCB_MOD_MASK_ANY));
    if (err) {
        const bool taken = err->error_code == XCB_ACCESS;
        error_ = taken ? "button " + std::to_string(button_) +
                             " is already grabbed by another X client (xbindkeys?)"
                       : "core grab probe failed, X error " + std::to_string(err->error_code);
        free(err);
        return true;
    }
    // UngrabButton only ever releases this client's own grabs
    xcb_ungrab_button(conn_, button_, root_, XCB_MOD_MASK_ANY);
    return false;
}

bool ButtonTrigger::refresh_devices() {
    devices_.clear();

    auto* reply = xcb_input_xi_query_device_reply(
        conn_, xcb_input_xi_query_device(conn_, XCB_INPUT_DEVICE_ALL), nullptr);
    if (!reply) {
        error_ = "XIQueryDevice failed";
        return false;
    }

    const int code = linux_code_for_x_button(button_);
    for (auto it = xcb_input_xi_query_device_infos_iterator(reply); it.rem;
         xcb_input_xi_device_info_next(&it)) {
        const xcb_input_xi_device_info_t* info = it.data;
        if (info->type != XCB_INPUT_DEVICE_TYPE_SLAVE_POINTER) {
            continue;
        }

        DeviceInfo dev;
        dev.name.assign(xcb_input_xi_device_info_name(info), info->name_len);

        if (device_node_atom_ != XCB_ATOM_NONE) {
            auto* prop = xcb_input_xi_get_property_reply(
                conn_,
                xcb_input_xi_get_property(conn_, info->deviceid, 0, device_node_atom_,
                                          XCB_ATOM_ANY, 0, 256),
                nullptr);
            if (prop && prop->format == 8 && prop->num_items > 0) {
                const char* data =
                    static_cast<const char*>(xcb_input_xi_get_property_items(prop));
                std::string node(data, prop->num_items);
                node.erase(node.find_last_not_of('\0') + 1);
                dev.physical = device_has_button(sysfs_root_, node, code);
            }
            free(prop);
        }

        devices_[info->deviceid] = dev;
    }
    free(reply);
    return true;
}

TriggerEvent ButtonTrigger::classify_press(uint16_t sourceid) {
    auto it = devices_.find(sourceid);
    if (it == devices_.end()) {
        // Hierarchy event may still be in flight
        refresh_devices();
        it = devices_.find(sourceid);
    }
    if (it == devices_.end()) {
        last_source_ = "unknown device " + std::to_string(sourceid);
        return TriggerEvent::Rejected;
    }
    last_source_ = it->second.name;
    return it->second.physical ? TriggerEvent::Pressed : TriggerEvent::Rejected;
}

TriggerEvent ButtonTrigger::wait(int timeout_ms) {
    if (!conn_) {
        return TriggerEvent::Disconnected;
    }

    // A client that grabs the button after us (xbindkeys started later in the
    // session) coexists silently with an XI2 grab and may receive the press.
    // Re-probe periodically and give up the button loudly: the caller logs the
    // reason on reconnect and retries until the other client goes away.
    const auto now = std::chrono::steady_clock::now();
    if (now >= next_probe_) {
        next_probe_ = now + std::chrono::milliseconds(reprobe_ms_);
        if (foreign_core_grab()) {
            disconnect();
            return TriggerEvent::Disconnected;
        }
    }

    for (int round = 0; round < 2; ++round) {
        while (xcb_generic_event_t* ev = xcb_poll_for_event(conn_)) {
            TriggerEvent result = TriggerEvent::None;
            if ((ev->response_type & ~0x80) == XCB_GE_GENERIC) {
                auto* ge = reinterpret_cast<xcb_ge_generic_event_t*>(ev);
                if (ge->extension == xi_opcode_) {
                    if (ge->event_type == XCB_INPUT_BUTTON_PRESS) {
                        auto* bp = reinterpret_cast<xcb_input_button_press_event_t*>(ev);
                        if (static_cast<int>(bp->detail) == button_) {
                            result = classify_press(bp->sourceid);
                        }
                    } else if (ge->event_type == XCB_INPUT_HIERARCHY) {
                        refresh_devices();
                    }
                }
            }
            free(ev);
            if (result != TriggerEvent::None) {
                return result;
            }
        }

        if (xcb_connection_has_error(conn_)) {
            error_ = "X connection lost";
            disconnect();
            return TriggerEvent::Disconnected;
        }
        if (round == 1) {
            break;
        }

        pollfd pfd{xcb_get_file_descriptor(conn_), POLLIN, 0};
        if (poll(&pfd, 1, timeout_ms) <= 0) {
            return TriggerEvent::None;
        }
    }
    return TriggerEvent::None;
}

}  // namespace trigger
}  // namespace nerd_dictation
