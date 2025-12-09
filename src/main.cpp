#include "nerd_dictation/config.hpp"
#include "nerd_dictation/daemon/daemon.hpp"
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
#include <csignal>
#include <execinfo.h>
#include <cstdlib>

/* ============================================================================
 * Crash handler with backtrace
 * ============================================================================ */

static void crash_handler(int sig) {
    const char* sig_name = (sig == SIGSEGV) ? "SIGSEGV" :
                           (sig == SIGABRT) ? "SIGABRT" :
                           (sig == SIGFPE)  ? "SIGFPE" : "UNKNOWN";

    // Print header
    const char header[] = "\n[nerd-dictation] FATAL: Caught signal ";
    write(STDERR_FILENO, header, sizeof(header) - 1);
    write(STDERR_FILENO, sig_name, strlen(sig_name));
    write(STDERR_FILENO, "\n", 1);

    // Get backtrace
    void* bt_buffer[64];
    int bt_size = backtrace(bt_buffer, 64);

    const char bt_header[] = "[nerd-dictation] Backtrace:\n";
    write(STDERR_FILENO, bt_header, sizeof(bt_header) - 1);

    // Print backtrace to stderr
    backtrace_symbols_fd(bt_buffer, bt_size, STDERR_FILENO);

    const char footer[] = "[nerd-dictation] End of backtrace\n";
    write(STDERR_FILENO, footer, sizeof(footer) - 1);

    // Re-raise the signal to get core dump
    signal(sig, SIG_DFL);
    raise(sig);
}

static void install_crash_handler() {
    struct sigaction sa;
    sa.sa_handler = crash_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESETHAND;  // Only handle once, then use default

    sigaction(SIGSEGV, &sa, nullptr);
    sigaction(SIGABRT, &sa, nullptr);
    sigaction(SIGFPE, &sa, nullptr);
}

namespace {
// Simple JSON client for daemon
std::string send_command_to_daemon(const std::string& action) {
    const char* runtime_dir = std::getenv("XDG_RUNTIME_DIR");
    std::string socket_path = runtime_dir ? 
        std::string(runtime_dir) + "/nerd-dictation.sock" :
        "/tmp/nerd-dictation.sock";
    
    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) {
        return R"({"status":"error","message":"Failed to create socket"})";
    }
    
    struct sockaddr_un addr;
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, socket_path.c_str(), sizeof(addr.sun_path) - 1);
    
    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(sock);
        return R"({"status":"error","message":"Daemon is not running"})";
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
    
    return R"({"status":"error","message":"No response from daemon"})";
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
    // Install crash handler for better debugging
    install_crash_handler();

    // Setup logging
    auto logger = spdlog::stderr_color_mt("nerd-dictation");
    spdlog::set_default_logger(logger);
    spdlog::set_level(spdlog::level::info);
    
    CLI::App app{"nerd-dictation — speech dictation with pluggable ASR backends"};
    
    // Subcommands
    auto daemon_cmd = app.add_subcommand("daemon", "Start daemon");
    
    auto begin_cmd = app.add_subcommand("begin", "Start dictation");
    
    auto end_cmd = app.add_subcommand("end", "End dictation");
    
    auto cancel_cmd = app.add_subcommand("cancel", "Cancel dictation");
    
    auto toggle_cmd = app.add_subcommand("toggle", "Toggle dictation (begin/end)");
    
    CLI11_PARSE(app, argc, argv);
    
    // Load configuration
    auto config_path_opt = nerd_dictation::find_config_file();
    nerd_dictation::Config config;
    if (config_path_opt) {
        config = nerd_dictation::load_config(*config_path_opt);
    }
    
    // Process commands
    if (*daemon_cmd) {
        spdlog::info("Starting daemon...");
        nerd_dictation::daemon::DictationDaemon daemon(config);
        daemon.run();
        return 0;
    }
    
    if (*begin_cmd) {
        if (is_daemon_running()) {
            std::string response = send_command_to_daemon("begin");
            std::cout << response << std::endl;
        } else {
            spdlog::error("Daemon is not running. Start it with: nerd-dictation daemon");
            return 1;
        }
        return 0;
    }
    
    if (*end_cmd) {
        if (is_daemon_running()) {
            std::string response = send_command_to_daemon("end");
            std::cout << response << std::endl;
        } else {
            spdlog::error("Daemon is not running");
            return 1;
        }
        return 0;
    }
    
    if (*cancel_cmd) {
        if (is_daemon_running()) {
            std::string response = send_command_to_daemon("cancel");
            std::cout << response << std::endl;
        } else {
            spdlog::error("Daemon is not running");
            return 1;
        }
        return 0;
    }
    
    if (*toggle_cmd) {
        if (is_daemon_running()) {
            std::string status_response = send_command_to_daemon("status");
            // Simple status check
            std::string action = (status_response.find("listening") != std::string::npos) ? "end" : "begin";
            std::string response = send_command_to_daemon(action);
            std::cout << response << std::endl;
        } else {
            spdlog::error("Daemon is not running. Start it with: nerd-dictation daemon");
            return 1;
        }
        return 0;
    }
    
    // If no command specified, show help
    std::cout << app.help() << std::endl;
    return 0;
}
