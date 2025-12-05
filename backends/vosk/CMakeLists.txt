cmake_minimum_required(VERSION 3.20)
project(nerd-dictation-vosk VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ============================================================================
# Поиск зависимостей
# ============================================================================

# Vosk библиотека (обязательна)
# Сначала пробуем через pkg-config, если доступно
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(VOSK_PKG QUIET vosk-api)
endif()

if(VOSK_PKG_FOUND)
    # Используем pkg-config результаты
    set(VOSK_LIB ${VOSK_PKG_LIBRARIES})
    set(VOSK_INCLUDE ${VOSK_PKG_INCLUDE_DIRS})
    message(STATUS "Vosk found via pkg-config (required for wake word detection):")
    message(STATUS "  Library: ${VOSK_LIB}")
    message(STATUS "  Include: ${VOSK_INCLUDE}")
else()
    # Fallback: поиск вручную в расширенном списке путей
    # Проверяем переменные окружения для нестандартных путей
    if(DEFINED ENV{VOSK_LIB_PATH})
        list(APPEND VOSK_LIB_SEARCH_PATHS $ENV{VOSK_LIB_PATH})
    endif()
    if(DEFINED ENV{VOSK_INCLUDE_PATH})
        list(APPEND VOSK_INCLUDE_SEARCH_PATHS $ENV{VOSK_INCLUDE_PATH})
    endif()
    
    # Стандартные пути
    list(APPEND VOSK_LIB_SEARCH_PATHS
        /usr/lib
        /usr/local/lib
        /usr/lib/x86_64-linux-gnu
        /usr/lib64
        /opt/vosk/lib
        ${CMAKE_INSTALL_PREFIX}/lib
    )
    
    list(APPEND VOSK_INCLUDE_SEARCH_PATHS
        /usr/include
        /usr/local/include
        /opt/vosk/include
        ${CMAKE_INSTALL_PREFIX}/include
    )
    
    find_library(VOSK_LIB
        NAMES vosk
        PATHS ${VOSK_LIB_SEARCH_PATHS}
        NO_DEFAULT_PATH
    )
    
    # Если не найдено в указанных путях, пробуем системный поиск
    if(NOT VOSK_LIB)
        find_library(VOSK_LIB NAMES vosk)
    endif()

    find_path(VOSK_INCLUDE
        NAMES vosk/vosk.h
        PATHS ${VOSK_INCLUDE_SEARCH_PATHS}
        NO_DEFAULT_PATH
    )
    
    # Если не найдено в указанных путях, пробуем системный поиск
    if(NOT VOSK_INCLUDE)
        find_path(VOSK_INCLUDE NAMES vosk/vosk.h)
    endif()
    
    # Проверяем результат и выдаём понятную ошибку если не найдено
    if(NOT VOSK_LIB OR NOT VOSK_INCLUDE)
        message(FATAL_ERROR "\n"
            "Vosk library is REQUIRED but not found!\n"
            "\n"
            "Vosk is required for wake word detection.\n"
            "\n"
            "Installation options:\n"
            "  Arch Linux:     yay -S vosk-api-bin\n"
            "  Ubuntu/Debian:  sudo apt install libvosk-dev (if available)\n"
            "  From source:    https://github.com/alphacephei/vosk-api\n"
            "\n"
            "If Vosk is installed in a non-standard location, set:\n"
            "  VOSK_LIB_PATH=/path/to/lib\n"
            "  VOSK_INCLUDE_PATH=/path/to/include\n"
            "\n"
            "Searched paths:\n"
            "  Libraries: /usr/lib, /usr/local/lib, /usr/lib/x86_64-linux-gnu, /usr/lib64, /opt/vosk/lib\n"
            "  Headers:   /usr/include, /usr/local/include, /opt/vosk/include\n"
        )
    endif()
    
    message(STATUS "Vosk found (required for wake word detection):")
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

# Vosk backend всегда включен, определение не нужно

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
