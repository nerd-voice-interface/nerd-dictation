#include "nerd_dictation/sounds/sounds.hpp"
#include <spdlog/spdlog.h>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <cstring>

#ifdef HAVE_PULSEAUDIO
#include <pulse/simple.h>
#include <pulse/error.h>
#endif

namespace nerd_dictation::sounds {

/* ========================================================================== */
/* WAV Loading                                                                 */
/* ========================================================================== */

#pragma pack(push, 1)
struct WavHeader {
    char riff[4];           /* "RIFF" */
    uint32_t file_size;     /* File size - 8 */
    char wave[4];           /* "WAVE" */
    char fmt[4];            /* "fmt " */
    uint32_t fmt_size;      /* Format chunk size (16 for PCM) */
    uint16_t audio_format;  /* 1 = PCM */
    uint16_t num_channels;  /* 1 = mono, 2 = stereo */
    uint32_t sample_rate;   /* e.g., 44100 */
    uint32_t byte_rate;     /* sample_rate * num_channels * bits_per_sample/8 */
    uint16_t block_align;   /* num_channels * bits_per_sample/8 */
    uint16_t bits_per_sample; /* 8, 16, etc. */
};
#pragma pack(pop)

WavData load_wav(const std::string& path) {
    WavData result;

    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        spdlog::warn("Не удалось открыть WAV файл: {}", path);
        return result;
    }

    /* Read header */
    WavHeader header;
    file.read(reinterpret_cast<char*>(&header), sizeof(header));

    if (std::strncmp(header.riff, "RIFF", 4) != 0 ||
        std::strncmp(header.wave, "WAVE", 4) != 0) {
        spdlog::warn("Неверный формат WAV файла: {}", path);
        return result;
    }

    if (header.audio_format != 1) {
        spdlog::warn("Поддерживается только PCM WAV: {}", path);
        return result;
    }

    result.sample_rate = header.sample_rate;
    result.channels = header.num_channels;
    result.bits_per_sample = header.bits_per_sample;

    /* Skip any extra format bytes */
    if (header.fmt_size > 16) {
        file.seekg(header.fmt_size - 16, std::ios::cur);
    }

    /* Find data chunk */
    char chunk_id[4];
    uint32_t chunk_size;
    while (file.read(chunk_id, 4) && file.read(reinterpret_cast<char*>(&chunk_size), 4)) {
        if (std::strncmp(chunk_id, "data", 4) == 0) {
            /* Found data chunk */
            size_t num_samples = chunk_size / (header.bits_per_sample / 8);
            result.samples.resize(num_samples);

            if (header.bits_per_sample == 16) {
                file.read(reinterpret_cast<char*>(result.samples.data()),
                         num_samples * sizeof(int16_t));
            } else if (header.bits_per_sample == 8) {
                /* Convert 8-bit to 16-bit */
                std::vector<uint8_t> temp(num_samples);
                file.read(reinterpret_cast<char*>(temp.data()), num_samples);
                for (size_t i = 0; i < num_samples; ++i) {
                    result.samples[i] = (static_cast<int16_t>(temp[i]) - 128) * 256;
                }
            } else {
                spdlog::warn("Неподдерживаемая битность: {} бит", header.bits_per_sample);
                result.samples.clear();
            }
            break;
        } else {
            /* Skip this chunk */
            file.seekg(chunk_size, std::ios::cur);
        }
    }

    if (result.valid()) {
        spdlog::debug("Загружен WAV: {} ({} Hz, {} ch, {} samples)",
                     path, result.sample_rate, result.channels, result.samples.size());
    }

    return result;
}

/* ========================================================================== */
/* File Search                                                                 */
/* ========================================================================== */

std::string find_sound_file(const std::string& name) {
    std::string filename = name + ".wav";

    /* 1. Environment variable */
    const char* sounds_dir = std::getenv("NERD_DICTATION_SOUNDS_DIR");
    if (sounds_dir) {
        std::filesystem::path path = std::filesystem::path(sounds_dir) / filename;
        if (std::filesystem::exists(path)) {
            return path.string();
        }
    }

    /* 2. ~/.local/share/nerd-dictation/sounds/ */
    const char* home = std::getenv("HOME");
    if (home) {
        std::filesystem::path path = std::filesystem::path(home) /
            ".local/share/nerd-dictation/sounds" / filename;
        if (std::filesystem::exists(path)) {
            return path.string();
        }
    }

    /* 3. /usr/share/nerd-dictation/sounds/ */
    {
        std::filesystem::path path = std::filesystem::path("/usr/share/nerd-dictation/sounds") / filename;
        if (std::filesystem::exists(path)) {
            return path.string();
        }
    }

    /* 4. /usr/local/share/nerd-dictation/sounds/ */
    {
        std::filesystem::path path = std::filesystem::path("/usr/local/share/nerd-dictation/sounds") / filename;
        if (std::filesystem::exists(path)) {
            return path.string();
        }
    }

    return "";
}

/* ========================================================================== */
/* SoundPlayer Implementation                                                  */
/* ========================================================================== */

SoundPlayer& SoundPlayer::instance() {
    static SoundPlayer player;
    return player;
}

SoundPlayer::~SoundPlayer() {
    shutdown();
}

bool SoundPlayer::init() {
    if (initialized_) {
        return true;
    }

    /* Load default sounds */
    const char* sound_names[] = {"begin", "end", "execute"};
    for (const char* name : sound_names) {
        std::string path = find_sound_file(name);
        if (!path.empty()) {
            load_sound(name, path);
        }
    }

    /* Start playback thread */
    running_ = true;
    playback_thread_ = std::thread(&SoundPlayer::playback_thread, this);
    initialized_ = true;

    spdlog::info("Звуковая подсистема инициализирована");
    return true;
}

void SoundPlayer::shutdown() {
    if (!initialized_) {
        return;
    }

    running_ = false;
    queue_cv_.notify_all();

    if (playback_thread_.joinable()) {
        playback_thread_.join();
    }

    {
        std::lock_guard<std::mutex> lock(sounds_mutex_);
        sounds_.clear();
    }

    initialized_ = false;
    spdlog::debug("Звуковая подсистема остановлена");
}

bool SoundPlayer::load_sound(const std::string& name, const std::string& path) {
    WavData wav = load_wav(path);
    if (!wav.valid()) {
        return false;
    }

    std::lock_guard<std::mutex> lock(sounds_mutex_);
    sounds_[name] = std::move(wav);
    return true;
}

void SoundPlayer::play(const std::string& name) {
    if (!initialized_) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        play_queue_.push(name);
    }
    queue_cv_.notify_one();
}

void SoundPlayer::playback_thread() {
    spdlog::debug("Поток воспроизведения звуков запущен");

    while (running_) {
        std::string sound_name;

        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            queue_cv_.wait(lock, [this] {
                return !play_queue_.empty() || !running_;
            });

            if (!running_) {
                break;
            }

            if (!play_queue_.empty()) {
                sound_name = play_queue_.front();
                play_queue_.pop();
            }
        }

        if (!sound_name.empty()) {
            WavData wav;
            {
                std::lock_guard<std::mutex> lock(sounds_mutex_);
                auto it = sounds_.find(sound_name);
                if (it != sounds_.end()) {
                    wav = it->second;  /* Copy for thread safety */
                }
            }

            if (wav.valid()) {
#ifdef HAVE_PULSEAUDIO
                play_wav_pulseaudio(wav);
#else
                spdlog::debug("Воспроизведение недоступно (нет PulseAudio)");
#endif
            } else {
                spdlog::debug("Звук не найден: {}", sound_name);
            }
        }
    }

    spdlog::debug("Поток воспроизведения звуков остановлен");
}

#ifdef HAVE_PULSEAUDIO
bool SoundPlayer::play_wav_pulseaudio(const WavData& wav) {
    pa_sample_spec ss;
    ss.format = PA_SAMPLE_S16LE;
    ss.rate = wav.sample_rate;
    ss.channels = wav.channels;

    int error;
    pa_simple* s = pa_simple_new(
        nullptr,                /* Default server */
        "nerd-dictation",       /* Application name */
        PA_STREAM_PLAYBACK,
        nullptr,                /* Default device */
        "notification",         /* Stream name */
        &ss,
        nullptr,                /* Default channel map */
        nullptr,                /* Default buffering */
        &error
    );

    if (!s) {
        spdlog::warn("PulseAudio ошибка: {}", pa_strerror(error));
        return false;
    }

    /* Write audio data */
    size_t bytes = wav.samples.size() * sizeof(int16_t);
    if (pa_simple_write(s, wav.samples.data(), bytes, &error) < 0) {
        spdlog::warn("PulseAudio write ошибка: {}", pa_strerror(error));
        pa_simple_free(s);
        return false;
    }

    /* Drain buffer */
    if (pa_simple_drain(s, &error) < 0) {
        spdlog::warn("PulseAudio drain ошибка: {}", pa_strerror(error));
    }

    pa_simple_free(s);
    return true;
}
#else
bool SoundPlayer::play_wav_pulseaudio(const WavData&) {
    return false;
}
#endif

bool SoundPlayer::play_wav_pipewire(const WavData&) {
    /* PipeWire playback - not implemented yet */
    /* PulseAudio simple API is easier for one-shot sounds */
    return false;
}

/* ========================================================================== */
/* Public API                                                                  */
/* ========================================================================== */

bool init_sounds() {
    return SoundPlayer::instance().init();
}

void shutdown_sounds() {
    SoundPlayer::instance().shutdown();
}

void play_begin() {
    SoundPlayer::instance().play("begin");
}

void play_end() {
    SoundPlayer::instance().play("end");
}

void play_execute() {
    SoundPlayer::instance().play("execute");
}

} /* namespace nerd_dictation::sounds */
