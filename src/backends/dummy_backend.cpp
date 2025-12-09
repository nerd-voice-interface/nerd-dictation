/**
 * Dummy ASR Backend - C API Implementation
 *
 * Тестовый бекенд для отладки фронтенда.
 * Генерирует случайные фразы с настраиваемой задержкой.
 * Не требует модели — всегда доступен.
 */

#include "nerd_dictation/backend_api.h"
#include <cstring>
#include <string>
#include <vector>
#include <random>
#include <chrono>
#include <mutex>

/* Sample rate для dummy бекенда */
static constexpr int DUMMY_SAMPLE_RATE = 16000;

/* Количество сэмплов для генерации результата */
static constexpr size_t SAMPLES_PER_RESULT = DUMMY_SAMPLE_RATE * 2;  /* 2 секунды */

/* Тестовые фразы */
static const char* SAMPLE_PHRASES[] = {
    "привет мир",
    "это тестовый текст",
    "распознавание речи работает",
    "dummy бекенд генерирует текст",
    "можно тестировать фронтенд",
    "здесь могла быть ваша реклама",
    "проверка связи один два три",
    "тестирование системы диктовки",
};
static constexpr size_t NUM_PHRASES = sizeof(SAMPLE_PHRASES) / sizeof(SAMPLE_PHRASES[0]);

/* Internal backend structure */
struct nerd_dictation_backend {
    int sample_rate;

    /* Счётчик принятых сэмплов */
    size_t samples_received;

    /* Генератор случайных чисел */
    std::mt19937 rng;

    /* Текущий partial результат (имитация) */
    std::string current_partial;
    int partial_word_count;

    /* Result buffer */
    std::string result_text;
    std::string error_message;

    /* Thread safety */
    std::mutex mutex;
};

/* ============================================================================
 * Lifecycle functions
 * ============================================================================ */

extern "C" nerd_dictation_t nerd_dictation_create(void) {
    auto* backend = new (std::nothrow) nerd_dictation_backend();
    if (!backend) {
        return nullptr;
    }

    backend->sample_rate = DUMMY_SAMPLE_RATE;
    backend->samples_received = 0;
    backend->partial_word_count = 0;

    /* Инициализация RNG с текущим временем */
    auto seed = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    backend->rng.seed(static_cast<unsigned>(seed));

    return backend;
}

extern "C" void nerd_dictation_destroy(nerd_dictation_t backend) {
    delete backend;
}

extern "C" int nerd_dictation_get_info(nerd_dictation_t backend, struct nerd_dictation_info* info) {
    if (!backend || !info) return -1;
    if (info->struct_size < sizeof(struct nerd_dictation_info)) return -2;

    info->name = "dummy";
    info->streaming = 1;
    info->sample_rate = backend->sample_rate;
    info->channels = 1;
    info->bits_per_sample = 16;

    return 0;
}

/* ============================================================================
 * Recognition functions
 * ============================================================================ */

extern "C" int nerd_dictation_feed(nerd_dictation_t backend, const int16_t* samples, size_t num_samples) {
    if (!backend) return -1;
    if (!samples || num_samples == 0) return 0;

    std::lock_guard<std::mutex> lock(backend->mutex);

    backend->samples_received += num_samples;

    /* Имитируем накопление partial результата */
    size_t progress = backend->samples_received % SAMPLES_PER_RESULT;
    size_t words_to_show = (progress * 5) / SAMPLES_PER_RESULT;  /* 0-4 слова */

    if (words_to_show > static_cast<size_t>(backend->partial_word_count)) {
        /* Выбираем случайную фразу и берём часть слов */
        std::uniform_int_distribution<size_t> dist(0, NUM_PHRASES - 1);
        const char* phrase = SAMPLE_PHRASES[dist(backend->rng)];

        /* Берём первые N слов */
        backend->current_partial.clear();
        const char* p = phrase;
        size_t word_count = 0;
        while (*p && word_count < words_to_show) {
            if (*p == ' ') {
                ++word_count;
            }
            backend->current_partial += *p;
            ++p;
        }

        backend->partial_word_count = static_cast<int>(words_to_show);
    }

    return 0;
}

extern "C" int nerd_dictation_result(nerd_dictation_t backend, struct nerd_dictation_result* result) {
    if (!backend || !result) return -1;

    std::lock_guard<std::mutex> lock(backend->mutex);

    /* Проверяем, накопилось ли достаточно сэмплов для "финального" результата */
    bool is_endpoint = (backend->samples_received >= SAMPLES_PER_RESULT) &&
                       ((backend->samples_received % SAMPLES_PER_RESULT) < static_cast<size_t>(backend->sample_rate / 10));

    if (is_endpoint) {
        /* Генерируем финальный результат */
        std::uniform_int_distribution<size_t> dist(0, NUM_PHRASES - 1);
        backend->result_text = SAMPLE_PHRASES[dist(backend->rng)];

        result->text = backend->result_text.c_str();
        result->is_final = 1;
        result->confidence = 0.95f;

        /* Сбрасываем partial */
        backend->current_partial.clear();
        backend->partial_word_count = 0;
    } else {
        /* Partial результат */
        backend->result_text = backend->current_partial;
        result->text = backend->result_text.c_str();
        result->is_final = 0;
        result->confidence = 0.7f;
    }

    return 0;
}

extern "C" int nerd_dictation_finalize(nerd_dictation_t backend, struct nerd_dictation_result* result) {
    if (!backend || !result) return -1;

    std::lock_guard<std::mutex> lock(backend->mutex);

    /* Генерируем финальный результат */
    std::uniform_int_distribution<size_t> dist(0, NUM_PHRASES - 1);
    backend->result_text = SAMPLE_PHRASES[dist(backend->rng)];

    result->text = backend->result_text.c_str();
    result->is_final = 1;
    result->confidence = 0.95f;

    /* Сбрасываем состояние */
    backend->samples_received = 0;
    backend->current_partial.clear();
    backend->partial_word_count = 0;

    return 0;
}

extern "C" int nerd_dictation_reset(nerd_dictation_t backend) {
    if (!backend) return -1;

    std::lock_guard<std::mutex> lock(backend->mutex);

    backend->samples_received = 0;
    backend->result_text.clear();
    backend->current_partial.clear();
    backend->partial_word_count = 0;

    return 0;
}

/* ============================================================================
 * Streaming API
 * ============================================================================ */

extern "C" int nerd_dictation_transcribe_stream(
    nerd_dictation_t backend,
    nerd_dictation_audio_callback audio_cb,
    nerd_dictation_result_callback result_cb,
    void* user_data
) {
    if (!backend || !audio_cb) return -1;

    std::vector<int16_t> audio_chunk(backend->sample_rate / 10);  /* 100ms chunks */
    std::string last_text;

    while (true) {
        int samples_read = audio_cb(audio_chunk.data(), audio_chunk.size(), user_data);

        if (samples_read < 0) {
            backend->error_message = "Audio callback returned error";
            return -3;
        }

        if (samples_read == 0) {
            break;
        }

        int ret = nerd_dictation_feed(backend, audio_chunk.data(), static_cast<size_t>(samples_read));
        if (ret < 0) {
            return ret;
        }

        struct nerd_dictation_result result;
        ret = nerd_dictation_result(backend, &result);
        if (ret < 0) {
            return ret;
        }

        if (result_cb && result.text) {
            std::string current_text(result.text);

            if (result.is_final || current_text != last_text) {
                result_cb(&result, user_data);

                if (result.is_final) {
                    last_text.clear();
                } else {
                    last_text = current_text;
                }
            }
        }
    }

    /* Finalize */
    struct nerd_dictation_result final_result;
    int ret = nerd_dictation_finalize(backend, &final_result);
    if (ret == 0 && result_cb && final_result.text && strlen(final_result.text) > 0) {
        result_cb(&final_result, user_data);
    }

    return 0;
}

/* ============================================================================
 * Utilities
 * ============================================================================ */

extern "C" const char* nerd_dictation_get_error(nerd_dictation_t backend) {
    if (!backend) return nullptr;
    return backend->error_message.empty() ? nullptr : backend->error_message.c_str();
}

extern "C" int nerd_dictation_api_version(void) {
    return NERD_DICTATION_API_VERSION;
}
