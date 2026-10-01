/**
 * Text Postprocessor - Native C++ implementation
 *
 * Features:
 * - Replace words with punctuation marks
 * - Replace words with key combinations
 * - Capitalize after sentence-ending punctuation
 * - Capitalize at the start of dictation
 *
 * Handles Russian/Cyrillic text with proper UTF-8 support.
 */

#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <variant>
#include <unordered_map>
#include <unordered_set>

namespace nerd_dictation {
namespace postprocess {

/**
 * Action: type text
 */
struct TextAction {
    std::string text;

    explicit TextAction(std::string t = "") : text(std::move(t)) {}
};

/**
 * Action: press keys (xdotool format: "ctrl+Return", "shift+Tab", etc.)
 */
struct KeyAction {
    std::string keys;

    explicit KeyAction(std::string k = "") : keys(std::move(k)) {}
};

/**
 * Action variant - either TextAction or KeyAction
 */
using Action = std::variant<TextAction, KeyAction>;

/**
 * Check if action is TextAction
 */
inline bool is_text_action(const Action& action) {
    return std::holds_alternative<TextAction>(action);
}

/**
 * Check if action is KeyAction
 */
inline bool is_key_action(const Action& action) {
    return std::holds_alternative<KeyAction>(action);
}

/**
 * Get TextAction from Action (throws if wrong type)
 */
inline const TextAction& get_text_action(const Action& action) {
    return std::get<TextAction>(action);
}

/**
 * Get KeyAction from Action (throws if wrong type)
 */
inline const KeyAction& get_key_action(const Action& action) {
    return std::get<KeyAction>(action);
}

/**
 * Text Postprocessor Configuration
 */
struct PostprocessConfig {
    // Words -> key combinations
    std::unordered_map<std::string, std::string> key_map;

    // Words -> punctuation marks
    std::unordered_map<std::string, std::string> punctuation_map;

    // Characters after which to capitalize next word
    std::unordered_set<std::string> capitalize_after;

    // Characters before which to remove space
    std::unordered_set<std::string> no_space_before;

    // Characters before which to add space
    std::unordered_set<std::string> space_before;

    /**
     * Create default Russian configuration
     */
    static PostprocessConfig create_default();
};

/**
 * Text Postprocessor with state preservation between calls
 *
 * Usage:
 *   TextPostProcessor processor;
 *   auto actions = processor.process("привет запятая мир");
 *   // actions[0] = TextAction{"Привет, мир"}
 */
class TextPostProcessor {
public:
    /**
     * Get singleton instance
     */
    static TextPostProcessor& instance();

    /**
     * Constructor with default config
     */
    TextPostProcessor();

    /**
     * Constructor with custom config
     */
    explicit TextPostProcessor(PostprocessConfig config);

    /**
     * Process text: replace words with punctuation/keys, capitalize
     *
     * @param text Recognized text
     * @return List of actions (TextAction or KeyAction)
     */
    std::vector<Action> process(const std::string& text);

    /**
     * Reset state (start new dictation)
     */
    void reset();

    /**
     * Get current configuration (read-only)
     */
    const PostprocessConfig& config() const { return config_; }

    /**
     * Set new configuration
     */
    void set_config(PostprocessConfig config);

    // Non-copyable
    TextPostProcessor(const TextPostProcessor&) = delete;
    TextPostProcessor& operator=(const TextPostProcessor&) = delete;

private:
    PostprocessConfig config_;

    // State
    bool capitalize_next_ = true;   // Start with capital letter
    bool pending_space_ = false;    // Buffered space at end

    // Thread safety
    mutable std::mutex mutex_;

    // Internal methods
    std::vector<Action> extract_key_actions(const std::string& text);
    std::string process_text_fragment(const std::string& text);
    std::string replace_punctuation(const std::string& text);
    std::string normalize_spaces(const std::string& text);
    std::string apply_capitalization(const std::string& text);
};

// ============================================================================
// UTF-8 utility functions
// ============================================================================

/**
 * Convert UTF-8 string to lowercase (supports Russian/Cyrillic)
 */
std::string utf8_to_lower(const std::string& str);

/**
 * Convert UTF-8 string to uppercase (supports Russian/Cyrillic)
 */
std::string utf8_to_upper(const std::string& str);

/**
 * Capitalize first character of UTF-8 string
 */
std::string utf8_capitalize_first(const std::string& str);

/**
 * Check if UTF-8 character is alphabetic
 */
bool utf8_is_alpha(const std::string& ch);

/**
 * Get next UTF-8 character from string
 * @param str Input string
 * @param pos Current position (updated to next character position)
 * @return UTF-8 character as string, empty if end of string
 */
std::string utf8_next_char(const std::string& str, size_t& pos);

/**
 * Check if string contains substring (case-insensitive for Russian)
 */
bool utf8_contains_word(const std::string& text, const std::string& word);

/**
 * Replace word in text (case-insensitive, whole word only)
 * @param text Input text
 * @param word Word to replace
 * @param replacement Replacement string
 * @return Text with replacements
 */
std::string utf8_replace_word(const std::string& text, const std::string& word,
                               const std::string& replacement);

// ============================================================================
// Convenience functions (use global singleton)
// ============================================================================

/**
 * Process text using global postprocessor
 */
std::vector<Action> process_text(const std::string& text);

/**
 * Reset global postprocessor state
 */
void reset_processor();

} // namespace postprocess
} // namespace nerd_dictation
