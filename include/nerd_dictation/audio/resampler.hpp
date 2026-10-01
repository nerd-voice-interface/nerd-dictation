/**
 * Resampler - streaming sample rate conversion for ASR backends
 *
 * The daemon captures at one rate (16 kHz: VAD and wake word need it),
 * while a backend may expect another (t-one wants 8 kHz). Feeding audio at
 * the wrong rate does not fail loudly: the model hears speech slowed down
 * an octave lower and returns garbage. This converts each chunk on its way
 * to the backend.
 *
 * Built on libsamplerate with persistent state, so consecutive chunks form
 * one continuous signal — no clicks or lost samples at chunk boundaries.
 *
 * Not thread-safe: the object is owned by a single thread.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

struct SRC_STATE_tag;

namespace nerd_dictation {
namespace audio {

class Resampler {
public:
    Resampler(int in_rate, int out_rate);
    ~Resampler();

    Resampler(const Resampler&) = delete;
    Resampler& operator=(const Resampler&) = delete;

    /// Convert the next chunk of mono int16 audio. The filter delay means
    /// the first call returns fewer samples than in*ratio; they are not lost,
    /// they come out with the following chunks.
    std::vector<int16_t> process(const int16_t* samples, size_t num_samples);

    /// Drop internal history: the next process() starts a fresh signal
    void reset();

    int in_rate() const { return in_rate_; }
    int out_rate() const { return out_rate_; }

private:
    int in_rate_;
    int out_rate_;
    double ratio_;
    SRC_STATE_tag* state_ = nullptr;  // nullptr when rates are equal
};

}  // namespace audio
}  // namespace nerd_dictation
