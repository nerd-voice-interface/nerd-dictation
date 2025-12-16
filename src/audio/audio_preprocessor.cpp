/**
 * Audio Preprocessor Implementation
 *
 * Uses WebRTC Audio Processing for AGC and noise suppression,
 * with libsamplerate for resampling between input rate and 16kHz.
 */

#include "nerd_dictation/audio/audio_preprocessor.hpp"
#include <modules/audio_processing/include/audio_processing.h>
#include <samplerate.h>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace nerd_dictation::audio {

// WebRTC processes audio in 10ms frames
static constexpr int kFrameDurationMs = 10;

class AudioPreprocessor::Impl {
public:
    Impl() = default;
    ~Impl() {
        cleanup();
    }

    bool initialize(const Config& config) {
        try {
            config_ = config;

            // Calculate frame sizes
            input_frame_size_ = config.input_sample_rate * kFrameDurationMs / 1000;
            process_frame_size_ = config.process_sample_rate * kFrameDurationMs / 1000;

            // Check if resampling is needed
            needs_resampling_ = (config.input_sample_rate != config.process_sample_rate);

            if (needs_resampling_) {
                // Initialize resamplers (libsamplerate)
                int error;
                src_state_up_ = src_new(SRC_SINC_FASTEST, 1, &error);
                if (!src_state_up_) {
                    spdlog::error("[AudioPreprocessor] Failed to create upsampler: {}",
                                  src_strerror(error));
                    return false;
                }

                src_state_down_ = src_new(SRC_SINC_FASTEST, 1, &error);
                if (!src_state_down_) {
                    spdlog::error("[AudioPreprocessor] Failed to create downsampler: {}",
                                  src_strerror(error));
                    cleanup();
                    return false;
                }
            }

            // Initialize WebRTC Audio Processing
            webrtc::AudioProcessing::Config apm_config;

            // High-pass filter
            apm_config.high_pass_filter.enabled = config.high_pass_filter_enabled;

            // Noise suppression
            apm_config.noise_suppression.enabled = true;
            switch (config.noise_suppression_level) {
                case 0: apm_config.noise_suppression.level = webrtc::AudioProcessing::Config::NoiseSuppression::kLow; break;
                case 1: apm_config.noise_suppression.level = webrtc::AudioProcessing::Config::NoiseSuppression::kModerate; break;
                case 2: apm_config.noise_suppression.level = webrtc::AudioProcessing::Config::NoiseSuppression::kHigh; break;
                case 3: apm_config.noise_suppression.level = webrtc::AudioProcessing::Config::NoiseSuppression::kVeryHigh; break;
                default: apm_config.noise_suppression.level = webrtc::AudioProcessing::Config::NoiseSuppression::kHigh; break;
            }

            // AGC2 (adaptive digital gain control)
            if (config.agc_enabled) {
                apm_config.gain_controller2.enabled = true;
                apm_config.gain_controller2.adaptive_digital.enabled = true;
            }

            // Create AudioProcessing instance
            apm_ = webrtc::AudioProcessingBuilder().Create();
            if (!apm_) {
                spdlog::error("[AudioPreprocessor] Failed to create WebRTC AudioProcessing");
                cleanup();
                return false;
            }
            apm_->ApplyConfig(apm_config);

            // Allocate buffers
            upsample_buffer_.resize(process_frame_size_ * 4);  // Extra space for resampling
            downsample_buffer_.resize(input_frame_size_ * 4);
            process_buffer_.resize(process_frame_size_);
            input_remainder_.reserve(input_frame_size_ * 2);

            initialized_ = true;
            spdlog::info("[AudioPreprocessor] Initialized: {}Hz -> {}Hz, NS={}, AGC={}",
                        config.input_sample_rate,
                        config.process_sample_rate,
                        config.noise_suppression_level,
                        config.agc_enabled ? "on" : "off");

            return true;
        } catch (const std::exception& e) {
            spdlog::error("[AudioPreprocessor] Exception during initialization: {}", e.what());
            cleanup();
            return false;
        } catch (...) {
            spdlog::error("[AudioPreprocessor] Unknown exception during initialization");
            cleanup();
            return false;
        }
    }

    void cleanup() {
        if (src_state_up_) {
            src_delete(src_state_up_);
            src_state_up_ = nullptr;
        }
        if (src_state_down_) {
            src_delete(src_state_down_);
            src_state_down_ = nullptr;
        }
        apm_ = nullptr;
        initialized_ = false;
    }

    int process(const int16_t* input, size_t num_samples, int16_t* output) {
        if (!initialized_) return -1;
        if (!input || !output || num_samples == 0) return 0;

        try {
            // Append input to remainder buffer
            size_t total_samples = input_remainder_.size() + num_samples;
            std::vector<float> float_input(total_samples);

            // Copy remainder
            if (!input_remainder_.empty()) {
                std::copy(input_remainder_.begin(), input_remainder_.end(), float_input.begin());
            }

            // Convert new input to float and append
            for (size_t i = 0; i < num_samples; ++i) {
                float_input[input_remainder_.size() + i] = input[i] / 32768.0f;
            }
            input_remainder_.clear();

            // Process in frames
            size_t samples_processed = 0;
            std::vector<float> output_float;
            output_float.reserve(num_samples + input_frame_size_);

            while (samples_processed + input_frame_size_ <= float_input.size()) {
                float* frame_data = float_input.data() + samples_processed;
                size_t frames_to_process = process_frame_size_;

                if (needs_resampling_) {
                    // Upsample frame to processing rate
                    SRC_DATA src_data_up;
                    src_data_up.data_in = frame_data;
                    src_data_up.data_out = upsample_buffer_.data();
                    src_data_up.input_frames = input_frame_size_;
                    src_data_up.output_frames = upsample_buffer_.size();
                    src_data_up.src_ratio = static_cast<double>(config_.process_sample_rate) / config_.input_sample_rate;
                    src_data_up.end_of_input = 0;

                    int error = src_process(src_state_up_, &src_data_up);
                    if (error) {
                        spdlog::error("[AudioPreprocessor] Upsample error: {}", src_strerror(error));
                        return -1;
                    }

                    frames_to_process = src_data_up.output_frames_gen;
                    frame_data = upsample_buffer_.data();
                }

                // Process each 10ms frame at processing rate
                for (size_t frame_start = 0; frame_start + process_frame_size_ <= frames_to_process; frame_start += process_frame_size_) {
                    // Copy to process buffer
                    for (size_t i = 0; i < process_frame_size_; ++i) {
                        process_buffer_[i] = frame_data[frame_start + i];
                    }

                    // Create stream config
                    webrtc::StreamConfig stream_config(config_.process_sample_rate, 1);

                    // Process frame
                    channel_ptrs_[0] = process_buffer_.data();
                    int result = apm_->ProcessStream(
                        const_cast<const float* const*>(channel_ptrs_),
                        stream_config,
                        stream_config,
                        channel_ptrs_
                    );
                    if (result != 0) {
                        spdlog::warn("[AudioPreprocessor] ProcessStream error: {}", result);
                    }

                    // Copy back
                    for (size_t i = 0; i < process_frame_size_; ++i) {
                        frame_data[frame_start + i] = process_buffer_[i];
                    }
                }

                if (needs_resampling_) {
                    // Downsample back to input rate
                    SRC_DATA src_data_down;
                    src_data_down.data_in = upsample_buffer_.data();
                    src_data_down.data_out = downsample_buffer_.data();
                    src_data_down.input_frames = frames_to_process;
                    src_data_down.output_frames = downsample_buffer_.size();
                    src_data_down.src_ratio = static_cast<double>(config_.input_sample_rate) / config_.process_sample_rate;
                    src_data_down.end_of_input = 0;

                    int error = src_process(src_state_down_, &src_data_down);
                    if (error) {
                        spdlog::error("[AudioPreprocessor] Downsample error: {}", src_strerror(error));
                        return -1;
                    }

                    // Append to output
                    for (long i = 0; i < src_data_down.output_frames_gen; ++i) {
                        output_float.push_back(downsample_buffer_[i]);
                    }
                } else {
                    // No resampling needed, just copy processed frames
                    for (size_t i = 0; i < frames_to_process; ++i) {
                        output_float.push_back(frame_data[i]);
                    }
                }

                samples_processed += input_frame_size_;
            }

            // Save remainder for next call
            if (samples_processed < float_input.size()) {
                input_remainder_.assign(
                    float_input.begin() + samples_processed,
                    float_input.end()
                );
            }

            // Convert output to int16
            size_t output_samples = std::min(output_float.size(), num_samples);
            for (size_t i = 0; i < output_samples; ++i) {
                float sample = output_float[i];
                // Clip to [-1, 1]
                sample = std::max(-1.0f, std::min(1.0f, sample));
                output[i] = static_cast<int16_t>(sample * 32767.0f);
            }

            return static_cast<int>(output_samples);
        } catch (const std::exception& e) {
            spdlog::error("[AudioPreprocessor] Exception in process(): {}", e.what());
            return -1;
        } catch (...) {
            spdlog::error("[AudioPreprocessor] Unknown exception in process()");
            return -1;
        }
    }

    void reset() {
        if (src_state_up_) src_reset(src_state_up_);
        if (src_state_down_) src_reset(src_state_down_);
        input_remainder_.clear();
        // Note: WebRTC APM doesn't have a public reset method
    }

    bool is_initialized() const { return initialized_; }

private:
    Config config_;
    bool initialized_ = false;
    bool needs_resampling_ = false;

    // Frame sizes (samples per 10ms)
    size_t input_frame_size_ = 160;   // default for 16kHz
    size_t process_frame_size_ = 160; // 16kHz * 10ms

    // Resampler states
    SRC_STATE* src_state_up_ = nullptr;
    SRC_STATE* src_state_down_ = nullptr;

    // WebRTC Audio Processing
    rtc::scoped_refptr<webrtc::AudioProcessing> apm_;

    // Buffers
    std::vector<float> upsample_buffer_;
    std::vector<float> downsample_buffer_;
    std::vector<float> process_buffer_;
    std::vector<float> input_remainder_;

    // Channel pointers for WebRTC API (mono = 1 channel)
    float* channel_ptrs_[1] = {nullptr};
};

// Public interface implementation

AudioPreprocessor::AudioPreprocessor() : impl_(std::make_unique<Impl>()) {}
AudioPreprocessor::~AudioPreprocessor() = default;

AudioPreprocessor::AudioPreprocessor(AudioPreprocessor&&) noexcept = default;
AudioPreprocessor& AudioPreprocessor::operator=(AudioPreprocessor&&) noexcept = default;

bool AudioPreprocessor::initialize(const Config& config) {
    return impl_->initialize(config);
}

int AudioPreprocessor::process(const int16_t* input, size_t num_samples, int16_t* output) {
    return impl_->process(input, num_samples, output);
}

void AudioPreprocessor::reset() {
    impl_->reset();
}

bool AudioPreprocessor::is_initialized() const {
    return impl_->is_initialized();
}

} // namespace nerd_dictation::audio
