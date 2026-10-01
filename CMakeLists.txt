cmake_minimum_required(VERSION 3.20)
project(nerd-dictation
    VERSION 1.0.0
    DESCRIPTION "Advanced speech dictation for Linux with pluggable ASR backends"
    LANGUAGES ASM C CXX
)

# ============================================================================
# Installation Configuration
# ============================================================================

# Local installation by default
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "${CMAKE_SOURCE_DIR}/install" CACHE PATH "Install directory" FORCE)
    message(STATUS "CMAKE_INSTALL_PREFIX set to: ${CMAKE_INSTALL_PREFIX}")
endif()

# ============================================================================
# Compiler Settings
# ============================================================================

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Warnings
if(MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Optimization for Release
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")

# ============================================================================
# Build Options
# ============================================================================

option(ENABLE_TESTS "Build tests" OFF)
option(BUILD_SHARED_LIBS "Build shared libraries" OFF)

# ============================================================================
# Paths for ThirdParty submodules
# ============================================================================

set(THIRDPARTY_DIR "${CMAKE_SOURCE_DIR}/ThirdParty")

# ============================================================================
# Finding System Dependencies via pkg-config
# ============================================================================

find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)

# PipeWire
pkg_check_modules(PIPEWIRE REQUIRED libpipewire-0.3)
if(PIPEWIRE_FOUND)
    message(STATUS "PipeWire found: ${PIPEWIRE_VERSION}")
    message(STATUS "  Include dirs: ${PIPEWIRE_INCLUDE_DIRS}")
    message(STATUS "  Libraries: ${PIPEWIRE_LIBRARIES}")
else()
    message(FATAL_ERROR "PipeWire not found. Install: sudo pacman -S libpipewire")
endif()

# PulseAudio Simple API (for sound playback)
pkg_check_modules(PULSE_SIMPLE libpulse-simple)
if(PULSE_SIMPLE_FOUND)
    message(STATUS "PulseAudio Simple API found: ${PULSE_SIMPLE_VERSION}")
    add_definitions(-DHAVE_PULSEAUDIO)
else()
    message(WARNING "PulseAudio Simple API not found, sound notifications will be disabled")
endif()

# WebRTC Audio Processing (for AGC and noise suppression)
pkg_check_modules(WEBRTC_APM REQUIRED webrtc-audio-processing-1)
if(WEBRTC_APM_FOUND)
    message(STATUS "WebRTC Audio Processing found: ${WEBRTC_APM_VERSION}")
else()
    message(FATAL_ERROR "WebRTC Audio Processing not found. Install: sudo pacman -S webrtc-audio-processing-1")
endif()

# libsamplerate (for audio resampling)
pkg_check_modules(SAMPLERATE REQUIRED samplerate)
if(SAMPLERATE_FOUND)
    message(STATUS "libsamplerate found: ${SAMPLERATE_VERSION}")
else()
    message(FATAL_ERROR "libsamplerate not found. Install: sudo pacman -S libsamplerate")
endif()

# XCB + XInput2 (mouse button trigger owned by the daemon)
pkg_check_modules(XCB_XINPUT xcb xcb-xinput)
if(XCB_XINPUT_FOUND)
    message(STATUS "xcb-xinput found: button trigger enabled")
else()
    message(WARNING "xcb-xinput not found: button trigger disabled. Install: sudo pacman -S libxcb")
endif()

# ONNX Runtime (for Silero VAD)
find_path(ONNXRUNTIME_INCLUDE_DIR
    NAMES onnxruntime/core/session/onnxruntime_cxx_api.h
    PATHS /usr/include /usr/local/include
)
find_library(ONNXRUNTIME_LIBRARY
    NAMES onnxruntime
    PATHS /usr/lib /usr/local/lib
)
if(ONNXRUNTIME_INCLUDE_DIR AND ONNXRUNTIME_LIBRARY)
    message(STATUS "ONNX Runtime found:")
    message(STATUS "  Include: ${ONNXRUNTIME_INCLUDE_DIR}")
    message(STATUS "  Library: ${ONNXRUNTIME_LIBRARY}")
    set(ONNXRUNTIME_FOUND TRUE)
else()
    message(WARNING "ONNX Runtime not found. VAD will be disabled.")
    message(WARNING "Install: sudo pacman -S onnxruntime")
    set(ONNXRUNTIME_FOUND FALSE)
endif()

# Vosk (for wake word detection)
find_library(VOSK_LIBRARY
    NAMES vosk
    PATHS /usr/lib /usr/local/lib
)
if(VOSK_LIBRARY)
    message(STATUS "Vosk found: ${VOSK_LIBRARY}")
    set(VOSK_FOUND TRUE)
else()
    message(WARNING "Vosk not found. Wake word detection will be disabled.")
    message(WARNING "Install: sudo pacman -S vosk-api")
    set(VOSK_FOUND FALSE)
endif()

# ============================================================================
# Git Submodules from ThirdParty
# ============================================================================

# TOML11 (header-only)
if(EXISTS "${THIRDPARTY_DIR}/toml11")
    message(STATUS "Using TOML11 from ThirdParty submodule")
    # toml11 has structure: include/toml.hpp or include/toml11/toml.hpp
    if(EXISTS "${THIRDPARTY_DIR}/toml11/include/toml11/toml.hpp")
        set(TOML11_INCLUDE "${THIRDPARTY_DIR}/toml11/include")
    elseif(EXISTS "${THIRDPARTY_DIR}/toml11/include/toml.hpp")
        set(TOML11_INCLUDE "${THIRDPARTY_DIR}/toml11/include")
    else()
        set(TOML11_INCLUDE "${THIRDPARTY_DIR}/toml11")
    endif()
    set(TOML11_FOUND TRUE)
else()
    # Try to find system version
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
    # Try to find via find_package
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
    # Try to find system version
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

# Eigen (required, for noise suppression)
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

# res_embed (for embedding sound files)
if(EXISTS "${THIRDPARTY_DIR}/res_embed")
    message(STATUS "Using res_embed from ThirdParty submodule")
    add_subdirectory(${THIRDPARTY_DIR}/res_embed)
    set(RES_EMBED_FOUND TRUE)
else()
    message(FATAL_ERROR "res_embed not found. Initialize submodule:")
    message(FATAL_ERROR "  git submodule add https://github.com/dmikushin/res_embed.git ThirdParty/res_embed")
    message(FATAL_ERROR "  git submodule update --init --recursive")
endif()

# pybind11 (for Python embedding)
if(EXISTS "${THIRDPARTY_DIR}/pybind11")
    message(STATUS "Using pybind11 from ThirdParty submodule")
    find_package(Python3 REQUIRED COMPONENTS Interpreter Development)
    add_subdirectory(${THIRDPARTY_DIR}/pybind11)
    set(PYBIND11_FOUND TRUE)
    message(STATUS "Python3 found: ${Python3_VERSION}")
    message(STATUS "  Interpreter: ${Python3_EXECUTABLE}")
    message(STATUS "  Include dirs: ${Python3_INCLUDE_DIRS}")
    message(STATUS "  Libraries: ${Python3_LIBRARIES}")
else()
    message(FATAL_ERROR "pybind11 not found. Initialize submodule:")
    message(FATAL_ERROR "  git submodule add https://github.com/pybind/pybind11.git ThirdParty/pybind11")
    message(FATAL_ERROR "  git submodule update --init --recursive")
endif()

# nerd-dictation-api (Backend C API)
if(EXISTS "${THIRDPARTY_DIR}/nerd-dictation-api")
    message(STATUS "Using nerd-dictation-api from ThirdParty submodule")
    set(NERD_DICTATION_API_INCLUDE "${THIRDPARTY_DIR}/nerd-dictation-api/include")
    set(NERD_DICTATION_API_FOUND TRUE)
else()
    message(FATAL_ERROR "nerd-dictation-api not found. Initialize submodule:")
    message(FATAL_ERROR "  git submodule add https://github.com/nerd-voice-interface/nerd-dictation-api.git ThirdParty/nerd-dictation-api")
    message(FATAL_ERROR "  git submodule update --init --recursive")
endif()

# JamSpell (context-aware spell correction)
# Note: We build JamSpell sources directly to avoid CMake conflicts with their global flags
option(ENABLE_JAMSPELL "Enable JamSpell spell correction" ON)
if(ENABLE_JAMSPELL AND EXISTS "${THIRDPARTY_DIR}/jamspell")
    message(STATUS "Using JamSpell from ThirdParty submodule")

    set(JAMSPELL_DIR "${THIRDPARTY_DIR}/jamspell")

    # Build cityhash library
    add_library(jamspell_cityhash STATIC
        ${JAMSPELL_DIR}/contrib/cityhash/city.cc
    )
    target_include_directories(jamspell_cityhash PUBLIC
        ${JAMSPELL_DIR}/contrib/cityhash
    )
    set_target_properties(jamspell_cityhash PROPERTIES
        POSITION_INDEPENDENT_CODE ON
        CXX_STANDARD 11
    )

    # Build phf (perfect hash function) library
    add_library(jamspell_phf STATIC
        ${JAMSPELL_DIR}/contrib/phf/phf.cc
    )
    target_include_directories(jamspell_phf PUBLIC
        ${JAMSPELL_DIR}/contrib/phf
    )
    # Disable pedantic warnings for non-standard label address usage
    target_compile_options(jamspell_phf PRIVATE -Wno-pedantic -Wno-implicit-fallthrough)
    set_target_properties(jamspell_phf PROPERTIES
        POSITION_INDEPENDENT_CODE ON
        CXX_STANDARD 11
    )

    # Build main JamSpell library
    add_library(jamspell_lib STATIC
        ${JAMSPELL_DIR}/jamspell/spell_corrector.cpp
        ${JAMSPELL_DIR}/jamspell/lang_model.cpp
        ${JAMSPELL_DIR}/jamspell/utils.cpp
        ${JAMSPELL_DIR}/jamspell/perfect_hash.cpp
        ${JAMSPELL_DIR}/jamspell/bloom_filter.cpp
    )
    target_include_directories(jamspell_lib PUBLIC
        ${JAMSPELL_DIR}
        ${JAMSPELL_DIR}/contrib
    )
    target_link_libraries(jamspell_lib
        PUBLIC jamspell_phf jamspell_cityhash
        PRIVATE Threads::Threads
    )
    # JamSpell needs uint64_t from <cstdint>, force-include it
    # Also disable pedantic warnings for non-standard label address usage
    target_compile_options(jamspell_lib PRIVATE
        -include cstdint
        -Wno-pedantic
    )
    set_target_properties(jamspell_lib PROPERTIES
        POSITION_INDEPENDENT_CODE ON
        CXX_STANDARD 11
    )

    set(JAMSPELL_FOUND TRUE)
else()
    if(ENABLE_JAMSPELL)
        message(WARNING "JamSpell not found. Spell correction will be disabled.")
        message(WARNING "Initialize submodule:")
        message(WARNING "  git submodule add https://github.com/bakwc/JamSpell.git ThirdParty/jamspell")
        message(WARNING "  git submodule update --init --recursive")
    endif()
    set(JAMSPELL_FOUND FALSE)
endif()

# ============================================================================
# Project Structure
# ============================================================================

set(INCLUDE_DIR "${CMAKE_SOURCE_DIR}/include")
set(SOURCE_DIR "${CMAKE_SOURCE_DIR}/src")

# Create directories if they don't exist
file(MAKE_DIRECTORY ${INCLUDE_DIR})
file(MAKE_DIRECTORY ${SOURCE_DIR})

# ============================================================================
# Source Files
# ============================================================================

set(SOURCES
    ${SOURCE_DIR}/main.cpp
    ${SOURCE_DIR}/config/config.cpp
    ${SOURCE_DIR}/input/input_simulator.cpp
    ${SOURCE_DIR}/input/postprocess.cpp
    ${SOURCE_DIR}/sounds/sounds.cpp
    ${SOURCE_DIR}/daemon/daemon.cpp
    ${SOURCE_DIR}/audio/audio_preprocessor.cpp
    ${SOURCE_DIR}/postprocess/text_postprocessor.cpp
)

# VAD Manager (requires ONNX Runtime)
if(ONNXRUNTIME_FOUND)
    list(APPEND SOURCES ${SOURCE_DIR}/vad/vad_manager.cpp)
endif()

# Wake Word Manager (requires Vosk)
if(VOSK_FOUND)
    list(APPEND SOURCES ${SOURCE_DIR}/wake_word/wake_word_manager.cpp)
endif()

# Button Trigger (requires XCB + XInput2)
if(XCB_XINPUT_FOUND)
    list(APPEND SOURCES ${SOURCE_DIR}/trigger/button_trigger.cpp)
endif()

# Spell Corrector (requires JamSpell)
if(JAMSPELL_FOUND)
    list(APPEND SOURCES ${SOURCE_DIR}/spelling/spell_corrector.cpp)
endif()

set(HEADERS
    ${INCLUDE_DIR}/nerd_dictation/types.hpp
    ${INCLUDE_DIR}/nerd_dictation/config.hpp
    ${INCLUDE_DIR}/nerd_dictation/backend_wrapper.hpp
    ${INCLUDE_DIR}/nerd_dictation/input/input_simulator.hpp
    ${INCLUDE_DIR}/nerd_dictation/input/postprocess.hpp
    ${INCLUDE_DIR}/nerd_dictation/sounds/sounds.hpp
    ${INCLUDE_DIR}/nerd_dictation/daemon/daemon.hpp
    ${INCLUDE_DIR}/nerd_dictation/audio/audio_preprocessor.hpp
    ${INCLUDE_DIR}/nerd_dictation/postprocess/text_postprocessor.hpp
)

# VAD Manager header (requires ONNX Runtime)
if(ONNXRUNTIME_FOUND)
    list(APPEND HEADERS ${INCLUDE_DIR}/nerd_dictation/vad/vad_manager.hpp)
endif()

# Wake Word Manager header (requires Vosk)
if(VOSK_FOUND)
    list(APPEND HEADERS ${INCLUDE_DIR}/nerd_dictation/wake_word/wake_word_manager.hpp)
endif()

# Button Trigger header (requires XCB + XInput2)
if(XCB_XINPUT_FOUND)
    list(APPEND HEADERS ${INCLUDE_DIR}/nerd_dictation/trigger/button_trigger.hpp)
endif()

# Spell Corrector header (requires JamSpell)
if(JAMSPELL_FOUND)
    list(APPEND HEADERS ${INCLUDE_DIR}/nerd_dictation/spelling/spell_corrector.hpp)
endif()

# ============================================================================
# Executable
# ============================================================================

add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS})

# ============================================================================
# Dummy Backend (built-in test backend)
# ============================================================================

add_library(nerd-dictation-dummy SHARED
    ${SOURCE_DIR}/backends/dummy_backend.cpp
)

target_include_directories(nerd-dictation-dummy
    PRIVATE
    ${INCLUDE_DIR}
)

# nerd-dictation-api for dummy backend
if(NERD_DICTATION_API_FOUND)
    target_include_directories(nerd-dictation-dummy PRIVATE ${NERD_DICTATION_API_INCLUDE})
endif()

set_target_properties(nerd-dictation-dummy PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    POSITION_INDEPENDENT_CODE ON
)

install(TARGETS nerd-dictation-dummy
    LIBRARY DESTINATION lib
)

# ============================================================================
# Embedded Resources (sound files)
# ============================================================================

if(RES_EMBED_FOUND)
    res_embed(TARGET ${PROJECT_NAME} NAME "sound_begin" PATH ${CMAKE_SOURCE_DIR}/sounds/begin.wav KEYWORD)
    res_embed(TARGET ${PROJECT_NAME} NAME "sound_end" PATH ${CMAKE_SOURCE_DIR}/sounds/end.wav KEYWORD)
    res_embed(TARGET ${PROJECT_NAME} NAME "sound_execute" PATH ${CMAKE_SOURCE_DIR}/sounds/execute.wav KEYWORD)
    target_compile_definitions(${PROJECT_NAME} PRIVATE HAVE_EMBEDDED_SOUNDS=1)
    message(STATUS "Sound files will be embedded into binary")
else()
    target_compile_definitions(${PROJECT_NAME} PRIVATE HAVE_EMBEDDED_SOUNDS=0)
    message(STATUS "Sound files will be loaded from filesystem")
endif()

# ============================================================================
# Include Directories
# ============================================================================

target_include_directories(${PROJECT_NAME}
    PRIVATE
    ${INCLUDE_DIR}
    ${SOURCE_DIR}
    ${PIPEWIRE_INCLUDE_DIRS}
    ${PULSE_INCLUDE_DIRS}
    ${WEBRTC_APM_INCLUDE_DIRS}
    ${SAMPLERATE_INCLUDE_DIRS}
)

# TOML11 (header-only)
if(TOML11_FOUND)
    target_include_directories(${PROJECT_NAME} PRIVATE ${TOML11_INCLUDE})
endif()

# CLI11 (header-only)
if(CLI11_FOUND)
    target_include_directories(${PROJECT_NAME} PRIVATE ${CLI11_INCLUDE})
endif()

# nerd-dictation-api (header-only)
if(NERD_DICTATION_API_FOUND)
    target_include_directories(${PROJECT_NAME} PRIVATE ${NERD_DICTATION_API_INCLUDE})
endif()

# ============================================================================
# Library Linking
# ============================================================================

target_link_libraries(${PROJECT_NAME}
    PRIVATE
    ${PIPEWIRE_LIBRARIES}
)

target_compile_options(${PROJECT_NAME}
    PRIVATE
    ${PIPEWIRE_CFLAGS_OTHER}
)

# PulseAudio Simple API (for sound notifications)
if(PULSE_SIMPLE_FOUND)
    target_link_libraries(${PROJECT_NAME} PRIVATE ${PULSE_SIMPLE_LIBRARIES})
    target_include_directories(${PROJECT_NAME} PRIVATE ${PULSE_SIMPLE_INCLUDE_DIRS})
endif()

# WebRTC Audio Processing (for AGC and noise suppression)
target_link_libraries(${PROJECT_NAME} PRIVATE ${WEBRTC_APM_LIBRARIES})
target_compile_options(${PROJECT_NAME} PRIVATE ${WEBRTC_APM_CFLAGS_OTHER})

# libsamplerate (for resampling)
target_link_libraries(${PROJECT_NAME} PRIVATE ${SAMPLERATE_LIBRARIES})

# spdlog
if(SPDLOG_FOUND)
    if(TARGET spdlog::spdlog)
        target_link_libraries(${PROJECT_NAME} PRIVATE spdlog::spdlog)
    endif()
endif()

# Eigen (required)
if(TARGET Eigen3::Eigen)
    target_link_libraries(${PROJECT_NAME} PRIVATE Eigen3::Eigen)
else()
    # Fallback for system Eigen
    find_package(Eigen3 REQUIRED)
    target_include_directories(${PROJECT_NAME} PRIVATE ${EIGEN3_INCLUDE_DIR})
endif()
target_compile_definitions(${PROJECT_NAME} PRIVATE HAVE_EIGEN)

# Python/pybind11 (for embedded Python processing)
if(PYBIND11_FOUND)
    target_link_libraries(${PROJECT_NAME} PRIVATE pybind11::embed)
    target_compile_definitions(${PROJECT_NAME} PRIVATE HAVE_PYTHON)
    # Set Python scripts directory and venv path
    target_compile_definitions(${PROJECT_NAME} PRIVATE
        PYTHON_SCRIPTS_DIR="${CMAKE_SOURCE_DIR}"
        PYTHON_VENV_SITE_PACKAGES="${CMAKE_SOURCE_DIR}/.venv/lib/python3.14/site-packages"
    )
endif()

# ONNX Runtime (for Silero VAD)
if(ONNXRUNTIME_FOUND)
    target_include_directories(${PROJECT_NAME} PRIVATE ${ONNXRUNTIME_INCLUDE_DIR})
    target_link_libraries(${PROJECT_NAME} PRIVATE ${ONNXRUNTIME_LIBRARY})
    target_compile_definitions(${PROJECT_NAME} PRIVATE HAVE_ONNXRUNTIME)
endif()

# Vosk (for wake word detection)
if(VOSK_FOUND)
    target_link_libraries(${PROJECT_NAME} PRIVATE ${VOSK_LIBRARY})
    target_compile_definitions(${PROJECT_NAME} PRIVATE HAVE_VOSK)
endif()

# XCB + XInput2 (button trigger)
if(XCB_XINPUT_FOUND)
    target_link_libraries(${PROJECT_NAME} PRIVATE ${XCB_XINPUT_LIBRARIES})
    target_include_directories(${PROJECT_NAME} PRIVATE ${XCB_XINPUT_INCLUDE_DIRS})
    target_compile_definitions(${PROJECT_NAME} PRIVATE HAVE_XCB_XINPUT)
endif()

# JamSpell (for spell correction)
if(JAMSPELL_FOUND)
    target_link_libraries(${PROJECT_NAME} PRIVATE jamspell_lib)
    target_compile_definitions(${PROJECT_NAME} PRIVATE HAVE_JAMSPELL)
endif()

# ============================================================================
# Installation
# ============================================================================

# Install executable
install(TARGETS ${PROJECT_NAME} DESTINATION bin)

# Install configuration files (if any)
# install(FILES config/default.toml DESTINATION share/nerd-dictation)

# Install headers (for development)
install(DIRECTORY ${INCLUDE_DIR}/nerd_dictation
    DESTINATION include
    FILES_MATCHING PATTERN "*.hpp"
)

# Sound files are embedded into binary via res_embed
# (separate file installation not required)

# ============================================================================
# Systemd Service
# ============================================================================

# Configure systemd service file with install prefix substitution
# Use absolute path for CMAKE_INSTALL_PREFIX
get_filename_component(ABS_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}" ABSOLUTE)
set(ABS_INSTALL_PREFIX "${ABS_INSTALL_PREFIX}")  # Ensure variable is set
configure_file(
    ${CMAKE_SOURCE_DIR}/systemd/nerd-dictation.service.in
    ${CMAKE_BINARY_DIR}/nerd-dictation.service
    @ONLY
    NEWLINE_STYLE UNIX
)

# Install systemd service file (only if not system installation)
if(NOT CMAKE_INSTALL_PREFIX STREQUAL "/usr" AND 
   NOT CMAKE_INSTALL_PREFIX STREQUAL "/usr/local")
    # Local installation - to lib/systemd/user
    install(FILES ${CMAKE_BINARY_DIR}/nerd-dictation.service
        DESTINATION lib/systemd/user
    )
    message(STATUS "Systemd service will be installed to: ${CMAKE_INSTALL_PREFIX}/lib/systemd/user/")
else()
    # System installation - to /usr/lib/systemd/user
    install(FILES ${CMAKE_BINARY_DIR}/nerd-dictation.service
        DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/systemd/user
    )
endif()

# ============================================================================
# ASR Backends (late binding via dlopen)
# ============================================================================
#
# IMPORTANT: Backends are NOT linked at compile time!
# They are loaded dynamically via dlopen() at runtime.
#
# Backends are built separately as shared libraries:
#   - libnerd-dictation-t-one.so   (T-one CTC, 8kHz)
#   - libnerd-dictation-gigaam.so  (GigaAM v3 CTC, 16kHz)
#   - libnerd-dictation-vosk.so    (Vosk/Kaldi)
#
# Install backends to /usr/lib or specify path in config.
#

# libdl is needed for dlopen
target_link_libraries(${PROJECT_NAME} PRIVATE ${CMAKE_DL_LIBS})

# ============================================================================
# Tests (if enabled)
# ============================================================================

if(ENABLE_TESTS)
    enable_testing()
    
    # GoogleTest submodule
    if(EXISTS "${THIRDPARTY_DIR}/googletest")
        message(STATUS "Using GoogleTest from ThirdParty submodule")
        add_subdirectory(${THIRDPARTY_DIR}/googletest)
        set(GTEST_FOUND TRUE)
    else()
        message(FATAL_ERROR "GoogleTest not found. Initialize submodule:")
        message(FATAL_ERROR "  git submodule add https://github.com/google/googletest.git ThirdParty/googletest")
        message(FATAL_ERROR "  git submodule update --init --recursive")
    endif()
    
    add_subdirectory(tests)
endif()

# ============================================================================
# Summary Information
# ============================================================================

message(STATUS "")
message(STATUS "=== Configuration Summary ===")
message(STATUS "Project: ${PROJECT_NAME} ${PROJECT_VERSION}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "Install prefix: ${CMAKE_INSTALL_PREFIX}")
message(STATUS "")
message(STATUS "Core Dependencies:")
message(STATUS "  PipeWire: ${PIPEWIRE_VERSION}")
message(STATUS "  PulseAudio Simple: ${PULSE_SIMPLE_FOUND}")
message(STATUS "  TOML11: ${TOML11_FOUND}")
message(STATUS "  spdlog: ${SPDLOG_FOUND}")
message(STATUS "  CLI11: ${CLI11_FOUND}")
message(STATUS "  Eigen: ${EIGEN_FOUND}")
message(STATUS "  res_embed: ${RES_EMBED_FOUND} (embedded sounds)")
message(STATUS "")
message(STATUS "Voice Activity Detection:")
if(ONNXRUNTIME_FOUND)
    message(STATUS "  VAD Manager: ENABLED (Silero VAD via ONNX Runtime)")
    message(STATUS "  ONNX Runtime: ${ONNXRUNTIME_LIBRARY}")
else()
    message(STATUS "  VAD Manager: DISABLED (ONNX Runtime not found)")
endif()
message(STATUS "")
message(STATUS "Wake Word Detection:")
if(VOSK_LIBRARY)
    message(STATUS "  Vosk: ENABLED (${VOSK_LIBRARY})")
else()
    message(STATUS "  Vosk: DISABLED (library not found)")
endif()
message(STATUS "")
message(STATUS "Spell Correction:")
if(JAMSPELL_FOUND)
    message(STATUS "  JamSpell: ENABLED (context-aware n-gram spell correction)")
else()
    message(STATUS "  JamSpell: DISABLED (submodule not found)")
endif()
message(STATUS "")
message(STATUS "ASR Backends (loaded via dlopen at runtime):")
message(STATUS "  - libnerd-dictation-dummy.so   (built-in test backend)")
message(STATUS "  - libnerd-dictation-t-one.so   (T-one streaming CTC, 8kHz)")
message(STATUS "  - libnerd-dictation-gigaam.so  (GigaAM v3 offline CTC, 16kHz)")
message(STATUS "  - Install external backends with: sudo pacman -U <package>.pkg.tar.zst")
message(STATUS "")
message(STATUS "============================")
message(STATUS "")
