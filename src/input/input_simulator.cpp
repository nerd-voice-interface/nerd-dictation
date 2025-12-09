#include "nerd_dictation/input/input_simulator.hpp"
#include <spdlog/spdlog.h>
#include <cstdlib>
#include <sstream>
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <cctype>

namespace nerd_dictation::input {

InputSimulator::InputSimulator(const std::string& method) : method_(method) {
    // Check method validity (as in original - no automatic fallback)
    if (method != "ydotool" && method != "xdotool" && method != "wtype" && 
        method != "dotool" && method != "stdout") {
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
    
    std::ostringstream cmd;
    
    if (method_ == "ydotool") {
        cmd << "ydotool type -- " << std::quoted(text);
    } else if (method_ == "xdotool") {
        cmd << "xdotool type -- " << std::quoted(text);
    } else if (method_ == "wtype") {
        cmd << "wtype " << std::quoted(text);
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
    
    // Execute command via shell (as in original: subprocess.run with shell=True)
    spdlog::debug("Executing command: {}", cmd.str());
    int result = std::system(cmd.str().c_str());
    if (result != 0) {
        spdlog::error("Error executing input command: {}", cmd.str());
    }
}

void InputSimulator::press_keys(const std::string& keys) {
    if (method_ == "stdout") {
        std::cout << "[KEY:" << keys << "]" << std::flush;
        return;
    }
    
    std::ostringstream cmd;
    
    if (method_ == "ydotool") {
        cmd << "ydotool key " << keys;
    } else if (method_ == "xdotool") {
        cmd << "xdotool key " << keys;
    } else if (method_ == "wtype") {
        cmd << "wtype -k " << keys;
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
    
    // Execute command via shell (as in original)
    spdlog::debug("Pressing keys: {}", cmd.str());
    int result = std::system(cmd.str().c_str());
    if (result != 0) {
        spdlog::error("Error pressing keys: {}", cmd.str());
    }
}

} // namespace nerd_dictation::input
