/**
 * Tests for the live wake word detector: nerd_dictation::wake_word::WakeWordManager.
 *
 * Mirrors how the daemon uses it (nerd_dictation_ex/cli.py,
 * _start_wake_word_detection / _wake_word_loop): word "запись", variant
 * "записи", Vosk model ~/.local/share/vosk/vosk-model-small-ru, int16 mono
 * speech segments fed to process_speech(), reset() between utterances.
 *
 * Real recordings from test-samples/:
 *  - wake-word-zapis.wav (8 kHz, says "запись") must be detected;
 *  - the phrase samples NN-*.wav (16 kHz, none contains the wake word per
 *    scripts/record-test-samples.py) must not.
 *
 * The recognizer always runs at 16 kHz, exactly as in the daemon. Vosk does
 * NOT resample: a recognizer created for 8 kHz on this 16 kHz model logs
 * "Sampling frequency mismatch, expected 16000, got 8000" and returns empty
 * text. The 8 kHz wake word recording (made for t-one) is therefore
 * upsampled x2 here before feeding — a test input adapter, not product code.
 */

#include <gtest/gtest.h>

#include "nerd_dictation/wake_word/wake_word_manager.hpp"

#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

using nerd_dictation::wake_word::WakeWordConfig;
using nerd_dictation::wake_word::WakeWordManager;
using nerd_dictation::wake_word::WakeWordResult;
namespace fs = std::filesystem;

namespace {

struct Wav {
    std::vector<int16_t> samples;
    int sample_rate = 0;
};

// Minimal RIFF reader: 16-bit PCM mono only, which is what test-samples hold
Wav load_wav(const fs::path& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("cannot open " + path.string());

    char tag[4];
    uint32_t size = 0;
    f.read(tag, 4);
    if (std::strncmp(tag, "RIFF", 4) != 0) throw std::runtime_error("no RIFF: " + path.string());
    f.read(reinterpret_cast<char*>(&size), 4);
    f.read(tag, 4);
    if (std::strncmp(tag, "WAVE", 4) != 0) throw std::runtime_error("no WAVE: " + path.string());

    Wav wav;
    uint16_t format = 0, channels = 0, bits = 0;
    while (f.read(tag, 4)) {
        f.read(reinterpret_cast<char*>(&size), 4);
        if (std::strncmp(tag, "fmt ", 4) == 0) {
            uint32_t rate = 0;
            f.read(reinterpret_cast<char*>(&format), 2);
            f.read(reinterpret_cast<char*>(&channels), 2);
            f.read(reinterpret_cast<char*>(&rate), 4);
            f.seekg(6, std::ios::cur);  // byte rate + block align
            f.read(reinterpret_cast<char*>(&bits), 2);
            f.seekg(static_cast<std::streamoff>(size) - 16, std::ios::cur);
            wav.sample_rate = static_cast<int>(rate);
        } else if (std::strncmp(tag, "data", 4) == 0) {
            if (format != 1 || channels != 1 || bits != 16) {
                throw std::runtime_error("not 16-bit PCM mono: " + path.string());
            }
            wav.samples.resize(size / sizeof(int16_t));
            f.read(reinterpret_cast<char*>(wav.samples.data()), size);
            return wav;
        } else {
            f.seekg(size + (size & 1), std::ios::cur);
        }
    }
    throw std::runtime_error("no data chunk: " + path.string());
}

constexpr int kDaemonRate = 16000;

// Bring a recording to the daemon's rate. Only the 8 kHz -> 16 kHz case
// exists in test-samples; linear interpolation is enough for recognition.
std::vector<int16_t> to_daemon_rate(const Wav& wav) {
    if (wav.sample_rate == kDaemonRate) return wav.samples;
    if (wav.sample_rate * 2 != kDaemonRate) {
        throw std::runtime_error("unsupported sample rate " + std::to_string(wav.sample_rate));
    }
    std::vector<int16_t> out(wav.samples.size() * 2);
    for (size_t i = 0; i < wav.samples.size(); ++i) {
        const int a = wav.samples[i];
        const int b = i + 1 < wav.samples.size() ? wav.samples[i + 1] : a;
        out[2 * i] = static_cast<int16_t>(a);
        out[2 * i + 1] = static_cast<int16_t>((a + b) / 2);
    }
    return out;
}

fs::path samples_dir() {
    if (const char* env = std::getenv("TEST_SAMPLES_DIR")) {
        if (fs::exists(fs::path(env) / "wake-word-zapis.wav")) return env;
    }
    for (const char* p : {"test-samples", "../test-samples", "../../test-samples"}) {
        if (fs::exists(fs::path(p) / "wake-word-zapis.wav")) return p;
    }
    return {};
}

std::string model_path() {
    const char* home = std::getenv("HOME");
    return home ? std::string(home) + "/.local/share/vosk/vosk-model-small-ru" : "";
}

// Same configuration as DictationDaemon in cli.py
WakeWordConfig daemon_config(int sample_rate) {
    WakeWordConfig c;
    c.model_path = model_path();
    c.wake_word = "запись";
    c.variants = {"записи"};
    c.sample_rate = sample_rate;
    return c;
}

class WakeWordManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (!nerd_dictation::wake_word::vosk_model_exists(model_path())) {
            GTEST_SKIP() << "Vosk model not found: " << model_path();
        }
        dir_ = samples_dir();
        if (dir_.empty()) {
            GTEST_SKIP() << "test-samples/ with wake-word-zapis.wav not found";
        }
    }

    // Re-init each test: the singleton may have been left in another state
    // (e.g. by the bad-path test); reset() as the daemon does per segment.
    static WakeWordManager& manager() {
        WakeWordManager& m = WakeWordManager::instance();
        EXPECT_TRUE(m.init(daemon_config(kDaemonRate))) << m.get_error();
        m.reset();
        return m;
    }

    static WakeWordResult recognize(const fs::path& file) {
        return manager().process_speech(to_daemon_rate(load_wav(file)));
    }

    fs::path dir_;
};

}  // namespace

TEST_F(WakeWordManagerTest, RecordedWakeWordIsDetected) {
    const WakeWordResult r = recognize(dir_ / "wake-word-zapis.wav");
    EXPECT_TRUE(r.detected) << "recognized text: '" << r.text << "'";
}

TEST_F(WakeWordManagerTest, PhrasesWithoutWakeWordAreNotDetected) {
    int checked = 0;
    for (const auto& e : fs::directory_iterator(dir_)) {
        const std::string name = e.path().filename();
        if (e.path().extension() != ".wav" || name.size() < 3 ||
            !std::isdigit(static_cast<unsigned char>(name[0])) ||
            !std::isdigit(static_cast<unsigned char>(name[1])) || name[2] != '-') {
            continue;
        }
        const WakeWordResult r = recognize(e.path());
        EXPECT_FALSE(r.detected) << name << ": recognized '" << r.text << "'";
        ++checked;
    }
    if (checked == 0) GTEST_SKIP() << "no NN-*.wav phrase samples";
}

TEST_F(WakeWordManagerTest, VariantAloneTriggersDetection) {
    // Main word that cannot match, variant that can: detection must come
    // from the variant list
    const std::vector<int16_t> audio = to_daemon_rate(load_wav(dir_ / "wake-word-zapis.wav"));
    WakeWordManager& m = manager();
    m.set_wake_word("абракадабра", {"запис"});
    const WakeWordResult r = m.process_speech(audio);
    m.set_wake_word("запись", {"записи"});
    EXPECT_TRUE(r.detected) << "recognized text: '" << r.text << "'";
}

TEST_F(WakeWordManagerTest, ResetSeparatesUtterances) {
    // The daemon resets after every segment; a reset recognizer must not
    // carry the previous utterance into the next result
    const std::vector<int16_t> wake = to_daemon_rate(load_wav(dir_ / "wake-word-zapis.wav"));
    WakeWordManager& m = manager();
    ASSERT_TRUE(m.process_speech(wake).detected);

    m.reset();
    const std::vector<int16_t> silence(kDaemonRate, 0);  // 1 s
    const WakeWordResult r = m.process_speech(silence);
    EXPECT_FALSE(r.detected) << "after reset, silence yielded '" << r.text << "'";
    EXPECT_TRUE(r.text.empty()) << "after reset, silence yielded '" << r.text << "'";
}

TEST_F(WakeWordManagerTest, EmptyAudioIsNotDetected) {
    WakeWordManager& m = manager();
    const WakeWordResult r = m.process_speech(std::vector<int16_t>{});
    EXPECT_FALSE(r.detected);
    EXPECT_TRUE(r.text.empty());
}

TEST(WakeWordManagerInit, BadModelPathFailsWithReason) {
    WakeWordConfig c = daemon_config(kDaemonRate);
    c.model_path = "/nonexistent/vosk-model";
    WakeWordManager& m = WakeWordManager::instance();
    EXPECT_FALSE(m.init(c));
    EXPECT_FALSE(m.get_error().empty());
    EXPECT_NE(m.get_error().find("/nonexistent/vosk-model"), std::string::npos) << m.get_error();
}
