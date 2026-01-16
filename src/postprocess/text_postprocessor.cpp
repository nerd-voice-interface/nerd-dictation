/**
 * Text Postprocessor Implementation
 *
 * Handles UTF-8 Russian text with proper case conversion and word matching.
 */

#include "nerd_dictation/postprocess/text_postprocessor.hpp"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <sstream>
#include <cctype>

namespace nerd_dictation {
namespace postprocess {

// ============================================================================
// UTF-8 utility functions implementation
// ============================================================================

/**
 * Get the number of bytes in a UTF-8 character based on the first byte
 */
static size_t utf8_char_length(unsigned char first_byte) {
    if ((first_byte & 0x80) == 0) return 1;        // ASCII: 0xxxxxxx
    if ((first_byte & 0xE0) == 0xC0) return 2;     // 2-byte: 110xxxxx
    if ((first_byte & 0xF0) == 0xE0) return 3;     // 3-byte: 1110xxxx
    if ((first_byte & 0xF8) == 0xF0) return 4;     // 4-byte: 11110xxx
    return 1; // Invalid, treat as single byte
}

std::string utf8_next_char(const std::string& str, size_t& pos) {
    if (pos >= str.size()) {
        return "";
    }

    size_t len = utf8_char_length(static_cast<unsigned char>(str[pos]));
    if (pos + len > str.size()) {
        len = str.size() - pos; // Truncated character
    }

    std::string result = str.substr(pos, len);
    pos += len;
    return result;
}

/**
 * Decode UTF-8 character to Unicode code point
 */
static uint32_t utf8_decode(const std::string& ch) {
    if (ch.empty()) return 0;

    unsigned char c0 = static_cast<unsigned char>(ch[0]);

    if ((c0 & 0x80) == 0) {
        // ASCII
        return c0;
    }

    if (ch.size() >= 2 && (c0 & 0xE0) == 0xC0) {
        // 2-byte character
        unsigned char c1 = static_cast<unsigned char>(ch[1]);
        return ((c0 & 0x1F) << 6) | (c1 & 0x3F);
    }

    if (ch.size() >= 3 && (c0 & 0xF0) == 0xE0) {
        // 3-byte character
        unsigned char c1 = static_cast<unsigned char>(ch[1]);
        unsigned char c2 = static_cast<unsigned char>(ch[2]);
        return ((c0 & 0x0F) << 12) | ((c1 & 0x3F) << 6) | (c2 & 0x3F);
    }

    if (ch.size() >= 4 && (c0 & 0xF8) == 0xF0) {
        // 4-byte character
        unsigned char c1 = static_cast<unsigned char>(ch[1]);
        unsigned char c2 = static_cast<unsigned char>(ch[2]);
        unsigned char c3 = static_cast<unsigned char>(ch[3]);
        return ((c0 & 0x07) << 18) | ((c1 & 0x3F) << 12) |
               ((c2 & 0x3F) << 6) | (c3 & 0x3F);
    }

    return c0; // Fallback
}

/**
 * Encode Unicode code point to UTF-8
 */
static std::string utf8_encode(uint32_t cp) {
    std::string result;

    if (cp < 0x80) {
        result.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        result.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        result.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        result.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        result.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        result.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        result.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        result.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        result.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        result.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }

    return result;
}

/**
 * Convert single Unicode code point to lowercase
 */
static uint32_t unicode_to_lower(uint32_t cp) {
    // ASCII uppercase A-Z
    if (cp >= 'A' && cp <= 'Z') {
        return cp + 32;
    }

    // Russian uppercase А-Я (U+0410 - U+042F)
    if (cp >= 0x0410 && cp <= 0x042F) {
        return cp + 32; // -> а-я (U+0430 - U+044F)
    }

    // Russian Ё (U+0401) -> ё (U+0451)
    if (cp == 0x0401) {
        return 0x0451;
    }

    return cp;
}

/**
 * Convert single Unicode code point to uppercase
 */
static uint32_t unicode_to_upper(uint32_t cp) {
    // ASCII lowercase a-z
    if (cp >= 'a' && cp <= 'z') {
        return cp - 32;
    }

    // Russian lowercase а-я (U+0430 - U+044F)
    if (cp >= 0x0430 && cp <= 0x044F) {
        return cp - 32; // -> А-Я (U+0410 - U+042F)
    }

    // Russian ё (U+0451) -> Ё (U+0401)
    if (cp == 0x0451) {
        return 0x0401;
    }

    return cp;
}

/**
 * Check if Unicode code point is alphabetic
 */
static bool unicode_is_alpha(uint32_t cp) {
    // ASCII letters
    if ((cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z')) {
        return true;
    }

    // Russian letters А-Я, а-я
    if ((cp >= 0x0410 && cp <= 0x044F)) {
        return true;
    }

    // Russian Ё, ё
    if (cp == 0x0401 || cp == 0x0451) {
        return true;
    }

    return false;
}

std::string utf8_to_lower(const std::string& str) {
    std::string result;
    result.reserve(str.size());

    size_t pos = 0;
    while (pos < str.size()) {
        std::string ch = utf8_next_char(str, pos);
        uint32_t cp = utf8_decode(ch);
        uint32_t lower_cp = unicode_to_lower(cp);
        result += utf8_encode(lower_cp);
    }

    return result;
}

std::string utf8_to_upper(const std::string& str) {
    std::string result;
    result.reserve(str.size());

    size_t pos = 0;
    while (pos < str.size()) {
        std::string ch = utf8_next_char(str, pos);
        uint32_t cp = utf8_decode(ch);
        uint32_t upper_cp = unicode_to_upper(cp);
        result += utf8_encode(upper_cp);
    }

    return result;
}

std::string utf8_capitalize_first(const std::string& str) {
    if (str.empty()) return str;

    std::string result;
    result.reserve(str.size());

    size_t pos = 0;
    std::string first_ch = utf8_next_char(str, pos);
    uint32_t cp = utf8_decode(first_ch);
    result += utf8_encode(unicode_to_upper(cp));
    result += str.substr(pos);

    return result;
}

bool utf8_is_alpha(const std::string& ch) {
    if (ch.empty()) return false;
    uint32_t cp = utf8_decode(ch);
    return unicode_is_alpha(cp);
}

/**
 * Check if character is whitespace
 */
static bool is_whitespace(const std::string& ch) {
    if (ch.empty()) return false;
    if (ch.size() == 1) {
        char c = ch[0];
        return c == ' ' || c == '\t' || c == '\n' || c == '\r';
    }
    // Non-breaking space (U+00A0) = C2 A0
    if (ch.size() == 2 && ch[0] == '\xC2' && ch[1] == '\xA0') {
        return true;
    }
    return false;
}

/**
 * Check if position is at word boundary (word is surrounded by non-alpha or string edges)
 */
static bool is_word_boundary_before(const std::string& text, size_t pos) {
    if (pos == 0) return true;

    // Find the character before pos
    size_t prev_pos = 0;
    size_t last_pos = 0;
    while (prev_pos < pos && prev_pos < text.size()) {
        last_pos = prev_pos;
        utf8_next_char(text, prev_pos);
        if (prev_pos > pos) {
            prev_pos = last_pos;
            break;
        }
    }

    if (last_pos >= text.size()) return true;

    std::string prev_ch = utf8_next_char(text, last_pos);
    return !utf8_is_alpha(prev_ch) || is_whitespace(prev_ch);
}

static bool is_word_boundary_after(const std::string& text, size_t pos) {
    if (pos >= text.size()) return true;

    size_t temp_pos = pos;
    std::string next_ch = utf8_next_char(text, temp_pos);
    return !utf8_is_alpha(next_ch) || is_whitespace(next_ch);
}

std::string utf8_replace_word(const std::string& text, const std::string& word,
                               const std::string& replacement) {
    if (text.empty() || word.empty()) return text;

    std::string lower_text = utf8_to_lower(text);
    std::string lower_word = utf8_to_lower(word);

    std::string result;
    result.reserve(text.size());

    size_t pos = 0;
    size_t last_end = 0;

    while (pos < lower_text.size()) {
        size_t found = lower_text.find(lower_word, pos);
        if (found == std::string::npos) {
            break;
        }

        size_t word_end = found + lower_word.size();

        // Check word boundaries
        bool boundary_before = is_word_boundary_before(lower_text, found);
        bool boundary_after = is_word_boundary_after(lower_text, word_end);

        if (boundary_before && boundary_after) {
            // Found whole word - replace
            result += text.substr(last_end, found - last_end);
            result += replacement;
            last_end = word_end;
            pos = word_end;
        } else {
            // Not a whole word, move past this position
            size_t temp = found;
            utf8_next_char(text, temp);
            pos = temp;
        }
    }

    // Append remaining text
    result += text.substr(last_end);

    return result;
}

bool utf8_contains_word(const std::string& text, const std::string& word) {
    std::string lower_text = utf8_to_lower(text);
    std::string lower_word = utf8_to_lower(word);

    size_t pos = 0;
    while (pos < lower_text.size()) {
        size_t found = lower_text.find(lower_word, pos);
        if (found == std::string::npos) {
            return false;
        }

        size_t word_end = found + lower_word.size();

        if (is_word_boundary_before(lower_text, found) &&
            is_word_boundary_after(lower_text, word_end)) {
            return true;
        }

        size_t temp = found;
        utf8_next_char(text, temp);
        pos = temp;
    }

    return false;
}

// ============================================================================
// PostprocessConfig implementation
// ============================================================================

PostprocessConfig PostprocessConfig::create_default() {
    PostprocessConfig config;

    // Words -> key combinations
    config.key_map = {
        {"выполнить", "ctrl+Return"},
        {"выполнять", "ctrl+Return"},
        {"выполняй", "ctrl+Return"},
        {"выполни", "ctrl+Return"},
        {"выполнял", "ctrl+Return"},
	{"выполнил", "ctrl+Return"},
    };

    // Words -> punctuation marks
    config.punctuation_map = {
        {"точка", "."},
        {"запятая", ","},
        {"двоеточие", ":"},
        {"тире", "—"},
        {"дефис", "-"},
        {"восклицательный знак", "!"},
        {"вопросительный знак", "?"},
        {"знак вопроса", "?"},
        {"вопрос", "?"},
        {"многоточие", "..."},
        {"кавычки", "\""},
        {"кавычка", "\""},
        {"открыть кавычки", "«"},
        {"закрыть кавычки", "»"},
        {"открыть скобку", "("},
        {"закрыть скобку", ")"},
        {"новая строка", "\n"},
        {"новый абзац", "\n\n"},
        // Emojis
        {"смайлик улыбка", "😊"},
        {"смайлик смех", "😂"},
        {"смайлик крутой", "😎"},
        {"смайлик грустный", "😢"},
        {"смайлик сердце", "❤️"},
        {"смайлик огонь", "🔥"},
        {"смайлик палец вверх", "👍"},
        {"смайлик ок", "👌"},
    };

    // Characters after which to capitalize next word
    config.capitalize_after = {
        ".", "!", "?",
        "😊", "😂", "😎", "😢", "❤️", "🔥", "👍", "👌"
    };

    // Characters before which to remove space
    config.no_space_before = {
        ".", ",", ":", "!", "?", "-", "...", "»", ")"
    };

    // Characters before which to add space (Russian typography)
    config.space_before = {
        "—"
    };

    return config;
}

// ============================================================================
// TextPostProcessor implementation
// ============================================================================

TextPostProcessor& TextPostProcessor::instance() {
    static TextPostProcessor instance;
    return instance;
}

TextPostProcessor::TextPostProcessor()
    : config_(PostprocessConfig::create_default())
{
}

TextPostProcessor::TextPostProcessor(PostprocessConfig config)
    : config_(std::move(config))
{
}

void TextPostProcessor::set_config(PostprocessConfig config) {
    std::lock_guard<std::mutex> lock(mutex_);
    config_ = std::move(config);
}

void TextPostProcessor::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    capitalize_next_ = true;
    pending_space_ = false;
}

std::vector<Action> TextPostProcessor::process(const std::string& text) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (text.empty()) {
        return {};
    }

    return extract_key_actions(text);
}

std::vector<Action> TextPostProcessor::extract_key_actions(const std::string& text) {
    // Sort key_map entries by length (longest first)
    std::vector<std::pair<std::string, std::string>> sorted_keys(
        config_.key_map.begin(), config_.key_map.end()
    );
    std::sort(sorted_keys.begin(), sorted_keys.end(),
              [](const auto& a, const auto& b) {
                  return a.first.size() > b.first.size();
              });

    // Try to find key words
    for (const auto& [word, keys] : sorted_keys) {
        if (utf8_contains_word(text, word)) {
            // Found key word - split text and process recursively
            std::string lower_text = utf8_to_lower(text);
            std::string lower_word = utf8_to_lower(word);

            size_t found = lower_text.find(lower_word);
            if (found == std::string::npos) {
                continue;
            }

            // Find actual word boundaries
            size_t word_end = found + lower_word.size();

            if (!is_word_boundary_before(lower_text, found) ||
                !is_word_boundary_after(lower_text, word_end)) {
                continue;
            }

            std::vector<Action> result;

            // Process text before keyword
            std::string before = text.substr(0, found);
            if (!before.empty()) {
                // Trim trailing whitespace from "before"
                while (!before.empty() && (before.back() == ' ' || before.back() == '\t')) {
                    before.pop_back();
                }
                if (!before.empty()) {
                    auto before_actions = extract_key_actions(before);
                    result.insert(result.end(), before_actions.begin(), before_actions.end());
                }
            }

            // Add key action
            result.push_back(KeyAction{keys});

            // Process text after keyword
            std::string after = text.substr(word_end);
            // Trim leading whitespace from "after"
            size_t after_start = 0;
            while (after_start < after.size() &&
                   (after[after_start] == ' ' || after[after_start] == '\t')) {
                after_start++;
            }
            after = after.substr(after_start);

            if (!after.empty()) {
                auto after_actions = extract_key_actions(after);
                result.insert(result.end(), after_actions.begin(), after_actions.end());
            }

            return result;
        }
    }

    // No key words found - process as text
    std::string processed = process_text_fragment(text);
    if (!processed.empty()) {
        return {TextAction{processed}};
    }

    return {};
}

std::string TextPostProcessor::process_text_fragment(const std::string& text) {
    // Replace words with punctuation
    std::string result = replace_punctuation(text);

    // Trim whitespace
    size_t start = 0;
    while (start < result.size() &&
           (result[start] == ' ' || result[start] == '\t')) {
        start++;
    }
    size_t end = result.size();
    while (end > start &&
           (result[end - 1] == ' ' || result[end - 1] == '\t')) {
        end--;
    }
    result = result.substr(start, end - start);

    if (result.empty()) {
        return "";
    }

    // Add buffered space from previous fragment
    size_t first_char_pos = 0;
    std::string first_char = utf8_next_char(result, first_char_pos);

    if (pending_space_) {
        // Check if first character is in no_space_before set
        bool no_space = config_.no_space_before.count(first_char) > 0;
        if (!no_space) {
            result = " " + result;
        }
    }

    // Buffer space for next fragment
    pending_space_ = true;

    // Apply capitalization
    result = apply_capitalization(result);

    return result;
}

std::string TextPostProcessor::replace_punctuation(const std::string& text) {
    // Sort punctuation_map entries by length (longest first)
    std::vector<std::pair<std::string, std::string>> sorted_punct(
        config_.punctuation_map.begin(), config_.punctuation_map.end()
    );
    std::sort(sorted_punct.begin(), sorted_punct.end(),
              [](const auto& a, const auto& b) {
                  return a.first.size() > b.first.size();
              });

    std::string result = text;

    // Replace word-commands with punctuation
    for (const auto& [word, punct] : sorted_punct) {
        result = utf8_replace_word(result, word, punct);
    }

    // Remove spaces before certain punctuation
    for (const auto& punct : config_.no_space_before) {
        std::string pattern = " " + punct;
        size_t pos;
        while ((pos = result.find(pattern)) != std::string::npos) {
            result.replace(pos, pattern.size(), punct);
        }
    }

    // Add space before certain punctuation (Russian typography, e.g., em-dash)
    for (const auto& punct : config_.space_before) {
        // Find punctuation preceded by non-space
        size_t pos = 0;
        while ((pos = result.find(punct, pos)) != std::string::npos) {
            if (pos > 0 && result[pos - 1] != ' ' && result[pos - 1] != '\n') {
                result.insert(pos, " ");
                pos += punct.size() + 1;
            } else {
                pos += punct.size();
            }
        }
    }

    return result;
}

std::string TextPostProcessor::apply_capitalization(const std::string& text) {
    if (text.empty()) {
        return text;
    }

    std::string result;
    result.reserve(text.size());

    size_t pos = 0;
    while (pos < text.size()) {
        std::string ch = utf8_next_char(text, pos);

        if (utf8_is_alpha(ch)) {
            if (capitalize_next_) {
                result += utf8_to_upper(ch);
                capitalize_next_ = false;
            } else {
                result += ch;
            }
        } else {
            result += ch;
            // Check if this character should trigger capitalization
            if (config_.capitalize_after.count(ch) > 0) {
                capitalize_next_ = true;
            }
        }
    }

    return result;
}

// ============================================================================
// Convenience functions
// ============================================================================

std::vector<Action> process_text(const std::string& text) {
    return TextPostProcessor::instance().process(text);
}

void reset_processor() {
    TextPostProcessor::instance().reset();
}

} // namespace postprocess
} // namespace nerd_dictation
