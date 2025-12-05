# Краткая сводка: План миграции на C++

## Что это за проект?

**nerd-dictation-ex** — система диктовки речи для Linux:
- Распознавание речи (ASR) через Vosk или GigaAM
- Daemon режим с моделями в памяти
- Захват аудио с микрофона
- Автоматический ввод текста
- Шумоподавление и wake word detection

**Текущая реализация:** Python 3.10+
**Целевая реализация:** C++20

---

## Зачем переписывать на C++?

### Ожидаемые улучшения производительности:

| Метрика | Python | C++ | Улучшение |
|---------|--------|-----|-----------|
| Задержка начала диктовки | 50-100ms | 10-20ms | **5x быстрее** |
| Задержка распознавания | 200-500ms | 50-150ms | **3-4x быстрее** |
| Потребление памяти | 500MB-2GB | 200MB-1GB | **2x меньше** |
| CPU использование | 20-40% | 5-15% | **3-4x эффективнее** |

### Дополнительные преимущества:
- ✅ Прямые биндинги к C библиотекам (Vosk, ONNX Runtime)
- ✅ Нативная многопоточность
- ✅ Лучший контроль над памятью
- ✅ Меньше зависимостей от Python экосистемы

---

## Архитектура C++ версии

```
nerd-dictation-ex-cpp/
├── src/
│   ├── main.cpp              # CLI точка входа
│   ├── daemon/               # Daemon сервер
│   ├── audio/                 # Захват аудио (PipeWire/PulseAudio)
│   ├── backends/              # ASR бэкенды (Vosk, GigaAM)
│   ├── input/                 # Симуляция ввода + постобработка
│   ├── config/                # Парсинг TOML конфигурации
│   └── utils/                 # Утилиты (логирование)
├── include/                   # Публичные заголовки
├── tests/                     # Unit тесты
└── CMakeLists.txt
```

---

## Основные компоненты

### 1. Аудио захват
- **PipeWire** (основной) или **PulseAudio** (fallback)
- Lock-free кольцевой буфер для минимальной задержки
- Прямой доступ к C API (без subprocess)

### 2. ASR Бэкенды

#### Vosk бэкенд
- Прямые вызовы `libvosk` C API
- Минимальные накладные расходы
- Потоковое распознавание

#### GigaAM бэкенд
- **ONNX Runtime** (рекомендуется) или **LibTorch**
- GPU ускорение через CUDA
- Batch processing для эффективности

### 3. Daemon
- Unix socket для IPC
- Многопоточность: аудио + распознавание + wake word
- Эффективная синхронизация

### 4. Шумоподавление
- Spectral gating (нативный C++)
- Опционально: интеграция noisereduce C++ версии

---

## Зависимости и их подключение

### Критические зависимости:

1. **libvosk** — Vosk ASR библиотека
   ```bash
   # Arch Linux
   yay -S vosk-api-bin
   ```

2. **onnxruntime** — Для GigaAM (ONNX модели)
   ```bash
   # Pre-built binaries с GitHub releases
   # Или через AUR: yay -S onnxruntime
   ```

3. **libpipewire** / **libpulse** — Аудио захват
   ```bash
   sudo pacman -S pipewire-pulse libpipewire libpulse
   ```

4. **toml11** — Парсинг конфигурации (header-only)
   ```bash
   git submodule add https://github.com/ToruNiina/toml11.git third_party/toml11
   ```

### Вспомогательные:

5. **Eigen** — Линейная алгебра (шумоподавление)
6. **spdlog** — Логирование
7. **CLI11** — CLI парсинг (header-only)

### Менеджер зависимостей:

**Рекомендуется:** vcpkg или системные пакеты + git submodules

Подробности в файле `CPP_DEPENDENCIES.md`

---

## План миграции (8-12 недель)

### Этап 1: Базовая инфраструктура (1-2 недели)
- ✅ CMake проект
- ✅ Зависимости
- ✅ Конфигурация (TOML)
- ✅ Базовый CLI

### Этап 2: Аудио захват (1 неделя)
- ✅ PipeWire/PulseAudio интеграция
- ✅ Кольцевой буфер

### Этап 3: Vosk бэкенд (1 неделя)
- ✅ Интеграция libvosk
- ✅ Потоковое распознавание

### Этап 4: Ввод и постобработка (3-5 дней)
- ✅ Симуляция ввода (ydotool/xdotool)
- ✅ Постобработка текста

### Этап 5: Daemon (1 неделя)
- ✅ Unix socket server
- ✅ Многопоточность

### Этап 6: Шумоподавление (3-5 дней)
- ✅ Spectral gating

### Этап 7: GigaAM бэкенд (1-2 недели)
- ✅ ONNX Runtime интеграция
- ✅ GPU поддержка

### Этап 8: Wake Word (1 неделя)
- ✅ Silero VAD + Vosk small

### Этап 9: Оптимизация (1-2 недели)
- ✅ Профилирование
- ✅ Оптимизация hot paths

---

## Пример CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.20)
project(nerd-dictation-ex VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)

# Зависимости
find_package(PkgConfig REQUIRED)
pkg_check_modules(PIPEWIRE REQUIRED libpipewire-0.3)
pkg_check_modules(PULSE REQUIRED libpulse)

find_library(VOSK_LIB vosk)
find_path(VOSK_INCLUDE vosk/vosk.h)

# Header-only библиотеки
find_path(TOML11_INCLUDE toml11/toml.hpp PATHS third_party/toml11)
find_package(Eigen3 REQUIRED)
find_package(spdlog CONFIG REQUIRED)

# Исходники
add_executable(nerd-dictation-ex
    src/main.cpp
    src/daemon/daemon.cpp
    src/audio/audio_capture.cpp
    # ...
)

target_link_libraries(nerd-dictation-ex
    PRIVATE
    ${PIPEWIRE_LIBRARIES}
    ${PULSE_LIBRARIES}
    ${VOSK_LIB}
    Eigen3::Eigen
    spdlog::spdlog
)

target_include_directories(nerd-dictation-ex
    PRIVATE
    ${PIPEWIRE_INCLUDE_DIRS}
    ${VOSK_INCLUDE}
    ${TOML11_INCLUDE}
    include
)
```

---

## Быстрый старт

### 1. Установка зависимостей (Arch Linux)

```bash
# Системные пакеты
sudo pacman -S pipewire-pulse libpipewire libpulse eigen cmake pkg-config

# Vosk (AUR)
yay -S vosk-api-bin

# ONNX Runtime (AUR, опционально)
yay -S onnxruntime

# Header-only библиотеки (git submodules)
git submodule add https://github.com/ToruNiina/toml11.git third_party/toml11
git submodule add https://github.com/gabime/spdlog.git third_party/spdlog
git submodule add https://github.com/CLIUtils/CLI11.git third_party/CLI11
```

### 2. Сборка проекта

```bash
mkdir build && cd build
cmake ..
make -j$(nproc)
```

### 3. Запуск

```bash
# Daemon
./nerd-dictation-ex daemon

# Диктовка
./nerd-dictation-ex begin
./nerd-dictation-ex end
```

---

## Ключевые оптимизации

1. **Lock-free ring buffer** для аудио буфера
2. **Zero-copy** передача данных где возможно
3. **Batch processing** для ASR инференса
4. **Pre-allocation** памяти для избежания аллокаций в hot paths
5. **SIMD** инструкции для обработки аудио (где применимо)

---

## Совместимость

C++ версия будет совместима с Python версией:
- ✅ Тот же формат конфигурации (`config.toml`)
- ✅ Те же CLI команды
- ✅ Те же пути к моделям
- ✅ Совместимость с systemd service

Можно использовать обе версии параллельно во время миграции.

---

## Документация

- **CPP_MIGRATION_PLAN.md** — Детальный план миграции
- **CPP_DEPENDENCIES.md** — Подробности по зависимостям
- **CPP_MIGRATION_SUMMARY_RU.md** — Этот файл (краткая сводка)

---

## Следующие шаги

1. ✅ Изучить план миграции (`CPP_MIGRATION_PLAN.md`)
2. ✅ Изучить зависимости (`CPP_DEPENDENCIES.md`)
3. 🔲 Настроить CMake проект
4. 🔲 Интегрировать зависимости
5. 🔲 Начать реализацию по этапам

---

## Вопросы?

Основные риски и их решения описаны в `CPP_MIGRATION_PLAN.md`.

**Общее время разработки:** 8-12 недель (зависит от опыта команды)
