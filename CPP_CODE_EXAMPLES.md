# Примеры кода для C++ реализации

## Структура основных компонентов

### 1. Базовый интерфейс бэкенда

```cpp
// include/nerd_dictation/backend.hpp
#pragma once

#include <string>
#include <functional>
#include <memory>
#include <filesystem>

namespace nerd_dictation {

struct ASRResult {
    std::string text;
    bool is_final = false;
    float confidence = 0.0f;
};

class ASRBackend {
public:
    virtual ~ASRBackend() = default;
    
    virtual void load_model(const std::string& path) = 0;
    
    // Потоковое распознавание через callback
    virtual void transcribe_stream(
        std::function<void(const int16_t*, size_t)> audio_callback,
        std::function<void(const ASRResult&)> result_callback
    ) = 0;
    
    virtual ASRResult transcribe_file(const std::filesystem::path& path) = 0;
    virtual ASRResult finalize() = 0;
    virtual void reset() = 0;
    
    virtual bool supports_streaming() const = 0;
    virtual std::string name() const = 0;
};

} // namespace nerd_dictation
```

---

### 2. Vosk бэкенд

```cpp
// src/backends/vosk_backend.cpp
#include "nerd_dictation/backend.hpp"
#include <vosk/vosk.h>
#include <spdlog/spdlog.h>

namespace nerd_dictation::backends {

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
        
        audio_callback([&](const int16_t* audio, size_t samples) {
            if (vosk_recognizer_accept_waveform(recognizer_, 
                    reinterpret_cast<const char*>(audio), 
                    samples * sizeof(int16_t))) {
                // Финальный результат
                const char* result_str = vosk_recognizer_result(recognizer_);
                // Парсим JSON (используем nlohmann/json или простой парсер)
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
    
    bool supports_streaming() const override { return true; }
    std::string name() const override { return "vosk"; }
    
private:
    ASRResult parse_vosk_result(const char* json_str) {
        // Простой парсинг JSON (или использовать библиотеку)
        // Пример: {"text": "привет мир", "confidence": 0.95}
        ASRResult result;
        // ... парсинг ...
        return result;
    }
};

} // namespace nerd_dictation::backends
```

---

### 3. Кольцевой буфер (lock-free)

```cpp
// include/nerd_dictation/audio/ring_buffer.hpp
#pragma once

#include <atomic>
#include <vector>
#include <cstring>

namespace nerd_dictation::audio {

template<typename T>
class RingBuffer {
private:
    std::vector<T> buffer_;
    std::atomic<size_t> write_pos_{0};
    std::atomic<size_t> read_pos_{0};
    size_t capacity_;
    
public:
    explicit RingBuffer(size_t capacity) 
        : capacity_(capacity), buffer_(capacity) {}
    
    bool push(const T* data, size_t count) {
        size_t current_write = write_pos_.load(std::memory_order_relaxed);
        size_t current_read = read_pos_.load(std::memory_order_acquire);
        
        size_t available = capacity_ - (current_write - current_read);
        if (available < count) {
            return false; // Буфер переполнен
        }
        
        size_t write_idx = current_write % capacity_;
        size_t space_until_wrap = capacity_ - write_idx;
        
        if (count <= space_until_wrap) {
            // Всё помещается до конца буфера
            std::memcpy(&buffer_[write_idx], data, count * sizeof(T));
        } else {
            // Разделяем на две части
            std::memcpy(&buffer_[write_idx], data, 
                       space_until_wrap * sizeof(T));
            std::memcpy(&buffer_[0], data + space_until_wrap,
                       (count - space_until_wrap) * sizeof(T));
        }
        
        write_pos_.store(current_write + count, std::memory_order_release);
        return true;
    }
    
    bool pop(T* data, size_t count) {
        size_t current_read = read_pos_.load(std::memory_order_relaxed);
        size_t current_write = write_pos_.load(std::memory_order_acquire);
        
        size_t available = current_write - current_read;
        if (available < count) {
            return false; // Недостаточно данных
        }
        
        size_t read_idx = current_read % capacity_;
        size_t space_until_wrap = capacity_ - read_idx;
        
        if (count <= space_until_wrap) {
            std::memcpy(data, &buffer_[read_idx], count * sizeof(T));
        } else {
            std::memcpy(data, &buffer_[read_idx],
                       space_until_wrap * sizeof(T));
            std::memcpy(data + space_until_wrap, &buffer_[0],
                       (count - space_until_wrap) * sizeof(T));
        }
        
        read_pos_.store(current_read + count, std::memory_order_release);
        return true;
    }
    
    size_t available() const {
        size_t current_write = write_pos_.load(std::memory_order_acquire);
        size_t current_read = read_pos_.load(std::memory_order_acquire);
        return current_write - current_read;
    }
    
    void clear() {
        read_pos_.store(write_pos_.load(std::memory_order_relaxed));
    }
};

} // namespace nerd_dictation::audio
```

---

### 4. PipeWire аудио захват

```cpp
// src/audio/pipewire_capture.cpp
#include "nerd_dictation/audio/audio_capture.hpp"
#include <pipewire/pipewire.h>
#include <pipewire/filter.h>
#include <spa/param/audio/format-utils.h>
#include <spdlog/spdlog.h>

namespace nerd_dictation::audio {

class PipeWireCapture {
private:
    struct pw_main_loop* loop_ = nullptr;
    struct pw_context* context_ = nullptr;
    struct pw_stream* stream_ = nullptr;
    RingBuffer<int16_t>* buffer_ = nullptr;
    std::function<void(const int16_t*, size_t)> callback_;
    
    static void on_stream_data(void* userdata, const struct pw_buffer* buffer) {
        auto* self = static_cast<PipeWireCapture*>(userdata);
        
        struct spa_data* d = buffer->buffer->datas;
        if (d->chunk->size == 0) {
            return;
        }
        
        const int16_t* samples = static_cast<const int16_t*>(d->data);
        size_t sample_count = d->chunk->size / sizeof(int16_t);
        
        if (self->callback_) {
            self->callback_(samples, sample_count);
        }
        
        if (self->buffer_) {
            self->buffer_->push(samples, sample_count);
        }
    }
    
    static const struct pw_stream_events stream_events_;
    
public:
    PipeWireCapture(int sample_rate, size_t buffer_capacity) {
        pw_init(nullptr, nullptr);
        
        loop_ = pw_main_loop_new(nullptr);
        context_ = pw_context_new(pw_main_loop_get_loop(loop_), nullptr, 0);
        
        stream_ = pw_stream_new(context_, "nerd-dictation", nullptr);
        
        // Настройка формата аудио
        struct spa_pod_builder b;
        uint8_t buffer[1024];
        spa_pod_builder_init(&b, buffer, sizeof(buffer));
        
        struct spa_audio_info_raw audio_info = {};
        audio_info.format = SPA_AUDIO_FORMAT_S16;
        audio_info.rate = sample_rate;
        audio_info.channels = 1;
        
        const struct spa_pod* params[1];
        params[0] = spa_format_audio_raw_build(&b, SPA_PARAM_EnumFormat,
                                               &audio_info);
        
        pw_stream_add_listener(stream_, nullptr, &stream_events_, this);
        pw_stream_connect(stream_,
                         PW_DIRECTION_INPUT,
                         PW_ID_ANY,
                         PW_STREAM_FLAG_AUTOCONNECT |
                         PW_STREAM_FLAG_MAP_BUFFERS,
                         params, 1);
        
        buffer_ = new RingBuffer<int16_t>(buffer_capacity);
        
        spdlog::info("PipeWire capture initialized");
    }
    
    ~PipeWireCapture() {
        if (stream_) {
            pw_stream_destroy(stream_);
        }
        if (context_) {
            pw_context_destroy(context_);
        }
        if (loop_) {
            pw_main_loop_destroy(loop_);
        }
        delete buffer_;
    }
    
    void set_callback(std::function<void(const int16_t*, size_t)> cb) {
        callback_ = std::move(cb);
    }
    
    void start() {
        pw_main_loop_run(loop_);
    }
    
    void stop() {
        if (loop_) {
            pw_main_loop_quit(loop_);
        }
    }
    
    RingBuffer<int16_t>* get_buffer() const {
        return buffer_;
    }
};

const struct pw_stream_events PipeWireCapture::stream_events_ = {
    .version = PW_VERSION_STREAM_EVENTS,
    .state_changed = nullptr,
    .control_info = nullptr,
    .io_changed = nullptr,
    .param_changed = nullptr,
    .add_buffer = nullptr,
    .remove_buffer = nullptr,
    .process = on_stream_data,
};

} // namespace nerd_dictation::audio
```

---

### 5. Daemon с многопоточностью

```cpp
// src/daemon/daemon.cpp
#include "nerd_dictation/daemon.hpp"
#include "nerd_dictation/backend.hpp"
#include "nerd_dictation/audio/audio_capture.hpp"
#include "nerd_dictation/input/input_simulator.hpp"
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

namespace nerd_dictation {

enum class DaemonState {
    Idle,
    Listening,
    Stopping
};

class DictationDaemon {
private:
    Config config_;
    std::unique_ptr<ASRBackend> backend_;
    
    std::atomic<DaemonState> state_{DaemonState::Idle};
    
    // Потоки
    std::thread audio_thread_;
    std::thread dictation_thread_;
    std::atomic<bool> running_{true};
    
    // Аудио
    std::unique_ptr<audio::AudioCapture> audio_capture_;
    std::unique_ptr<audio::RingBuffer<int16_t>> audio_buffer_;
    std::mutex audio_buffer_mutex_;
    std::condition_variable audio_condition_;
    
    // Unix socket
    int server_fd_ = -1;
    std::string socket_path_;
    
    void audio_listener_loop() {
        audio_capture_->set_callback([this](const int16_t* samples, size_t count) {
            std::lock_guard<std::mutex> lock(audio_buffer_mutex_);
            audio_buffer_->push(samples, count);
            audio_condition_.notify_one();
        });
        
        audio_capture_->start();
    }
    
    void dictation_loop() {
        while (running_ && state_ == DaemonState::Listening) {
            std::vector<int16_t> chunk(4000); // ~0.25 секунды при 16kHz
            
            {
                std::unique_lock<std::mutex> lock(audio_buffer_mutex_);
                audio_condition_.wait(lock, [this] {
                    return audio_buffer_->available() >= chunk.size() || 
                           !running_;
                });
                
                if (!running_) break;
                
                audio_buffer_->pop(chunk.data(), chunk.size());
            }
            
            // Передаём в бэкенд
            backend_->transcribe_stream(
                [&chunk](auto callback) {
                    callback(chunk.data(), chunk.size());
                },
                [this](const ASRResult& result) {
                    if (result.is_final && !result.text.empty()) {
                        // Постобработка и ввод
                        process_and_type(result.text);
                    }
                }
            );
        }
    }
    
    void process_and_type(const std::string& text) {
        // Постобработка текста
        TextPostProcessor processor;
        auto actions = processor.process(text);
        
        InputSimulator input(config_.input_method);
        for (const auto& action : actions) {
            std::visit([&input](auto&& arg) {
                using T = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<T, TextAction>) {
                    input.type_text(arg.text);
                } else if constexpr (std::is_same_v<T, KeyAction>) {
                    input.press_keys(arg.keys);
                }
            }, action);
        }
    }
    
    void socket_server_loop() {
        struct sockaddr_un addr;
        addr.sun_family = AF_UNIX;
        std::strncpy(addr.sun_path, socket_path_.c_str(), 
                    sizeof(addr.sun_path) - 1);
        
        server_fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
        if (server_fd_ < 0) {
            throw std::runtime_error("Failed to create socket");
        }
        
        unlink(socket_path_.c_str());
        if (bind(server_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            throw std::runtime_error("Failed to bind socket");
        }
        
        listen(server_fd_, 5);
        
        while (running_) {
            int client_fd = accept(server_fd_, nullptr, nullptr);
            if (client_fd >= 0) {
                handle_client(client_fd);
                close(client_fd);
            }
        }
    }
    
    void handle_client(int client_fd) {
        char buffer[4096];
        ssize_t n = read(client_fd, buffer, sizeof(buffer) - 1);
        if (n > 0) {
            buffer[n] = '\0';
            // Парсим JSON команду и выполняем
            // Отправляем ответ обратно
        }
    }
    
public:
    DictationDaemon(const Config& config) : config_(config) {
        socket_path_ = std::string(std::getenv("XDG_RUNTIME_DIR")) + 
                      "/nerd-dictation-ex.sock";
        
        // Инициализация бэкенда
        backend_ = create_backend(config_.backend);
        backend_->load_model(config_.get_model_path());
        
        // Инициализация аудио
        audio_capture_ = std::make_unique<audio::PipeWireCapture>(
            config_.audio.sample_rate, 
            config_.audio.sample_rate * 2  // 2 секунды буфера
        );
        audio_buffer_ = audio_capture_->get_buffer();
    }
    
    void run() {
        // Запуск потоков
        audio_thread_ = std::thread(&DictationDaemon::audio_listener_loop, this);
        
        // Запуск socket server в отдельном потоке
        std::thread socket_thread(&DictationDaemon::socket_server_loop, this);
        
        // Основной цикл
        socket_thread.join();
    }
    
    void begin_dictation() {
        if (state_ == DaemonState::Listening) {
            return;
        }
        
        state_ = DaemonState::Listening;
        dictation_thread_ = std::thread(&DictationDaemon::dictation_loop, this);
    }
    
    void end_dictation() {
        state_ = DaemonState::Idle;
        if (dictation_thread_.joinable()) {
            dictation_thread_.join();
        }
        backend_->finalize();
    }
};

} // namespace nerd_dictation
```

---

### 6. Конфигурация (TOML парсинг)

```cpp
// src/config/config.cpp
#include "nerd_dictation/config.hpp"
#include <toml11/toml.hpp>
#include <filesystem>
#include <fstream>

namespace nerd_dictation {

Config load_config(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) {
        // Возвращаем дефолтную конфигурацию
        return Config{};
    }
    
    auto data = toml::parse(path);
    
    Config config;
    
    // [general]
    if (data.contains("general")) {
        const auto& general = toml::find(data, "general");
        config.backend = toml::find_or<std::string>(general, "backend", "vosk");
        config.input_method = toml::find_or<std::string>(general, "input_method", "ydotool");
    }
    
    // [audio]
    if (data.contains("audio")) {
        const auto& audio = toml::find(data, "audio");
        config.audio.source = toml::find_or<std::string>(audio, "source", "pw-cat");
        config.audio.sample_rate = toml::find_or<int>(audio, "sample_rate", 16000);
        config.audio.chunk_size = toml::find_or<size_t>(audio, "chunk_size", 8000);
    }
    
    // [vosk]
    if (data.contains("vosk")) {
        const auto& vosk = toml::find(data, "vosk");
        config.vosk.model_path = toml::find_or<std::string>(vosk, "model_path", "");
    }
    
    return config;
}

} // namespace nerd_dictation
```

---

### 7. CLI (main.cpp)

```cpp
// src/main.cpp
#include <CLI11/CLI.hpp>
#include "nerd_dictation/config.hpp"
#include "nerd_dictation/daemon.hpp"
#include <spdlog/spdlog.h>

int main(int argc, char* argv[]) {
    CLI::App app{"nerd-dictation-ex — диктовка с подключаемыми бэкендами ASR"};
    
    std::string config_path = "~/.config/nerd-dictation-ex/config.toml";
    
    // Подкоманды
    auto daemon_cmd = app.add_subcommand("daemon", "Запустить daemon");
    
    auto begin_cmd = app.add_subcommand("begin", "Начать диктовку");
    begin_cmd->add_option("-b,--backend", "Backend name");
    
    auto end_cmd = app.add_subcommand("end", "Завершить диктовку");
    auto cancel_cmd = app.add_subcommand("cancel", "Отменить диктовку");
    auto toggle_cmd = app.add_subcommand("toggle", "Переключить диктовку");
    
    CLI11_PARSE(app, argc, argv);
    
    // Загрузка конфигурации
    Config config = load_config(config_path);
    
    if (*daemon_cmd) {
        DictationDaemon daemon(config);
        daemon.run();
    } else if (*begin_cmd) {
        // Подключение к daemon через socket или прямой запуск
        // ...
    }
    
    return 0;
}
```

---

## Резюме

Эти примеры показывают основные паттерны реализации:

1. **Интерфейсы** — абстрактные базовые классы
2. **Lock-free структуры** — для производительности
3. **Многопоточность** — std::thread + атомики
4. **Callbacks** — для асинхронной обработки
5. **RAII** — умные указатели для управления ресурсами

Дополнительные детали см. в `CPP_MIGRATION_PLAN.md`.
