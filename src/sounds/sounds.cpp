#include "nerd_dictation/sounds/sounds.hpp"
#include <spdlog/spdlog.h>
#include <cstdlib>
#include <filesystem>
#include <unistd.h>
#include <sys/wait.h>
#include <array>

namespace nerd_dictation::sounds {

namespace {

/* Кэшированный плеер */
std::string cached_player;
bool player_checked = false;

std::string find_player() {
    if (player_checked) {
        return cached_player;
    }
    player_checked = true;

    /* Проверяем доступные плееры */
    const std::array<const char*, 3> players = {"paplay", "pw-play", "aplay"};

    for (const char* player : players) {
        std::string which_cmd = std::string("which ") + player + " >/dev/null 2>&1";
        if (std::system(which_cmd.c_str()) == 0) {
            cached_player = player;
            spdlog::debug("Используем аудио-плеер: {}", player);
            return cached_player;
        }
    }

    spdlog::warn("Не найден аудио-плеер (paplay, pw-play или aplay)");
    return "";
}

} /* anonymous namespace */

std::string find_sound_file(const std::string& name) {
    std::string filename = name + ".wav";

    /* 1. Переменная окружения */
    const char* sounds_dir = std::getenv("NERD_DICTATION_SOUNDS_DIR");
    if (sounds_dir) {
        std::filesystem::path path = std::filesystem::path(sounds_dir) / filename;
        if (std::filesystem::exists(path)) {
            return path.string();
        }
    }

    /* 2. ~/.local/share/nerd-dictation/sounds/ */
    const char* home = std::getenv("HOME");
    if (home) {
        std::filesystem::path path = std::filesystem::path(home) / ".local/share/nerd-dictation/sounds" / filename;
        if (std::filesystem::exists(path)) {
            return path.string();
        }
    }

    /* 3. /usr/share/nerd-dictation/sounds/ */
    {
        std::filesystem::path path = std::filesystem::path("/usr/share/nerd-dictation/sounds") / filename;
        if (std::filesystem::exists(path)) {
            return path.string();
        }
    }

    /* 4. /usr/local/share/nerd-dictation/sounds/ */
    {
        std::filesystem::path path = std::filesystem::path("/usr/local/share/nerd-dictation/sounds") / filename;
        if (std::filesystem::exists(path)) {
            return path.string();
        }
    }

    return "";
}

void play_sound_async(const std::string& sound_name) {
    std::string sound_file = find_sound_file(sound_name);
    if (sound_file.empty()) {
        spdlog::debug("Звуковой файл не найден: {}", sound_name);
        return;
    }

    std::string player = find_player();
    if (player.empty()) {
        return;
    }

    /* Fork для асинхронного воспроизведения */
    pid_t pid = fork();
    if (pid == 0) {
        /* Дочерний процесс */
        /* Закрываем stdin/stdout/stderr чтобы не мешать родителю */
        close(STDIN_FILENO);
        close(STDOUT_FILENO);
        close(STDERR_FILENO);

        /* Запускаем плеер */
        execlp(player.c_str(), player.c_str(), sound_file.c_str(), nullptr);

        /* Если execlp вернулся - ошибка */
        _exit(1);
    } else if (pid < 0) {
        spdlog::warn("Не удалось создать процесс для воспроизведения звука");
    }
    /* Родительский процесс продолжает работу, не ждём дочерний */
}

void play_begin() {
    play_sound_async("begin");
}

void play_end() {
    play_sound_async("end");
}

void play_execute() {
    play_sound_async("execute");
}

} /* namespace nerd_dictation::sounds */
