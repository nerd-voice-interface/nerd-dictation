#include "nerd_dictation/daemon/daemon.hpp"
#include "nerd_dictation/types.hpp"
#include "nerd_dictation/backends/dummy_backend.hpp"
#include "nerd_dictation_vosk/backend.hpp"
#include "nerd_dictation/input/input_simulator.hpp"
#include "nerd_dictation/input/postprocess.hpp"
#include "nerd_dictation/sounds/sounds.hpp"
#include <spdlog/spdlog.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <cstring>
#include <sstream>
#include <fstream>
#include <iostream>
#include <cstdlib>
#include <thread>
#include <chrono>
#include <vector>
#include <algorithm>
#include <cctype>

// Простой JSON парсер (заглушка)
namespace {
std::string json_escape(const std::string& s) {
    std::string result;
    for (char c : s) {
        if (c == '"') result += "\\\"";
        else if (c == '\\') result += "\\\\";
        else if (c == '\n') result += "\\n";
        else if (c == '\r') result += "\\r";
        else if (c == '\t') result += "\\t";
        else result += c;
    }
    return result;
}

std::string create_json_response(const std::string& status, const std::string& message = "") {
    std::ostringstream oss;
    oss << "{\"status\":\"" << status << "\"";
    if (!message.empty()) {
        oss << ",\"message\":\"" << json_escape(message) << "\"";
    }
    oss << "}";
    return oss.str();
}
}

namespace nerd_dictation::daemon {

DictationDaemon::DictationDaemon(const Config& config) : config_(config) {
    // Определяем путь к socket
    const char* runtime_dir = std::getenv("XDG_RUNTIME_DIR");
    if (runtime_dir) {
        socket_path_ = std::string(runtime_dir) + "/nerd-dictation.sock";
    } else {
        socket_path_ = "/tmp/nerd-dictation.sock";
    }
    
    // Инициализируем wake word настройки
    wake_word_enabled_ = config_.wake_word.enabled;
    wake_word_ = config_.wake_word.word;
    
    // Вычисляем размер аудио буфера
    size_t bytes_per_second = config_.audio.sample_rate * 2; // 16-bit = 2 bytes
    size_t buffer_bytes = bytes_per_second * AUDIO_BUFFER_SECONDS;
    audio_buffer_max_chunks_ = std::max<size_t>(1, buffer_bytes / config_.audio.chunk_size);
    
    // Создаём бэкенд в зависимости от конфигурации
    std::string backend_name = config_.backend;
    
    if (backend_name == "vosk") {
        backend_ = backends::create_vosk_backend();
        std::string model_path = config_.model_path.empty() 
            ? "~/.local/share/vosk/vosk-model-ru" 
            : config_.model_path;
        // Разворачиваем ~
        if (model_path.front() == '~') {
            const char* home = std::getenv("HOME");
            if (home) {
                model_path = std::string(home) + model_path.substr(1);
            }
        }
        backend_->load_model(model_path);
    } else {
        // Dummy бэкенд по умолчанию
        backend_ = backends::create_dummy_backend();
        backend_->load_model(config_.model_path.empty() ? "dummy" : config_.model_path);
    }
    
    spdlog::info("Daemon инициализирован, socket: {}", socket_path_);
}

DictationDaemon::~DictationDaemon() {
    running_ = false;
    
    if (dictation_thread_.joinable()) {
        dictation_cv_.notify_all();
        dictation_thread_.join();
    }
    
    if (socket_thread_.joinable()) {
        socket_thread_.join();
    }
    
    if (server_fd_ >= 0) {
        close(server_fd_);
        unlink(socket_path_.c_str());
    }
}

void DictationDaemon::process_and_type(const std::string& text) {
    input::TextPostProcessor processor;
    auto actions = processor.process(text);
    
    input::InputSimulator input_sim(config_.input_method);
    
    for (const auto& action : actions) {
        std::visit([&input_sim](auto&& arg) {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, input::TextAction>) {
                input_sim.type_text(arg.text);
            } else if constexpr (std::is_same_v<T, input::KeyAction>) {
                sounds::play_execute();
                input_sim.press_keys(arg.keys);
            }
        }, action);
    }
}

void DictationDaemon::dictation_loop() {
    spdlog::info("Начало цикла диктовки");
    
    std::unique_lock<std::mutex> lock(dictation_mutex_);
    
    // TODO: Реализовать реальный аудио поток из audio_buffer
    // Пока используем заглушку для тестирования
    std::vector<int16_t> empty_chunk;
    bool audio_available = false;
    
    while (running_ && state_ == DaemonState::Listening) {
        // Провайдер аудио: возвращает true если есть данные, false если поток закончился
        backend_->transcribe_stream(
            [&](std::vector<int16_t>& chunk) -> bool {
                // Проверяем, нужно ли продолжать
                if (!running_ || state_ != DaemonState::Listening) {
                    return false; // Поток закончился
                }
                
                // TODO: Читать реальное аудио из audio_buffer или audio_capture
                // Пока возвращаем пустой чанк для dummy бэкенда
                // Для Vosk бэкенда здесь должен быть реальный аудио поток
                chunk = empty_chunk;
                
                // Небольшая задержка чтобы не нагружать CPU
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                
                // Возвращаем true чтобы продолжить, false чтобы остановиться
                return running_ && state_ == DaemonState::Listening;
            },
            [this](const ASRResult& result) {
                if (result.is_final && !result.text.empty()) {
                    spdlog::info("Распознано: {}", result.text);
                    process_and_type(result.text);
                }
            }
        );
        
        // Ждём завершения или команды остановки
        dictation_cv_.wait(lock, [this] {
            return !running_ || state_ != DaemonState::Listening;
        });
    }
    
    // Финальный результат
    auto final = backend_->finalize();
    if (!final.text.empty()) {
        process_and_type(final.text);
    }
    
    backend_->reset();
    state_ = DaemonState::Idle;
    spdlog::info("Цикл диктовки завершён");
}

void DictationDaemon::begin_dictation() {
    if (state_ == DaemonState::Listening) {
        spdlog::warn("Диктовка уже запущена");
        return;
    }
    
    state_ = DaemonState::Listening;
    sounds::play_begin();
    
    if (!dictation_thread_.joinable()) {
        dictation_thread_ = std::thread(&DictationDaemon::dictation_loop, this);
    } else {
        dictation_cv_.notify_all();
    }
    
    spdlog::info("Диктовка запущена");
}

void DictationDaemon::end_dictation() {
    if (state_ != DaemonState::Listening) {
        spdlog::warn("Диктовка не запущена");
        return;
    }
    
    sounds::play_end();
    state_ = DaemonState::Idle;
    dictation_cv_.notify_all();
    
    if (dictation_thread_.joinable()) {
        dictation_thread_.join();
    }
    
    spdlog::info("Диктовка завершена");
}

void DictationDaemon::cancel_dictation() {
    if (state_ != DaemonState::Listening) {
        return;
    }
    
    state_ = DaemonState::Idle;
    backend_->reset();
    dictation_cv_.notify_all();
    
    if (dictation_thread_.joinable()) {
        dictation_thread_.join();
    }
    
    spdlog::info("Диктовка отменена");
}

void DictationDaemon::handle_client(int client_fd) {
    char buffer[4096];
    ssize_t n = read(client_fd, buffer, sizeof(buffer) - 1);
    
    if (n <= 0) {
        close(client_fd);
        return;
    }
    
    buffer[n] = '\0';
    
    // Простой парсинг JSON команды
    std::string cmd_str(buffer);
    std::string action;
    
    // Ищем "action":"...
    size_t pos = cmd_str.find("\"action\"");
    if (pos != std::string::npos) {
        pos = cmd_str.find('"', pos + 8);
        if (pos != std::string::npos) {
            size_t end = cmd_str.find('"', pos + 1);
            if (end != std::string::npos) {
                action = cmd_str.substr(pos + 1, end - pos - 1);
            }
        }
    }
    
    std::string response;
    
    if (action == "begin") {
        begin_dictation();
        response = create_json_response("ok", "Диктовка запущена");
    } else if (action == "end") {
        end_dictation();
        response = create_json_response("ok", "Диктовка завершена");
    } else if (action == "cancel") {
        cancel_dictation();
        response = create_json_response("ok", "Диктовка отменена");
    } else if (action == "status") {
        std::string state_str = state_ == DaemonState::Listening ? "listening" : "idle";
        std::ostringstream oss;
        oss << "{\"status\":\"ok\",\"state\":\"" << state_str 
            << "\",\"backend\":\"" << json_escape(get_backend_name()) << "\"}";
        response = oss.str();
    } else {
        response = create_json_response("error", "Неизвестная команда: " + action);
    }
    
    write(client_fd, response.c_str(), response.length());
    
    close(client_fd);
}

void DictationDaemon::socket_server_loop() {
    struct sockaddr_un addr;
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, socket_path_.c_str(), sizeof(addr.sun_path) - 1);
    
    server_fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd_ < 0) {
        spdlog::error("Не удалось создать socket");
        return;
    }
    
    unlink(socket_path_.c_str());
    
    if (bind(server_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        spdlog::error("Не удалось привязать socket: {}", socket_path_);
        close(server_fd_);
        return;
    }
    
    if (listen(server_fd_, 5) < 0) {
        spdlog::error("Не удалось начать прослушивание socket");
        close(server_fd_);
        return;
    }
    
    spdlog::info("Socket server запущен: {}", socket_path_);
    
    while (running_) {
        int client_fd = accept(server_fd_, nullptr, nullptr);
        if (client_fd >= 0) {
            handle_client(client_fd);
        }
    }
}

void DictationDaemon::run() {
    // Запускаем постоянный аудио-слушатель
    start_audio_listener();
    
    // Запускаем wake word detection если включено
    if (wake_word_enabled_) {
        start_wake_word_detection();
    }
    
    // Запускаем socket server
    socket_thread_ = std::thread(&DictationDaemon::socket_server_loop, this);
    socket_thread_.join();
}

std::string DictationDaemon::get_backend_name() const {
    return backend_ ? backend_->name() : "none";
}

bool DictationDaemon::load_wake_word_models() {
    if (!wake_word_enabled_) {
        return false;
    }
    
    std::string model_path = config_.wake_word.model_path;
    
    // Разворачиваем ~
    if (!model_path.empty() && model_path.front() == '~') {
        const char* home = std::getenv("HOME");
        if (home) {
            model_path = std::string(home) + model_path.substr(1);
        }
    }
    
    spdlog::info("Загрузка Vosk модели для wake word: {}", model_path);
    
    wake_word_model_ = vosk_model_new(model_path.c_str());
    if (!wake_word_model_) {
        spdlog::error("Не удалось загрузить Vosk модель для wake word: {}", model_path);
        return false;
    }
    
    wake_word_recognizer_ = vosk_recognizer_new(wake_word_model_, config_.audio.sample_rate);
    if (!wake_word_recognizer_) {
        vosk_model_free(wake_word_model_);
        wake_word_model_ = nullptr;
        spdlog::error("Не удалось создать Vosk recognizer для wake word");
        return false;
    }
    
    spdlog::info("Vosk модель для wake word загружена");
    return true;
}

void DictationDaemon::start_audio_listener() {
    if (audio_listener_thread_.joinable()) {
        return;
    }
    
    audio_listener_thread_ = std::thread(&DictationDaemon::audio_listener_loop, this);
    spdlog::info("Аудио-слушатель запущен");
}

void DictationDaemon::start_wake_word_detection() {
    if (!wake_word_enabled_) {
        return;
    }
    
    if (wake_word_thread_.joinable()) {
        return;
    }
    
    if (!load_wake_word_models()) {
        spdlog::warn("Wake word detection отключён из-за ошибок загрузки");
        return;
    }
    
    wake_word_running_ = true;
    wake_word_thread_ = std::thread(&DictationDaemon::wake_word_loop, this);
    spdlog::info("Wake word detection запущен (слово: '{}')", wake_word_);
}

std::string DictationDaemon::get_audio_command() const {
    std::string source = config_.audio.source;
    int sample_rate = config_.audio.sample_rate;
    
    if (source == "pw-cat") {
        return "pw-cat --record --format s16 --rate " + std::to_string(sample_rate) + " --channels 1 -";
    } else if (source == "parec") {
        return "parec --format=s16ne --rate=" + std::to_string(sample_rate) + " --channels=1 --raw";
    } else if (source == "sox") {
        return "sox -q -d -t raw -b 16 -e signed-integer -r " + std::to_string(sample_rate) + " -c 1 -";
    }
    
    return "pw-cat --record --format s16 --rate " + std::to_string(sample_rate) + " --channels 1 -";
}

void DictationDaemon::audio_listener_loop() {
    size_t chunk_size = config_.audio.chunk_size;
    
    while (running_) {
        try {
            std::string cmd = get_audio_command();
            audio_proc_pipe_ = popen(cmd.c_str(), "r");
            
            if (!audio_proc_pipe_) {
                spdlog::error("Не удалось запустить аудио процесс: {}", cmd);
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                continue;
            }
            
            std::vector<int16_t> chunk(chunk_size / sizeof(int16_t));
            
            while (running_) {
                size_t bytes_read = fread(chunk.data(), 1, chunk_size, audio_proc_pipe_);
                
                if (bytes_read == 0) {
                    break; // EOF или процесс завершился
                }
                
                if (bytes_read < chunk_size) {
                    chunk.resize(bytes_read / sizeof(int16_t));
                }
                
                // Добавляем в кольцевой буфер для диктовки (с ограничением размера)
                {
                    std::lock_guard<std::mutex> lock(audio_buffer_mutex_);
                    audio_buffer_.push_back(chunk);
                    if (audio_buffer_.size() > audio_buffer_max_chunks_) {
                        audio_buffer_.pop_front();
                    }
                }
                
                // Дублируем в очередь для wake word (если не активна диктовка)
                if (wake_word_running_ && state_ != DaemonState::Listening) {
                    std::lock_guard<std::mutex> lock(wake_word_queue_mutex_);
                    wake_word_queue_.push(chunk);
                }
            }
            
            pclose(audio_proc_pipe_);
            audio_proc_pipe_ = nullptr;
            
        } catch (const std::exception& e) {
            spdlog::error("Ошибка аудио-слушателя: {}", e.what());
        }
        
        if (running_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    }
}

void DictationDaemon::wake_word_loop() {
    if (!wake_word_recognizer_) {
        return;
    }
    
    std::vector<int16_t> vad_buffer;
    std::vector<int16_t> speech_buffer;
    bool is_speech_active = false;
    
    // VAD требует чанки по 512 сэмплов при 16kHz
    constexpr size_t vad_chunk_samples = 512;
    
    while (wake_word_running_ && running_) {
        // Пропускаем, если диктовка уже активна
        if (state_ == DaemonState::Listening) {
            // Сбросить состояние и очистить очередь
            vad_buffer.clear();
            speech_buffer.clear();
            is_speech_active = false;
            
            {
                std::lock_guard<std::mutex> lock(wake_word_queue_mutex_);
                while (!wake_word_queue_.empty()) {
                    wake_word_queue_.pop();
                }
            }
            
            vosk_recognizer_reset(wake_word_recognizer_);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
        
        // Получить аудио из очереди
        std::vector<int16_t> chunk;
        {
            std::lock_guard<std::mutex> lock(wake_word_queue_mutex_);
            if (wake_word_queue_.empty()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            chunk = wake_word_queue_.front();
            wake_word_queue_.pop();
        }
        
        // Добавляем в VAD буфер
        vad_buffer.insert(vad_buffer.end(), chunk.begin(), chunk.end());
        
        // Обрабатываем VAD чанками по 512 сэмплов
        while (vad_buffer.size() >= vad_chunk_samples) {
            std::vector<int16_t> vad_chunk(vad_buffer.begin(), vad_buffer.begin() + vad_chunk_samples);
            vad_buffer.erase(vad_buffer.begin(), vad_buffer.begin() + vad_chunk_samples);
            
            // Упрощённая VAD: считаем что речь активна если есть данные
            // В оригинале используется Silero VAD, но для простоты используем упрощённую логику
            if (!vad_chunk.empty()) {
                // Проверяем уровень сигнала (простая эвристика)
                int32_t sum = 0;
                for (int16_t sample : vad_chunk) {
                    sum += std::abs(sample);
                }
                int32_t avg = sum / vad_chunk.size();
                
                if (avg > 1000) { // Порог для детекции речи
                    if (!is_speech_active) {
                        is_speech_active = true;
                        speech_buffer = vad_chunk;
                    } else {
                        speech_buffer.insert(speech_buffer.end(), vad_chunk.begin(), vad_chunk.end());
                    }
                } else {
                    if (is_speech_active && !speech_buffer.empty()) {
                        // Конец речи - распознаём накопленный буфер
                        std::vector<char> audio_bytes(speech_buffer.size() * sizeof(int16_t));
                        std::memcpy(audio_bytes.data(), speech_buffer.data(), audio_bytes.size());
                        
                        // Отправляем аудио в Vosk recognizer
                        if (vosk_recognizer_accept_waveform(wake_word_recognizer_, 
                                                             audio_bytes.data(), 
                                                             audio_bytes.size())) {
                            // Финальный результат (фраза завершена)
                            const char* result_str = vosk_recognizer_result(wake_word_recognizer_);
                            if (result_str) {
                                std::string result(result_str);
                                
                                // Простой парсинг JSON для извлечения текста
                                size_t text_pos = result.find("\"text\"");
                                if (text_pos != std::string::npos) {
                                    size_t start = result.find('"', text_pos + 7);
                                    if (start != std::string::npos) {
                                        size_t end = result.find('"', start + 1);
                                        if (end != std::string::npos) {
                                            std::string text = result.substr(start + 1, end - start - 1);
                                            
                                            // Приводим к нижнему регистру
                                            std::transform(text.begin(), text.end(), text.begin(), ::tolower);
                                            
                                            spdlog::info("[wake word] распознано: '{}'", text);
                                            
                                            // Проверяем на wake word (включая падежные формы)
                                            std::string wake_lower = wake_word_;
                                            std::transform(wake_lower.begin(), wake_lower.end(), wake_lower.begin(), ::tolower);
                                            
                                            if (text.find(wake_lower) != std::string::npos || 
                                                text.find("записи") != std::string::npos) {
                                                spdlog::info("[wake word] активация по слову '{}'!", wake_word_);
                                                
                                                vosk_recognizer_reset(wake_word_recognizer_);
                                                
                                                // Очистить аудио-буфер чтобы wake word не попал в диктовку
                                                {
                                                    std::lock_guard<std::mutex> lock(audio_buffer_mutex_);
                                                    audio_buffer_.clear();
                                                }
                                                
                                                // Автоматически запустить диктовку
                                                begin_dictation();
                                            }
                                        }
                                    }
                                }
                            }
                        }
                        
                        speech_buffer.clear();
                        is_speech_active = false;
                        vosk_recognizer_reset(wake_word_recognizer_);
                    }
                }
            }
        }
    }
}

} // namespace nerd_dictation::daemon
