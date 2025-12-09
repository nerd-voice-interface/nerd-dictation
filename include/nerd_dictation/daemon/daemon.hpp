#pragma once

#include "nerd_dictation/config.hpp"
#include "nerd_dictation/backend_wrapper.hpp"
#include "nerd_dictation/audio/audio_preprocessor.hpp"
#include <memory>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <string>
#include <vector>
#include <queue>
#include <deque>

/* Forward declarations for Vosk (wake word only) */
#if HAVE_VOSK_WAKE_WORD
struct VoskModel;
struct VoskRecognizer;
#else
/* Stubs when Vosk is not available at compile time */
using VoskModel = void;
using VoskRecognizer = void;
#endif

namespace nerd_dictation::daemon {

enum class DaemonState {
    Idle,
    Listening,
    Stopping
};

/**
 * Daemon for nerd-dictation
 * Keeps model in memory and processes commands via Unix socket
 *
 * Uses C API for backends (loads .so dynamically)
 */
class DictationDaemon {
private:
    Config config_;
    std::unique_ptr<Backend> backend_;

    std::atomic<DaemonState> state_{DaemonState::Idle};
    std::atomic<bool> running_{true};

    /* Threads */
    std::thread dictation_thread_;
    std::thread socket_thread_;
    std::thread wake_word_thread_;
    std::thread audio_listener_thread_;

    /* Synchronization */
    std::mutex dictation_mutex_;
    std::condition_variable dictation_cv_;
    std::mutex audio_buffer_mutex_;
    std::mutex wake_word_queue_mutex_;

    /* Unix socket */
    std::string socket_path_;
    int server_fd_ = -1;

    /* Audio buffer for dictation (ring buffer) */
    std::deque<std::vector<int16_t>> audio_buffer_;
    size_t audio_buffer_max_chunks_ = 100;
    static constexpr size_t AUDIO_BUFFER_SECONDS = 1.5;

    /* Queue for wake word detection (no limit) */
    std::queue<std::vector<int16_t>> wake_word_queue_;

    /* Audio capture process */
    int audio_proc_pid_ = -1;
    FILE* audio_proc_pipe_ = nullptr;

    /* Audio preprocessing (AGC + noise suppression) */
    std::unique_ptr<audio::AudioPreprocessor> audio_preprocessor_;
    std::vector<int16_t> preprocessed_buffer_;

    /* Wake word detection (requires Vosk) */
    bool wake_word_enabled_ = true;
    std::string wake_word_ = "запись";  // Default wake word (Russian)
    std::atomic<bool> wake_word_running_{false};

    VoskModel* wake_word_model_ = nullptr;
    VoskRecognizer* wake_word_recognizer_ = nullptr;

    void dictation_loop();
    void socket_server_loop();
    void handle_client(int client_fd);
    void process_and_type(const std::string& text);
    void audio_listener_loop();
    void wake_word_loop();
    bool load_wake_word_models();
    void start_audio_listener();
    void start_wake_word_detection();
    std::vector<int16_t> get_audio_chunk();
    std::string get_audio_command() const;

public:
    explicit DictationDaemon(const Config& config);
    ~DictationDaemon();

    /**
     * Run daemon (blocking call)
     */
    void run();

    /**
     * Start dictation
     */
    void begin_dictation();

    /**
     * End dictation
     */
    void end_dictation();

    /**
     * Cancel dictation
     */
    void cancel_dictation();

    /**
     * Get status
     */
    DaemonState get_state() const { return state_.load(); }
    std::string get_backend_name() const;
};

} /* namespace nerd_dictation::daemon */
