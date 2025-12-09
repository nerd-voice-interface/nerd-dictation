#include "nerd_dictation/input/postprocess.hpp"
#include <regex>
#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace nerd_dictation::input {

// Words → key presses (Russian commands)
static const std::map<std::string, std::string> KEY_MAP = {
    {"выполнить", "ctrl+Return"},
    {"выполнять", "ctrl+Return"},
    {"выполняй", "ctrl+Return"},
    {"выполни", "ctrl+Return"},
};

// Words → punctuation marks (Russian commands)
static const std::map<std::string, std::string> PUNCTUATION_MAP = {
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
    // Emojis (as in original)
    {"смайлик улыбка", "😊"},
    {"смайлик смех", "😂"},
    {"смайлик крутой", "😎"},
    {"смайлик грустный", "😢"},
    {"смайлик сердце", "❤️"},
    {"смайлик огонь", "🔥"},
    {"смайлик палец вверх", "👍"},
    {"смайлик ок", "👌"},
};

// Characters after which the next word is capitalized (use strings to support emojis)
static const std::set<std::string> CAPITALIZE_AFTER = {".", "!", "?", "😊", "😂", "😎", "😢", "❤️", "🔥", "👍", "👌", "\n"};

// Characters before which space is removed
static const std::set<std::string> NO_SPACE_BEFORE = {".", ",", ":", "!", "?", "—", "-", "...", "»", ")"};

std::vector<Action> TextPostProcessor::extract_key_actions(const std::string& text) {
    std::vector<Action> actions;
    
    // Search for key words (longer phrases first)
    std::vector<std::pair<std::string, std::string>> sorted_keys;
    for (const auto& [word, keys] : KEY_MAP) {
        sorted_keys.push_back({word, keys});
    }
    std::sort(sorted_keys.begin(), sorted_keys.end(), 
              [](const auto& a, const auto& b) { return a.first.length() > b.first.length(); });
    
    // Recursively split text by key words (as in original)
    std::string remaining = text;
    size_t pos = 0;
    
    while (pos < remaining.length()) {
        bool found_key = false;
        
        for (const auto& [word, keys] : sorted_keys) {
            // Use simple string search instead of regex \b (which doesn't work with UTF-8)
            size_t found = remaining.find(word, pos);
            if (found != std::string::npos) {
                // Check if it's a whole word: surrounded by spaces or at string boundaries
                bool is_word_start = (found == 0) || std::isspace(static_cast<unsigned char>(remaining[found - 1]));
                bool is_word_end = (found + word.length() == remaining.length()) || 
                                  std::isspace(static_cast<unsigned char>(remaining[found + word.length()]));
                
                if (is_word_start && is_word_end) {
                    // Text before key word - process recursively
                    if (found > pos) {
                        std::string before = remaining.substr(pos, found - pos);
                        auto before_actions = extract_key_actions(before);
                        actions.insert(actions.end(), before_actions.begin(), before_actions.end());
                    }
                    
                    // Keys
                    actions.push_back(KeyAction{keys});
                    
                    pos = found + word.length();
                    found_key = true;
                    break;
                } else {
                    // Not a whole word, continue searching
                    pos = found + 1;
                }
            }
        }
        
        if (!found_key) {
            break;
        }
    }
    
    // Remaining text - process as text fragment
    if (pos < remaining.length()) {
        std::string rest = remaining.substr(pos);
        std::string processed = process_text_fragment(rest);
        if (!processed.empty()) {
            actions.push_back(TextAction{processed});
        }
    }
    
    // If no key words found - process entire text
    if (actions.empty()) {
        std::string processed = process_text_fragment(text);
        if (!processed.empty()) {
            actions.push_back(TextAction{processed});
        }
    }
    
    return actions;
}

std::string TextPostProcessor::replace_punctuation(const std::string& text) {
    std::string result = text;
    
    // Sort by length (longer phrases first) to match longer phrases first
    std::vector<std::pair<std::string, std::string>> sorted;
    for (const auto& [word, punct] : PUNCTUATION_MAP) {
        sorted.push_back({word, punct});
    }
    std::sort(sorted.begin(), sorted.end(),
              [](const auto& a, const auto& b) { return a.first.length() > b.first.length(); });
    
    for (const auto& [word, punct] : sorted) {
        // Simple string replacement: find word surrounded by spaces or at boundaries
        // This works better with UTF-8 than regex \b
        size_t pos = 0;
        while (pos < result.length()) {
            size_t found = result.find(word, pos);
            if (found == std::string::npos) {
                break;
            }
            
            // Check if it's a whole word: surrounded by spaces or at string boundaries
            bool is_word_start = (found == 0) || std::isspace(static_cast<unsigned char>(result[found - 1]));
            bool is_word_end = (found + word.length() == result.length()) || 
                              std::isspace(static_cast<unsigned char>(result[found + word.length()]));
            
            if (is_word_start && is_word_end) {
                // Replace word with punctuation
                // Remove space before if punctuation shouldn't have space before it
                bool remove_space_before = (found > 0 && NO_SPACE_BEFORE.count(punct) > 0 && 
                                           std::isspace(static_cast<unsigned char>(result[found - 1])));
                
                size_t replace_start = remove_space_before ? found - 1 : found;
                size_t replace_length = remove_space_before ? word.length() + 1 : word.length();
                
                result.replace(replace_start, replace_length, punct);
                pos = replace_start + punct.length();
            } else {
                pos = found + 1;
            }
        }
    }
    
    // Remove spaces before punctuation marks (cleanup pass for any remaining)
    for (const std::string& punct : NO_SPACE_BEFORE) {
        std::string space_punct = " " + punct;
        size_t pos = 0;
        while ((pos = result.find(space_punct, pos)) != std::string::npos) {
            result.replace(pos, space_punct.length(), punct);
            pos += punct.length();
        }
    }
    
    // Special handling for paired punctuation (quotes, parentheses)
    // Remove spaces inside quotes: « текст » -> «текст»
    // « is 2 bytes (0xC2 0xAB), » is 2 bytes (0xC2 0xBB)
    size_t quote_pos = 0;
    while ((quote_pos = result.find("«", quote_pos)) != std::string::npos) {
        // Check if there's a space after opening quote
        if (quote_pos + 2 < result.length() && result[quote_pos + 2] == ' ') {
            // Remove space after opening quote
            result.erase(quote_pos + 2, 1);
        }
        
        // Find corresponding closing quote
        size_t close_quote = result.find("»", quote_pos + 2);
        if (close_quote != std::string::npos && close_quote > 0 && result[close_quote - 1] == ' ') {
            // Remove space before closing quote
            result.erase(close_quote - 1, 1);
            quote_pos = close_quote;
        } else {
            quote_pos += 2; // Move past the opening quote (2 bytes)
        }
    }
    
    // Remove spaces inside parentheses: ( текст ) -> (текст)
    size_t paren_pos = 0;
    while ((paren_pos = result.find("( ", paren_pos)) != std::string::npos) {
        // Remove space after opening paren
        result.replace(paren_pos, 2, "(");
        // Find corresponding closing paren
        size_t close_paren = result.find(" )", paren_pos);
        if (close_paren != std::string::npos) {
            // Remove space before closing paren
            result.replace(close_paren, 2, ")");
        }
        paren_pos += 1; // Move past the paren
    }
    
    return result;
}

std::string TextPostProcessor::process_text_fragment(const std::string& text) {
    // Replace words with punctuation marks
    std::string processed = replace_punctuation(text);
    
    // Remove extra spaces at edges, but preserve newlines
    // Find first non-whitespace (but keep newlines)
    size_t start = 0;
    while (start < processed.length() && 
           (processed[start] == ' ' || processed[start] == '\t' || processed[start] == '\r')) {
        start++;
    }
    
    if (start == processed.length()) {
        // Only whitespace, but check for newlines
        if (processed.find('\n') != std::string::npos) {
            return processed; // Return newlines only
        }
        return "";
    }
    
    // Find last non-whitespace (but keep newlines)
    size_t end = processed.length() - 1;
    while (end > start && 
           (processed[end] == ' ' || processed[end] == '\t' || processed[end] == '\r')) {
        end--;
    }
    
    processed = processed.substr(start, end - start + 1);
    
    if (processed.empty()) {
        return "";
    }
    
    // Add buffered space from previous fragment (as in original)
    if (pending_space_ && !processed.empty()) {
        // Check first character/character sequence
        bool needs_space = true;
        
        // Check multi-character sequences (ellipsis, emojis)
        for (const std::string& punct : NO_SPACE_BEFORE) {
            if (processed.length() >= punct.length() && 
                processed.substr(0, punct.length()) == punct) {
                needs_space = false;
                break;
            }
        }
        
        // Also check single-byte characters (for fast check)
        if (needs_space && !processed.empty()) {
            unsigned char first_byte = static_cast<unsigned char>(processed[0]);
            // Check only ASCII characters
            if (first_byte < 128) {
                char first_char = static_cast<char>(first_byte);
                if (first_char == '.' || first_char == ',' || first_char == ':' || 
                    first_char == '!' || first_char == '?' || first_char == '-' || 
                    first_char == '»' || first_char == ')') {
                    needs_space = false;
                }
            }
        }
        
        if (needs_space) {
            processed = " " + processed;
        }
    }
    
    // Buffer space for next fragment
    pending_space_ = true;
    
    // Capitalize
    processed = apply_capitalization(processed);
    
    return processed;
}

std::string TextPostProcessor::apply_capitalization(const std::string& text) {
    if (text.empty()) {
        return text;
    }
    
    std::string result;
    result.reserve(text.length() * 2); // Reserve more space for UTF-8
    
    size_t i = 0;
    while (i < text.length()) {
        // Check multi-character sequences (emojis and UTF-8 punctuation)
        bool found_multi = false;
        for (const std::string& punct : CAPITALIZE_AFTER) {
            if (text.substr(i, punct.length()) == punct) {
                result += punct;
                i += punct.length();
                capitalize_next_ = true;
                found_multi = true;
                break;
            }
        }
        
        if (found_multi) {
            continue;
        }
        
        // Process UTF-8 characters
        unsigned char first_byte = static_cast<unsigned char>(text[i]);
        
        // Check if it's a UTF-8 start byte (not continuation byte)
        if (first_byte < 0x80) {
            // ASCII character
            char c = static_cast<char>(first_byte);
            
            if (std::isalpha(static_cast<unsigned char>(c))) {
                if (capitalize_next_) {
                    result += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
                    capitalize_next_ = false;
                } else {
                    result += c;
                }
            } else {
                result += c;
                // Check single-byte characters for capitalization
                if (c == '.' || c == '!' || c == '?' || c == '\n') {
                    capitalize_next_ = true;
                }
                // Spaces and other whitespace don't reset capitalize_next_
                // It stays true until we encounter a letter
            }
            i++;
        } else if ((first_byte & 0xE0) == 0xC0) {
            // 2-byte UTF-8 character (like Cyrillic)
            if (i + 1 < text.length()) {
                unsigned char byte1 = static_cast<unsigned char>(text[i]);
                unsigned char byte2 = static_cast<unsigned char>(text[i + 1]);
                
                if (capitalize_next_ && byte1 == 0xD1 && byte2 >= 0x80 && byte2 <= 0x8F) {
                    // Lowercase Cyrillic р-я (0x80-0x8F) -> Р-Я (0xA0-0xAF)
                    // р = 0xD1 0x80 -> Р = 0xD0 0xA0
                    result += static_cast<char>(0xD0);
                    result += static_cast<char>(byte2 + 0x20);
                    capitalize_next_ = false;
                    i += 2;
                } else if (capitalize_next_ && byte1 == 0xD0 && byte2 >= 0xB0 && byte2 <= 0xBF) {
                    // Lowercase Cyrillic а-п (0xB0-0xBF) -> А-П (0x90-0x9F)
                    result += static_cast<char>(0xD0);
                    result += static_cast<char>(byte2 - 0x20);
                    capitalize_next_ = false;
                    i += 2;
                } else {
                    // Not a Cyrillic letter or already uppercase, or capitalize_next_ is false
                    result += text.substr(i, 2);
                    i += 2;
                }
            } else {
                result += text[i];
                i++;
            }
        } else if ((first_byte & 0xF0) == 0xE0) {
            // 3-byte UTF-8 character
            if (i + 2 < text.length()) {
                result += text.substr(i, 3);
                i += 3;
            } else {
                result += text[i];
                i++;
            }
        } else if ((first_byte & 0xF8) == 0xF0) {
            // 4-byte UTF-8 character
            if (i + 3 < text.length()) {
                result += text.substr(i, 4);
                i += 4;
            } else {
                result += text[i];
                i++;
            }
        } else {
            // Invalid UTF-8 or continuation byte, skip
            result += text[i];
            i++;
        }
    }
    
    return result;
}

std::vector<Action> TextPostProcessor::process(const std::string& text) {
    if (text.empty()) {
        return {};
    }
    
    return extract_key_actions(text);
}

void TextPostProcessor::reset() {
    capitalize_next_ = true;
    pending_space_ = false;
}

} // namespace nerd_dictation::input
