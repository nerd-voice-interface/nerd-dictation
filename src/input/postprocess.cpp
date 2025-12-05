#include "nerd_dictation/input/postprocess.hpp"
#include <regex>
#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <string>
#include <vector>

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
    // Смайлики (как в оригинале)
    {"смайлик улыбка", "😊"},
    {"смайлик смех", "😂"},
    {"смайлик крутой", "😎"},
    {"смайлик грустный", "😢"},
    {"смайлик сердце", "❤️"},
    {"смайлик огонь", "🔥"},
    {"смайлик палец вверх", "👍"},
    {"смайлик ок", "👌"},
};

// Знаки после которых следующее слово с большой буквы (используем строки для поддержки эмодзи)
static const std::set<std::string> CAPITALIZE_AFTER = {".", "!", "?", "😊", "😂", "😎", "😢", "❤️", "🔥", "👍", "👌", "\n"};

// Знаки перед которыми удаляется пробел
static const std::set<std::string> NO_SPACE_BEFORE = {".", ",", ":", "!", "?", "—", "-", "...", "»", ")"};

std::vector<Action> TextPostProcessor::extract_key_actions(const std::string& text) {
    std::vector<Action> actions;
    
    // Ищем ключевые слова для клавиш (сначала длинные фразы)
    std::vector<std::pair<std::string, std::string>> sorted_keys;
    for (const auto& [word, keys] : KEY_MAP) {
        sorted_keys.push_back({word, keys});
    }
    std::sort(sorted_keys.begin(), sorted_keys.end(), 
              [](const auto& a, const auto& b) { return a.first.length() > b.first.length(); });
    
    // Рекурсивно разбиваем текст по ключевым словам (как в оригинале)
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
                // Текст до ключевого слова - обрабатываем рекурсивно
                if (match.position() > 0) {
                    std::string before = search_str.substr(0, match.position());
                    auto before_actions = extract_key_actions(before);
                    actions.insert(actions.end(), before_actions.begin(), before_actions.end());
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
    
    // Остаток текста - обрабатываем как текстовый фрагмент
    if (pos < remaining.length()) {
        std::string rest = remaining.substr(pos);
        std::string processed = process_text_fragment(rest);
        if (!processed.empty()) {
            actions.push_back(TextAction{processed});
        }
    }
    
    // Если не нашли ключевых слов - обрабатываем весь текст
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
    // Заменяем слова на знаки препинания
    std::string processed = replace_punctuation(text);
    
    // Убираем лишние пробелы по краям
    size_t start = processed.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) {
        return "";
    }
    size_t end = processed.find_last_not_of(" \t\n\r");
    processed = processed.substr(start, end - start + 1);
    
    if (processed.empty()) {
        return "";
    }
    
    // Добавляем буферизованный пробел от предыдущего фрагмента
    if (pending_space_ && !processed.empty()) {
        // Проверяем первый символ (может быть многосимвольным для эмодзи)
        bool needs_space = true;
        for (const std::string& punct : NO_SPACE_BEFORE) {
            if (processed.find(punct) == 0) {
                needs_space = false;
                break;
            }
        }
        // Также проверяем однобайтовые символы
        if (needs_space && processed.length() > 0) {
            char first_char = processed[0];
            if (first_char == '.' || first_char == ',' || first_char == ':' || 
                first_char == '!' || first_char == '?' || first_char == '—' || 
                first_char == '-' || first_char == '»' || first_char == ')') {
                needs_space = false;
            }
        }
        if (needs_space) {
            processed = " " + processed;
        }
    }
    
    // Буферизуем пробел для следующего фрагмента
    pending_space_ = true;
    
    // Капитализируем
    processed = apply_capitalization(processed);
    
    return processed;
}

std::string TextPostProcessor::apply_capitalization(const std::string& text) {
    if (text.empty()) {
        return text;
    }
    
    std::string result;
    result.reserve(text.length() * 2); // Резервируем больше места для UTF-8
    
    size_t i = 0;
    while (i < text.length()) {
        // Проверяем многосимвольные последовательности (эмодзи и UTF-8)
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
        
        // Обрабатываем обычные символы
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
            // Проверяем однобайтовые символы для капитализации
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
