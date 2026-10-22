#include "nerd_dictation/input/input_simulator.hpp"
#include <spdlog/spdlog.h>
#include <cerrno>
#include <cstdlib>
#include <sstream>
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <spawn.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

extern char** environ;

namespace nerd_dictation::input {

namespace {

// tty-i3-keys types into the text console for the user who owns it: a user
// process cannot inject input into a VT itself (TIOCSTI needs
// CAP_SYS_ADMIN). See TypeService in tty-i3-keys for the protocol.
constexpr const char* CONSOLE_TYPE_SOCKET = "/run/tty-i3-keys-type.sock";
constexpr int CONSOLE_REPLY_SECONDS = 10;

enum class Console { Typed, X, Unavailable };

// Send `bytes` to the console typing service. An empty request only asks
// whether the console (not X) is in front.
Console console_type(const std::string& bytes) {
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        return Console::Unavailable;
    }
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", CONSOLE_TYPE_SOCKET);
    timeval timeout{CONSOLE_REPLY_SECONDS, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    if (connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        close(fd);
        return Console::Unavailable;    // no tty-i3-keys: X as before
    }
    size_t sent = 0;
    while (sent < bytes.size()) {
        ssize_t n = send(fd, bytes.data() + sent, bytes.size() - sent, MSG_NOSIGNAL);
        if (n <= 0) {
            close(fd);
            spdlog::error("Console typing: send failed");
            return Console::Unavailable;
        }
        sent += static_cast<size_t>(n);
    }
    shutdown(fd, SHUT_WR);
    std::string reply;
    char buf[256];
    ssize_t n;
    while ((n = recv(fd, buf, sizeof(buf), 0)) > 0 && reply.size() < 1024) {
        reply.append(buf, static_cast<size_t>(n));
    }
    close(fd);
    while (!reply.empty() && (reply.back() == '\n' || reply.back() == '\r')) {
        reply.pop_back();
    }
    if (reply == "ok") {
        return Console::Typed;
    }
    if (reply == "x") {
        return Console::X;
    }
    spdlog::error("Console typing: {}", reply.empty() ? "no reply" : reply);
    return Console::Typed;      // the console is in front but refused: X cannot help
}

// What a key combination types on a terminal, or "" if it has no such
// meaning. "ctrl+Return" (the "execute" voice command) is Enter there.
std::string console_bytes(const std::string& keys) {
    std::string key = keys;
    bool ctrl = false;
    size_t plus;
    while ((plus = key.find('+')) != std::string::npos) {
        std::string mod = key.substr(0, plus);
        std::transform(mod.begin(), mod.end(), mod.begin(), ::tolower);
        if (mod == "ctrl" || mod == "control") {
            ctrl = true;
        } else if (mod != "shift") {
            return "";
        }
        key = key.substr(plus + 1);
    }
    std::string lower = key;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    if (lower == "return" || lower == "enter" || lower == "kp_enter") return "\r";
    if (lower == "backspace") return "\x7f";
    if (lower == "tab") return "\t";
    if (lower == "escape" || lower == "esc") return "\x1b";
    if (lower == "space") return ctrl ? std::string(1, '\0') : " ";
    if (ctrl && lower.size() == 1 && std::isalpha(static_cast<unsigned char>(lower[0]))) {
        return std::string(1, static_cast<char>(lower[0] & 0x1f));
    }
    return "";
}

// Run a command directly, without a shell: dictated text reaches xdotool as
// one argument, so "$(...)" or backticks in it are never executed.
// Returns the exit status, or -1 if it could not run.
int run_command(const std::vector<std::string>& argv) {
    std::vector<char*> args;
    for (const auto& a : argv) {
        args.push_back(const_cast<char*>(a.c_str()));
    }
    args.push_back(nullptr);
    pid_t pid;
    if (posix_spawnp(&pid, args[0], nullptr, nullptr, args.data(), environ) != 0) {
        return -1;
    }
    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) {
            return -1;
        }
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

std::string join(const std::vector<std::string>& argv) {
    std::ostringstream out;
    for (size_t i = 0; i < argv.size(); i++) {
        out << (i ? " " : "") << std::quoted(argv[i]);
    }
    return out.str();
}

// "ctrl+Return BackSpace" -> {"ctrl+Return", "BackSpace"}
std::vector<std::string> split_keys(const std::string& keys) {
    std::istringstream in(keys);
    std::vector<std::string> out;
    std::string key;
    while (in >> key) {
        out.push_back(key);
    }
    return out;
}

}  // namespace

InputSimulator::InputSimulator(const std::string& method) : method_(method) {
    // Check method validity (as in original - no automatic fallback)
    if (method != "auto" && method != "ydotool" && method != "xdotool" &&
        method != "wtype" && method != "dotool" && method != "stdout") {
        spdlog::warn("Unknown input method: {}, using stdout", method);
        method_ = "stdout";
    }
    // In original there is no automatic fallback - just use specified method
    // or output error when executing command
}

void InputSimulator::type_text(const std::string& text) {
    if (text.empty()) {
        return;
    }

    if (method_ == "stdout") {
        std::cout << text << std::flush;
        return;
    }

    // "auto": the text console when it is in front, else X (xdotool).
    if (method_ == "auto" && console_type(text) == Console::Typed) {
        return;
    }

    std::vector<std::string> cmd;

    if (method_ == "ydotool") {
        cmd = {"ydotool", "type", "--", text};
    } else if (method_ == "xdotool" || method_ == "auto") {
        cmd = {"xdotool", "type", "--", text};
    } else if (method_ == "wtype") {
        cmd = {"wtype", text};
    } else if (method_ == "dotool") {
        // dotool accepts commands via stdin (as in original)
        std::string dotool_cmd = "type " + text;
        FILE* pipe = popen("dotool", "w");
        if (pipe) {
            fwrite(dotool_cmd.c_str(), 1, dotool_cmd.length(), pipe);
            pclose(pipe);
        } else {
            spdlog::error("Failed to start dotool");
        }
        return;
    }

    spdlog::debug("Executing command: {}", join(cmd));
    if (run_command(cmd) != 0) {
        spdlog::error("Error executing input command: {}", join(cmd));
    }
}

void InputSimulator::press_keys(const std::string& keys) {
    if (method_ == "stdout") {
        std::cout << "[KEY:" << keys << "]" << std::flush;
        return;
    }

    if (method_ == "auto") {
        // On the console a key is the bytes it types; one with no such
        // meaning cannot be pressed there.
        std::string bytes = console_bytes(keys);
        Console where = console_type(bytes);    // "" only asks
        if (where == Console::Typed) {
            if (bytes.empty()) {
                spdlog::warn("Key {} has no meaning on the text console", keys);
            }
            return;
        }
    }

    std::vector<std::string> cmd;
    std::vector<std::string> key_list = split_keys(keys);

    if (method_ == "ydotool") {
        cmd = {"ydotool", "key"};
    } else if (method_ == "xdotool" || method_ == "auto") {
        cmd = {"xdotool", "key", "--"};
    } else if (method_ == "wtype") {
        cmd = {"wtype", "-k"};
    } else if (method_ == "dotool") {
        // dotool: "key ctrl+enter" (as in original)
        std::string dotool_keys = keys;
        // Replace Return with enter (as in original)
        size_t pos = dotool_keys.find("Return");
        if (pos != std::string::npos) {
            dotool_keys.replace(pos, 6, "enter");
        }
        // Convert to lowercase (as in original)
        std::transform(dotool_keys.begin(), dotool_keys.end(), dotool_keys.begin(), ::tolower);

        std::string dotool_cmd = "key " + dotool_keys;
        FILE* pipe = popen("dotool", "w");
        if (pipe) {
            fwrite(dotool_cmd.c_str(), 1, dotool_cmd.length(), pipe);
            pclose(pipe);
        } else {
            spdlog::error("Failed to start dotool");
        }
        return;
    }

    cmd.insert(cmd.end(), key_list.begin(), key_list.end());
    spdlog::debug("Pressing keys: {}", join(cmd));
    if (run_command(cmd) != 0) {
        spdlog::error("Error pressing keys: {}", join(cmd));
    }
}

} // namespace nerd_dictation::input
