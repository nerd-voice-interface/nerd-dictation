#pragma once

#include "nerd_dictation/backends/base.hpp"
#include <memory>

namespace nerd_dictation::backends {

/**
 * Создать экземпляр dummy бэкенда
 */
std::unique_ptr<ASRBackend> create_dummy_backend();

} // namespace nerd_dictation::backends
