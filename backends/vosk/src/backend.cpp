#include "nerd_dictation_vosk/backend.hpp"
#include "nerd_dictation/types.hpp"
#include "nerd_dictation/backends/base.hpp"
#include <spdlog/spdlog.h>
#include <vosk/vosk.h>
#include <fstream>
#include <sstream>
#include <string>

namespace nerd_dictation::backends {

/**
 * Vosk бэкенд для распознавания речи
 */
class VoskBackend : public ASRBackend {
private:
    vosk_model_t* model_ = nullptr;
    vosk_recognizer_t* recognizer_ = nullptr;
    int sample_rate_ = 16000;
    
public:
    ~VoskBackend() override {
        if (recognizer_) {
            vosk_recognizer_free(recognizer_);
        }
        if (model_) {
            vosk_model_free(model_);
        }
    }
    
    void load_model(const std::string& path) override {
        spdlog::info("Loading Vosk model from: {}", path);
        
        model_ = vosk_model_new(path.c_str());
        if (!model_) {
            throw std::runtime_error("Failed to load Vosk model: " + path);
        }
        
        recognizer_ = vosk_recognizer_new(model_, sample_rate_);
        if (!recognizer_) {
            throw std::runtime_error("Failed to create Vosk recognizer");
        }
        
        spdlog::info("Vosk model loaded successfully");
    }
    
    void transcribe_stream(
        std::function<void(const int16_t*, size_t)> audio_callback,
        std::function<void(const ASRResult&)> result_callback
    ) override {
        if (!recognizer_) {
            throw std::runtime_error("Model not loaded");
        }
        
        spdlog::info("Vosk: начало потокового распознавания");
        
        // Симулируем обработку аудио через callback
        // В реальной реализации здесь будет чтение аудио потока
        audio_callback([&](const int16_t* audio, size_t samples) {
            // Обрабатываем аудио через Vosk
            if (vosk_recognizer_accept_waveform(recognizer_,
                    reinterpret_cast<const char*>(audio),
                    samples * sizeof(int16_t))) {
                // Финальный результат
                const char* result_str = vosk_recognizer_result(recognizer_);
                ASRResult result = parse_vosk_result(result_str);
                result.is_final = true;
                result_callback(result);
            } else {
                // Частичный результат
                const char* partial_str = vosk_recognizer_partial_result(recognizer_);
                ASRResult result = parse_vosk_result(partial_str);
                result.is_final = false;
                result_callback(result);
            }
        });
    }
    
    ASRResult finalize() override {
        if (!recognizer_) {
            return ASRResult{};
        }
        
        const char* result_str = vosk_recognizer_final_result(recognizer_);
        ASRResult result = parse_vosk_result(result_str);
        result.is_final = true;
        return result;
    }
    
    void reset() override {
        if (recognizer_ && model_) {
            vosk_recognizer_free(recognizer_);
            recognizer_ = vosk_recognizer_new(model_, sample_rate_);
        }
    }
    
    bool supports_streaming() const override {
        return true;
    }
    
    std::string name() const override {
        return "vosk";
    }
    
private:
    ASRResult parse_vosk_result(const char* json_str) {
        ASRResult result;
        
        // Простой парсинг JSON (можно использовать библиотеку)
        std::string json(json_str);
        
        // Ищем "text": "..."
        size_t text_pos = json.find("\"text\"");
        if (text_pos != std::string::npos) {
            size_t colon_pos = json.find(':', text_pos);
            if (colon_pos != std::string::npos) {
                size_t quote_start = json.find('"', colon_pos);
                if (quote_start != std::string::npos) {
                    size_t quote_end = json.find('"', quote_start + 1);
                    if (quote_end != std::string::npos) {
                        result.text = json.substr(quote_start + 1, quote_end - quote_start - 1);
                    }
                }
            }
        }
        
        // Ищем "confidence": ...
        size_t conf_pos = json.find("\"confidence\"");
        if (conf_pos != std::string::npos) {
            size_t colon_pos = json.find(':', conf_pos);
            if (colon_pos != std::string::npos) {
                try {
                    std::string conf_str = json.substr(colon_pos + 1);
                    size_t comma_pos = conf_str.find(',');
                    if (comma_pos != std::string::npos) {
                        conf_str = conf_str.substr(0, comma_pos);
                    }
                    result.confidence = std::stof(conf_str);
                } catch (...) {
                    // Игнорируем ошибки парсинга confidence
                }
            }
        }
        
        return result;
    }
};

std::unique_ptr<ASRBackend> create_vosk_backend() {
    return std::make_unique<VoskBackend>();
}

} // namespace nerd_dictation::backends
