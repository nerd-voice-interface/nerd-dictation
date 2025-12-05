cmake_minimum_required(VERSION 3.20)
project(nerd-dictation-ex 
    VERSION 1.0.0 
    DESCRIPTION "Расширенная диктовка для Linux с подключаемыми бэкендами ASR"
    LANGUAGES CXX
)

# ============================================================================
# Настройки компилятора
# ============================================================================

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Предупреждения
if(MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Оптимизация для Release
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")

# ============================================================================
# Опции сборки
# ============================================================================

option(ENABLE_TESTS "Build tests" OFF)
option(BUILD_SHARED_LIBS "Build shared libraries" OFF)

# ============================================================================
# Пути для ThirdParty submodules
# ============================================================================

set(THIRDPARTY_DIR "${CMAKE_SOURCE_DIR}/ThirdParty")

# ============================================================================
# Поиск системных зависимостей через pkg-config
# ============================================================================

find_package(PkgConfig REQUIRED)

# PipeWire
pkg_check_modules(PIPEWIRE REQUIRED libpipewire-0.3)
if(PIPEWIRE_FOUND)
    message(STATUS "PipeWire found: ${PIPEWIRE_VERSION}")
    message(STATUS "  Include dirs: ${PIPEWIRE_INCLUDE_DIRS}")
    message(STATUS "  Libraries: ${PIPEWIRE_LIBRARIES}")
else()
    message(FATAL_ERROR "PipeWire not found. Install: sudo pacman -S libpipewire")
endif()

# PulseAudio (fallback)
pkg_check_modules(PULSE libpulse)
if(PULSE_FOUND)
    message(STATUS "PulseAudio found: ${PULSE_VERSION}")
    add_definitions(-DHAVE_PULSEAUDIO)
else()
    message(WARNING "PulseAudio not found, only PipeWire will be available")
endif()

# ============================================================================
# Git Submodules из ThirdParty
# ============================================================================

# TOML11 (header-only)
if(EXISTS "${THIRDPARTY_DIR}/toml11")
    message(STATUS "Using TOML11 from ThirdParty submodule")
    # toml11 имеет структуру: include/toml.hpp или include/toml11/toml.hpp
    if(EXISTS "${THIRDPARTY_DIR}/toml11/include/toml11/toml.hpp")
        set(TOML11_INCLUDE "${THIRDPARTY_DIR}/toml11/include")
    elseif(EXISTS "${THIRDPARTY_DIR}/toml11/include/toml.hpp")
        set(TOML11_INCLUDE "${THIRDPARTY_DIR}/toml11/include")
    else()
        set(TOML11_INCLUDE "${THIRDPARTY_DIR}/toml11")
    endif()
    set(TOML11_FOUND TRUE)
else()
    # Попробуем найти системную версию
    find_path(TOML11_INCLUDE
        NAMES toml11/toml.hpp
        PATHS
            /usr/include
            /usr/local/include
    )
    if(TOML11_INCLUDE)
        message(STATUS "Using system TOML11: ${TOML11_INCLUDE}")
        set(TOML11_FOUND TRUE)
    else()
        message(FATAL_ERROR "TOML11 not found. Initialize submodule:")
        message(FATAL_ERROR "  git submodule add https://github.com/ToruNiina/toml11.git ThirdParty/toml11")
        message(FATAL_ERROR "  git submodule update --init --recursive")
    endif()
endif()

# spdlog
if(EXISTS "${THIRDPARTY_DIR}/spdlog")
    message(STATUS "Using spdlog from ThirdParty submodule")
    add_subdirectory(${THIRDPARTY_DIR}/spdlog)
    set(SPDLOG_FOUND TRUE)
else()
    # Попробуем найти через find_package
    find_package(spdlog CONFIG QUIET)
    if(spdlog_FOUND)
        message(STATUS "Using system spdlog")
        set(SPDLOG_FOUND TRUE)
    else()
        message(WARNING "spdlog not found. Initialize submodule:")
        message(WARNING "  git submodule add https://github.com/gabime/spdlog.git ThirdParty/spdlog")
        message(WARNING "  git submodule update --init --recursive")
        message(FATAL_ERROR "spdlog is required")
    endif()
endif()

# CLI11 (header-only)
if(EXISTS "${THIRDPARTY_DIR}/CLI11")
    message(STATUS "Using CLI11 from ThirdParty submodule")
    set(CLI11_INCLUDE "${THIRDPARTY_DIR}/CLI11/include")
    set(CLI11_FOUND TRUE)
else()
    # Попробуем найти системную версию
    find_path(CLI11_INCLUDE
        NAMES CLI/CLI.hpp
        PATHS
            /usr/include
            /usr/local/include
    )
    if(CLI11_INCLUDE)
        message(STATUS "Using system CLI11: ${CLI11_INCLUDE}")
        set(CLI11_FOUND TRUE)
    else()
        message(FATAL_ERROR "CLI11 not found. Initialize submodule:")
        message(FATAL_ERROR "  git submodule add https://github.com/CLIUtils/CLI11.git ThirdParty/CLI11")
        message(FATAL_ERROR "  git submodule update --init --recursive")
    endif()
endif()

# Eigen (обязательно, для шумоподавления)
if(EXISTS "${THIRDPARTY_DIR}/eigen")
    message(STATUS "Using Eigen from ThirdParty submodule")
    add_subdirectory(${THIRDPARTY_DIR}/eigen)
    set(EIGEN_FOUND TRUE)
else()
    find_package(Eigen3 REQUIRED)
    if(Eigen3_FOUND)
        message(STATUS "Using system Eigen3")
        set(EIGEN_FOUND TRUE)
    else()
        message(FATAL_ERROR "Eigen not found. Initialize submodule:")
        message(FATAL_ERROR "  git submodule add https://gitlab.com/libeigen/eigen.git ThirdParty/eigen")
        message(FATAL_ERROR "  git submodule update --init --recursive")
        message(FATAL_ERROR "Or install system package: sudo pacman -S eigen")
    endif()
endif()

# ============================================================================
# Структура проекта
# ============================================================================

set(INCLUDE_DIR "${CMAKE_SOURCE_DIR}/include")
set(SOURCE_DIR "${CMAKE_SOURCE_DIR}/src")

# Создаём директории если их нет
file(MAKE_DIRECTORY ${INCLUDE_DIR})
file(MAKE_DIRECTORY ${SOURCE_DIR})

# ============================================================================
# Исходные файлы
# ============================================================================

set(SOURCES
    ${SOURCE_DIR}/main.cpp
    ${SOURCE_DIR}/config/config.cpp
    ${SOURCE_DIR}/backends/dummy_backend.cpp
    ${SOURCE_DIR}/input/input_simulator.cpp
    ${SOURCE_DIR}/input/postprocess.cpp
    ${SOURCE_DIR}/sounds/sounds.cpp
    ${SOURCE_DIR}/daemon/daemon.cpp
)

set(HEADERS
    ${INCLUDE_DIR}/nerd_dictation/types.hpp
    ${INCLUDE_DIR}/nerd_dictation/config.hpp
    ${INCLUDE_DIR}/nerd_dictation/backends/base.hpp
    ${INCLUDE_DIR}/nerd_dictation/backends/dummy_backend.hpp
    ${INCLUDE_DIR}/nerd_dictation/input/input_simulator.hpp
    ${INCLUDE_DIR}/nerd_dictation/input/postprocess.hpp
    ${INCLUDE_DIR}/nerd_dictation/sounds/sounds.hpp
    ${INCLUDE_DIR}/nerd_dictation/daemon/daemon.hpp
)

# ============================================================================
# Исполняемый файл
# ============================================================================

add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS})

# ============================================================================
# Include директории
# ============================================================================

target_include_directories(${PROJECT_NAME}
    PRIVATE
    ${INCLUDE_DIR}
    ${SOURCE_DIR}
    ${PIPEWIRE_INCLUDE_DIRS}
    ${PULSE_INCLUDE_DIRS}
)

# TOML11 (header-only)
if(TOML11_FOUND)
    target_include_directories(${PROJECT_NAME} PRIVATE ${TOML11_INCLUDE})
endif()

# CLI11 (header-only)
if(CLI11_FOUND)
    target_include_directories(${PROJECT_NAME} PRIVATE ${CLI11_INCLUDE})
endif()

# ============================================================================
# Линковка библиотек
# ============================================================================

target_link_libraries(${PROJECT_NAME}
    PRIVATE
    ${PIPEWIRE_LIBRARIES}
)

target_compile_options(${PROJECT_NAME}
    PRIVATE
    ${PIPEWIRE_CFLAGS_OTHER}
)

# PulseAudio (если найден)
if(PULSE_FOUND)
    target_link_libraries(${PROJECT_NAME} PRIVATE ${PULSE_LIBRARIES})
    target_include_directories(${PROJECT_NAME} PRIVATE ${PULSE_INCLUDE_DIRS})
endif()

# spdlog
if(SPDLOG_FOUND)
    if(TARGET spdlog::spdlog)
        target_link_libraries(${PROJECT_NAME} PRIVATE spdlog::spdlog)
    endif()
endif()

# Eigen (обязательно)
if(TARGET Eigen3::Eigen)
    target_link_libraries(${PROJECT_NAME} PRIVATE Eigen3::Eigen)
else()
    # Fallback для системного Eigen
    find_package(Eigen3 REQUIRED)
    target_include_directories(${PROJECT_NAME} PRIVATE ${EIGEN3_INCLUDE_DIR})
endif()
target_compile_definitions(${PROJECT_NAME} PRIVATE HAVE_EIGEN)

# ============================================================================
# Установка
# ============================================================================

install(TARGETS ${PROJECT_NAME} DESTINATION bin)

# ============================================================================
# Тесты (если включены)
# ============================================================================

if(ENABLE_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ============================================================================
# Итоговая информация
# ============================================================================

message(STATUS "")
message(STATUS "=== Configuration Summary ===")
message(STATUS "Project: ${PROJECT_NAME} ${PROJECT_VERSION}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "")
message(STATUS "Core Dependencies:")
message(STATUS "  PipeWire: ${PIPEWIRE_VERSION}")
message(STATUS "  PulseAudio: ${PULSE_FOUND}")
message(STATUS "  TOML11: ${TOML11_FOUND}")
message(STATUS "  spdlog: ${SPDLOG_FOUND}")
message(STATUS "  CLI11: ${CLI11_FOUND}")
message(STATUS "  Eigen: ${EIGEN_FOUND}")
message(STATUS "")
message(STATUS "Note: Backend dependencies (Vosk, ONNX Runtime) are handled")
message(STATUS "      by their respective backend CMake files.")
message(STATUS "============================")
message(STATUS "")
