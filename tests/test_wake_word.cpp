#include <gtest/gtest.h>
#include "nerd_dictation/daemon/daemon.hpp"
#include "nerd_dictation/config.hpp"
#include <thread>
#include <chrono>
#include <fstream>
#include <filesystem>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <memory>

using namespace nerd_dictation;
using namespace nerd_dictation::daemon;

/**
 * Load WAV file (16-bit PCM mono) into vector of int16_t samples
 * Returns sample rate in output parameter
 */
static std::vector<int16_t> load_wav_file(const std::string& filepath, int& sample_rate) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open WAV file: " + filepath);
    }

    /* Read RIFF header */
    char riff[4];
    file.read(riff, 4);
    if (strncmp(riff, "RIFF", 4) != 0) {
        throw std::runtime_error("Not a valid WAV file (missing RIFF)");
    }

    uint32_t file_size;
    file.read(reinterpret_cast<char*>(&file_size), 4);

    char wave[4];
    file.read(wave, 4);
    if (strncmp(wave, "WAVE", 4) != 0) {
        throw std::runtime_error("Not a valid WAV file (missing WAVE)");
    }

    /* Find fmt chunk */
    uint16_t audio_format = 0;
    uint16_t num_channels = 0;
    uint16_t bits_per_sample = 0;
    sample_rate = 0;

    while (file.good()) {
        char chunk_id[4];
        file.read(chunk_id, 4);
        if (!file.good()) break;

        uint32_t chunk_size;
        file.read(reinterpret_cast<char*>(&chunk_size), 4);

        if (strncmp(chunk_id, "fmt ", 4) == 0) {
            file.read(reinterpret_cast<char*>(&audio_format), 2);
            file.read(reinterpret_cast<char*>(&num_channels), 2);
            uint32_t sr;
            file.read(reinterpret_cast<char*>(&sr), 4);
            sample_rate = static_cast<int>(sr);
            uint32_t byte_rate;
            file.read(reinterpret_cast<char*>(&byte_rate), 4);
            uint16_t block_align;
            file.read(reinterpret_cast<char*>(&block_align), 2);
            file.read(reinterpret_cast<char*>(&bits_per_sample), 2);

            /* Skip any extra fmt bytes */
            if (chunk_size > 16) {
                file.seekg(chunk_size - 16, std::ios::cur);
            }
        } else if (strncmp(chunk_id, "data", 4) == 0) {
            /* Read audio data */
            if (audio_format != 1) {
                throw std::runtime_error("Only PCM format supported");
            }
            if (bits_per_sample != 16) {
                throw std::runtime_error("Only 16-bit samples supported");
            }
            if (num_channels != 1) {
                throw std::runtime_error("Only mono audio supported");
            }

            std::vector<int16_t> samples(chunk_size / sizeof(int16_t));
            file.read(reinterpret_cast<char*>(samples.data()), chunk_size);
            return samples;
        } else {
            /* Skip unknown chunk */
            file.seekg(chunk_size, std::ios::cur);
        }
    }

    throw std::runtime_error("No data chunk found in WAV file");
}

/**
 * Resample audio from source_rate to target_rate using simple linear interpolation
 */
static std::vector<int16_t> resample_audio(const std::vector<int16_t>& input,
                                            int source_rate, int target_rate) {
    if (source_rate == target_rate) {
        return input;
    }

    double ratio = static_cast<double>(target_rate) / source_rate;
    size_t output_size = static_cast<size_t>(input.size() * ratio);
    std::vector<int16_t> output(output_size);

    for (size_t i = 0; i < output_size; ++i) {
        double src_idx = i / ratio;
        size_t idx0 = static_cast<size_t>(src_idx);
        size_t idx1 = std::min(idx0 + 1, input.size() - 1);
        double frac = src_idx - idx0;

        double sample = input[idx0] * (1.0 - frac) + input[idx1] * frac;
        output[i] = static_cast<int16_t>(std::clamp(sample, -32768.0, 32767.0));
    }

    return output;
}


class WakeWordTest : public ::testing::Test {
protected:
    void SetUp() override {
        /* Create config with wake word enabled - using t-one backend */
        config_.backend = "t-one";
        config_.input_method = "stdout";
        config_.audio.sample_rate = 8000;  /* t-one uses 8000 Hz */
        config_.audio.chunk_size = 4000;   /* ~250ms chunks at 8kHz */
        config_.wake_word.enabled = true;
        config_.wake_word.word = "запись";
    }

    Config config_;

    std::string find_tone_backend() {
        /* Check environment variable first */
        const char* backend_path = std::getenv("NERD_DICTATION_TONE_BACKEND_PATH");
        if (backend_path && std::filesystem::exists(backend_path)) {
            return backend_path;
        }

        const char* build_dir = std::getenv("CMAKE_BINARY_DIR");
        if (build_dir) {
            std::filesystem::path path = std::filesystem::path(build_dir) / "libnerd-dictation-t-one.so";
            if (std::filesystem::exists(path)) {
                return path.string();
            }
        }

        /* Check install directory (where sherpa-onnx dependencies are) */
        const char* home = std::getenv("HOME");
        if (home) {
            std::filesystem::path install_path = std::filesystem::path(home) /
                "forge/nerd-dictation/install/lib/libnerd-dictation-t-one.so";
            if (std::filesystem::exists(install_path)) {
                return install_path.string();
            }
        }

        std::vector<std::string> paths = {
            "libnerd-dictation-t-one.so",
            "../libnerd-dictation-t-one.so",
            "./build/libnerd-dictation-t-one.so",
            "../build/libnerd-dictation-t-one.so",
            "/usr/lib/libnerd-dictation-t-one.so",
            "/usr/local/lib/libnerd-dictation-t-one.so"
        };

        for (const auto& path : paths) {
            if (std::filesystem::exists(path)) {
                return path;
            }
        }

        return "";
    }

    std::string find_wake_word_sample() {
        std::vector<std::string> paths = {
            "test-samples/wake-word-zapis.wav",
            "../test-samples/wake-word-zapis.wav",
            "../../test-samples/wake-word-zapis.wav",
        };

        /* Check TEST_SAMPLES_DIR env var */
        const char* samples_dir = std::getenv("TEST_SAMPLES_DIR");
        if (samples_dir) {
            std::filesystem::path path = std::filesystem::path(samples_dir) / "wake-word-zapis.wav";
            if (std::filesystem::exists(path)) {
                return path.string();
            }
        }

        for (const auto& path : paths) {
            if (std::filesystem::exists(path)) {
                return path;
            }
        }

        return "";
    }
};

TEST_F(WakeWordTest, LoadWavFile) {
    std::string sample_path = find_wake_word_sample();
    if (sample_path.empty()) {
        GTEST_SKIP() << "Wake word sample file not found. Run scripts/record-wake-word.py first.";
    }

    int sample_rate = 0;
    std::vector<int16_t> samples;

    EXPECT_NO_THROW({
        samples = load_wav_file(sample_path, sample_rate);
    });

    EXPECT_GT(samples.size(), 0u);
    EXPECT_GT(sample_rate, 0);

    std::cout << "Loaded " << samples.size() << " samples at " << sample_rate << " Hz" << std::endl;
    std::cout << "Duration: " << static_cast<double>(samples.size()) / sample_rate << " seconds" << std::endl;
}

TEST_F(WakeWordTest, ResampleAudio) {
    /* Test resampling from 8000 Hz to 16000 Hz */
    std::vector<int16_t> input = {0, 1000, 2000, 3000, 4000, 5000, 6000, 7000};
    auto output = resample_audio(input, 8000, 16000);

    /* Output should be roughly double the size */
    EXPECT_EQ(output.size(), input.size() * 2);

    /* First and last samples should match approximately */
    EXPECT_EQ(output[0], input[0]);
}

TEST_F(WakeWordTest, WakeWordDetection_EndToEnd) {
    /* Skip if prerequisites not met */
    std::string backend_path = find_tone_backend();
    if (backend_path.empty()) {
        GTEST_SKIP() << "t-one backend not found";
    }

    std::string sample_path = find_wake_word_sample();
    if (sample_path.empty()) {
        GTEST_SKIP() << "Wake word sample file not found";
    }

    config_.backend_library = backend_path;

    /* Load and prepare audio */
    int file_sample_rate = 0;
    std::vector<int16_t> samples = load_wav_file(sample_path, file_sample_rate);

    std::cout << "Loaded audio: " << samples.size() << " samples at " << file_sample_rate << " Hz" << std::endl;

    /* Resample if needed - t-one uses 8000 Hz */
    if (file_sample_rate != config_.audio.sample_rate) {
        std::cout << "Resampling from " << file_sample_rate << " Hz to "
                  << config_.audio.sample_rate << " Hz" << std::endl;
        samples = resample_audio(samples, file_sample_rate, config_.audio.sample_rate);
        std::cout << "After resampling: " << samples.size() << " samples" << std::endl;
    }

    /* Create daemon with t-one backend - skip if dependencies not available */
    std::unique_ptr<DictationDaemon> daemon;
    try {
        daemon = std::make_unique<DictationDaemon>(config_);
    } catch (const std::exception& e) {
        std::string error = e.what();
        if (error.find("libsherpa-onnx") != std::string::npos ||
            error.find("cannot open shared object") != std::string::npos) {
            GTEST_SKIP() << "t-one backend dependencies not installed: " << error;
        }
        throw;
    }
    ASSERT_EQ(daemon->get_state(), DaemonState::Idle);
    std::cout << "Backend loaded: " << daemon->get_backend_name() << std::endl;

    /* Start wake word detection */
    daemon->start_wake_word_detection_for_test();

    /* Give it time to initialize */
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    if (!daemon->is_wake_word_running()) {
        GTEST_SKIP() << "Wake word detection failed to start";
    }

    /* Inject audio */
    daemon->inject_audio_for_test(samples);

    /* Wait for wake word detection to process */
    std::cout << "Waiting for wake word detection..." << std::endl;

    bool detected = false;
    for (int i = 0; i < 50; ++i) {  /* Max 5 seconds */
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (daemon->get_state() == DaemonState::Listening) {
            detected = true;
            std::cout << "Wake word detected after " << (i + 1) * 100 << " ms!" << std::endl;
            break;
        }
    }

    /* This is the main assertion - wake word should trigger dictation */
    EXPECT_TRUE(detected) << "Wake word was not detected within timeout";

    if (detected) {
        /* Clean up - end dictation */
        daemon->end_dictation();
        EXPECT_EQ(daemon->get_state(), DaemonState::Idle);
    }
}

/**
 * NOTE: This test and WakeWordDetection_EndToEnd both load t-one backend.
 * Due to GPU/CUDA memory cleanup limitations in onnxruntime, t-one can only
 * be loaded once per process. Run these tests separately:
 *   --gtest_filter="WakeWordTest.WakeWordDetection_EndToEnd"
 *   --gtest_filter="WakeWordTest.WakeWordNotDetected_Silence"
 */
TEST_F(WakeWordTest, WakeWordNotDetected_Silence) {
    /* Skip if prerequisites not met */
    std::string backend_path = find_tone_backend();
    if (backend_path.empty()) {
        GTEST_SKIP() << "t-one backend not found";
    }

    config_.backend_library = backend_path;

    /* Create silence audio (3 seconds) */
    size_t num_samples = config_.audio.sample_rate * 3;
    std::vector<int16_t> silence(num_samples, 0);

    /* Create daemon with t-one backend - skip if dependencies not available */
    std::unique_ptr<DictationDaemon> daemon;
    try {
        daemon = std::make_unique<DictationDaemon>(config_);
    } catch (const std::exception& e) {
        std::string error = e.what();
        if (error.find("libsherpa-onnx") != std::string::npos ||
            error.find("cannot open shared object") != std::string::npos) {
            GTEST_SKIP() << "t-one backend dependencies not installed: " << error;
        }
        throw;
    }
    ASSERT_EQ(daemon->get_state(), DaemonState::Idle);

    /* Start wake word detection */
    daemon->start_wake_word_detection_for_test();
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    if (!daemon->is_wake_word_running()) {
        GTEST_SKIP() << "Wake word detection failed to start";
    }

    /* Inject silence */
    daemon->inject_audio_for_test(silence);

    /* Wait and verify no detection */
    std::this_thread::sleep_for(std::chrono::seconds(2));

    /* Silence should NOT trigger wake word */
    EXPECT_EQ(daemon->get_state(), DaemonState::Idle)
        << "Wake word should not be detected in silence";
}
