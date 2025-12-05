#pragma once

#include <string>
#include <vector>
#include <variant>

namespace nerd_dictation::input {

/**
 * Действие: ввод текста
 */
struct TextAction {
    std::string text;
};

/**
 * Действие: нажатие клавиш
 */
struct KeyAction {
    std::string keys;  // Формат: "ctrl+Return", "shift+Tab"
};

using Action = std::variant<TextAction, KeyAction>;

/**
 * Постобработчик текста
 * - Замена слов на знаки препинания
 * - Замена слов на нажатия клавиш
 * - Капитализация после точки/!/?
 */
class TextPostProcessor {
private:
    bool capitalize_next_ = true;
    bool pending_space_ = false;
    
    std::string replace_punctuation(const std::string& text);
    std::string apply_capitalization(const std::string& text);
    std::string process_text_fragment(const std::string& text);  // Как в оригинале: _process_text_fragment
    std::vector<Action> extract_key_actions(const std::string& text);
    
public:
    /**
     * Обработать текст
     */
    std::vector<Action> process(const std::string& text);
    
    /**
     * Сбросить состояние (начать новую диктовку)
     */
    void reset();
};

} // namespace nerd_dictation::input
