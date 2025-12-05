#pragma once

#include <string>
#include <filesystem>
#include <optional>

namespace nerd_dictation {

struct AudioConfig {
    std::string source = "pw-cat";
    int sample_rate = 16000;
    size_t chunk_size = 8000;
};

struct WakeWordConfig {
    bool enabled = true;
    std::string word = "запись";  // Wake word по умолчанию
    std::string model_path = "~/.local/share/vosk/vosk-model-small-ru";
};

struct Config {
    std::string backend = "dummy";
    std::string input_method = "xdotool";  // По умолчанию xdotool, как в оригинале
    AudioConfig audio;
    
    // Путь к модели (для реальных бэкендов)
    std::string model_path;
    
    // Wake word detection
    WakeWordConfig wake_word;
};

/**
 * Загрузить конфигурацию из TOML файла
 */
Config load_config(const std::filesystem::path& config_path);

/**
 * Найти конфигурационный файл
 * Проверяет: ~/.config/nerd-dictation/config.toml, затем default.toml
 */
std::optional<std::filesystem::path> find_config_file();

} // namespace nerd_dictation
