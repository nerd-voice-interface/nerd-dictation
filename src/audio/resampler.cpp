/**
 * Resampler - implementation (libsamplerate)
 */

#include "nerd_dictation/audio/resampler.hpp"

#include <samplerate.h>

#include <cmath>
#include <stdexcept>
#include <string>

namespace nerd_dictation {
namespace audio {

Resampler::Resampler(int in_rate, int out_rate)
    : in_rate_(in_rate), out_rate_(out_rate),
      ratio_(static_cast<double>(out_rate) / in_rate) {
    if (in_rate <= 0 || out_rate <= 0) {
        throw std::runtime_error("Resampler: invalid rates " + std::to_string(in_rate) +
                                 " -> " + std::to_string(out_rate));
    }
    if (in_rate == out_rate) {
        return;
    }
    int error = 0;
    state_ = src_new(SRC_SINC_MEDIUM_QUALITY, 1, &error);
    if (!state_) {
        throw std::runtime_error(std::string("Resampler: src_new failed: ") + src_strerror(error));
    }
}

Resampler::~Resampler() {
    if (state_) {
        src_delete(state_);
    }
}

std::vector<int16_t> Resampler::process(const int16_t* samples, size_t num_samples) {
    if (!state_) {
        return std::vector<int16_t>(samples, samples + num_samples);
    }
    if (num_samples == 0) {
        return {};
    }

    std::vector<float> in(num_samples);
    src_short_to_float_array(samples, in.data(), static_cast<int>(num_samples));

    // Room for this chunk plus whatever the filter was still holding back
    std::vector<float> out(static_cast<size_t>(std::ceil(num_samples * ratio_)) + 256);
    std::vector<int16_t> result;

    SRC_DATA data{};
    data.data_in = in.data();
    data.input_frames = static_cast<long>(num_samples);
    data.src_ratio = ratio_;
    data.end_of_input = 0;

    // libsamplerate may stop early when the output buffer fills up; keep
    // feeding the rest of the input until everything is consumed
    while (data.input_frames > 0) {
        data.data_out = out.data();
        data.output_frames = static_cast<long>(out.size());
        const int error = src_process(state_, &data);
        if (error) {
            throw std::runtime_error(std::string("Resampler: src_process failed: ") +
                                     src_strerror(error));
        }
        const size_t old = result.size();
        result.resize(old + static_cast<size_t>(data.output_frames_gen));
        src_float_to_short_array(out.data(), result.data() + old,
                                 static_cast<int>(data.output_frames_gen));
        data.data_in += data.input_frames_used;
        data.input_frames -= data.input_frames_used;
        if (data.input_frames_used == 0 && data.output_frames_gen == 0) {
            break;  // no progress possible; avoid spinning
        }
    }
    return result;
}

void Resampler::reset() {
    if (state_) {
        src_reset(state_);
    }
}

}  // namespace audio
}  // namespace nerd_dictation
