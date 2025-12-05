#include "nerd_dictation/config.hpp"
#include "nerd_dictation/daemon/daemon.hpp"
#include "nerd_dictation/backends/dummy_backend.hpp"
#include "nerd_dictation/input/input_simulator.hpp"
#include "nerd_dictation/input/postprocess.hpp"
#include "nerd_dictation/sounds/sounds.hpp"
#include <CLI/CLI.hpp>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <cstring>
#include <fstream>
#include <iostream>

namespace {
// Простой JSON клиент для daemon
std::string send_command_to_daemon(const std::string& action) {
    const char* runtime_dir = std::getenv("XDG_RUNTIME_DIR");
    std::string socket_path = runtime_dir ? 
        std::string(runtime_dir) + "/nerd-dictation.sock" :
        "/tmp/nerd-dictation.sock";
    
    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) {
        return R"({"status":"error","message":"Не удалось создать socket"})";
    }
    
    struct sockaddr_un addr;
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, socket_path.c_str(), sizeof(addr.sun_path) - 1);
    
    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(sock);
        return R"({"status":"error","message":"Daemon не запущен"})";
    }
    
    std::string cmd = R"({"action":")" + action + R"("})";
    write(sock, cmd.c_str(), cmd.length());
    
    char buffer[4096];
    ssize_t n = read(sock, buffer, sizeof(buffer) - 1);
    close(sock);
    
    if (n > 0) {
        buffer[n] = '\0';
        return std::string(buffer);
    }
    
    return R"({"status":"error","message":"Нет ответа от daemon"})";
}

bool is_daemon_running() {
    const char* runtime_dir = std::getenv("XDG_RUNTIME_DIR");
    std::string socket_path = runtime_dir ? 
        std::string(runtime_dir) + "/nerd-dictation.sock" :
        "/tmp/nerd-dictation.sock";
    
    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) return false;
    
    struct sockaddr_un addr;
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, socket_path.c_str(), sizeof(addr.sun_path) - 1);
    
    bool connected = (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) == 0);
    close(sock);
    
    return connected;
}
}

int main(int argc, char* argv[]) {
    // Настройка логирования
    auto logger = spdlog::stderr_color_mt("nerd-dictation");
    spdlog::set_default_logger(logger);
    spdlog::set_level(spdlog::level::info);
    
    CLI::App app{"nerd-dictation — диктовка с подключаемыми бэкендами ASR"};
    
    // Подкоманды
    auto daemon_cmd = app.add_subcommand("daemon", "Запустить daemon");
    
    auto begin_cmd = app.add_subcommand("begin", "Начать диктовку");
    
    auto end_cmd = app.add_subcommand("end", "Завершить диктовку");
    
    auto cancel_cmd = app.add_subcommand("cancel", "Отменить диктовку");
    
    auto toggle_cmd = app.add_subcommand("toggle", "Переключить диктовку (begin/end)");
    
    CLI11_PARSE(app, argc, argv);
    
    // Загрузка конфигурации
    auto config_path_opt = nerd_dictation::find_config_file();
    nerd_dictation::Config config;
    if (config_path_opt) {
        config = nerd_dictation::load_config(*config_path_opt);
    }
    
    // Обработка команд
    if (*daemon_cmd) {
        spdlog::info("Запуск daemon...");
        nerd_dictation::daemon::DictationDaemon daemon(config);
        daemon.run();
        return 0;
    }
    
    if (*begin_cmd) {
        if (is_daemon_running()) {
            std::string response = send_command_to_daemon("begin");
            std::cout << response << std::endl;
        } else {
            spdlog::error("Daemon не запущен. Запустите: nerd-dictation daemon");
            return 1;
        }
        return 0;
    }
    
    if (*end_cmd) {
        if (is_daemon_running()) {
            std::string response = send_command_to_daemon("end");
            std::cout << response << std::endl;
        } else {
            spdlog::error("Daemon не запущен");
            return 1;
        }
        return 0;
    }
    
    if (*cancel_cmd) {
        if (is_daemon_running()) {
            std::string response = send_command_to_daemon("cancel");
            std::cout << response << std::endl;
        } else {
            spdlog::error("Daemon не запущен");
            return 1;
        }
        return 0;
    }
    
    if (*toggle_cmd) {
        if (is_daemon_running()) {
            std::string status_response = send_command_to_daemon("status");
            // Простая проверка статуса
            std::string action = (status_response.find("listening") != std::string::npos) ? "end" : "begin";
            std::string response = send_command_to_daemon(action);
            std::cout << response << std::endl;
        } else {
            spdlog::error("Daemon не запущен. Запустите: nerd-dictation daemon");
            return 1;
        }
        return 0;
    }
    
    // Если команда не указана, показываем помощь
    std::cout << app.help() << std::endl;
    return 0;
}
