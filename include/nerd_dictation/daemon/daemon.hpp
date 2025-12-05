#pragma once

#include "nerd_dictation/config.hpp"
#include "nerd_dictation/backends/base.hpp"
#include <memory>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <string>

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
    
    // Аудио буфер для wake word detection
    std::vector<std::vector<int16_t>> audio_buffer_;
    static constexpr size_t AUDIO_BUFFER_SECONDS = 1.5;
    
    // Wake word detection (требует Vosk)
    bool wake_word_enabled_ = true;
    std::string wake_word_ = "запись";
    std::atomic<bool> wake_word_running_{false};
    
    void dictation_loop();
    void socket_server_loop();
    void handle_client(int client_fd);
    void process_and_type(const std::string& text);
    void audio_listener_loop();
    void wake_word_loop();
    bool load_wake_word_models();
    
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
