#include "nerd_dictation/input/input_simulator.hpp"
#include <spdlog/spdlog.h>
#include <cstdlib>
#include <sstream>
#include <iostream>
#include <iomanip>

namespace nerd_dictation::input {

InputSimulator::InputSimulator(const std::string& method) : method_(method) {
    if (method != "ydotool" && method != "xdotool" && method != "wtype" && 
        method != "dotool" && method != "stdout") {
        spdlog::warn("Неизвестный метод ввода: {}, используется stdout", method);
        method_ = "stdout";
    }
    
    // Проверяем доступность инструмента (кроме stdout)
    if (method_ != "stdout") {
        std::string check_cmd;
        if (method_ == "ydotool") {
            check_cmd = "which ydotool > /dev/null 2>&1";
        } else if (method_ == "xdotool") {
            check_cmd = "which xdotool > /dev/null 2>&1";
        } else if (method_ == "wtype") {
            check_cmd = "which wtype > /dev/null 2>&1";
        } else if (method_ == "dotool") {
            check_cmd = "which dotool > /dev/null 2>&1";
        }
        
        if (!check_cmd.empty()) {
            int result = std::system(check_cmd.c_str());
            if (result != 0) {
                spdlog::warn("Инструмент {} не найден, используется stdout", method_);
                method_ = "stdout";
            }
        }
    }
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
        // dotool принимает команды через stdin
        std::string dotool_cmd = "type " + text;
        FILE* pipe = popen("dotool", "w");
        if (pipe) {
            fwrite(dotool_cmd.c_str(), 1, dotool_cmd.length(), pipe);
            pclose(pipe);
        }
        return;
    }
    
    spdlog::debug("Выполняю команду: {}", cmd.str());
    int result = std::system(cmd.str().c_str());
    if (result != 0) {
        spdlog::error("Ошибка выполнения команды ввода: {}", cmd.str());
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
        std::string dotool_cmd = "key " + keys;
        FILE* pipe = popen("dotool", "w");
        if (pipe) {
            fwrite(dotool_cmd.c_str(), 1, dotool_cmd.length(), pipe);
            pclose(pipe);
        }
        return;
    }
    
    spdlog::debug("Нажимаю клавиши: {}", cmd.str());
    int result = std::system(cmd.str().c_str());
    if (result != 0) {
        spdlog::error("Ошибка нажатия клавиш: {}", cmd.str());
    }
}

} // namespace nerd_dictation::input
