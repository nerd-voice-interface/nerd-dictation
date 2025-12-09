#include <gtest/gtest.h>
#include "nerd_dictation/daemon/daemon.hpp"
#include "nerd_dictation/config.hpp"
#include <thread>
#include <chrono>
#include <fstream>
#include <filesystem>
#include <cstdlib>

using namespace nerd_dictation;
using namespace nerd_dictation::daemon;

class DaemonTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create minimal config with dummy backend
        config_.backend = "dummy";
        config_.input_method = "stdout";
        config_.audio.sample_rate = 16000;
        config_.audio.chunk_size = 8000;
        config_.wake_word.enabled = false; // Disable wake word for simpler testing
    }
    
    Config config_;
    
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
        
        // Try relative paths
        std::vector<std::string> paths = {
            "libnerd-dictation-dummy.so",
            "../libnerd-dictation-dummy.so",
            "./build/libnerd-dictation-dummy.so",
            "../build/libnerd-dictation-dummy.so"
        };
        
        for (const auto& path : paths) {
            if (std::filesystem::exists(path)) {
                return path;
            }
        }
        
        // Last resort: try just the name (for system-installed)
        return "libnerd-dictation-dummy.so";
    }
};

TEST_F(DaemonTest, CreateDaemon) {
    std::string backend_path = find_dummy_backend();
    if (backend_path.empty() || (!std::filesystem::exists(backend_path) && backend_path != "libnerd-dictation-dummy.so")) {
        GTEST_SKIP() << "Dummy backend not found, skipping daemon tests";
    }
    
    config_.backend_library = backend_path;
    
    EXPECT_NO_THROW({
        DictationDaemon daemon(config_);
        EXPECT_EQ(daemon.get_state(), DaemonState::Idle);
        EXPECT_EQ(daemon.get_backend_name(), "dummy");
    });
}

TEST_F(DaemonTest, DaemonStateInitial) {
    std::string backend_path = find_dummy_backend();
    if (backend_path.empty() || (!std::filesystem::exists(backend_path) && backend_path != "libnerd-dictation-dummy.so")) {
        GTEST_SKIP() << "Dummy backend not found";
    }
    
    config_.backend_library = backend_path;
    DictationDaemon daemon(config_);
    EXPECT_EQ(daemon.get_state(), DaemonState::Idle);
}

TEST_F(DaemonTest, BeginDictation) {
    std::string backend_path = find_dummy_backend();
    if (backend_path.empty() || (!std::filesystem::exists(backend_path) && backend_path != "libnerd-dictation-dummy.so")) {
        GTEST_SKIP() << "Dummy backend not found";
    }
    
    config_.backend_library = backend_path;
    DictationDaemon daemon(config_);
    
    daemon.begin_dictation();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    EXPECT_EQ(daemon.get_state(), DaemonState::Listening);
}

TEST_F(DaemonTest, EndDictation) {
    std::string backend_path = find_dummy_backend();
    if (backend_path.empty() || (!std::filesystem::exists(backend_path) && backend_path != "libnerd-dictation-dummy.so")) {
        GTEST_SKIP() << "Dummy backend not found";
    }
    
    config_.backend_library = backend_path;
    DictationDaemon daemon(config_);
    
    daemon.begin_dictation();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    daemon.end_dictation();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    EXPECT_EQ(daemon.get_state(), DaemonState::Idle);
}

TEST_F(DaemonTest, CancelDictation) {
    std::string backend_path = find_dummy_backend();
    if (backend_path.empty() || (!std::filesystem::exists(backend_path) && backend_path != "libnerd-dictation-dummy.so")) {
        GTEST_SKIP() << "Dummy backend not found";
    }
    
    config_.backend_library = backend_path;
    DictationDaemon daemon(config_);
    
    daemon.begin_dictation();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    daemon.cancel_dictation();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    EXPECT_EQ(daemon.get_state(), DaemonState::Idle);
}

TEST_F(DaemonTest, MultipleBeginEnd) {
    std::string backend_path = find_dummy_backend();
    if (backend_path.empty() || (!std::filesystem::exists(backend_path) && backend_path != "libnerd-dictation-dummy.so")) {
        GTEST_SKIP() << "Dummy backend not found";
    }
    
    config_.backend_library = backend_path;
    DictationDaemon daemon(config_);
    
    // Multiple begin/end cycles
    for (int i = 0; i < 3; ++i) {
        daemon.begin_dictation();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        EXPECT_EQ(daemon.get_state(), DaemonState::Listening);
        
        daemon.end_dictation();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        EXPECT_EQ(daemon.get_state(), DaemonState::Idle);
    }
}

TEST_F(DaemonTest, BackendName) {
    std::string backend_path = find_dummy_backend();
    if (backend_path.empty() || (!std::filesystem::exists(backend_path) && backend_path != "libnerd-dictation-dummy.so")) {
        GTEST_SKIP() << "Dummy backend not found";
    }
    
    config_.backend_library = backend_path;
    DictationDaemon daemon(config_);
    EXPECT_EQ(daemon.get_backend_name(), "dummy");
}

// Note: Socket server and audio listener tests are more complex
// and would require mocking or actual audio/system setup
// These can be added later if needed
