#pragma once

#include <string>

namespace nerd_dictation::input {

/**
 * Text input simulation via various methods
 */
class InputSimulator {
private:
    std::string method_;
    
public:
    explicit InputSimulator(const std::string& method);
    
    /**
     * Type text
     */
    void type_text(const std::string& text);
    
    /**
     * Press key combination
     */
    void press_keys(const std::string& keys);
};

} // namespace nerd_dictation::input
