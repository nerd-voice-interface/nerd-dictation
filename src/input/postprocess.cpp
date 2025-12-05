#include "nerd_dictation/input/postprocess.hpp"
#include <regex>
#include <algorithm>
#include <cctype>
#include <map>
#include <set>

namespace nerd_dictation::input {

// Слова → нажатия клавиш
static const std::map<std::string, std::string> KEY_MAP = {
    {"выполнить", "ctrl+Return"},
    {"выполнять", "ctrl+Return"},
    {"выполняй", "ctrl+Return"},
    {"выполни", "ctrl+Return"},
};

// Слова → знаки препинания
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
};

// Знаки после которых следующее слово с большой буквы
static const std::set<char> CAPITALIZE_AFTER = {'.', '!', '?', '\n'};

// Знаки перед которыми удаляется пробел
static const std::set<char> NO_SPACE_BEFORE = {'.', ',', ':', '!', '?', '—', '-', '»', ')'};

std::vector<Action> TextPostProcessor::extract_key_actions(const std::string& text) {
    std::vector<Action> actions;
    
    // Ищем ключевые слова для клавиш (сначала длинные фразы)
    std::vector<std::pair<std::string, std::string>> sorted_keys;
    for (const auto& [word, keys] : KEY_MAP) {
        sorted_keys.push_back({word, keys});
    }
    std::sort(sorted_keys.begin(), sorted_keys.end(), 
              [](const auto& a, const auto& b) { return a.first.length() > b.first.length(); });
    
    std::string remaining = text;
    size_t pos = 0;
    
    while (pos < remaining.length()) {
        bool found_key = false;
        
        for (const auto& [word, keys] : sorted_keys) {
            std::regex pattern("\\b" + std::regex_replace(word, std::regex("([.*+?^${}()|[\\]\\\\])"), "\\$1") + "\\b",
                             std::regex_constants::icase);
            std::smatch match;
            
            if (std::regex_search(remaining.begin() + pos, remaining.end(), match, pattern)) {
                // Текст до ключевого слова
                if (match.position() > 0) {
                    std::string before = remaining.substr(pos, match.position());
                    std::string processed = apply_capitalization(replace_punctuation(before));
                    if (!processed.empty()) {
                        actions.push_back(TextAction{processed});
                    }
                }
                
                // Клавиши
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
    
    // Остаток текста
    if (pos < remaining.length()) {
        std::string rest = remaining.substr(pos);
        std::string processed = apply_capitalization(replace_punctuation(rest));
        if (!processed.empty()) {
            actions.push_back(TextAction{processed});
        }
    }
    
    return actions.empty() ? std::vector<Action>{TextAction{apply_capitalization(replace_punctuation(text))}} : actions;
}

std::string TextPostProcessor::replace_punctuation(const std::string& text) {
    std::string result = text;
    
    // Сортируем по длине (сначала длинные фразы)
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
    
    // Убираем пробелы перед знаками препинания
    for (char punct : NO_SPACE_BEFORE) {
        std::string punct_str(1, punct);
        std::string space_punct = " " + punct_str;
        size_t pos = 0;
        while ((pos = result.find(space_punct, pos)) != std::string::npos) {
            result.replace(pos, space_punct.length(), punct_str);
            pos += punct_str.length();
        }
    }
    
    return result;
}

std::string TextPostProcessor::apply_capitalization(const std::string& text) {
    if (text.empty()) {
        return text;
    }
    
    std::string result;
    result.reserve(text.length());
    
    for (size_t i = 0; i < text.length(); ++i) {
        char c = text[i];
        
        if (std::isalpha(c)) {
            if (capitalize_next_) {
                result += std::toupper(c);
                capitalize_next_ = false;
            } else {
                result += c;
            }
        } else {
            result += c;
            if (CAPITALIZE_AFTER.count(c)) {
                capitalize_next_ = true;
            }
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
