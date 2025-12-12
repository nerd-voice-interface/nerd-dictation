/**
 * Wake Word Manager - Native C++ Vosk-based wake word detection
 *
 * Thread-safe manager for wake word detection using Vosk ASR.
 * Designed to work with VADManager for speech segmentation.
 */

#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <functional>
#include <cstdint>

namespace nerd_dictation {
namespace wake_word {

/**
 * Wake Word Detection Result
 */
struct WakeWordResult {
    std::string text;           // Recognized text
    bool detected = false;      // True if wake word was detected
    float confidence = 0.0f;    // Confidence (if available)
};

/**
 * Wake Word Manager Configuration
 */
struct WakeWordConfig {
    std::string model_path;             // Path to Vosk model directory
    std::string wake_word = "запись";   // Wake word to detect
    std::vector<std::string> variants;  // Alternative forms (e.g., "записи")
    int sample_rate = 16000;            // Audio sample rate
    int log_level = -1;                 // Vosk log level (-1 = quiet)
};

/**
 * Wake Word Manager
 *
 * Usage:
 *   WakeWordManager manager;
 *   manager.init(config);
 *
 *   // Process speech segment (after VAD detected speech end)
 *   auto result = manager.process_speech(audio_bytes);
 *   if (result.detected) {
 *       // Wake word detected!
 *   }
 */
class WakeWordManager {
public:
    /**
     * Get singleton instance
     */
    static WakeWordManager& instance();

    /**
     * Initialize with configuration
     * @param config Wake word configuration
     * @return true on success
     */
    bool init(const WakeWordConfig& config);

    /**
     * Check if initialized
     */
    bool is_initialized() const;

    /**
     * Process accumulated speech audio and check for wake word
     *
     * @param audio_data Raw int16 PCM audio bytes
     * @return Result with recognized text and detection status
     */
    WakeWordResult process_speech(const std::vector<int16_t>& audio_data);

    /**
     * Process speech from raw bytes
     */
    WakeWordResult process_speech(const uint8_t* data, size_t size);

    /**
     * Reset recognizer state
     * Call after wake word detection or when starting new detection session
     */
    void reset();

    /**
     * Get current wake word
     */
    std::string wake_word() const;

    /**
     * Set wake word and variants
     */
    void set_wake_word(const std::string& word, const std::vector<std::string>& variants = {});

    /**
     * Get sample rate
     */
    int sample_rate() const { return config_.sample_rate; }

    /**
     * Get last error message
     */
    std::string get_error() const;

    // Non-copyable, non-movable
    WakeWordManager(const WakeWordManager&) = delete;
    WakeWordManager& operator=(const WakeWordManager&) = delete;
    WakeWordManager(WakeWordManager&&) = delete;
    WakeWordManager& operator=(WakeWordManager&&) = delete;

private:
    WakeWordManager();
    ~WakeWordManager();

    // Vosk handles (opaque pointers)
    struct VoskImpl;
    std::unique_ptr<VoskImpl> vosk_;

    // Configuration
    WakeWordConfig config_;

    // State
    bool initialized_ = false;
    std::string last_error_;

    // Thread safety
    mutable std::mutex mutex_;

    // Internal methods
    bool load_model();
    bool contains_wake_word(const std::string& text) const;
};

/**
 * Check if Vosk model exists at path
 */
bool vosk_model_exists(const std::string& path);

/**
 * Find default Vosk model for wake word detection
 *
 * Searches in order:
 * 1. ~/.local/share/vosk/vosk-model-small-ru
 * 2. /usr/share/vosk/vosk-model-small-ru
 *
 * @return Path to model or empty string if not found
 */
std::string find_wake_word_model();

} // namespace wake_word
} // namespace nerd_dictation
