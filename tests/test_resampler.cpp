/**
 * Tests for the streaming Resampler used to feed backends whose sample rate
 * differs from the capture rate (t-one: 8 kHz, capture: 16 kHz).
 *
 * The properties that matter for ASR:
 *  - the frequency content of speech is preserved (a 1 kHz tone stays 1 kHz);
 *  - content above the new Nyquist frequency is filtered, not folded back
 *    into the speech band as aliasing;
 *  - chunked streaming produces the same signal as one-shot conversion, so
 *    chunk boundaries add no clicks and lose no samples;
 *  - equal rates pass audio through untouched.
 */

#include <gtest/gtest.h>

#include "nerd_dictation/audio/resampler.hpp"

#include <cmath>
#include <cstdint>
#include <vector>

using nerd_dictation::audio::Resampler;

namespace {

constexpr double kPi = 3.14159265358979323846;

std::vector<int16_t> sine(double freq, int rate, size_t n, double amp = 16000.0) {
    std::vector<int16_t> s(n);
    for (size_t i = 0; i < n; ++i) {
        s[i] = static_cast<int16_t>(std::lround(amp * std::sin(2.0 * kPi * freq * i / rate)));
    }
    return s;
}

// Power of one frequency bin (Goertzel), normalized per sample
double goertzel_power(const std::vector<int16_t>& x, size_t from, double freq, int rate) {
    const double w = 2.0 * kPi * freq / rate;
    const double coeff = 2.0 * std::cos(w);
    double s1 = 0.0, s2 = 0.0;
    for (size_t i = from; i < x.size(); ++i) {
        const double s0 = x[i] + coeff * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    const double n = static_cast<double>(x.size() - from);
    return (s1 * s1 + s2 * s2 - coeff * s1 * s2) / (n * n);
}

double rms(const std::vector<int16_t>& x, size_t from) {
    double acc = 0.0;
    for (size_t i = from; i < x.size(); ++i) acc += static_cast<double>(x[i]) * x[i];
    return std::sqrt(acc / static_cast<double>(x.size() - from));
}

// libsamplerate's sinc converters hold back part of their filter length until
// more input arrives (end_of_input is never set in streaming use). Measured
// for SRC_SINC_MEDIUM_QUALITY at 16k->8k: 46 output samples (5.75 ms).
// 128 leaves margin for libsamplerate versions without hiding a real loss.
constexpr long kMaxHeldBack = 128;

// Skip the filter's start-up transient before measuring spectra
constexpr size_t kSettle = 1024;

}  // namespace

TEST(Resampler, KeepsSpeechBandToneAndHalvesLength) {
    Resampler r(16000, 8000);
    const auto in = sine(1000.0, 16000, 32000);  // 2 s
    const auto out = r.process(in.data(), in.size());

    const long expected = 16000;
    EXPECT_LE(static_cast<long>(out.size()), expected);
    EXPECT_GE(static_cast<long>(out.size()), expected - kMaxHeldBack) << out.size();

    // The 1 kHz bin dominates its neighbours in the 8 kHz output
    const double p1k = goertzel_power(out, kSettle, 1000.0, 8000);
    for (double f : {500.0, 1500.0, 2000.0, 3000.0}) {
        EXPECT_GT(p1k, 1000.0 * goertzel_power(out, kSettle, f, 8000)) << f << " Hz";
    }
}

TEST(Resampler, FiltersAboveNewNyquist) {
    // 6 kHz exists at 16 kHz but not at 8 kHz (Nyquist 4 kHz). Without an
    // anti-aliasing filter it would fold to 2 kHz, inside the speech band.
    Resampler r(16000, 8000);
    const auto in = sine(6000.0, 16000, 32000);
    const auto out = r.process(in.data(), in.size());
    ASSERT_GT(out.size(), kSettle + 1000);

    const double in_rms = rms(in, 0);
    const double out_rms = rms(out, kSettle);
    const double attenuation_db = 20.0 * std::log10(in_rms / std::max(out_rms, 1e-9));
    EXPECT_GE(attenuation_db, 40.0) << "in rms " << in_rms << ", out rms " << out_rms;
}

TEST(Resampler, ChunkedEqualsOneShot) {
    const auto in = sine(1000.0, 16000, 32000);

    Resampler whole(16000, 8000);
    const auto ref = whole.process(in.data(), in.size());

    Resampler chunked(16000, 8000);
    std::vector<int16_t> got;
    for (size_t off = 0; off < in.size(); off += 4000) {
        const size_t n = std::min<size_t>(4000, in.size() - off);
        const auto part = chunked.process(in.data() + off, n);
        got.insert(got.end(), part.begin(), part.end());
    }

    const size_t n = std::min(ref.size(), got.size());
    EXPECT_GE(n, ref.size() - static_cast<size_t>(kMaxHeldBack));
    int worst = 0;
    for (size_t i = 0; i < n; ++i) {
        worst = std::max(worst, std::abs(static_cast<int>(ref[i]) - got[i]));
    }
    EXPECT_LE(worst, 1) << "chunk boundaries alter the signal";
}

TEST(Resampler, EqualRatesPassThrough) {
    Resampler r(16000, 16000);
    const auto in = sine(1000.0, 16000, 4001);
    const auto out = r.process(in.data(), in.size());
    EXPECT_EQ(out, in);
}

TEST(Resampler, ResetStartsFreshSignal) {
    const auto a = sine(3000.0, 16000, 8000);
    const auto b = sine(500.0, 16000, 8000);

    Resampler used(16000, 8000);
    used.process(a.data(), a.size());
    used.reset();
    const auto after_reset = used.process(b.data(), b.size());

    Resampler fresh(16000, 8000);
    const auto from_fresh = fresh.process(b.data(), b.size());

    EXPECT_EQ(after_reset, from_fresh);
}

TEST(Resampler, RejectsInvalidRates) {
    EXPECT_THROW(Resampler(0, 8000), std::runtime_error);
    EXPECT_THROW(Resampler(16000, -1), std::runtime_error);
}
