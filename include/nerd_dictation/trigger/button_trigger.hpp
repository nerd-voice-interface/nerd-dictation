/**
 * Button Trigger - mouse button that toggles dictation, owned by the daemon
 *
 * Grabs one pointer button on the X root window via XInput2 and reports a
 * press only when it comes from a device that physically has that button.
 *
 * Why the source device matters: libinput synthesizes a middle click on
 * touchpads (three-finger tap / clickfinger) although they have no middle
 * button at all. A core-level binding (xbindkeys b:2) cannot tell these
 * apart from a real press and starts dictation on a stray palm touch.
 * XInput2 events carry `sourceid`, the physical slave device; its evdev
 * node tells whether the button really exists in hardware.
 *
 * XCB rather than Xlib: Xlib's default I/O error handler calls exit(), so
 * a restart of the X server would kill the whole dictation daemon. XCB
 * reports a broken connection through xcb_connection_has_error().
 *
 * Not thread-safe: the object is owned by a single thread.
 */

#pragma once

#include <chrono>
#include <cstdint>
#include <map>
#include <string>

struct xcb_connection_t;

namespace nerd_dictation {
namespace trigger {

enum class TriggerEvent {
    None,          // timeout, or an event that is not ours
    Pressed,       // press from a device that physically has the button
    Rejected,      // press that the device synthesized (e.g. touchpad tap)
    Disconnected,  // X connection lost; connect() again
};

/**
 * Does the evdev device behind `device_node` physically report `linux_code`?
 *
 * Reads <sysfs_input_root>/<eventN>/device/capabilities/key: hex words of
 * 64 bits, most significant word first. Any malformed input yields false.
 */
bool device_has_button(const std::string& sysfs_input_root,
                       const std::string& device_node,
                       int linux_code);

class ButtonTrigger {
public:
    /// X button number (2 = middle). sysfs_input_root is overridable for tests.
    explicit ButtonTrigger(int button,
                           std::string sysfs_input_root = "/sys/class/input",
                           int reprobe_ms = 5000);
    ~ButtonTrigger();

    ButtonTrigger(const ButtonTrigger&) = delete;
    ButtonTrigger& operator=(const ButtonTrigger&) = delete;

    /// Connect to $DISPLAY, require XInput 2, grab the button.
    /// On failure the reason is in get_error().
    bool connect();
    bool connected() const;

    /// Wait up to timeout_ms for the next relevant event.
    TriggerEvent wait(int timeout_ms);

    /// Name of the device that produced the last Pressed/Rejected event
    std::string last_source() const { return last_source_; }
    std::string get_error() const { return error_; }
    int button() const { return button_; }

private:
    struct DeviceInfo {
        std::string name;
        bool physical = false;
    };

    void disconnect();
    bool refresh_devices();
    bool foreign_core_grab();
    TriggerEvent classify_press(uint16_t sourceid);

    int button_;
    std::string sysfs_root_;
    int reprobe_ms_;
    xcb_connection_t* conn_ = nullptr;
    uint32_t root_ = 0;
    std::chrono::steady_clock::time_point next_probe_;
    uint8_t xi_opcode_ = 0;
    uint32_t device_node_atom_ = 0;
    std::map<uint16_t, DeviceInfo> devices_;
    std::string last_source_;
    std::string error_;
};

}  // namespace trigger
}  // namespace nerd_dictation
