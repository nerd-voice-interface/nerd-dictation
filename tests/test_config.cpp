#include <gtest/gtest.h>
#include "nerd_dictation/config.hpp"
#include <filesystem>
#include <fstream>
#include <cstdlib>

using namespace nerd_dictation;

class ConfigTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create temporary config directory
        test_config_dir = std::filesystem::temp_directory_path() / "nerd-dictation-test-config";
        std::filesystem::create_directories(test_config_dir);
        
        // Set HOME to temp directory for testing
        original_home = std::getenv("HOME");
        test_home = test_config_dir.string();
    }
    
    void TearDown() override {
        std::filesystem::remove_all(test_config_dir);
    }
    
    std::filesystem::path test_config_dir;
    std::string test_home;
    const char* original_home;
    
    void create_test_config(const std::string& content) {
        auto config_path = test_config_dir / "config.toml";
        std::ofstream file(config_path);
        file << content;
        file.close();
    }
};

TEST_F(ConfigTest, DefaultConfig) {
    Config config;
    
    EXPECT_EQ(config.backend, "dummy");
    EXPECT_EQ(config.input_method, "xdotool");
    EXPECT_EQ(config.audio.sample_rate, 16000);
    EXPECT_EQ(config.audio.chunk_size, 8000);
    EXPECT_TRUE(config.wake_word.enabled);
}

TEST_F(ConfigTest, AudioConfigDefaults) {
    AudioConfig audio;
    
    EXPECT_EQ(audio.source, "pw-cat");
    EXPECT_EQ(audio.sample_rate, 16000);
    EXPECT_EQ(audio.chunk_size, 8000);
}

TEST_F(ConfigTest, WakeWordConfigDefaults) {
    WakeWordConfig wake_word;
    
    EXPECT_TRUE(wake_word.enabled);
    EXPECT_EQ(wake_word.word, "запись");
    EXPECT_FALSE(wake_word.model_path.empty());
}

// Note: load_config and find_config_file tests would require
// setting up actual config files and environment, which is more complex
// These can be added later if needed
