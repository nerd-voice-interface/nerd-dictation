#include "nerd_dictation_vosk/backend.hpp"
#include "nerd_dictation/types.hpp"
#include "nerd_dictation/backends/base.hpp"
#include <spdlog/spdlog.h>
#include <vosk/vosk.h>
#include <fstream>
#include <sstream>
#include <string>
#include <algorithm>
#include <cstring>
#include <vector>
#include <filesystem>

namespace nerd_dictation::backends {

/**
 * Vosk бэкенд для распознавания речи
 */
class VoskBackend : public ASRBackend {
private:
    vosk_model_t* model_ = nullptr;
    vosk_recognizer_t* recognizer_ = nullptr;
    int sample_rate_ = 16000;
    std::string model_path_;
    
public:
    VoskBackend() {
        // Отключаем логи Kaldi (если функция доступна)
        // Vosk API может не иметь эту функцию в некоторых версиях
    }
    
    ~VoskBackend() override {
        reset();
        if (model_) {
            vosk_model_free(model_);
        }
    }
    
    void load_model(const std::string& path) override {
        spdlog::info("Loading Vosk model from: {}", path);
        
        model_path_ = path;
        
        model_ = vosk_model_new(path.c_str());
        if (!model_) {
            throw std::runtime_error("Failed to load Vosk model: " + path);
        }
        
        recognizer_ = vosk_recognizer_new(model_, sample_rate_);
        if (!recognizer_) {
            vosk_model_free(model_);
            model_ = nullptr;
            throw std::runtime_error("Failed to create Vosk recognizer");
        }
        
        spdlog::info("Vosk model loaded successfully (sample rate: {} Hz)", sample_rate_);
    }
    
    void transcribe_stream(
        std::function<bool(std::vector<int16_t>&)> audio_provider,
        std::function<void(const ASRResult&)> result_callback
    ) override {
        if (!recognizer_) {
            throw std::runtime_error("Model not loaded. Call load_model() first.");
        }
        
        spdlog::info("Vosk: начало потокового распознавания");
        
        std::vector<int16_t> audio_chunk;
        size_t chunk_count = 0;
        
        // Читаем аудио чанки из провайдера
        while (audio_provider(audio_chunk)) {
            if (audio_chunk.empty()) {
                continue;
            }
            
            chunk_count++;
            
            // Отправляем аудио в Vosk
            if (vosk_recognizer_accept_waveform(recognizer_,
                    reinterpret_cast<const char*>(audio_chunk.data()),
                    audio_chunk.size() * sizeof(int16_t))) {
                // Финальный результат (фраза завершена)
                const char* result_str = vosk_recognizer_result(recognizer_);
                ASRResult result = parse_vosk_result(result_str);
                result.is_final = true;
                
                if (!result.text.empty()) {
                    spdlog::info("Vosk: финальный результат: '{}'", result.text);
                    result_callback(result);
                }
            } else {
                // Частичный результат (фраза ещё не завершена)
                const char* partial_str = vosk_recognizer_partial_result(recognizer_);
                ASRResult result = parse_vosk_result(partial_str);
                result.is_final = false;
                
                // Выводим частичные результаты реже, чтобы не спамить
                static size_t partial_counter = 0;
                if (++partial_counter % 10 == 0 && !result.text.empty()) {
                    spdlog::debug("Vosk: частичный результат: '{}'", result.text);
                    result_callback(result);
                }
            }
        }
        
        spdlog::info("Vosk: потоковое распознавание завершено (обработано {} чанков)", chunk_count);
    }
    
    ASRResult finalize() override {
        if (!recognizer_) {
            return ASRResult{};
        }
        
        const char* result_str = vosk_recognizer_final_result(recognizer_);
        ASRResult result = parse_vosk_result(result_str);
        result.is_final = true;
        
        if (!result.text.empty()) {
            spdlog::info("Vosk: финальный результат: '{}'", result.text);
        }
        
        return result;
    }
    
    void reset() override {
        if (recognizer_ && model_) {
            vosk_recognizer_free(recognizer_);
            recognizer_ = vosk_recognizer_new(model_, sample_rate_);
            spdlog::debug("Vosk: состояние распознавателя сброшено");
        }
    }
    
    bool supports_streaming() const override {
        return true;
    }
    
    std::string name() const override {
        return "vosk";
    }
    
    ASRResult transcribe_file(const std::filesystem::path& path) override {
        if (!recognizer_) {
            throw std::runtime_error("Model not loaded");
        }
        
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            throw std::runtime_error("Failed to open audio file: " + path.string());
        }
        
        // Читаем WAV файл (упрощённая версия)
        // В реальной реализации нужно правильно парсить WAV заголовок
        std::vector<char> buffer((std::istreambuf_iterator<char>(file)),
                                 std::istreambuf_iterator<char>());
        
        if (buffer.size() < 44) { // Минимальный размер WAV файла
            throw std::runtime_error("Invalid WAV file");
        }
        
        // Пропускаем WAV заголовок (44 байта) и читаем PCM данные
        size_t data_start = 44;
        if (buffer.size() <= data_start) {
            return ASRResult{};
        }
        
        std::vector<std::string> results;
        const int16_t* audio_data = reinterpret_cast<const int16_t*>(buffer.data() + data_start);
        size_t samples = (buffer.size() - data_start) / sizeof(int16_t);
        
        // Обрабатываем чанками по 4000 сэмплов
        const size_t chunk_samples = 4000;
        for (size_t i = 0; i < samples; i += chunk_samples) {
            size_t chunk_size = std::min(chunk_samples, samples - i);
            
            if (vosk_recognizer_accept_waveform(recognizer_,
                    reinterpret_cast<const char*>(audio_data + i),
                    chunk_size * sizeof(int16_t))) {
                const char* result_str = vosk_recognizer_result(recognizer_);
                ASRResult result = parse_vosk_result(result_str);
                if (!result.text.empty()) {
                    results.push_back(result.text);
                }
            }
        }
        
        // Финальный результат
        ASRResult final_result = finalize();
        if (!final_result.text.empty()) {
            results.push_back(final_result.text);
        }
        
        // Объединяем все результаты
        std::ostringstream oss;
        for (size_t i = 0; i < results.size(); ++i) {
            if (i > 0) oss << " ";
            oss << results[i];
        }
        
        return ASRResult{oss.str(), true};
    }
    
private:
    ASRResult parse_vosk_result(const char* json_str) {
        ASRResult result;
        
        if (!json_str || strlen(json_str) == 0) {
            return result;
        }
        
        std::string json(json_str);
        
        // Ищем "text": "..." или "partial": "..."
        size_t text_pos = json.find("\"text\"");
        size_t partial_pos = json.find("\"partial\"");
        
        size_t target_pos = std::string::npos;
        bool is_partial = false;
        
        if (partial_pos != std::string::npos && 
            (text_pos == std::string::npos || partial_pos < text_pos)) {
            target_pos = partial_pos;
            is_partial = true;
        } else if (text_pos != std::string::npos) {
            target_pos = text_pos;
            is_partial = false;
        }
        
        if (target_pos != std::string::npos) {
            size_t colon_pos = json.find(':', target_pos);
            if (colon_pos != std::string::npos) {
                size_t quote_start = json.find('"', colon_pos);
                if (quote_start != std::string::npos) {
                    size_t quote_end = json.find('"', quote_start + 1);
                    if (quote_end != std::string::npos) {
                        result.text = json.substr(quote_start + 1, quote_end - quote_start - 1);
                        result.is_final = !is_partial;
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
                    // Убираем пробелы
                    conf_str.erase(std::remove_if(conf_str.begin(), conf_str.end(), ::isspace), conf_str.end());
                    
                    size_t comma_pos = conf_str.find(',');
                    size_t brace_pos = conf_str.find('}');
                    size_t end_pos = std::min(comma_pos, brace_pos);
                    
                    if (end_pos != std::string::npos) {
                        conf_str = conf_str.substr(0, end_pos);
                    }
                    
                    if (!conf_str.empty()) {
                        result.confidence = std::stof(conf_str);
                    }
                } catch (const std::exception& e) {
                    spdlog::debug("Failed to parse confidence: {}", e.what());
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
