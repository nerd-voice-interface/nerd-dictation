#pragma once

#include "nerd_dictation/backends/base.hpp"
#include <memory>

namespace nerd_dictation::backends {

/**
 * Создать экземпляр Vosk бэкенда
 */
std::unique_ptr<ASRBackend> create_vosk_backend();

} // namespace nerd_dictation::backends
