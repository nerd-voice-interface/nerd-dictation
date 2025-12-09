#pragma once

#include <string>
#include <vector>
#include <variant>

namespace nerd_dictation::input {

/**
 * Action: text input
 */
struct TextAction {
    std::string text;
};

/**
 * Action: key press
 */
struct KeyAction {
    std::string keys;  // Format: "ctrl+Return", "shift+Tab"
};

using Action = std::variant<TextAction, KeyAction>;

/**
 * Text post-processor
 * - Replace words with punctuation marks
 * - Replace words with key presses
 * - Capitalization after ./!/?
 */
class TextPostProcessor {
private:
    bool capitalize_next_ = true;
    bool pending_space_ = false;
    
    std::string replace_punctuation(const std::string& text);
    std::string apply_capitalization(const std::string& text);
    std::string process_text_fragment(const std::string& text);  // As in original: _process_text_fragment
    std::vector<Action> extract_key_actions(const std::string& text);
    
public:
    /**
     * Process text
     */
    std::vector<Action> process(const std::string& text);
    
    /**
     * Reset state (start new dictation)
     */
    void reset();
};

} // namespace nerd_dictation::input
