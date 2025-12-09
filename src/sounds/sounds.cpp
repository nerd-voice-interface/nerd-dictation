#include "nerd_dictation/sounds/sounds.hpp"
#include <spdlog/spdlog.h>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <cstring>

#if HAVE_EMBEDDED_SOUNDS
#include <res_embed.h>
#endif

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

WavData load_wav_from_memory(const char* data, size_t size) {
    WavData result;

    if (!data || size < sizeof(WavHeader)) {
        return result;
    }

    /* Read header */
    const WavHeader* header = reinterpret_cast<const WavHeader*>(data);

    if (std::strncmp(header->riff, "RIFF", 4) != 0 ||
        std::strncmp(header->wave, "WAVE", 4) != 0) {
        spdlog::warn("Неверный формат WAV в памяти");
        return result;
    }

    if (header->audio_format != 1) {
        spdlog::warn("Поддерживается только PCM WAV");
        return result;
    }

    result.sample_rate = header->sample_rate;
    result.channels = header->num_channels;
    result.bits_per_sample = header->bits_per_sample;

    /* Find data chunk */
    size_t offset = sizeof(WavHeader);
    if (header->fmt_size > 16) {
        offset += header->fmt_size - 16;
    }

    while (offset + 8 <= size) {
        const char* chunk_id = data + offset;
        uint32_t chunk_size;
        std::memcpy(&chunk_size, data + offset + 4, sizeof(chunk_size));

        if (std::strncmp(chunk_id, "data", 4) == 0) {
            /* Found data chunk */
            size_t data_offset = offset + 8;
            if (data_offset + chunk_size > size) {
                chunk_size = size - data_offset;
            }

            size_t num_samples = chunk_size / (header->bits_per_sample / 8);
            result.samples.resize(num_samples);

            if (header->bits_per_sample == 16) {
                std::memcpy(result.samples.data(), data + data_offset,
                           num_samples * sizeof(int16_t));
            } else if (header->bits_per_sample == 8) {
                const uint8_t* src = reinterpret_cast<const uint8_t*>(data + data_offset);
                for (size_t i = 0; i < num_samples; ++i) {
                    result.samples[i] = (static_cast<int16_t>(src[i]) - 128) * 256;
                }
            } else {
                spdlog::warn("Неподдерживаемая битность: {} бит", header->bits_per_sample);
                result.samples.clear();
            }
            break;
        } else {
            offset += 8 + chunk_size;
        }
    }

    if (result.valid()) {
        spdlog::debug("Загружен WAV из памяти ({} Hz, {} ch, {} samples)",
                     result.sample_rate, result.channels, result.samples.size());
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

#if HAVE_EMBEDDED_SOUNDS
    /* Load sounds from embedded resources */
    struct EmbeddedSound {
        const char* name;
        const char* resource_name;
    };
    static const EmbeddedSound embedded_sounds[] = {
        {"begin", "sound_begin"},
        {"end", "sound_end"},
        {"execute", "sound_execute"},
    };

    for (const auto& sound : embedded_sounds) {
        size_t size = 0;
        const char* data = res::embed::get(sound.resource_name, &size);
        if (data && size > 0) {
            WavData wav = load_wav_from_memory(data, size);
            if (wav.valid()) {
                std::lock_guard<std::mutex> lock(sounds_mutex_);
                sounds_[sound.name] = std::move(wav);
                spdlog::debug("Загружен встроенный звук: {}", sound.name);
            }
        } else {
            spdlog::warn("Встроенный звук не найден: {}", sound.resource_name);
        }
    }
    spdlog::info("Звуковая подсистема инициализирована (встроенные ресурсы)");
#else
    /* Load default sounds from filesystem */
    const char* sound_names[] = {"begin", "end", "execute"};
    for (const char* name : sound_names) {
        std::string path = find_sound_file(name);
        if (!path.empty()) {
            load_sound(name, path);
        }
    }
    spdlog::info("Звуковая подсистема инициализирована");
#endif

    initialized_ = true;
    return true;
}

void SoundPlayer::shutdown() {
    if (!initialized_) {
        return;
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

    WavData wav;
    {
        std::lock_guard<std::mutex> lock(sounds_mutex_);
        auto it = sounds_.find(name);
        if (it != sounds_.end()) {
            wav = it->second;  /* Copy for thread safety */
        }
    }

    if (wav.valid()) {
        play_wav_async(wav);
    } else {
        spdlog::debug("Звук не найден: {}", name);
    }
}

#ifdef HAVE_PULSEAUDIO
void SoundPlayer::play_wav_async(const WavData& wav) {
    /* Play in detached thread for parallel playback */
    std::thread([wav]() {
        pa_sample_spec ss;
        ss.format = PA_SAMPLE_S16LE;
        ss.rate = wav.sample_rate;
        ss.channels = static_cast<uint8_t>(wav.channels);

        int error;
        pa_simple* s = pa_simple_new(
            nullptr, "nerd-dictation", PA_STREAM_PLAYBACK,
            nullptr, "notification", &ss, nullptr, nullptr, &error
        );
        if (!s) {
            spdlog::warn("PulseAudio ошибка: {}", pa_strerror(error));
            return;
        }

        size_t bytes = wav.samples.size() * sizeof(int16_t);
        pa_simple_write(s, wav.samples.data(), bytes, &error);
        pa_simple_drain(s, &error);
        pa_simple_free(s);
    }).detach();
}
#else
void SoundPlayer::play_wav_async(const WavData&) {
    spdlog::debug("Воспроизведение недоступно (нет PulseAudio)");
}
#endif

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
