#include "nerd_dictation/backends/base.hpp"
#include <thread>
#include <chrono>
#include <random>
#include <spdlog/spdlog.h>

namespace nerd_dictation::backends {

/**
 * Dummy бэкенд для тестирования фронтенда
 * Генерирует случайный текст через некоторое время
 */
class DummyBackend : public ASRBackend {
private:
    std::string model_path_;
    bool model_loaded_ = false;
    std::mt19937 rng_;
    std::vector<std::string> sample_texts_ = {
        "привет мир",
        "это тестовый текст",
        "распознавание речи работает",
        "dummy бэкенд генерирует текст",
        "можно тестировать фронтенд",
    };
    
public:
    DummyBackend() : rng_(std::random_device{}()) {}
    
    void load_model(const std::string& path) override {
        model_path_ = path;
        model_loaded_ = true;
        spdlog::info("DummyBackend: модель загружена (путь: {})", path);
    }
    
    void transcribe_stream(
        std::function<bool(std::vector<int16_t>&)> /* audio_provider */,
        std::function<void(const ASRResult&)> result_callback
    ) override {
        if (!model_loaded_) {
            spdlog::error("DummyBackend: модель не загружена");
            return;
        }
        
        spdlog::info("DummyBackend: начало потокового распознавания");
        
        // Симулируем обработку аудио
        std::thread([this, result_callback]() {
            // Ждём немного перед первым результатом
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            
            // Генерируем несколько частичных результатов
            for (int i = 0; i < 3; ++i) {
                std::uniform_int_distribution<> dist(0, sample_texts_.size() - 1);
                std::string text = sample_texts_[dist(rng_)];
                
                ASRResult partial(text, false, 0.8f);
                result_callback(partial);
                
                std::this_thread::sleep_for(std::chrono::milliseconds(300));
            }
            
            // Финальный результат
            std::uniform_int_distribution<> dist(0, sample_texts_.size() - 1);
            std::string final_text = sample_texts_[dist(rng_)];
            ASRResult final(final_text, true, 0.95f);
            result_callback(final);
            
            spdlog::info("DummyBackend: распознавание завершено");
        }).detach();
    }
    
    ASRResult finalize() override {
        std::uniform_int_distribution<> dist(0, sample_texts_.size() - 1);
        std::string text = sample_texts_[dist(rng_)];
        return ASRResult(text, true, 0.95f);
    }
    
    void reset() override {
        spdlog::debug("DummyBackend: сброс состояния");
    }
    
    bool supports_streaming() const override {
        return true;
    }
    
    std::string name() const override {
        return "dummy";
    }
};

std::unique_ptr<ASRBackend> create_dummy_backend() {
    return std::make_unique<DummyBackend>();
}

} // namespace nerd_dictation::backends
