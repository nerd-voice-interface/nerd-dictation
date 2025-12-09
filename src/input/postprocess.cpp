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
            std::regex pattern("\\b" + std::regex_replace(word, std::regex("([.*+?^${}()|[\\]\\\\])"), "\\$1") + "\\b",
                             std::regex_constants::icase);
            std::smatch match;
            std::string search_str = remaining.substr(pos);
            
            if (std::regex_search(search_str, match, pattern)) {
                // Text before key word - process recursively
                if (match.position() > 0) {
                    std::string before = search_str.substr(0, match.position());
                    auto before_actions = extract_key_actions(before);
                    actions.insert(actions.end(), before_actions.begin(), before_actions.end());
                }
                
                // Keys
                actions.push_back(KeyAction{keys});
                
                pos += match.position() + match.length();
                found_key = true;
                break;
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
    
    // Sort by length (longer phrases first)
    std::vector<std::pair<std::string, std::string>> sorted;
    for (const auto& [word, punct] : PUNCTUATION_MAP) {
        sorted.push_back({word, punct});
    }
    std::sort(sorted.begin(), sorted.end(),
              [](const auto& a, const auto& b) { return a.first.length() > b.first.length(); });
    
    for (const auto& [word, punct] : sorted) {
        std::regex pattern("\\b" + std::regex_replace(word, std::regex("([.*+?^${}()|[\\]\\\\])"), "\\$1") + "\\b",
                          std::regex_constants::icase);
        result = std::regex_replace(result, pattern, punct);
    }
    
    // Remove spaces before punctuation marks
    for (const std::string& punct : NO_SPACE_BEFORE) {
        std::string space_punct = " " + punct;
        size_t pos = 0;
        while ((pos = result.find(space_punct, pos)) != std::string::npos) {
            result.replace(pos, space_punct.length(), punct);
            pos += punct.length();
        }
    }
    
    return result;
}

std::string TextPostProcessor::process_text_fragment(const std::string& text) {
    // Replace words with punctuation marks
    std::string processed = replace_punctuation(text);
    
    // Remove extra spaces at edges
    size_t start = processed.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) {
        return "";
    }
    size_t end = processed.find_last_not_of(" \t\n\r");
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
        // Check multi-character sequences (emojis and UTF-8)
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
        
        // Process regular characters
        char c = text[i];
        
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
        }
        
        i++;
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
