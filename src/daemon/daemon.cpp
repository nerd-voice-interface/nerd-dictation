#include "nerd_dictation/daemon/daemon.hpp"
#include "nerd_dictation/backend_wrapper.hpp"
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

/* Vosk C API for wake word detection (conditionally compiled) */
#if HAVE_VOSK_WAKE_WORD
extern "C" {
    VoskModel* vosk_model_new(const char* model_path);
    void vosk_model_free(VoskModel* model);
    VoskRecognizer* vosk_recognizer_new(VoskModel* model, float sample_rate);
    void vosk_recognizer_free(VoskRecognizer* recognizer);
    int vosk_recognizer_accept_waveform(VoskRecognizer* recognizer, const char* data, int length);
    const char* vosk_recognizer_result(VoskRecognizer* recognizer);
    void vosk_recognizer_reset(VoskRecognizer* recognizer);
}
#endif

/* Simple JSON helpers */
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

std::string expand_path(const std::string& path) {
    if (!path.empty() && path.front() == '~') {
        const char* home = std::getenv("HOME");
        if (home) {
            return std::string(home) + path.substr(1);
        }
    }
    return path;
}
} /* anonymous namespace */

namespace nerd_dictation::daemon {

DictationDaemon::DictationDaemon(const Config& config) : config_(config) {
    /* Socket path */
    const char* runtime_dir = std::getenv("XDG_RUNTIME_DIR");
    if (runtime_dir) {
        socket_path_ = std::string(runtime_dir) + "/nerd-dictation.sock";
    } else {
        socket_path_ = "/tmp/nerd-dictation.sock";
    }

    /* Wake word settings */
    wake_word_enabled_ = config_.wake_word.enabled;
    wake_word_ = config_.wake_word.word;

    /* Audio buffer size */
    size_t bytes_per_second = config_.audio.sample_rate * 2; /* 16-bit = 2 bytes */
    size_t buffer_bytes = bytes_per_second * AUDIO_BUFFER_SECONDS;
    audio_buffer_max_chunks_ = std::max<size_t>(1, buffer_bytes / config_.audio.chunk_size);

    /* Load backend from .so */
    std::string backend_name = config_.backend;
    std::string backend_path = config_.backend_library;

    if (backend_path.empty()) {
        /* Default paths based on backend name */
        if (backend_name == "t-one" || backend_name == "tone") {
            backend_path = "libnerd-dictation-t-one.so";
        } else if (backend_name == "vosk") {
            backend_path = "libnerd-dictation-vosk.so";
        } else {
            backend_path = "libnerd-dictation-" + backend_name + ".so";
        }
    }

    backend_path = expand_path(backend_path);

    try {
        spdlog::info("Загрузка бекенда: {}", backend_path);
        backend_ = std::make_unique<Backend>(backend_path);
        spdlog::info("Бекенд загружен: {} (sample rate: {} Hz)",
                     backend_->name(), backend_->sample_rate());
    } catch (const std::exception& e) {
        spdlog::error("Не удалось загрузить бекенд {}: {}", backend_path, e.what());
        throw;
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

    if (wake_word_thread_.joinable()) {
        wake_word_running_ = false;
        wake_word_thread_.join();
    }

    if (audio_listener_thread_.joinable()) {
        audio_listener_thread_.join();
    }

    if (server_fd_ >= 0) {
        close(server_fd_);
        unlink(socket_path_.c_str());
    }

#if HAVE_VOSK_WAKE_WORD
    if (wake_word_recognizer_) {
        vosk_recognizer_free(wake_word_recognizer_);
    }
    if (wake_word_model_) {
        vosk_model_free(wake_word_model_);
    }
#endif
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

    while (running_ && state_ == DaemonState::Listening) {
        /* Streaming recognition using C API wrapper */
        backend_->transcribe_stream(
            [this](std::vector<int16_t>& chunk) -> bool {
                /* Check if we should continue */
                if (!running_ || state_ != DaemonState::Listening) {
                    return false;
                }

                /* Get audio from buffer */
                {
                    std::lock_guard<std::mutex> audio_lock(audio_buffer_mutex_);
                    if (!audio_buffer_.empty()) {
                        chunk = audio_buffer_.front();
                        audio_buffer_.pop_front();
                        return true;
                    }
                }

                /* No audio available, wait a bit */
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                chunk.clear();
                return running_ && state_ == DaemonState::Listening;
            },
            [this](const ASRResult& result) {
                if (result.is_final && !result.text.empty()) {
                    spdlog::info("Распознано: {}", result.text);
                    process_and_type(result.text);
                }
            }
        );

        /* Wait for stop signal or state change */
        dictation_cv_.wait(lock, [this] {
            return !running_ || state_ != DaemonState::Listening;
        });
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

    /* Simple JSON parsing */
    std::string cmd_str(buffer);
    std::string action;

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
    /* Start audio listener */
    start_audio_listener();

    /* Start wake word detection if enabled */
    if (wake_word_enabled_) {
        start_wake_word_detection();
    }

    /* Start socket server */
    socket_thread_ = std::thread(&DictationDaemon::socket_server_loop, this);
    socket_thread_.join();
}

std::string DictationDaemon::get_backend_name() const {
    return backend_ ? backend_->name() : "none";
}

bool DictationDaemon::load_wake_word_models() {
#if HAVE_VOSK_WAKE_WORD
    if (!wake_word_enabled_) {
        return false;
    }

    std::string model_path = expand_path(config_.wake_word.model_path);

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
#else
    spdlog::warn("Wake word detection disabled (Vosk not available at compile time)");
    return false;
#endif
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
                    break; /* EOF */
                }

                if (bytes_read < chunk_size) {
                    chunk.resize(bytes_read / sizeof(int16_t));
                }

                /* Add to ring buffer for dictation */
                {
                    std::lock_guard<std::mutex> lock(audio_buffer_mutex_);
                    audio_buffer_.push_back(chunk);
                    if (audio_buffer_.size() > audio_buffer_max_chunks_) {
                        audio_buffer_.pop_front();
                    }
                }

                /* Copy to wake word queue (if not listening) */
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
#if HAVE_VOSK_WAKE_WORD
    if (!wake_word_recognizer_) {
        return;
    }

    std::vector<int16_t> vad_buffer;
    std::vector<int16_t> speech_buffer;
    bool is_speech_active = false;

    constexpr size_t vad_chunk_samples = 512;

    while (wake_word_running_ && running_) {
        /* Skip if dictation is active */
        if (state_ == DaemonState::Listening) {
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

        /* Get audio from queue */
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

        vad_buffer.insert(vad_buffer.end(), chunk.begin(), chunk.end());

        /* Process VAD chunks */
        while (vad_buffer.size() >= vad_chunk_samples) {
            std::vector<int16_t> vad_chunk(vad_buffer.begin(), vad_buffer.begin() + vad_chunk_samples);
            vad_buffer.erase(vad_buffer.begin(), vad_buffer.begin() + vad_chunk_samples);

            if (!vad_chunk.empty()) {
                /* Simple VAD: check signal level */
                int32_t sum = 0;
                for (int16_t sample : vad_chunk) {
                    sum += std::abs(sample);
                }
                int32_t avg = sum / vad_chunk.size();

                if (avg > 1000) {
                    if (!is_speech_active) {
                        is_speech_active = true;
                        speech_buffer = vad_chunk;
                    } else {
                        speech_buffer.insert(speech_buffer.end(), vad_chunk.begin(), vad_chunk.end());
                    }
                } else {
                    if (is_speech_active && !speech_buffer.empty()) {
                        /* End of speech - recognize */
                        std::vector<char> audio_bytes(speech_buffer.size() * sizeof(int16_t));
                        std::memcpy(audio_bytes.data(), speech_buffer.data(), audio_bytes.size());

                        if (vosk_recognizer_accept_waveform(wake_word_recognizer_,
                                                             audio_bytes.data(),
                                                             audio_bytes.size())) {
                            const char* result_str = vosk_recognizer_result(wake_word_recognizer_);
                            if (result_str) {
                                std::string result(result_str);

                                /* Parse JSON for text */
                                size_t text_pos = result.find("\"text\"");
                                if (text_pos != std::string::npos) {
                                    size_t start = result.find('"', text_pos + 7);
                                    if (start != std::string::npos) {
                                        size_t end = result.find('"', start + 1);
                                        if (end != std::string::npos) {
                                            std::string text = result.substr(start + 1, end - start - 1);

                                            std::transform(text.begin(), text.end(), text.begin(), ::tolower);

                                            spdlog::info("[wake word] распознано: '{}'", text);

                                            std::string wake_lower = wake_word_;
                                            std::transform(wake_lower.begin(), wake_lower.end(), wake_lower.begin(), ::tolower);

                                            if (text.find(wake_lower) != std::string::npos ||
                                                text.find("записи") != std::string::npos) {
                                                spdlog::info("[wake word] активация по слову '{}'!", wake_word_);

                                                vosk_recognizer_reset(wake_word_recognizer_);

                                                {
                                                    std::lock_guard<std::mutex> lock(audio_buffer_mutex_);
                                                    audio_buffer_.clear();
                                                }

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
#else
    /* Vosk not available - wake word detection disabled */
    (void)wake_word_running_;
#endif
}

} /* namespace nerd_dictation::daemon */
