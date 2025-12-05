# Подключение зависимостей для C++ версии

## Обзор зависимостей

### Критические зависимости

1. **libvosk** - Vosk ASR библиотека
2. **onnxruntime** или **libtorch** - Для GigaAM бэкенда
3. **libpipewire** / **libpulse** - Аудио захват
4. **toml11** - Парсинг конфигурации

### Вспомогательные зависимости

5. **Eigen** - Линейная алгебра (шумоподавление)
6. **spdlog** - Логирование
7. **CLI11** - CLI парсинг

---

## Детальное описание зависимостей

### 1. libvosk

**Описание:** C библиотека для распознавания речи на основе Kaldi.

**Установка:**

#### Arch Linux (AUR)
```bash
yay -S vosk-api-bin
# или
yay -S vosk-api-git  # для сборки из исходников
```

#### Из исходников
```bash
git clone https://github.com/alphacephei/vosk-api
cd vosk-api
cd c
make
sudo make install
```

**Подключение в CMake:**
```cmake
# Поиск Vosk
find_library(VOSK_LIB
    NAMES vosk
    PATHS
        /usr/lib
        /usr/local/lib
        ${CMAKE_SOURCE_DIR}/third_party/vosk-api/c
)

find_path(VOSK_INCLUDE
    NAMES vosk/vosk.h
    PATHS
        /usr/include
        /usr/local/include
        ${CMAKE_SOURCE_DIR}/third_party/vosk-api/c
)

if(NOT VOSK_LIB OR NOT VOSK_INCLUDE)
    message(FATAL_ERROR "Vosk library not found")
endif()

target_link_libraries(nerd-dictation-ex PRIVATE ${VOSK_LIB})
target_include_directories(nerd-dictation-ex PRIVATE ${VOSK_INCLUDE})
```

**Использование:**
```cpp
#include <vosk/vosk.h>

vosk_model_t* model = vosk_model_new("/path/to/model");
vosk_recognizer_t* recognizer = vosk_recognizer_new(model, 16000);
```

---

### 2. ONNX Runtime

**Описание:** Оптимизированная библиотека для выполнения ONNX моделей.

**Установка:**

#### Pre-built binaries (рекомендуется)
```bash
# Скачать с https://github.com/microsoft/onnxruntime/releases
# Linux x64, CPU:
wget https://github.com/microsoft/onnxruntime/releases/download/v1.16.3/onnxruntime-linux-x64-1.16.3.tgz
tar -xzf onnxruntime-linux-x64-1.16.3.tgz
export ONNXRUNTIME_ROOT=/path/to/onnxruntime-linux-x64-1.16.3
```

#### vcpkg
```bash
./vcpkg install onnxruntime
```

#### Arch Linux (AUR)
```bash
yay -S onnxruntime
```

**Подключение в CMake:**
```cmake
# Вариант 1: Pre-built
set(ONNXRUNTIME_ROOT "$ENV{ONNXRUNTIME_ROOT}" CACHE PATH "ONNX Runtime root")

find_path(ONNXRUNTIME_INCLUDE
    NAMES onnxruntime_cxx_api.h
    PATHS
        ${ONNXRUNTIME_ROOT}/include
        /usr/include
)

find_library(ONNXRUNTIME_LIB
    NAMES onnxruntime
    PATHS
        ${ONNXRUNTIME_ROOT}/lib
        /usr/lib
)

# Вариант 2: vcpkg
find_package(onnxruntime CONFIG REQUIRED)

target_link_libraries(nerd-dictation-ex PRIVATE onnxruntime::onnxruntime)
target_include_directories(nerd-dictation-ex PRIVATE ${ONNXRUNTIME_INCLUDE})
```

**Использование:**
```cpp
#include <onnxruntime/core/session/onnxruntime_cxx_api.h>

Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "nerd-dictation");
Ort::SessionOptions session_options;
Ort::Session session(env, "/path/to/model.onnx", session_options);
```

**GPU поддержка (CUDA):**
```cmake
# Для CUDA provider нужны дополнительные библиотеки
find_package(CUDA REQUIRED)
# ONNX Runtime должен быть собран с CUDA support
```

---

### 3. LibTorch (альтернатива для GigaAM)

**Описание:** PyTorch C++ API.

**Установка:**

#### Pre-built (рекомендуется)
```bash
# Скачать с https://pytorch.org/get-started/locally/
# Linux, CPU:
wget https://download.pytorch.org/libtorch/cpu/libtorch-cxx11-abi-shared-with-deps-2.1.0%2Bcpu.zip
unzip libtorch-cxx11-abi-shared-with-deps-2.1.0+cpu.zip
export LIBTORCH_ROOT=/path/to/libtorch

# CUDA:
wget https://download.pytorch.org/libtorch/cu118/libtorch-cxx11-abi-shared-with-deps-2.1.0%2Bcu118.zip
```

**Подключение в CMake:**
```cmake
set(Torch_DIR "${LIBTORCH_ROOT}/share/cmake/Torch")
find_package(Torch REQUIRED)

target_link_libraries(nerd-dictation-ex PRIVATE ${TORCH_LIBRARIES})
target_compile_definitions(nerd-dictation-ex PRIVATE _GLIBCXX_USE_CXX11_ABI=1)
```

**Использование:**
```cpp
#include <torch/torch.h>
#include <torch/script.h>

torch::jit::script::Module module = torch::jit::load("/path/to/model.pt");
module.eval();
```

---

### 4. PipeWire

**Описание:** Современный аудио сервер для Linux.

**Установка:**
```bash
# Arch Linux
sudo pacman -S pipewire-pulse libpipewire

# Ubuntu/Debian
sudo apt install libpipewire-0.3-dev
```

**Подключение в CMake:**
```cmake
pkg_check_modules(PIPEWIRE REQUIRED libpipewire-0.3)

target_link_libraries(nerd-dictation-ex PRIVATE ${PIPEWIRE_LIBRARIES})
target_include_directories(nerd-dictation-ex PRIVATE ${PIPEWIRE_INCLUDE_DIRS})
target_compile_options(nerd-dictation-ex PRIVATE ${PIPEWIRE_CFLAGS_OTHER})
```

**Использование:**
```cpp
#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>

// Инициализация
pw_init(nullptr, nullptr);

// Создание контекста
struct pw_context* context = pw_context_new(...);
struct pw_main_loop* loop = pw_main_loop_new(nullptr);
```

---

### 5. PulseAudio (fallback)

**Описание:** Аудио сервер (fallback если PipeWire недоступен).

**Установка:**
```bash
# Arch Linux
sudo pacman -S libpulse

# Ubuntu/Debian
sudo apt install libpulse-dev
```

**Подключение в CMake:**
```cmake
pkg_check_modules(PULSE REQUIRED libpulse)

target_link_libraries(nerd-dictation-ex PRIVATE ${PULSE_LIBRARIES})
target_include_directories(nerd-dictation-ex PRIVATE ${PULSE_INCLUDE_DIRS})
```

**Использование:**
```cpp
#include <pulse/simple.h>
#include <pulse/error.h>

pa_simple* s = pa_simple_new(
    nullptr,           // server
    "nerd-dictation",  // app name
    PA_STREAM_RECORD,
    nullptr,           // device
    "record",          // stream name
    &ss,               // sample spec
    nullptr,           // channel map
    nullptr,           // buffering attributes
    &error
);
```

---

### 6. toml11

**Описание:** Header-only TOML парсер для C++11.

**Установка:**

#### Git submodule (рекомендуется)
```bash
git submodule add https://github.com/ToruNiina/toml11.git third_party/toml11
```

#### vcpkg
```bash
./vcpkg install toml11
```

#### Header-only (просто скопировать)
```bash
mkdir -p third_party/toml11
cd third_party/toml11
git clone https://github.com/ToruNiina/toml11.git .
```

**Подключение в CMake:**
```cmake
# Вариант 1: Submodule
add_subdirectory(third_party/toml11)
target_link_libraries(nerd-dictation-ex PRIVATE toml11::toml11)

# Вариант 2: Header-only
find_path(TOML11_INCLUDE toml11/toml.hpp)
target_include_directories(nerd-dictation-ex PRIVATE ${TOML11_INCLUDE})
```

**Использование:**
```cpp
#include <toml11/toml.hpp>

auto data = toml::parse("config.toml");
auto backend = toml::find<std::string>(data, "general", "backend");
```

---

### 7. Eigen

**Описание:** Header-only библиотека для линейной алгебры.

**Установка:**

#### Git submodule
```bash
git submodule add https://gitlab.com/libeigen/eigen.git third_party/eigen
```

#### vcpkg
```bash
./vcpkg install eigen3
```

#### Системный пакет
```bash
# Arch Linux
sudo pacman -S eigen

# Ubuntu/Debian
sudo apt install libeigen3-dev
```

**Подключение в CMake:**
```cmake
find_package(Eigen3 REQUIRED)
target_link_libraries(nerd-dictation-ex PRIVATE Eigen3::Eigen)
```

**Использование:**
```cpp
#include <Eigen/Dense>

Eigen::MatrixXf matrix(10, 10);
Eigen::VectorXf vector(10);
```

---

### 8. spdlog

**Описание:** Быстрая библиотека логирования.

**Установка:**

#### Git submodule
```bash
git submodule add https://github.com/gabime/spdlog.git third_party/spdlog
```

#### vcpkg
```bash
./vcpkg install spdlog
```

#### Header-only (опционально)
```bash
# Можно использовать как header-only
```

**Подключение в CMake:**
```cmake
# Вариант 1: Submodule
add_subdirectory(third_party/spdlog)
target_link_libraries(nerd-dictation-ex PRIVATE spdlog::spdlog)

# Вариант 2: vcpkg
find_package(spdlog CONFIG REQUIRED)
target_link_libraries(nerd-dictation-ex PRIVATE spdlog::spdlog)
```

**Использование:**
```cpp
#include <spdlog/spdlog.h>

spdlog::info("Backend loaded: {}", backend_name);
spdlog::error("Failed to load model: {}", error);
```

---

### 9. CLI11

**Описание:** Header-only библиотека для парсинга CLI аргументов.

**Установка:**

#### Git submodule
```bash
git submodule add https://github.com/CLIUtils/CLI11.git third_party/CLI11
```

#### Header-only
```bash
mkdir -p third_party/CLI11
cd third_party/CLI11
git clone https://github.com/CLIUtils/CLI11.git .
```

**Подключение в CMake:**
```cmake
# Header-only, просто include
find_path(CLI11_INCLUDE CLI11/CLI.hpp)
target_include_directories(nerd-dictation-ex PRIVATE ${CLI11_INCLUDE})

# Или через submodule
add_subdirectory(third_party/CLI11)
target_link_libraries(nerd-dictation-ex PRIVATE CLI11::CLI11)
```

**Использование:**
```cpp
#include <CLI11/CLI.hpp>

CLI::App app{"nerd-dictation-ex"};
std::string backend;
app.add_option("-b,--backend", backend, "Backend name");
CLI11_PARSE(app, argc, argv);
```

---

## Полный CMakeLists.txt пример

```cmake
cmake_minimum_required(VERSION 3.20)
project(nerd-dictation-ex VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Опционально: vcpkg toolchain
# set(CMAKE_TOOLCHAIN_FILE "${CMAKE_SOURCE_DIR}/vcpkg/scripts/buildsystems/vcpkg.cmake")

# ============================================================================
# Зависимости
# ============================================================================

# pkg-config для системных библиотек
find_package(PkgConfig REQUIRED)

# PipeWire
pkg_check_modules(PIPEWIRE REQUIRED libpipewire-0.3)

# PulseAudio (fallback)
pkg_check_modules(PULSE REQUIRED libpulse)

# Vosk
find_library(VOSK_LIB vosk PATHS /usr/lib /usr/local/lib)
find_path(VOSK_INCLUDE vosk/vosk.h PATHS /usr/include /usr/local/include)
if(NOT VOSK_LIB OR NOT VOSK_INCLUDE)
    message(WARNING "Vosk not found, Vosk backend will be disabled")
    set(VOSK_FOUND FALSE)
else()
    set(VOSK_FOUND TRUE)
endif()

# ONNX Runtime (опционально)
if(ENABLE_GIGAAM)
    if(DEFINED ENV{ONNXRUNTIME_ROOT})
        set(ONNXRUNTIME_ROOT $ENV{ONNXRUNTIME_ROOT})
    endif()
    
    find_path(ONNXRUNTIME_INCLUDE
        NAMES onnxruntime_cxx_api.h
        PATHS ${ONNXRUNTIME_ROOT}/include /usr/include
    )
    
    find_library(ONNXRUNTIME_LIB
        NAMES onnxruntime
        PATHS ${ONNXRUNTIME_ROOT}/lib /usr/lib
    )
    
    if(ONNXRUNTIME_INCLUDE AND ONNXRUNTIME_LIB)
        set(ONNXRUNTIME_FOUND TRUE)
    else()
        message(WARNING "ONNX Runtime not found, GigaAM backend will be disabled")
        set(ONNXRUNTIME_FOUND FALSE)
    endif()
endif()

# TOML11 (header-only)
find_path(TOML11_INCLUDE toml11/toml.hpp
    PATHS
        ${CMAKE_SOURCE_DIR}/third_party/toml11
        /usr/include
)

if(NOT TOML11_INCLUDE)
    # Попробуем скачать как submodule
    message(STATUS "TOML11 not found, trying submodule...")
    find_path(TOML11_INCLUDE toml11/toml.hpp
        PATHS ${CMAKE_SOURCE_DIR}/third_party/toml11
    )
endif()

# Eigen
find_package(Eigen3 REQUIRED)

# spdlog
find_package(spdlog CONFIG QUIET)
if(NOT spdlog_FOUND)
    # Fallback на submodule
    add_subdirectory(third_party/spdlog)
endif()

# CLI11 (header-only)
find_path(CLI11_INCLUDE CLI11/CLI.hpp
    PATHS
        ${CMAKE_SOURCE_DIR}/third_party/CLI11/include
        /usr/include
)

# ============================================================================
# Исходники
# ============================================================================

add_executable(nerd-dictation-ex
    src/main.cpp
    src/daemon/daemon.cpp
    src/audio/audio_capture.cpp
    src/audio/audio_buffer.cpp
    src/audio/noise_reduction.cpp
    src/backends/base.cpp
    src/input/input_simulator.cpp
    src/input/postprocess.cpp
    src/config/config.cpp
    src/utils/logger.cpp
)

# ============================================================================
# Линковка
# ============================================================================

target_link_libraries(nerd-dictation-ex
    PRIVATE
    ${PIPEWIRE_LIBRARIES}
    ${PULSE_LIBRARIES}
    Eigen3::Eigen
    spdlog::spdlog
)

target_include_directories(nerd-dictation-ex
    PRIVATE
    ${PIPEWIRE_INCLUDE_DIRS}
    ${PULSE_INCLUDE_DIRS}
    ${TOML11_INCLUDE}
    ${CLI11_INCLUDE}
    include
    src
)

target_compile_options(nerd-dictation-ex
    PRIVATE
    ${PIPEWIRE_CFLAGS_OTHER}
)

# Условная линковка Vosk
if(VOSK_FOUND)
    target_link_libraries(nerd-dictation-ex PRIVATE ${VOSK_LIB})
    target_include_directories(nerd-dictation-ex PRIVATE ${VOSK_INCLUDE})
    target_compile_definitions(nerd-dictation-ex PRIVATE ENABLE_VOSK_BACKEND)
endif()

# Условная линковка ONNX Runtime
if(ONNXRUNTIME_FOUND)
    target_link_libraries(nerd-dictation-ex PRIVATE ${ONNXRUNTIME_LIB})
    target_include_directories(nerd-dictation-ex PRIVATE ${ONNXRUNTIME_INCLUDE})
    target_compile_definitions(nerd-dictation-ex PRIVATE ENABLE_GIGAAM_BACKEND)
endif()

# ============================================================================
# Установка
# ============================================================================

install(TARGETS nerd-dictation-ex DESTINATION bin)
install(FILES config/default.toml DESTINATION share/nerd-dictation-ex)
```

---

## Скрипт установки зависимостей

Создайте `scripts/install-deps.sh`:

```bash
#!/bin/bash
set -e

echo "Installing dependencies for nerd-dictation-ex C++..."

# Arch Linux
if command -v pacman &> /dev/null; then
    echo "Detected Arch Linux"
    sudo pacman -S --needed \
        pipewire-pulse \
        libpipewire \
        libpulse \
        eigen \
        cmake \
        pkg-config \
        git
    
    # Vosk через AUR
    if command -v yay &> /dev/null; then
        yay -S vosk-api-bin
    else
        echo "Install yay and run: yay -S vosk-api-bin"
    fi
    
    # ONNX Runtime через AUR (опционально)
    if command -v yay &> /dev/null; then
        yay -S onnxruntime
    fi
fi

# Ubuntu/Debian
if command -v apt &> /dev/null; then
    echo "Detected Ubuntu/Debian"
    sudo apt update
    sudo apt install -y \
        libpipewire-0.3-dev \
        libpulse-dev \
        libeigen3-dev \
        cmake \
        pkg-config \
        git \
        build-essential
    
    echo "Install Vosk and ONNX Runtime manually from source or pre-built binaries"
fi

# Git submodules для header-only библиотек
echo "Setting up git submodules..."
git submodule update --init --recursive

echo "Dependencies installed!"
```

---

## Проверка зависимостей

Создайте `scripts/check-deps.sh`:

```bash
#!/bin/bash

echo "Checking dependencies..."

check_lib() {
    if pkg-config --exists "$1" 2>/dev/null; then
        echo "✓ $1 found"
        pkg-config --modversion "$1"
    else
        echo "✗ $1 not found"
    fi
}

check_lib "libpipewire-0.3"
check_lib "libpulse"

if [ -f /usr/lib/libvosk.so ] || [ -f /usr/local/lib/libvosk.so ]; then
    echo "✓ libvosk found"
else
    echo "✗ libvosk not found"
fi

if [ -n "$ONNXRUNTIME_ROOT" ]; then
    echo "✓ ONNX Runtime found at $ONNXRUNTIME_ROOT"
else
    echo "✗ ONNX Runtime not found (set ONNXRUNTIME_ROOT)"
fi

echo ""
echo "Header-only libraries (check manually):"
echo "  - toml11: third_party/toml11 or system"
echo "  - CLI11: third_party/CLI11 or system"
echo "  - spdlog: third_party/spdlog or system"
```

---

## Резюме

### Минимальные зависимости для базовой функциональности:
- ✅ libvosk
- ✅ libpipewire или libpulse
- ✅ toml11
- ✅ spdlog
- ✅ CLI11

### Для полной функциональности:
- ✅ + onnxruntime (GigaAM)
- ✅ + Eigen (шумоподавление)

### Рекомендуемый подход:
1. Использовать системные пакеты где возможно
2. Git submodules для header-only библиотек
3. Pre-built binaries для сложных библиотек (ONNX Runtime, LibTorch)
4. vcpkg как альтернатива для кроссплатформенности
