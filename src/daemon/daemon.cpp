#include "nerd_dictation/daemon/daemon.hpp"
#include "nerd_dictation/backends/dummy_backend.hpp"
#include "nerd_dictation/input/input_simulator.hpp"
#include "nerd_dictation/input/postprocess.hpp"
#include "nerd_dictation/sounds/sounds.hpp"
#include <spdlog/spdlog.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <cstring>
#include <sstream>
#include <fstream>
#include <iostream>
#include <cstdlib>

// Простой JSON парсер (заглушка, можно заменить на nlohmann/json)
namespace {
std::string json_escape(const std::string& s) {
    std::string result;
    for (char c : s) {
        if (c == '"' || c == '\\') result += '\\';
        result += c;
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
        socket_path_ = std::string(runtime_dir) + "/nerd-dictation-ex.sock";
    } else {
        socket_path_ = "/tmp/nerd-dictation-ex.sock";
    }
    
    // Создаём dummy бэкенд
    backend_ = backends::create_dummy_backend();
    backend_->load_model(config_.model_path.empty() ? "dummy" : config_.model_path);
    
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
    
    while (running_ && state_ == DaemonState::Listening) {
        // Симулируем получение аудио и распознавание
        backend_->transcribe_stream(
            [](const int16_t*, size_t) {
                // Dummy бэкенд не требует реального аудио
            },
            [this](const backends::ASRResult& result) {
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
    socket_thread_ = std::thread(&DictationDaemon::socket_server_loop, this);
    socket_thread_.join();
}

std::string DictationDaemon::get_backend_name() const {
    return backend_ ? backend_->name() : "none";
}

} // namespace nerd_dictation::daemon
