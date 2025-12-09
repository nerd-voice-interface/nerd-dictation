#pragma once

#include <string>
#include <vector>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <atomic>
#include <unordered_map>

namespace nerd_dictation::sounds {

/**
 * WAV file in memory
 */
struct WavData {
    std::vector<int16_t> samples;
    uint32_t sample_rate = 0;
    uint16_t channels = 0;
    uint16_t bits_per_sample = 0;

    bool valid() const { return !samples.empty() && sample_rate > 0; }
};

/**
 * Built-in WAV player via PipeWire
 */
class SoundPlayer {
public:
    static SoundPlayer& instance();

    /**
     * Initialize player and load sounds
     */
    bool init();

    /**
     * Shutdown
     */
    void shutdown();

    /**
     * Play sound asynchronously
     */
    void play(const std::string& name);

    /**
     * Load WAV file
     */
    bool load_sound(const std::string& name, const std::string& path);

    ~SoundPlayer();

private:
    SoundPlayer() = default;
    SoundPlayer(const SoundPlayer&) = delete;
    SoundPlayer& operator=(const SoundPlayer&) = delete;

    void play_wav_async(const WavData& wav);

    std::unordered_map<std::string, WavData> sounds_;
    std::mutex sounds_mutex_;
    std::atomic<bool> initialized_{false};
};

/**
 * Find sound file by name
 */
std::string find_sound_file(const std::string& name);

/**
 * Load WAV file
 */
WavData load_wav(const std::string& path);

/**
 * Play dictation start sound
 */
void play_begin();

/**
 * Play dictation end sound
 */
void play_end();

/**
 * Play command execution sound
 */
void play_execute();

/**
 * Initialize sound subsystem
 */
bool init_sounds();

/**
 * Shutdown sound subsystem
 */
void shutdown_sounds();

} /* namespace nerd_dictation::sounds */
