/**
 * Audio Preprocessor - WebRTC-based AGC and Noise Suppression
 *
 * Provides adaptive gain control and noise reduction for speech recognition.
 * Works with any input sample rate by resampling to 16kHz for WebRTC processing.
 */

#ifndef NERD_DICTATION_AUDIO_PREPROCESSOR_HPP
#define NERD_DICTATION_AUDIO_PREPROCESSOR_HPP

#include <cstddef>
#include <cstdint>
#include <memory>

namespace nerd_dictation::audio {

/**
 * Audio preprocessor with AGC and noise suppression.
 *
 * Uses WebRTC Audio Processing library for:
 * - Adaptive Gain Control (AGC2)
 * - Noise Suppression
 * - High-pass filtering
 *
 * Internally resamples to 16kHz for processing, then back to original rate.
 */
class AudioPreprocessor {
public:
    struct Config {
        int input_sample_rate = 16000;   // Input sample rate (8kHz, 16kHz, etc.)
        int process_sample_rate = 16000; // Processing sample rate (WebRTC minimum 16kHz)

        // Noise suppression level: 0=Low, 1=Moderate, 2=High, 3=VeryHigh
        int noise_suppression_level = 2;  // High by default

        // AGC settings
        bool agc_enabled = true;
        float agc_target_level_dbfs = 3.0f;  // Target level (3 dB below full scale)

        // High-pass filter to remove DC offset and low-frequency noise
        bool high_pass_filter_enabled = true;
    };

    AudioPreprocessor();
    ~AudioPreprocessor();

    // Non-copyable
    AudioPreprocessor(const AudioPreprocessor&) = delete;
    AudioPreprocessor& operator=(const AudioPreprocessor&) = delete;

    // Movable
    AudioPreprocessor(AudioPreprocessor&&) noexcept;
    AudioPreprocessor& operator=(AudioPreprocessor&&) noexcept;

    /**
     * Initialize the preprocessor with given configuration.
     * @return true on success, false on failure
     */
    bool initialize(const Config& config);

    /**
     * Process audio samples.
     * Input and output are int16 samples at input_sample_rate.
     *
     * @param input Input samples (int16, mono)
     * @param num_samples Number of input samples
     * @param output Output buffer (must be at least num_samples)
     * @return Number of output samples, or -1 on error
     */
    int process(const int16_t* input, size_t num_samples, int16_t* output);

    /**
     * Reset the preprocessor state (clear buffers, reset AGC).
     */
    void reset();

    /**
     * Check if preprocessor is initialized.
     */
    bool is_initialized() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nerd_dictation::audio

#endif // NERD_DICTATION_AUDIO_PREPROCESSOR_HPP
