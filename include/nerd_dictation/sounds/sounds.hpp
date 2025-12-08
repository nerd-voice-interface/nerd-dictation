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
 * WAV файл в памяти
 */
struct WavData {
    std::vector<int16_t> samples;
    uint32_t sample_rate = 0;
    uint16_t channels = 0;
    uint16_t bits_per_sample = 0;

    bool valid() const { return !samples.empty() && sample_rate > 0; }
};

/**
 * Встроенный проигрыватель WAV через PipeWire
 */
class SoundPlayer {
public:
    static SoundPlayer& instance();

    /**
     * Инициализация плеера и загрузка звуков
     */
    bool init();

    /**
     * Завершение работы
     */
    void shutdown();

    /**
     * Воспроизвести звук асинхронно
     */
    void play(const std::string& name);

    /**
     * Загрузить WAV файл
     */
    bool load_sound(const std::string& name, const std::string& path);

    ~SoundPlayer();

private:
    SoundPlayer() = default;
    SoundPlayer(const SoundPlayer&) = delete;
    SoundPlayer& operator=(const SoundPlayer&) = delete;

    void playback_thread();
    bool play_wav_pipewire(const WavData& wav);
    bool play_wav_pulseaudio(const WavData& wav);

    std::unordered_map<std::string, WavData> sounds_;
    std::mutex sounds_mutex_;

    std::queue<std::string> play_queue_;
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;

    std::thread playback_thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> initialized_{false};
};

/**
 * Найти звуковой файл по имени
 */
std::string find_sound_file(const std::string& name);

/**
 * Загрузить WAV файл
 */
WavData load_wav(const std::string& path);

/**
 * Воспроизвести звук начала диктовки
 */
void play_begin();

/**
 * Воспроизвести звук окончания диктовки
 */
void play_end();

/**
 * Воспроизвести звук выполнения команды
 */
void play_execute();

/**
 * Инициализировать звуковую подсистему
 */
bool init_sounds();

/**
 * Завершить звуковую подсистему
 */
void shutdown_sounds();

} /* namespace nerd_dictation::sounds */
