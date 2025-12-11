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
        spdlog::info("Loading backend: {}", backend_path);
        backend_ = std::make_unique<Backend>(backend_path);
        spdlog::info("Backend loaded: {} (sample rate: {} Hz)",
                     backend_->name(), backend_->sample_rate());
    } catch (const std::exception& e) {
        spdlog::error("Failed to load backend {}: {}", backend_path, e.what());
        throw;
    }

    /* Initialize audio preprocessor (AGC + noise suppression) */
    audio_preprocessor_ = std::make_unique<audio::AudioPreprocessor>();
    audio::AudioPreprocessor::Config preproc_config;
    preproc_config.input_sample_rate = backend_->sample_rate();
    preproc_config.process_sample_rate = 16000;  /* WebRTC minimum */
    preproc_config.noise_suppression_level = 2;  /* High */
    preproc_config.agc_enabled = true;
    preproc_config.high_pass_filter_enabled = true;

    if (!audio_preprocessor_->initialize(preproc_config)) {
        spdlog::warn("Audio preprocessor not initialized, continuing without processing");
        audio_preprocessor_.reset();
    }

    spdlog::info("Daemon initialized, socket: {}", socket_path_);
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

    /* Shutdown sound system */
    sounds::shutdown_sounds();
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
    spdlog::info("Starting dictation loop");

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
                    spdlog::info("Recognized: {}", result.text);
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
    spdlog::info("Dictation loop finished");
}

void DictationDaemon::begin_dictation() {
    if (state_ == DaemonState::Listening) {
        spdlog::warn("Dictation already started");
        return;
    }

    state_ = DaemonState::Listening;
    sounds::play_begin();

    if (!dictation_thread_.joinable()) {
        dictation_thread_ = std::thread(&DictationDaemon::dictation_loop, this);
    } else {
        dictation_cv_.notify_all();
    }

    spdlog::info("Dictation started");
}

void DictationDaemon::end_dictation() {
    if (state_ != DaemonState::Listening) {
        spdlog::warn("Dictation not started");
        return;
    }

    sounds::play_end();
    state_ = DaemonState::Idle;
    dictation_cv_.notify_all();

    if (dictation_thread_.joinable()) {
        dictation_thread_.join();
    }

    spdlog::info("Dictation ended");
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

    spdlog::info("Dictation cancelled");
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
        response = create_json_response("ok", "Dictation started");
    } else if (action == "end") {
        end_dictation();
        response = create_json_response("ok", "Dictation ended");
    } else if (action == "cancel") {
        cancel_dictation();
        response = create_json_response("ok", "Dictation cancelled");
    } else if (action == "status") {
        std::string state_str = state_ == DaemonState::Listening ? "listening" : "idle";
        std::ostringstream oss;
        oss << "{\"status\":\"ok\",\"state\":\"" << state_str
            << "\",\"backend\":\"" << json_escape(get_backend_name()) << "\"}";
        response = oss.str();
    } else {
        response = create_json_response("error", "Unknown command: " + action);
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
        spdlog::error("Failed to create socket");
        return;
    }

    unlink(socket_path_.c_str());

    if (bind(server_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        spdlog::error("Failed to bind socket: {}", socket_path_);
        close(server_fd_);
        return;
    }

    if (listen(server_fd_, 5) < 0) {
        spdlog::error("Failed to listen on socket");
        close(server_fd_);
        return;
    }

    spdlog::info("Socket server started: {}", socket_path_);

    while (running_) {
        int client_fd = accept(server_fd_, nullptr, nullptr);
        if (client_fd >= 0) {
            handle_client(client_fd);
        }
    }
}

void DictationDaemon::run() {
    /* Initialize sound system */
    sounds::init_sounds();

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

void DictationDaemon::start_audio_listener() {
    if (audio_listener_thread_.joinable()) {
        return;
    }

    audio_listener_thread_ = std::thread(&DictationDaemon::audio_listener_loop, this);
    spdlog::info("Audio listener started");
}

void DictationDaemon::start_wake_word_detection() {
    if (!wake_word_enabled_) {
        return;
    }

    if (wake_word_thread_.joinable()) {
        return;
    }

    if (!backend_) {
        spdlog::warn("Wake word detection disabled: backend not loaded");
        return;
    }

    wake_word_running_ = true;
    wake_word_thread_ = std::thread(&DictationDaemon::wake_word_loop, this);
    spdlog::info("Wake word detection started (word: '{}', backend: {})",
                 wake_word_, backend_->name());
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
                spdlog::error("Failed to start audio process: {}", cmd);
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                continue;
            }

            std::vector<int16_t> chunk(chunk_size / sizeof(int16_t));
            preprocessed_buffer_.resize(chunk_size / sizeof(int16_t));

            while (running_) {
                size_t bytes_read = fread(chunk.data(), 1, chunk_size, audio_proc_pipe_);

                if (bytes_read == 0) {
                    break; /* EOF */
                }

                size_t num_samples = bytes_read / sizeof(int16_t);
                if (num_samples < chunk.size()) {
                    chunk.resize(num_samples);
                }

                /* Apply audio preprocessing (AGC + noise suppression) */
                std::vector<int16_t>* audio_to_use = &chunk;
                if (audio_preprocessor_ && audio_preprocessor_->is_initialized()) {
                    preprocessed_buffer_.resize(num_samples);
                    int processed = audio_preprocessor_->process(
                        chunk.data(), num_samples, preprocessed_buffer_.data()
                    );
                    if (processed > 0) {
                        preprocessed_buffer_.resize(static_cast<size_t>(processed));
                        audio_to_use = &preprocessed_buffer_;
                    }
                }

                /* Add to ring buffer for dictation */
                {
                    std::lock_guard<std::mutex> lock(audio_buffer_mutex_);
                    audio_buffer_.push_back(*audio_to_use);
                    if (audio_buffer_.size() > audio_buffer_max_chunks_) {
                        audio_buffer_.pop_front();
                    }
                }

                /* Copy to wake word queue (if not listening) */
                if (wake_word_running_ && state_ != DaemonState::Listening) {
                    std::lock_guard<std::mutex> lock(wake_word_queue_mutex_);
                    wake_word_queue_.push(*audio_to_use);
                }
            }

            pclose(audio_proc_pipe_);
            audio_proc_pipe_ = nullptr;

        } catch (const std::exception& e) {
            spdlog::error("Audio listener error: {}", e.what());
        }

        if (running_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    }
}

void DictationDaemon::wake_word_loop() {
    if (!backend_) {
        spdlog::error("Wake word loop: backend not available");
        return;
    }

    /* Helper lambda to check text for wake word */
    auto check_wake_word = [this](const std::string& text) -> bool {
        if (text.empty()) return false;

        std::string text_lower = text;
        std::transform(text_lower.begin(), text_lower.end(), text_lower.begin(), ::tolower);

        std::string wake_lower = wake_word_;
        std::transform(wake_lower.begin(), wake_lower.end(), wake_lower.begin(), ::tolower);

        /* Check for wake word and common variants */
        if (text_lower.find(wake_lower) != std::string::npos ||
            text_lower.find("записи") != std::string::npos ||   /* Russian variant */
            text_lower.find("запись") != std::string::npos) {   /* Exact match */
            return true;
        }
        return false;
    };

    size_t silence_chunks = 0;
    constexpr size_t SILENCE_THRESHOLD_CHUNKS = 5;  /* ~320ms at 512 samples per chunk @ 8kHz (matches reference 300ms) */
    constexpr int16_t VAD_THRESHOLD = 200;  /* Lower threshold for voice detection */
    std::string last_partial;

    /* Timing for detailed logging */
    auto loop_start = std::chrono::steady_clock::now();
    auto last_audio_time = loop_start;
    auto speech_start_time = loop_start;
    bool in_speech = false;
    size_t chunks_processed = 0;
    size_t chunks_with_audio = 0;

    spdlog::info("Wake word loop started using {} backend", backend_->name());

    while (wake_word_running_ && running_) {
        /* Skip if dictation is active - just wait, don't touch backend */
        if (state_ == DaemonState::Listening) {
            {
                std::lock_guard<std::mutex> lock(wake_word_queue_mutex_);
                while (!wake_word_queue_.empty()) {
                    wake_word_queue_.pop();
                }
            }
            /* Don't reset backend here - dictation_loop is using it! */
            silence_chunks = 0;
            last_partial.clear();
            in_speech = false;
            chunks_processed = 0;
            chunks_with_audio = 0;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        /* Get audio from queue */
        std::vector<int16_t> chunk;
        size_t queue_size = 0;
        {
            std::lock_guard<std::mutex> lock(wake_word_queue_mutex_);
            queue_size = wake_word_queue_.size();
            if (wake_word_queue_.empty()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            chunk = wake_word_queue_.front();
            wake_word_queue_.pop();
        }

        if (chunk.empty()) {
            continue;
        }

        auto now = std::chrono::steady_clock::now();
        chunks_processed++;

        /* Simple VAD: check if this chunk has audio activity */
        int64_t sum = 0;
        int16_t max_sample = 0;
        for (int16_t sample : chunk) {
            int16_t abs_sample = std::abs(sample);
            sum += abs_sample;
            if (abs_sample > max_sample) max_sample = abs_sample;
        }
        int32_t avg = static_cast<int32_t>(sum / chunk.size());

        bool has_audio = (avg > VAD_THRESHOLD);

        /* Track speech start */
        if (has_audio && !in_speech) {
            in_speech = true;
            speech_start_time = now;
            spdlog::info("[ww] speech START, queue={}, avg={}, max={}", queue_size, avg, max_sample);
        } else if (!has_audio && in_speech) {
            auto speech_duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - speech_start_time).count();
            spdlog::info("[ww] speech END after {}ms, chunks_with_audio={}", speech_duration, chunks_with_audio);
            in_speech = false;
            chunks_with_audio = 0;
        }

        if (has_audio) {
            chunks_with_audio++;
            last_audio_time = now;
        }

        /* Feed audio to backend and check for wake word */
        auto feed_start = std::chrono::steady_clock::now();
        try {
            backend_->feed(chunk);
            auto feed_end = std::chrono::steady_clock::now();
            auto feed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(feed_end - feed_start).count();

            auto result_start = std::chrono::steady_clock::now();
            ASRResult r = backend_->result();
            auto result_end = std::chrono::steady_clock::now();
            auto result_ms = std::chrono::duration_cast<std::chrono::milliseconds>(result_end - result_start).count();

            /* Log timing if significant */
            if (feed_ms > 10 || result_ms > 10) {
                spdlog::info("[ww] feed={}ms result={}ms", feed_ms, result_ms);
            }

            if (!r.text.empty()) {
                /* Check partial result for faster detection */
                if (r.text != last_partial) {
                    auto since_speech = in_speech ?
                        std::chrono::duration_cast<std::chrono::milliseconds>(now - speech_start_time).count() : 0;
                    spdlog::info("[ww] partial: '{}' (since_speech={}ms, is_final={})",
                                 r.text, since_speech, r.is_final);
                    last_partial = r.text;
                }

                if (check_wake_word(r.text)) {
                    auto total_time = std::chrono::duration_cast<std::chrono::milliseconds>(now - speech_start_time).count();
                    spdlog::info("[ww] DETECTED: '{}' in {}ms (is_final: {})",
                                 r.text, total_time, r.is_final);
                    spdlog::info("[ww] activation by word '{}'!", wake_word_);

                    backend_->reset();
                    last_partial.clear();
                    in_speech = false;
                    chunks_with_audio = 0;

                    {
                        std::lock_guard<std::mutex> lock(audio_buffer_mutex_);
                        audio_buffer_.clear();
                    }

                    begin_dictation();
                    continue;
                }

                /* If final result, reset for next utterance */
                if (r.is_final) {
                    spdlog::info("[ww] final (no match): '{}'", r.text);
                    backend_->reset();
                    last_partial.clear();
                }
            }
        } catch (const std::exception& e) {
            spdlog::warn("Wake word processing error: {}", e.what());
        }

        /* Track silence to reset recognizer after prolonged silence */
        if (!has_audio) {
            silence_chunks++;
            if (silence_chunks >= SILENCE_THRESHOLD_CHUNKS) {
                spdlog::debug("[ww] silence reset after {} chunks", silence_chunks);
                try {
                    backend_->reset();
                    last_partial.clear();
                } catch (const std::exception& e) {
                    spdlog::warn("Failed to reset backend after silence: {}", e.what());
                }
                silence_chunks = 0;
            }
        } else {
            silence_chunks = 0;
        }
    }

    spdlog::info("Wake word loop finished");
}

void DictationDaemon::inject_audio_for_test(const std::vector<int16_t>& audio_data) {
    /* Split audio into chunks and add to wake word queue */
    size_t chunk_samples = config_.audio.chunk_size / sizeof(int16_t);

    for (size_t offset = 0; offset < audio_data.size(); offset += chunk_samples) {
        size_t remaining = std::min(chunk_samples, audio_data.size() - offset);
        std::vector<int16_t> chunk(audio_data.begin() + offset,
                                    audio_data.begin() + offset + remaining);

        /* Add to ring buffer for dictation */
        {
            std::lock_guard<std::mutex> lock(audio_buffer_mutex_);
            audio_buffer_.push_back(chunk);
            if (audio_buffer_.size() > audio_buffer_max_chunks_) {
                audio_buffer_.pop_front();
            }
        }

        /* Copy to wake word queue */
        if (wake_word_enabled_ && state_ != DaemonState::Listening) {
            std::lock_guard<std::mutex> lock(wake_word_queue_mutex_);
            wake_word_queue_.push(chunk);
        }
    }

    spdlog::info("Injected {} samples ({} chunks) for testing",
                 audio_data.size(), (audio_data.size() + chunk_samples - 1) / chunk_samples);
}

void DictationDaemon::start_wake_word_detection_for_test() {
    if (!wake_word_enabled_) {
        spdlog::warn("Wake word detection is disabled in config");
        return;
    }

    start_wake_word_detection();
}

} /* namespace nerd_dictation::daemon */
