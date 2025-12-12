/**
 * VAD Manager - Persistent Voice Activity Detection using Silero VAD ONNX
 *
 * Thread-safe singleton that provides VAD for both wake word detection
 * and dictation. Eliminates Python VAD loading and associated heap issues.
 */

#pragma once

#include <memory>
#include <mutex>
#include <vector>
#include <string>
#include <array>
#include <cstdint>

// Forward declare ONNX Runtime types
namespace Ort {
    struct Env;
    struct Session;
    struct SessionOptions;
    struct MemoryInfo;
}

namespace nerd_dictation {
namespace vad {

/**
 * VAD Result - speech probability and state
 */
struct VADResult {
    float speech_probability = 0.0f;
    bool is_speech = false;
};

/**
 * VAD Manager Configuration
 */
struct VADConfig {
    std::string model_path;     // Path to silero_vad.onnx
    int sample_rate = 16000;    // 8000 or 16000
    float threshold = 0.5f;     // Speech detection threshold
    bool use_gpu = false;       // Use CUDA provider if available
};

/**
 * VAD Manager - Thread-safe singleton
 *
 * Usage:
 *   auto& vad = VADManager::instance();
 *   vad.init(config);
 *
 *   // Process audio chunks
 *   auto result = vad.process_chunk(audio_samples);
 *   if (result.is_speech) { ... }
 *
 *   // Reset state for new session
 *   vad.reset_states();
 */
class VADManager {
public:
    /**
     * Get singleton instance
     */
    static VADManager& instance();

    /**
     * Initialize VAD with configuration
     * @param config VAD configuration
     * @return true on success
     */
    bool init(const VADConfig& config);

    /**
     * Check if VAD is initialized
     */
    bool is_initialized() const;

    /**
     * Process audio chunk and get speech probability
     *
     * @param samples Audio samples (int16 PCM)
     * @param num_samples Number of samples (must be 512 for 16kHz or 256 for 8kHz)
     * @return VAD result with speech probability
     */
    VADResult process_chunk(const int16_t* samples, size_t num_samples);

    /**
     * Process audio chunk (vector version)
     */
    VADResult process_chunk(const std::vector<int16_t>& samples);

    /**
     * Reset internal states for new session
     * Call this at the START of dictation, not during idle
     */
    void reset_states();

    /**
     * Get current speech threshold
     */
    float threshold() const { return config_.threshold; }

    /**
     * Set speech threshold
     */
    void set_threshold(float threshold);

    /**
     * Get sample rate
     */
    int sample_rate() const { return config_.sample_rate; }

    /**
     * Get required chunk size in samples
     */
    size_t chunk_size() const;

    /**
     * Get last error message
     */
    std::string get_error() const;

    // Non-copyable, non-movable
    VADManager(const VADManager&) = delete;
    VADManager& operator=(const VADManager&) = delete;
    VADManager(VADManager&&) = delete;
    VADManager& operator=(VADManager&&) = delete;

private:
    VADManager();
    ~VADManager();

    // ONNX Runtime components (PIMPL)
    struct OnnxImpl;
    std::unique_ptr<OnnxImpl> onnx_;

    // Configuration
    VADConfig config_;

    // State
    bool initialized_ = false;
    std::string last_error_;

    // Internal state (Silero VAD specific)
    // State tensor: (2, 1, 128) - hidden states for LSTM
    std::array<float, 2 * 1 * 128> state_;

    // Context buffer: last 64 samples (16kHz) or 32 samples (8kHz)
    std::vector<float> context_;
    int last_sample_rate_ = 0;

    // Thread safety
    mutable std::mutex mutex_;

    // Internal methods
    bool load_model();
    size_t context_size() const;
    void init_state();
};

/**
 * Find default Silero VAD model path
 *
 * Searches in order:
 * 1. ~/.cache/torch/hub/snakers4_silero-vad_master/src/silero_vad/data/silero_vad.onnx
 * 2. /usr/share/nerd-dictation/silero_vad.onnx
 * 3. <executable_dir>/silero_vad.onnx
 *
 * @return Path to model or empty string if not found
 */
std::string find_vad_model();

} // namespace vad
} // namespace nerd_dictation
