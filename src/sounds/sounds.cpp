#include "nerd_dictation/sounds/sounds.hpp"
#include <spdlog/spdlog.h>
#include <cstdlib>
#include <filesystem>

namespace nerd_dictation::sounds {

std::string find_sound_file(const std::string& name) {
    // Проверяем в sounds/ относительно исполняемого файла
    // Для простоты используем фиксированные пути или переменные окружения
    const char* sounds_dir = std::getenv("NERD_DICTATION_SOUNDS_DIR");
    if (sounds_dir) {
        std::filesystem::path sound_path = std::filesystem::path(sounds_dir) / (name + ".wav");
        if (std::filesystem::exists(sound_path)) {
            return sound_path.string();
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
    
    // Пробуем разные плееры
    const char* players[] = {"paplay", "pw-play", "aplay", nullptr};
    
    for (const char** player = players; *player; ++player) {
        std::string cmd = std::string(*player) + " " + sound_file + " &";
        int result = std::system(cmd.c_str());
        if (result == 0) {
            return;
        }
    }
    
    spdlog::warn("Не удалось воспроизвести звук: {}", sound_name);
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

} // namespace nerd_dictation::sounds
