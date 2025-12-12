/**
 * Wake Word Manager Implementation using Vosk C API
 */

#include "nerd_dictation/wake_word/wake_word_manager.hpp"
#include <spdlog/spdlog.h>
#include <cstdlib>
#include <filesystem>
#include <algorithm>
#include <cctype>

// Vosk C API declarations
extern "C" {
    typedef struct VoskModel VoskModel;
    typedef struct VoskRecognizer VoskRecognizer;

    VoskModel *vosk_model_new(const char *model_path);
    void vosk_model_free(VoskModel *model);
    int vosk_model_find_word(VoskModel *model, const char *word);

    VoskRecognizer *vosk_recognizer_new(VoskModel *model, float sample_rate);
    void vosk_recognizer_free(VoskRecognizer *recognizer);
    void vosk_recognizer_reset(VoskRecognizer *recognizer);

    int vosk_recognizer_accept_waveform_s(VoskRecognizer *recognizer, const short *data, int length);
    const char *vosk_recognizer_result(VoskRecognizer *recognizer);
    const char *vosk_recognizer_partial_result(VoskRecognizer *recognizer);
    const char *vosk_recognizer_final_result(VoskRecognizer *recognizer);

    void vosk_set_log_level(int log_level);
}

namespace fs = std::filesystem;

namespace nerd_dictation {
namespace wake_word {

// PIMPL for Vosk handles
struct WakeWordManager::VoskImpl {
    VoskModel* model = nullptr;
    VoskRecognizer* recognizer = nullptr;

    ~VoskImpl() {
        if (recognizer) {
            vosk_recognizer_free(recognizer);
            recognizer = nullptr;
        }
        if (model) {
            vosk_model_free(model);
            model = nullptr;
        }
    }
};

WakeWordManager& WakeWordManager::instance() {
    static WakeWordManager instance;
    return instance;
}

WakeWordManager::WakeWordManager()
    : vosk_(std::make_unique<VoskImpl>())
{
}

WakeWordManager::~WakeWordManager() = default;

bool WakeWordManager::init(const WakeWordConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);

    config_ = config;

    // Set Vosk log level
    vosk_set_log_level(config_.log_level);

    // Find model if not specified
    if (config_.model_path.empty()) {
        config_.model_path = find_wake_word_model();
        if (config_.model_path.empty()) {
            last_error_ = "Vosk model not found";
            spdlog::error("WakeWordManager: {}", last_error_);
            return false;
        }
    }

    // Check model exists
    if (!vosk_model_exists(config_.model_path)) {
        last_error_ = "Model path does not exist: " + config_.model_path;
        spdlog::error("WakeWordManager: {}", last_error_);
        return false;
    }

    return load_model();
}

bool WakeWordManager::load_model() {
    try {
        // Free existing model if any
        vosk_.reset(new VoskImpl());

        spdlog::info("WakeWordManager: Loading Vosk model from {}", config_.model_path);

        // Load model
        vosk_->model = vosk_model_new(config_.model_path.c_str());
        if (!vosk_->model) {
            last_error_ = "Failed to load Vosk model";
            spdlog::error("WakeWordManager: {}", last_error_);
            return false;
        }

        // Create recognizer
        vosk_->recognizer = vosk_recognizer_new(vosk_->model, static_cast<float>(config_.sample_rate));
        if (!vosk_->recognizer) {
            last_error_ = "Failed to create Vosk recognizer";
            spdlog::error("WakeWordManager: {}", last_error_);
            vosk_model_free(vosk_->model);
            vosk_->model = nullptr;
            return false;
        }

        spdlog::info("WakeWordManager: Model loaded, wake word='{}', sample_rate={}",
                     config_.wake_word, config_.sample_rate);

        initialized_ = true;
        return true;

    } catch (const std::exception& e) {
        last_error_ = std::string("Error loading model: ") + e.what();
        spdlog::error("WakeWordManager: {}", last_error_);
        initialized_ = false;
        return false;
    }
}

bool WakeWordManager::is_initialized() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return initialized_;
}

WakeWordResult WakeWordManager::process_speech(const std::vector<int16_t>& audio_data) {
    std::lock_guard<std::mutex> lock(mutex_);

    WakeWordResult result;

    if (!initialized_ || !vosk_->recognizer) {
        last_error_ = "WakeWordManager not initialized";
        return result;
    }

    if (audio_data.empty()) {
        return result;
    }

    // Feed audio to recognizer
    vosk_recognizer_accept_waveform_s(
        vosk_->recognizer,
        audio_data.data(),
        static_cast<int>(audio_data.size())
    );

    // Get result
    const char* json_result = vosk_recognizer_result(vosk_->recognizer);
    if (!json_result) {
        return result;
    }

    // Parse JSON result to extract text
    // Format: {"text": "recognized text"}
    std::string json_str(json_result);

    // Simple JSON parsing for "text" field
    size_t text_pos = json_str.find("\"text\"");
    if (text_pos != std::string::npos) {
        size_t colon_pos = json_str.find(':', text_pos);
        if (colon_pos != std::string::npos) {
            size_t quote_start = json_str.find('"', colon_pos);
            if (quote_start != std::string::npos) {
                size_t quote_end = json_str.find('"', quote_start + 1);
                if (quote_end != std::string::npos) {
                    result.text = json_str.substr(quote_start + 1, quote_end - quote_start - 1);
                }
            }
        }
    }

    // Check if wake word is in the result
    if (!result.text.empty()) {
        result.detected = contains_wake_word(result.text);
        if (result.detected) {
            spdlog::info("WakeWordManager: Wake word detected in '{}'", result.text);
        }
    }

    return result;
}

WakeWordResult WakeWordManager::process_speech(const uint8_t* data, size_t size) {
    // Convert bytes to int16 samples
    size_t num_samples = size / sizeof(int16_t);
    std::vector<int16_t> samples(num_samples);
    std::memcpy(samples.data(), data, num_samples * sizeof(int16_t));
    return process_speech(samples);
}

void WakeWordManager::reset() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (vosk_->recognizer) {
        vosk_recognizer_reset(vosk_->recognizer);
        spdlog::debug("WakeWordManager: Recognizer reset");
    }
}

std::string WakeWordManager::wake_word() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return config_.wake_word;
}

void WakeWordManager::set_wake_word(const std::string& word, const std::vector<std::string>& variants) {
    std::lock_guard<std::mutex> lock(mutex_);
    config_.wake_word = word;
    config_.variants = variants;
    spdlog::info("WakeWordManager: Wake word set to '{}'", word);
}

std::string WakeWordManager::get_error() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return last_error_;
}

bool WakeWordManager::contains_wake_word(const std::string& text) const {
    // Convert text to lowercase for comparison
    std::string lower_text = text;
    std::transform(lower_text.begin(), lower_text.end(), lower_text.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    // Check main wake word
    std::string lower_wake = config_.wake_word;
    std::transform(lower_wake.begin(), lower_wake.end(), lower_wake.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    if (lower_text.find(lower_wake) != std::string::npos) {
        return true;
    }

    // Check variants
    for (const auto& variant : config_.variants) {
        std::string lower_variant = variant;
        std::transform(lower_variant.begin(), lower_variant.end(), lower_variant.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        if (lower_text.find(lower_variant) != std::string::npos) {
            return true;
        }
    }

    return false;
}

// Free functions

bool vosk_model_exists(const std::string& path) {
    if (!fs::exists(path)) {
        return false;
    }
    // Check for required files in model directory
    return fs::exists(fs::path(path) / "am") ||
           fs::exists(fs::path(path) / "conf") ||
           fs::exists(fs::path(path) / "graph");
}

std::string find_wake_word_model() {
    std::vector<std::string> search_paths;

    // 1. User local share
    if (const char* home = std::getenv("HOME")) {
        search_paths.push_back(std::string(home) + "/.local/share/vosk/vosk-model-small-ru");
        search_paths.push_back(std::string(home) + "/.local/share/vosk/vosk-model-ru");
    }

    // 2. XDG data home
    if (const char* data_home = std::getenv("XDG_DATA_HOME")) {
        search_paths.push_back(std::string(data_home) + "/vosk/vosk-model-small-ru");
    }

    // 3. System paths
    search_paths.push_back("/usr/share/vosk/vosk-model-small-ru");
    search_paths.push_back("/usr/local/share/vosk/vosk-model-small-ru");

    // Search for model
    for (const auto& path : search_paths) {
        if (vosk_model_exists(path)) {
            spdlog::debug("WakeWordManager: Found model at {}", path);
            return path;
        }
    }

    spdlog::warn("WakeWordManager: Vosk model not found in any search path");
    return "";
}

} // namespace wake_word
} // namespace nerd_dictation
