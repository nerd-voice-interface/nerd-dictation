/**
 * Tests for the daemon-owned mouse button trigger.
 *
 * device_has_button() is checked on a fixture built from the real capability
 * masks of this ThinkPad (touchpad: no BTN_MIDDLE, TrackPoint: has it) and on
 * live sysfs when those devices are present.
 *
 * ButtonTrigger is checked against a private Xvfb server:
 *  - a click injected through XTEST (xdotool) has no evdev node behind it,
 *    so it must be Rejected, never Pressed;
 *  - a second client cannot steal a grab that is already held, and says why;
 *  - when the X server dies, wait() reports Disconnected and the process
 *    survives (Xlib would have called exit()), and connect() works again
 *    once the server is back.
 */

#include <gtest/gtest.h>

#include "nerd_dictation/trigger/button_trigger.hpp"

#include <xcb/xcb.h>
#include <xcb/xinput.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

using namespace nerd_dictation::trigger;
namespace fs = std::filesystem;

namespace {

constexpr int BTN_LEFT_CODE = 0x110;
constexpr int BTN_MIDDLE_CODE = 0x112;

// Real masks read from /sys/class/input/event{13,14}/device/capabilities/key
constexpr const char* kTouchpadMask = "e520 10000 0 0 0 0";
constexpr const char* kTrackPointMask = "70000 0 0 0 0";

class SysfsFixture : public ::testing::Test {
protected:
    void SetUp() override {
        root_ = fs::temp_directory_path() /
                ("nd-sysfs-" + std::to_string(::getpid()));
        fs::create_directories(root_);
    }
    void TearDown() override { fs::remove_all(root_); }

    void add(const std::string& node, const std::string& mask) {
        const fs::path dir = root_ / node / "device" / "capabilities";
        fs::create_directories(dir);
        std::ofstream(dir / "key") << mask << "\n";
    }

    fs::path root_;
};

}  // namespace

TEST_F(SysfsFixture, TouchpadHasNoPhysicalMiddleButton) {
    add("event13", kTouchpadMask);
    EXPECT_FALSE(device_has_button(root_, "/dev/input/event13", BTN_MIDDLE_CODE));
    EXPECT_TRUE(device_has_button(root_, "/dev/input/event13", BTN_LEFT_CODE));
}

TEST_F(SysfsFixture, TrackPointHasPhysicalMiddleButton) {
    add("event14", kTrackPointMask);
    EXPECT_TRUE(device_has_button(root_, "/dev/input/event14", BTN_MIDDLE_CODE));
}

TEST_F(SysfsFixture, MalformedInputIsNeverPhysical) {
    add("event1", "zz 10000");          // not hex
    add("event2", "");                  // empty
    add("event3", "70000");             // one word; bit 274 lives in word 4
    EXPECT_FALSE(device_has_button(root_, "/dev/input/event1", BTN_MIDDLE_CODE));
    EXPECT_FALSE(device_has_button(root_, "/dev/input/event2", BTN_MIDDLE_CODE));
    EXPECT_FALSE(device_has_button(root_, "/dev/input/event3", BTN_MIDDLE_CODE));
    EXPECT_FALSE(device_has_button(root_, "/dev/input/event99", BTN_MIDDLE_CODE));
    EXPECT_FALSE(device_has_button(root_, "/dev/input/mouse0", BTN_MIDDLE_CODE));
    EXPECT_FALSE(device_has_button(root_, "", BTN_MIDDLE_CODE));
    add("event4", kTrackPointMask);
    EXPECT_FALSE(device_has_button(root_, "/dev/input/event4", -1));
}

// Five words reach the parser; each word must be bare hex as the kernel prints
TEST_F(SysfsFixture, NonHexWordsAreMalformed) {
    add("event5", "-1 0 0 0 0");                    // strtoul: ULONG_MAX
    add("event6", "0x70000 0 0 0 0");               // prefix
    add("event7", "11111111111111111 0 0 0 0");     // 17 digits: overflow
    add("event8", "70000 0 0 +0 0");                // sign in another word
    for (const char* n : {"event5", "event6", "event7", "event8"}) {
        EXPECT_FALSE(device_has_button(root_, std::string("/dev/input/") + n, BTN_MIDDLE_CODE)) << n;
    }
}

TEST(RealSysfs, ThisMachineTouchpadVsTrackPoint) {
    std::string touchpad, trackpoint;
    for (const auto& e : fs::directory_iterator("/sys/class/input")) {
        const std::string node = e.path().filename();
        if (node.rfind("event", 0) != 0) continue;
        std::ifstream f(e.path() / "device" / "name");
        std::string name;
        std::getline(f, name);
        if (name.find("Synaptics") != std::string::npos) touchpad = node;
        if (name.find("TrackPoint") != std::string::npos) trackpoint = node;
    }
    if (touchpad.empty() || trackpoint.empty()) {
        GTEST_SKIP() << "no Synaptics touchpad + TrackPoint on this machine";
    }
    EXPECT_FALSE(device_has_button("/sys/class/input", "/dev/input/" + touchpad, BTN_MIDDLE_CODE));
    EXPECT_TRUE(device_has_button("/sys/class/input", "/dev/input/" + trackpoint, BTN_MIDDLE_CODE));
}

// ---------------------------------------------------------------------------
// Against a private Xvfb
// ---------------------------------------------------------------------------

namespace {

class XvfbFixture : public ::testing::Test {
protected:
    static constexpr int kDisplay = 97;

    void SetUp() override {
        if (::access("/usr/bin/Xvfb", X_OK) != 0 && ::access("/usr/sbin/Xvfb", X_OK) != 0) {
            GTEST_SKIP() << "Xvfb not installed";
        }
        if (fs::exists(socket_path())) {
            GTEST_SKIP() << "display :" << kDisplay << " is busy";
        }
        saved_display_ = std::getenv("DISPLAY") ? std::getenv("DISPLAY") : "";
        ::setenv("DISPLAY", (":" + std::to_string(kDisplay)).c_str(), 1);
        start();
    }

    void TearDown() override {
        stop();
        if (saved_display_.empty()) ::unsetenv("DISPLAY");
        else ::setenv("DISPLAY", saved_display_.c_str(), 1);
    }

    static fs::path socket_path() {
        return "/tmp/.X11-unix/X" + std::to_string(kDisplay);
    }

    void start() {
        pid_ = ::fork();
        if (pid_ == 0) {
            const std::string disp = ":" + std::to_string(kDisplay);
            ::execlp("Xvfb", "Xvfb", disp.c_str(), "-nolisten", "tcp", "-noreset",
                     static_cast<char*>(nullptr));
            ::_exit(127);
        }
        ASSERT_GT(pid_, 0);
        for (int i = 0; i < 100 && !fs::exists(socket_path()); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        ASSERT_TRUE(fs::exists(socket_path())) << "Xvfb did not come up";
    }

    void stop() {
        if (pid_ > 0) {
            ::kill(pid_, SIGTERM);
            ::waitpid(pid_, nullptr, 0);
            pid_ = -1;
        }
        for (int i = 0; i < 100 && fs::exists(socket_path()); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    }

    // Wait until the trigger reports something other than None
    static TriggerEvent next(ButtonTrigger& t, int total_ms) {
        for (int waited = 0; waited < total_ms; waited += 100) {
            const TriggerEvent e = t.wait(100);
            if (e != TriggerEvent::None) return e;
        }
        return TriggerEvent::None;
    }

    pid_t pid_ = -1;
    std::string saved_display_;
};

}  // namespace

TEST_F(XvfbFixture, InjectedClickIsRejected) {
    ButtonTrigger t(2);
    ASSERT_TRUE(t.connect()) << t.get_error();

    ASSERT_EQ(std::system("xdotool click 2"), 0);

    EXPECT_EQ(next(t, 3000), TriggerEvent::Rejected);
    EXPECT_NE(t.last_source().find("XTEST"), std::string::npos) << t.last_source();
}

// The grab uses AnyModifier: a press with Shift held is caught too
TEST_F(XvfbFixture, PressWithModifierIsCaught) {
    ButtonTrigger t(2);
    ASSERT_TRUE(t.connect()) << t.get_error();

    ASSERT_EQ(std::system("xdotool keydown shift click 2 keyup shift"), 0);
    EXPECT_EQ(next(t, 3000), TriggerEvent::Rejected);
}

// Xvfb has no evdev devices, so give the XTEST pointer a "Device Node" that
// points into a fixture sysfs where that node has BTN_MIDDLE. This drives the
// whole Pressed path: property read, sysfs lookup, sourceid match.
TEST_F(XvfbFixture, PhysicalSourceIsPressed) {
    const fs::path sysroot = fs::temp_directory_path() / ("nd-xsys-" + std::to_string(::getpid()));
    fs::create_directories(sysroot / "event14" / "device" / "capabilities");
    std::ofstream(sysroot / "event14" / "device" / "capabilities" / "key") << kTrackPointMask << "\n";

    xcb_connection_t* c = xcb_connect(nullptr, nullptr);
    ASSERT_FALSE(xcb_connection_has_error(c));
    static const char kName[] = "Device Node";
    auto* atom = xcb_intern_atom_reply(c, xcb_intern_atom(c, 0, sizeof(kName) - 1, kName), nullptr);
    ASSERT_NE(atom, nullptr);
    auto* devs = xcb_input_xi_query_device_reply(c, xcb_input_xi_query_device(c, XCB_INPUT_DEVICE_ALL), nullptr);
    ASSERT_NE(devs, nullptr);
    int xtest = -1;
    for (auto it = xcb_input_xi_query_device_infos_iterator(devs); it.rem; xcb_input_xi_device_info_next(&it)) {
        const std::string name(xcb_input_xi_device_info_name(it.data), it.data->name_len);
        if (it.data->type == XCB_INPUT_DEVICE_TYPE_SLAVE_POINTER && name.find("XTEST") != std::string::npos) {
            xtest = it.data->deviceid;
        }
    }
    free(devs);
    ASSERT_GE(xtest, 0) << "no XTEST pointer";
    const std::string node = "/dev/input/event14";
    xcb_generic_error_t* err = xcb_request_check(c, xcb_input_xi_change_property_checked(
        c, xtest, XCB_PROP_MODE_REPLACE, 8, atom->atom, XCB_ATOM_STRING,
        node.size(), node.data()));
    free(atom);
    ASSERT_EQ(err, nullptr) << "cannot set Device Node on XTEST";
    xcb_disconnect(c);

    {
        ButtonTrigger t(2, sysroot.string());
        ASSERT_TRUE(t.connect()) << t.get_error();
        ASSERT_EQ(std::system("xdotool click 2"), 0);
        EXPECT_EQ(next(t, 3000), TriggerEvent::Pressed);
        EXPECT_NE(t.last_source().find("XTEST"), std::string::npos) << t.last_source();
    }  // grab released here

    // Control on the very same device: the fixture now says no BTN_MIDDLE
    std::ofstream(sysroot / "event14" / "device" / "capabilities" / "key") << kTouchpadMask << "\n";
    {
        ButtonTrigger t(2, sysroot.string());
        ASSERT_TRUE(t.connect()) << t.get_error();
        ASSERT_EQ(std::system("xdotool click 2"), 0);
        EXPECT_EQ(next(t, 3000), TriggerEvent::Rejected);
    }
    fs::remove_all(sysroot);
}

TEST_F(XvfbFixture, SecondGrabIsRefusedWithReason) {
    ButtonTrigger first(2);
    ASSERT_TRUE(first.connect()) << first.get_error();

    ButtonTrigger second(2);
    EXPECT_FALSE(second.connect());
    EXPECT_NE(second.get_error().find("already grabbed"), std::string::npos)
        << second.get_error();
    EXPECT_FALSE(second.connected());
}

// xbindkeys holds a CORE passive grab (XGrabButton). The X server does not
// report it as a conflict for an XInput2 grab, so both would silently hold
// the button and the press would go to whichever grab the server finds
// first. connect() must detect the foreign core grab and refuse.
TEST_F(XvfbFixture, ForeignCoreGrabIsDetected) {
    if (std::system("command -v xbindkeys >/dev/null 2>&1") != 0) {
        GTEST_SKIP() << "xbindkeys not installed";
    }
    const fs::path rc = fs::temp_directory_path() / ("nd-xbk-" + std::to_string(::getpid()));
    std::ofstream(rc) << "\"true\"\n  b:2\n";

    const pid_t xbk = ::fork();
    if (xbk == 0) {
        ::execlp("xbindkeys", "xbindkeys", "-n", "-f", rc.c_str(), static_cast<char*>(nullptr));
        ::_exit(127);
    }
    ASSERT_GT(xbk, 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(500));  // let it grab

    ButtonTrigger t(2);
    const bool ok = t.connect();
    const std::string err = t.get_error();

    ::kill(xbk, SIGTERM);
    ::waitpid(xbk, nullptr, 0);
    fs::remove(rc);

    EXPECT_FALSE(ok) << "XI2 grab coexists with xbindkeys' core grab";
    EXPECT_NE(err.find("already grabbed"), std::string::npos) << err;
}

// A press the trigger grabbed must not reach applications. A window under
// the pointer listening for ButtonPress gets nothing while the grab is held;
// the control click after the grab is released proves the window is live.
TEST_F(XvfbFixture, GrabbedClickNeverReachesApplications) {
    xcb_connection_t* c = xcb_connect(nullptr, nullptr);
    ASSERT_FALSE(xcb_connection_has_error(c));
    xcb_screen_t* scr = xcb_setup_roots_iterator(xcb_get_setup(c)).data;
    const xcb_window_t w = xcb_generate_id(c);
    const uint32_t evmask = XCB_EVENT_MASK_BUTTON_PRESS;
    xcb_create_window(c, XCB_COPY_FROM_PARENT, w, scr->root, 0, 0,
                      scr->width_in_pixels, scr->height_in_pixels, 0,
                      XCB_WINDOW_CLASS_INPUT_OUTPUT, scr->root_visual,
                      XCB_CW_EVENT_MASK, &evmask);
    xcb_map_window(c, w);
    free(xcb_get_input_focus_reply(c, xcb_get_input_focus(c), nullptr));  // sync: mapped
    ASSERT_EQ(std::system("xdotool mousemove 10 10"), 0);

    auto window_got_press = [c](int ms) {
        const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
        while (std::chrono::steady_clock::now() < end) {
            while (xcb_generic_event_t* ev = xcb_poll_for_event(c)) {
                const bool press = (ev->response_type & ~0x80) == XCB_BUTTON_PRESS;
                free(ev);
                if (press) return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        return false;
    };

    {
        ButtonTrigger t(2);
        ASSERT_TRUE(t.connect()) << t.get_error();
        ASSERT_EQ(std::system("xdotool click 2"), 0);
        EXPECT_EQ(next(t, 3000), TriggerEvent::Rejected);
        EXPECT_FALSE(window_got_press(500)) << "grabbed click leaked to the application";
    }

    ASSERT_EQ(std::system("xdotool click 2"), 0);
    EXPECT_TRUE(window_got_press(2000)) << "control: without the grab the window must see it";
    xcb_disconnect(c);
}

// xbindkeys started AFTER the daemon grabbed the button (the usual order at
// login) takes a core grab that silently coexists with ours. The periodic
// re-probe must notice it and give the button up with the reason.
TEST_F(XvfbFixture, ForeignCoreGrabAfterConnectIsDetected) {
    if (std::system("command -v xbindkeys >/dev/null 2>&1") != 0) {
        GTEST_SKIP() << "xbindkeys not installed";
    }
    ButtonTrigger t(2, "/sys/class/input", 300 /* reprobe_ms */);
    ASSERT_TRUE(t.connect()) << t.get_error();

    const fs::path rc = fs::temp_directory_path() / ("nd-xbk2-" + std::to_string(::getpid()));
    std::ofstream(rc) << "\"true\"\n  b:2\n";
    const pid_t xbk = ::fork();
    if (xbk == 0) {
        ::execlp("xbindkeys", "xbindkeys", "-n", "-f", rc.c_str(), static_cast<char*>(nullptr));
        ::_exit(127);
    }
    ASSERT_GT(xbk, 0);

    const TriggerEvent e = next(t, 3000);
    const std::string err = t.get_error();
    ::kill(xbk, SIGTERM);
    ::waitpid(xbk, nullptr, 0);
    fs::remove(rc);

    EXPECT_EQ(e, TriggerEvent::Disconnected);
    EXPECT_NE(err.find("already grabbed"), std::string::npos) << err;
}

TEST_F(XvfbFixture, SurvivesServerRestart) {
    ButtonTrigger t(2);
    ASSERT_TRUE(t.connect()) << t.get_error();

    stop();
    // Reaching the next line at all proves the process was not exit()ed
    EXPECT_EQ(next(t, 3000), TriggerEvent::Disconnected);
    EXPECT_FALSE(t.connected());
    EXPECT_FALSE(t.connect());  // no server yet

    start();
    EXPECT_TRUE(t.connect()) << t.get_error();
    ASSERT_EQ(std::system("xdotool click 2"), 0);
    EXPECT_EQ(next(t, 3000), TriggerEvent::Rejected);
}

TEST(ButtonTriggerConfig, UnsupportedButtonIsReported) {
    ButtonTrigger t(42);
    EXPECT_FALSE(t.connect());
    EXPECT_NE(t.get_error().find("unsupported button 42"), std::string::npos) << t.get_error();
}
