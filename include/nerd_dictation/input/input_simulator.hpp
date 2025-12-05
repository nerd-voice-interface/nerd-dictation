#pragma once

#include <string>

namespace nerd_dictation::input {

/**
 * Симуляция ввода текста через различные методы
 */
class InputSimulator {
private:
    std::string method_;
    
public:
    explicit InputSimulator(const std::string& method);
    
    /**
     * Ввести текст
     */
    void type_text(const std::string& text);
    
    /**
     * Нажать комбинацию клавиш
     */
    void press_keys(const std::string& keys);
};

} // namespace nerd_dictation::input
