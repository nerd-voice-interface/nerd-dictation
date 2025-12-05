#pragma once

#include "nerd_dictation/config.hpp"
#include "nerd_dictation/backends/base.hpp"
#include <memory>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <string>
#include <vector>
#include <queue>
#include <deque>

#ifdef ENABLE_VOSK_BACKEND
#include <vosk/vosk.h>
#endif

namespace nerd_dictation::daemon {

enum class DaemonState {
    Idle,
    Listening,
    Stopping
};

/**
 * Daemon для nerd-dictation
 * Держит модель в памяти и обрабатывает команды через Unix socket
 */
class DictationDaemon {
private:
    Config config_;
    std::unique_ptr<backends::ASRBackend> backend_;
    
    std::atomic<DaemonState> state_{DaemonState::Idle};
    std::atomic<bool> running_{true};
    
    // Потоки
    std::thread dictation_thread_;
    std::thread socket_thread_;
    std::thread wake_word_thread_;
    std::thread audio_listener_thread_;
    
    // Синхронизация
    std::mutex dictation_mutex_;
    std::condition_variable dictation_cv_;
    std::mutex audio_buffer_mutex_;
    std::mutex wake_word_queue_mutex_;
    
    // Unix socket
    std::string socket_path_;
    int server_fd_ = -1;
    
    // Аудио буфер для диктовки (кольцевой буфер)
    std::deque<std::vector<int16_t>> audio_buffer_;
    size_t audio_buffer_max_chunks_ = 100;
    static constexpr size_t AUDIO_BUFFER_SECONDS = 1.5;
    
    // Очередь для wake word detection (без лимита)
    std::queue<std::vector<int16_t>> wake_word_queue_;
    
    // Процесс захвата аудио
    int audio_proc_pid_ = -1;
    FILE* audio_proc_pipe_ = nullptr;
    
    // Wake word detection (требует Vosk)
    bool wake_word_enabled_ = true;
    std::string wake_word_ = "запись";
    std::atomic<bool> wake_word_running_{false};
    
    #ifdef ENABLE_VOSK_BACKEND
    vosk_model_t* wake_word_model_ = nullptr;
    vosk_recognizer_t* wake_word_recognizer_ = nullptr;
    #endif
    
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
     * Запустить daemon (блокирующий вызов)
     */
    void run();
    
    /**
     * Начать диктовку
     */
    void begin_dictation();
    
    /**
     * Завершить диктовку
     */
    void end_dictation();
    
    /**
     * Отменить диктовку
     */
    void cancel_dictation();
    
    /**
     * Получить статус
     */
    DaemonState get_state() const { return state_.load(); }
    std::string get_backend_name() const;
};

} // namespace nerd_dictation::daemon
