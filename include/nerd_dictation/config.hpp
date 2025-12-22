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
    std::string word = "запись";  // Default wake word (Russian)
    std::string model_path = "~/.local/share/vosk/vosk-model-small-ru";
};

struct SpellCorrectionConfig {
    bool enabled = true;
    std::string model_path = "~/.local/share/jamspell/ru.bin";  // JamSpell model
    double known_words_penalty = 20.0;
    double unknown_words_penalty = 5.0;
};

struct Config {
    std::string backend = "dummy";
    std::string input_method = "xdotool";  /* Default xdotool, as in original */
    AudioConfig audio;

    /* Path to backend .so library (if empty - standard path is used) */
    std::string backend_library;

    /* Path to model (for backends loading model from file) */
    std::string model_path;

    /* Wake word detection */
    WakeWordConfig wake_word;

    /* Spell correction (JamSpell) */
    SpellCorrectionConfig spell_correction;
};

/**
 * Load configuration from TOML file
 */
Config load_config(const std::filesystem::path& config_path);

/**
 * Find configuration file
 * Checks: ~/.config/nerd-dictation/config.toml, then default.toml
 */
std::optional<std::filesystem::path> find_config_file();

} // namespace nerd_dictation
