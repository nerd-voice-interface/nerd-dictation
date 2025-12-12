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
#include <sys/resource.h>
#include <ctime>
#include <fcntl.h>

#ifdef HAVE_PYTHON
#include <pybind11/embed.h>
#include <pybind11/stl.h>
#include <pybind11/functional.h>
#include "nerd_dictation/backend_wrapper.hpp"
namespace py = pybind11;

/* ============================================================================
 * Native sounds module for Python
 * ============================================================================ */
PYBIND11_EMBEDDED_MODULE(_nerd_dictation_native, m) {
    m.doc() = "Native C++ functions for nerd-dictation";

    // Sounds submodule
    auto sounds = m.def_submodule("sounds", "Native sound playback via PulseAudio");
    sounds.def("init", &nerd_dictation::sounds::init_sounds, "Initialize sound subsystem");
    sounds.def("shutdown", &nerd_dictation::sounds::shutdown_sounds, "Shutdown sound subsystem");
    sounds.def("play_begin", &nerd_dictation::sounds::play_begin, "Play dictation start sound");
    sounds.def("play_end", &nerd_dictation::sounds::play_end, "Play dictation end sound");
    sounds.def("play_execute", &nerd_dictation::sounds::play_execute, "Play command execution sound");

    // ASR Result class
    py::class_<nerd_dictation::ASRResult>(m, "ASRResult")
        .def(py::init<>())
        .def(py::init<const std::string&, bool, float>(),
             py::arg("text"), py::arg("is_final") = false, py::arg("confidence") = -1.0f)
        .def_readwrite("text", &nerd_dictation::ASRResult::text)
        .def_readwrite("is_final", &nerd_dictation::ASRResult::is_final)
        .def_readwrite("confidence", &nerd_dictation::ASRResult::confidence)
        .def("__repr__", [](const nerd_dictation::ASRResult& r) {
            return "<ASRResult text='" + r.text + "' is_final=" +
                   (r.is_final ? "True" : "False") + " confidence=" +
                   std::to_string(r.confidence) + ">";
        });

    // Backend Info class
    py::class_<nerd_dictation::BackendInfo>(m, "BackendInfo")
        .def(py::init<>())
        .def_readwrite("name", &nerd_dictation::BackendInfo::name)
        .def_readwrite("streaming", &nerd_dictation::BackendInfo::streaming)
        .def_readwrite("sample_rate", &nerd_dictation::BackendInfo::sample_rate)
        .def_readwrite("channels", &nerd_dictation::BackendInfo::channels)
        .def_readwrite("bits_per_sample", &nerd_dictation::BackendInfo::bits_per_sample)
        .def("__repr__", [](const nerd_dictation::BackendInfo& i) {
            return "<BackendInfo name='" + i.name + "' streaming=" +
                   (i.streaming ? "True" : "False") + " sample_rate=" +
                   std::to_string(i.sample_rate) + ">";
        });

    // Backend class - main ASR interface
    py::class_<nerd_dictation::Backend>(m, "Backend")
        .def(py::init<const std::string&>(), py::arg("library_path"),
             "Load backend from .so file")
        .def("get_info", &nerd_dictation::Backend::get_info,
             "Get backend information")
        .def("name", &nerd_dictation::Backend::name,
             "Get backend name")
        .def("supports_streaming", &nerd_dictation::Backend::supports_streaming,
             "Check if streaming is supported")
        .def("sample_rate", &nerd_dictation::Backend::sample_rate,
             "Get expected sample rate")
        .def("feed", [](nerd_dictation::Backend& self, py::bytes data) {
            std::string_view sv = data;
            const int16_t* samples = reinterpret_cast<const int16_t*>(sv.data());
            size_t num_samples = sv.size() / sizeof(int16_t);
            self.feed(samples, num_samples);
        }, py::arg("audio_bytes"),
           "Feed audio data (raw int16 PCM bytes)")
        .def("feed_samples", [](nerd_dictation::Backend& self, const std::vector<int16_t>& samples) {
            self.feed(samples);
        }, py::arg("samples"),
           "Feed audio samples (list of int16)")
        .def("result", &nerd_dictation::Backend::result,
             "Get current result")
        .def("finalize", &nerd_dictation::Backend::finalize,
             "Finalize and get final result")
        .def("reset", &nerd_dictation::Backend::reset,
             "Reset state for new session")
        .def("get_error", &nerd_dictation::Backend::get_error,
             "Get last error message")
        .def("__bool__", [](const nerd_dictation::Backend& self) {
            return static_cast<bool>(self);
        });
}
#endif

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

    // Save crash report to file
    const char* home = getenv("HOME");
    if (home) {
        char report_path[512];
        time_t now = time(nullptr);
        struct tm* t = localtime(&now);
        snprintf(report_path, sizeof(report_path),
                 "%s/.local/share/nerd-dictation/crash-%04d%02d%02d-%02d%02d%02d.log",
                 home, t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
                 t->tm_hour, t->tm_min, t->tm_sec);

        int fd = open(report_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd >= 0) {
            write(fd, "Signal: ", 8);
            write(fd, sig_name, strlen(sig_name));
            write(fd, "\nBacktrace:\n", 12);
            backtrace_symbols_fd(bt_buffer, bt_size, fd);
            close(fd);

            const char saved[] = "[nerd-dictation] Crash report saved to: ";
            write(STDERR_FILENO, saved, sizeof(saved) - 1);
            write(STDERR_FILENO, report_path, strlen(report_path));
            write(STDERR_FILENO, "\n", 1);
        }
    }

    // Re-raise the signal to get core dump
    signal(sig, SIG_DFL);
    raise(sig);
}

static void install_crash_handler() {
    // Enable core dumps (unlimited size)
    struct rlimit core_limit;
    core_limit.rlim_cur = RLIM_INFINITY;
    core_limit.rlim_max = RLIM_INFINITY;
    setrlimit(RLIMIT_CORE, &core_limit);

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

#ifdef HAVE_PYTHON
    // ========================================================================
    // Python-based processing via cli.py
    // ========================================================================

    // Initialize Python interpreter manually for better control
    py::initialize_interpreter();

    int exit_code = 0;

    try {
        // Add scripts directory (priority) and venv site-packages to Python path
        py::module_ sys = py::module_::import("sys");
        py::list path = sys.attr("path");
        // Local scripts first (for development), then venv for dependencies
        path.attr("insert")(0, PYTHON_SCRIPTS_DIR);
        path.attr("insert")(1, PYTHON_VENV_SITE_PACKAGES);

        spdlog::info("Python interpreter initialized");
        spdlog::info("Venv site-packages: {}", PYTHON_VENV_SITE_PACKAGES);
        spdlog::info("Scripts directory: {}", PYTHON_SCRIPTS_DIR);

        // Convert argv to Python list
        py::list py_argv;
        for (int i = 0; i < argc; ++i) {
            py_argv.append(argv[i]);
        }
        sys.attr("argv") = py_argv;

        // Import and run cli module from nerd_dictation_ex package
        py::module_ cli = py::module_::import("nerd_dictation_ex.cli");

        spdlog::info("Running cli.main()...");
        cli.attr("main")();

    } catch (py::error_already_set& e) {
        // Check if it's SystemExit (raised by argparse --help or sys.exit())
        if (e.matches(PyExc_SystemExit)) {
            // Normal exit (argparse --help, sys.exit(0), etc.)
            // Clear the error before finalization
            PyErr_Clear();
            exit_code = 0;
        } else {
            spdlog::error("Python error: {}", e.what());
            PyErr_Clear();
            exit_code = 1;
        }
    } catch (const std::exception& e) {
        spdlog::error("Error: {}", e.what());
        exit_code = 1;
    }

    // Clean finalization - clear any pending errors first
    PyErr_Clear();
    py::finalize_interpreter();

    return exit_code;
#else
    // ========================================================================
    // Fallback: C++ implementation (when Python not available)
    // ========================================================================
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
#endif
}
