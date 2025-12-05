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
    // Проверяем валидность метода (как в оригинале - без автоматического fallback)
    if (method != "ydotool" && method != "xdotool" && method != "wtype" && 
        method != "dotool" && method != "stdout") {
        spdlog::warn("Неизвестный метод ввода: {}, используется stdout", method);
        method_ = "stdout";
    }
    // В оригинале нет автоматического fallback - просто используется указанный метод
    // или выводится ошибка при выполнении команды
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
        // dotool принимает команды через stdin (как в оригинале)
        std::string dotool_cmd = "type " + text;
        FILE* pipe = popen("dotool", "w");
        if (pipe) {
            fwrite(dotool_cmd.c_str(), 1, dotool_cmd.length(), pipe);
            pclose(pipe);
        } else {
            spdlog::error("Ошибка запуска dotool");
        }
        return;
    }
    
    // Выполняем команду через shell (как в оригинале: subprocess.run с shell=True)
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
        // dotool: "key ctrl+enter" (как в оригинале)
        std::string dotool_keys = keys;
        // Заменяем Return на enter (как в оригинале)
        size_t pos = dotool_keys.find("Return");
        if (pos != std::string::npos) {
            dotool_keys.replace(pos, 6, "enter");
        }
        // Приводим к нижнему регистру (как в оригинале)
        std::transform(dotool_keys.begin(), dotool_keys.end(), dotool_keys.begin(), ::tolower);
        
        std::string dotool_cmd = "key " + dotool_keys;
        FILE* pipe = popen("dotool", "w");
        if (pipe) {
            fwrite(dotool_cmd.c_str(), 1, dotool_cmd.length(), pipe);
            pclose(pipe);
        } else {
            spdlog::error("Ошибка запуска dotool");
        }
        return;
    }
    
    // Выполняем команду через shell (как в оригинале)
    spdlog::debug("Нажимаю клавиши: {}", cmd.str());
    int result = std::system(cmd.str().c_str());
    if (result != 0) {
        spdlog::error("Ошибка нажатия клавиш: {}", cmd.str());
    }
}

} // namespace nerd_dictation::input
