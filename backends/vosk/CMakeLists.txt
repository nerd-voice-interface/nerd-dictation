cmake_minimum_required(VERSION 3.20)
project(nerd-dictation-vosk VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ============================================================================
# Поиск зависимостей
# ============================================================================

# Vosk библиотека
find_library(VOSK_LIB
    NAMES vosk
    PATHS
        /usr/lib
        /usr/local/lib
        /usr/lib/x86_64-linux-gnu
)

find_path(VOSK_INCLUDE
    NAMES vosk/vosk.h
    PATHS
        /usr/include
        /usr/local/include
)

if(NOT VOSK_LIB OR NOT VOSK_INCLUDE)
    message(WARNING "Vosk library not found. Vosk backend will be disabled.")
    message(WARNING "  Install: yay -S vosk-api-bin (Arch Linux)")
    message(WARNING "  Or build from: https://github.com/alphacephei/vosk-api")
    set(VOSK_FOUND FALSE)
    return()
else()
    set(VOSK_FOUND TRUE)
    message(STATUS "Vosk found:")
    message(STATUS "  Library: ${VOSK_LIB}")
    message(STATUS "  Include: ${VOSK_INCLUDE}")
endif()

# Заголовки основного проекта (относительно backends/vosk/)
get_filename_component(MAIN_PROJECT_ROOT "${CMAKE_SOURCE_DIR}/../.." ABSOLUTE)
set(MAIN_PROJECT_INCLUDE "${MAIN_PROJECT_ROOT}/include")

# ============================================================================
# Исходные файлы
# ============================================================================

set(VOSK_SOURCES
    src/backend.cpp
)

set(VOSK_HEADERS
    include/nerd_dictation_vosk/backend.hpp
)

# ============================================================================
# Библиотека
# ============================================================================

add_library(nerd-dictation-vosk SHARED
    ${VOSK_SOURCES}
    ${VOSK_HEADERS}
)

target_include_directories(nerd-dictation-vosk
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${MAIN_PROJECT_INCLUDE}
    ${VOSK_INCLUDE}
)

target_link_libraries(nerd-dictation-vosk
    PUBLIC
    ${VOSK_LIB}
)

target_compile_definitions(nerd-dictation-vosk
    PUBLIC
    ENABLE_VOSK_BACKEND
)

# ============================================================================
# Установка
# ============================================================================

install(TARGETS nerd-dictation-vosk
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
)

install(FILES ${VOSK_HEADERS}
    DESTINATION include/nerd_dictation_vosk
)

# Экспорт символов для динамической библиотеки
set_target_properties(nerd-dictation-vosk PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    POSITION_INDEPENDENT_CODE ON
)
