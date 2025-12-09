#include <gtest/gtest.h>
#include "nerd_dictation/backend_wrapper.hpp"
#include <filesystem>
#include <cstdlib>

using namespace nerd_dictation;

class BackendWrapperTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Find dummy backend library
        // It should be in build directory or system path
        backend_path_ = find_dummy_backend();
        ASSERT_FALSE(backend_path_.empty()) << "Dummy backend not found";
    }
    
    std::string find_dummy_backend() {
        // Try environment variable first
        const char* backend_path = std::getenv("NERD_DICTATION_DUMMY_BACKEND_PATH");
        if (backend_path && std::filesystem::exists(backend_path)) {
            return backend_path;
        }
        
        // Try CMAKE_BINARY_DIR
        const char* build_dir = std::getenv("CMAKE_BINARY_DIR");
        if (build_dir) {
            std::filesystem::path path = std::filesystem::path(build_dir) / "libnerd-dictation-dummy.so";
            if (std::filesystem::exists(path)) {
                return path.string();
            }
        }
        
        // Try relative paths from test executable
        std::vector<std::string> paths = {
            "libnerd-dictation-dummy.so",
            "../libnerd-dictation-dummy.so",
            "../../libnerd-dictation-dummy.so",
            "./build/libnerd-dictation-dummy.so",
            "../build/libnerd-dictation-dummy.so",
            "../../build/libnerd-dictation-dummy.so",
            "/usr/lib/libnerd-dictation-dummy.so",
            "/usr/local/lib/libnerd-dictation-dummy.so"
        };
        
        for (const auto& path : paths) {
            if (std::filesystem::exists(path)) {
                return path;
            }
        }
        
        // Last resort: try just the name (for system-installed)
        return "libnerd-dictation-dummy.so";
    }
    
    std::string backend_path_;
};

TEST_F(BackendWrapperTest, LoadBackend) {
    if (backend_path_.empty() || (!std::filesystem::exists(backend_path_) && backend_path_ != "libnerd-dictation-dummy.so")) {
        GTEST_SKIP() << "Dummy backend not found";
    }
    
    EXPECT_NO_THROW({
        Backend backend(backend_path_);
        EXPECT_TRUE(backend);
    });
}

TEST_F(BackendWrapperTest, BackendInfo) {
    if (backend_path_.empty() || (!std::filesystem::exists(backend_path_) && backend_path_ != "libnerd-dictation-dummy.so")) {
        GTEST_SKIP() << "Dummy backend not found";
    }
    
    Backend backend(backend_path_);
    
    auto info = backend.get_info();
    EXPECT_EQ(info.name, "dummy");
    EXPECT_EQ(info.sample_rate, 16000);
    EXPECT_TRUE(info.streaming);
    EXPECT_EQ(info.channels, 1);
    EXPECT_EQ(info.bits_per_sample, 16);
}

TEST_F(BackendWrapperTest, BackendName) {
    if (backend_path_.empty() || (!std::filesystem::exists(backend_path_) && backend_path_ != "libnerd-dictation-dummy.so")) {
        GTEST_SKIP() << "Dummy backend not found";
    }
    
    Backend backend(backend_path_);
    EXPECT_EQ(backend.name(), "dummy");
}

TEST_F(BackendWrapperTest, SampleRate) {
    if (backend_path_.empty() || (!std::filesystem::exists(backend_path_) && backend_path_ != "libnerd-dictation-dummy.so")) {
        GTEST_SKIP() << "Dummy backend not found";
    }
    
    Backend backend(backend_path_);
    EXPECT_EQ(backend.sample_rate(), 16000);
}

TEST_F(BackendWrapperTest, SupportsStreaming) {
    if (backend_path_.empty() || (!std::filesystem::exists(backend_path_) && backend_path_ != "libnerd-dictation-dummy.so")) {
        GTEST_SKIP() << "Dummy backend not found";
    }
    
    Backend backend(backend_path_);
    EXPECT_TRUE(backend.supports_streaming());
}

TEST_F(BackendWrapperTest, FeedAudio) {
    if (backend_path_.empty() || (!std::filesystem::exists(backend_path_) && backend_path_ != "libnerd-dictation-dummy.so")) {
        GTEST_SKIP() << "Dummy backend not found";
    }
    
    Backend backend(backend_path_);
    
    std::vector<int16_t> samples(1600, 0); // 100ms at 16kHz
    EXPECT_NO_THROW({
        backend.feed(samples);
    });
}

TEST_F(BackendWrapperTest, GetResult) {
    if (backend_path_.empty() || (!std::filesystem::exists(backend_path_) && backend_path_ != "libnerd-dictation-dummy.so")) {
        GTEST_SKIP() << "Dummy backend not found";
    }
    
    Backend backend(backend_path_);
    
    // Feed some audio first
    std::vector<int16_t> samples(16000 * 2, 0); // 2 seconds
    backend.feed(samples);
    
    // Get result
    auto result = backend.result();
    EXPECT_FALSE(result.text.empty());
}

TEST_F(BackendWrapperTest, Finalize) {
    if (backend_path_.empty() || (!std::filesystem::exists(backend_path_) && backend_path_ != "libnerd-dictation-dummy.so")) {
        GTEST_SKIP() << "Dummy backend not found";
    }
    
    Backend backend(backend_path_);
    
    // Feed some audio
    std::vector<int16_t> samples(16000 * 2, 0); // 2 seconds
    backend.feed(samples);
    
    // Finalize
    auto result = backend.finalize();
    EXPECT_TRUE(result.is_final);
    EXPECT_FALSE(result.text.empty());
}

TEST_F(BackendWrapperTest, Reset) {
    if (backend_path_.empty() || (!std::filesystem::exists(backend_path_) && backend_path_ != "libnerd-dictation-dummy.so")) {
        GTEST_SKIP() << "Dummy backend not found";
    }
    
    Backend backend(backend_path_);
    
    // Feed some audio
    std::vector<int16_t> samples(16000, 0);
    backend.feed(samples);
    
    // Reset
    EXPECT_NO_THROW({
        backend.reset();
    });
}

TEST_F(BackendWrapperTest, StreamingRecognition) {
    if (backend_path_.empty() || (!std::filesystem::exists(backend_path_) && backend_path_ != "libnerd-dictation-dummy.so")) {
        GTEST_SKIP() << "Dummy backend not found";
    }
    
    Backend backend(backend_path_);
    
    std::vector<int16_t> audio_data(16000 * 2, 0); // 2 seconds
    size_t pos = 0;
    
    auto audio_provider = [&](std::vector<int16_t>& chunk) -> bool {
        if (pos >= audio_data.size()) {
            return false;
        }
        size_t chunk_size = std::min<size_t>(1600, audio_data.size() - pos);
        chunk.assign(audio_data.begin() + pos, audio_data.begin() + pos + chunk_size);
        pos += chunk_size;
        return true;
    };
    
    std::vector<ASRResult> results;
    auto result_callback = [&](const ASRResult& result) {
        results.push_back(result);
    };
    
    EXPECT_NO_THROW({
        backend.transcribe_stream(audio_provider, result_callback);
    });
    
    EXPECT_GT(results.size(), 0);
}

TEST_F(BackendWrapperTest, InvalidPath) {
    EXPECT_THROW({
        Backend backend("/nonexistent/path/libnerd-dictation-dummy.so");
    }, std::runtime_error);
}
