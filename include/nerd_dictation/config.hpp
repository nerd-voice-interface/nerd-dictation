#pragma once

/**
 * Конфигурация nerd-dictation-ex
 * 
 * TODO: Реализовать загрузку из TOML файла
 */

namespace nerd_dictation {

struct AudioConfig {
    std::string source = "pw-cat";
    int sample_rate = 16000;
    size_t chunk_size = 8000;
};

struct Config {
    std::string backend = "vosk";
    std::string input_method = "ydotool";
    AudioConfig audio;
    
    // TODO: Добавить остальные секции конфигурации
};

// TODO: Реализовать функцию загрузки
// Config load_config(const std::filesystem::path& path);

} // namespace nerd_dictation
