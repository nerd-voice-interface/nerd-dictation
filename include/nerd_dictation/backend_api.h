/**
 * nerd-dictation Backend C API
 *
 * Портабельный интерфейс для ASR бекендов.
 * Не зависит от C++ ABI — совместим между компиляторами.
 */

#ifndef NERD_DICTATION_BACKEND_API_H
#define NERD_DICTATION_BACKEND_API_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque handle для бекенда */
typedef struct nerd_dictation_backend* nerd_dictation_t;

/**
 * Информация о бекенде (версионируемая структура)
 */
struct nerd_dictation_info {
    uint32_t struct_size;       /* sizeof(struct nerd_dictation_info) для версионирования */
    const char* name;           /* Имя бекенда (например "t-one", "vosk") */
    int streaming;              /* 1 если поддерживает streaming, 0 иначе */
    int sample_rate;            /* Ожидаемая частота дискретизации (Hz) */
    int channels;               /* Количество каналов (обычно 1) */
    int bits_per_sample;        /* Бит на сэмпл (обычно 16) */
};

/**
 * Результат распознавания
 */
struct nerd_dictation_result {
    const char* text;           /* Распознанный текст (NULL-terminated, owned by backend) */
    int is_final;               /* 1 если финальный результат, 0 если partial */
    float confidence;           /* Уверенность [0..1], -1 если недоступно */
};

/**
 * Callback для получения результатов распознавания
 * @param result Указатель на результат (валиден только во время вызова)
 * @param user_data Пользовательские данные, переданные в nerd_dictation_transcribe_stream
 */
typedef void (*nerd_dictation_result_callback)(
    const struct nerd_dictation_result* result,
    void* user_data
);

/**
 * Callback для получения аудио данных
 * @param samples Буфер для заполнения (int16_t, mono)
 * @param max_samples Максимальное количество сэмплов для записи
 * @param user_data Пользовательские данные
 * @return Количество записанных сэмплов, 0 если поток закончился, <0 при ошибке
 */
typedef int (*nerd_dictation_audio_callback)(
    int16_t* samples,
    size_t max_samples,
    void* user_data
);

/* ============================================================================
 * Функции жизненного цикла
 * ============================================================================ */

/**
 * Создать экземпляр бекенда
 * @return Handle или NULL при ошибке
 *
 * ВАЖНО: Эта функция должна быть реализована каждым бекендом
 * с именем nerd_dictation_create (экспортируется из .so)
 */
nerd_dictation_t nerd_dictation_create(void);

/**
 * Уничтожить экземпляр бекенда
 * @param backend Handle, полученный от nerd_dictation_create
 */
void nerd_dictation_destroy(nerd_dictation_t backend);

/**
 * Получить информацию о бекенде
 * @param backend Handle бекенда
 * @param info Указатель на структуру для заполнения (caller должен установить struct_size)
 * @return 0 при успехе, <0 при ошибке
 */
int nerd_dictation_get_info(nerd_dictation_t backend, struct nerd_dictation_info* info);

/* ============================================================================
 * Функции распознавания
 * ============================================================================ */

/**
 * Подать аудио данные для распознавания
 * @param backend Handle бекенда
 * @param samples Указатель на аудио данные (int16_t, mono)
 * @param num_samples Количество сэмплов
 * @return 0 при успехе, <0 при ошибке
 */
int nerd_dictation_feed(nerd_dictation_t backend, const int16_t* samples, size_t num_samples);

/**
 * Получить текущий результат распознавания
 * @param backend Handle бекенда
 * @param result Указатель на структуру результата для заполнения
 * @return 0 при успехе (даже если текст пустой), <0 при ошибке
 *
 * Текст в result->text валиден до следующего вызова nerd_dictation_feed,
 * nerd_dictation_result, nerd_dictation_finalize или nerd_dictation_reset
 */
int nerd_dictation_result(nerd_dictation_t backend, struct nerd_dictation_result* result);

/**
 * Сигнализировать окончание аудио потока и получить финальный результат
 * @param backend Handle бекенда
 * @param result Указатель на структуру результата для заполнения
 * @return 0 при успехе, <0 при ошибке
 */
int nerd_dictation_finalize(nerd_dictation_t backend, struct nerd_dictation_result* result);

/**
 * Сбросить состояние распознавателя для нового сеанса
 * @param backend Handle бекенда
 * @return 0 при успехе, <0 при ошибке
 */
int nerd_dictation_reset(nerd_dictation_t backend);

/* ============================================================================
 * Streaming API (опциональный, convenience wrapper)
 * ============================================================================ */

/**
 * Потоковое распознавание с callbacks
 * @param backend Handle бекенда
 * @param audio_cb Callback для получения аудио (вызывается пока не вернёт 0 или <0)
 * @param result_cb Callback для результатов (вызывается при каждом новом результате)
 * @param user_data Пользовательские данные для callbacks
 * @return 0 при успехе, <0 при ошибке
 */
int nerd_dictation_transcribe_stream(
    nerd_dictation_t backend,
    nerd_dictation_audio_callback audio_cb,
    nerd_dictation_result_callback result_cb,
    void* user_data
);

/* ============================================================================
 * Утилиты
 * ============================================================================ */

/**
 * Получить строку последней ошибки
 * @param backend Handle бекенда (может быть NULL для глобальных ошибок)
 * @return Строка ошибки или NULL если ошибок нет
 */
const char* nerd_dictation_get_error(nerd_dictation_t backend);

/**
 * Получить версию API
 * @return Версия в формате MAJOR*10000 + MINOR*100 + PATCH
 */
int nerd_dictation_api_version(void);

#define NERD_DICTATION_API_VERSION 10000  /* 1.0.0 */

#ifdef __cplusplus
}
#endif

#endif /* NERD_DICTATION_BACKEND_API_H */
