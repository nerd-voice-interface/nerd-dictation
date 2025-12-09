#include "nerd_dictation/config.hpp"
#include <toml.hpp>
#include <spdlog/spdlog.h>
#include <cstdlib>
#include <filesystem>

namespace nerd_dictation {

std::optional<std::filesystem::path> find_config_file() {
    // Check ~/.config/nerd-dictation/config.toml
    const char* home = std::getenv("HOME");
    if (home) {
        std::filesystem::path user_config = std::filesystem::path(home) / ".config" / "nerd-dictation" / "config.toml";
        if (std::filesystem::exists(user_config)) {
            return user_config;
        }
        
        // Check default.toml in the same directory
        std::filesystem::path default_config = std::filesystem::path(home) / ".config" / "nerd-dictation" / "default.toml";
        if (std::filesystem::exists(default_config)) {
            return default_config;
        }
    }
    
    return std::nullopt;
}

Config load_config(const std::filesystem::path& config_path) {
    Config config;
    
    if (!std::filesystem::exists(config_path)) {
        spdlog::warn("Configuration file not found: {}, using default values", config_path.string());
        return config;
    }
    
    try {
        auto data = toml::parse(config_path);
        
        // [general]
        if (data.contains("general")) {
            const auto& general = toml::find(data, "general");
            if (general.contains("backend")) {
                config.backend = toml::find<std::string>(general, "backend");
            }
            if (general.contains("input_method")) {
                config.input_method = toml::find<std::string>(general, "input_method");
            }
        }
        
        // [audio]
        if (data.contains("audio")) {
            const auto& audio = toml::find(data, "audio");
            if (audio.contains("source")) {
                config.audio.source = toml::find<std::string>(audio, "source");
            }
            if (audio.contains("sample_rate")) {
                config.audio.sample_rate = toml::find<int>(audio, "sample_rate");
            }
            if (audio.contains("chunk_size")) {
                config.audio.chunk_size = toml::find<int>(audio, "chunk_size");
            }
        }
        
        // [dummy] or [vosk] or [gigaam] - model_path
        if (data.contains("dummy")) {
            const auto& dummy = toml::find(data, "dummy");
            if (dummy.contains("model_path")) {
                config.model_path = toml::find<std::string>(dummy, "model_path");
            }
        } else if (data.contains("vosk")) {
            const auto& vosk = toml::find(data, "vosk");
            if (vosk.contains("model_path")) {
                std::string path = toml::find<std::string>(vosk, "model_path");
                // Expand ~
                if (path.front() == '~') {
                    const char* home = std::getenv("HOME");
                    if (home) {
                        path = std::string(home) + path.substr(1);
                    }
                }
                config.model_path = path;
            }
        }
        
        // [wake_word] - wake word detection settings
        if (data.contains("wake_word")) {
            const auto& wake_word = toml::find(data, "wake_word");
            if (wake_word.contains("enabled")) {
                config.wake_word.enabled = toml::find<bool>(wake_word, "enabled");
            }
            if (wake_word.contains("word")) {
                config.wake_word.word = toml::find<std::string>(wake_word, "word");
            }
            if (wake_word.contains("model_path")) {
                std::string path = toml::find<std::string>(wake_word, "model_path");
                // Expand ~
                if (!path.empty() && path.front() == '~') {
                    const char* home = std::getenv("HOME");
                    if (home) {
                        path = std::string(home) + path.substr(1);
                    }
                }
                config.wake_word.model_path = path;
            }
        }
        
        spdlog::info("Configuration loaded from: {}", config_path.string());
        
    } catch (const std::exception& e) {
        spdlog::error("Error loading configuration: {}", e.what());
    }
    
    return config;
}

} // namespace nerd_dictation
