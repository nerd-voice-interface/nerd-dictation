#pragma once

#include "nerd_dictation/types.hpp"
#include <functional>
#include <memory>
#include <string>

namespace nerd_dictation::backends {

/**
 * Базовый интерфейс для ASR бэкендов
 */
class ASRBackend {
public:
    virtual ~ASRBackend() = default;
    
    /**
     * Загрузить модель
     */
    virtual void load_model(const std::string& path) = 0;
    
    /**
     * Потоковое распознавание
     * 
     * audio_provider вызывается для получения следующего чанка аудио
     *   Возвращает true если есть данные, false если поток закончился
     *   Заполняет переданный буфер аудио данными
     * 
     * result_callback вызывается для передачи результатов распознавания
     */
    virtual void transcribe_stream(
        std::function<bool(std::vector<int16_t>&)> audio_provider,
        std::function<void(const ASRResult&)> result_callback
    ) = 0;
    
    /**
     * Завершить распознавание и получить финальный результат
     */
    virtual ASRResult finalize() = 0;
    
    /**
     * Сбросить состояние распознавателя
     */
    virtual void reset() = 0;
    
    /**
     * Поддерживает ли потоковое распознавание
     */
    virtual bool supports_streaming() const = 0;
    
    /**
     * Имя бэкенда
     */
    virtual std::string name() const = 0;
};

} // namespace nerd_dictation::backends
