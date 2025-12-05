# nerd-dictation-ex (C++)

C++ реализация системы диктовки речи для Linux с модульной архитектурой и подключаемыми бэкендами распознавания речи.

## Статус проекта

🚧 **В разработке** — проект находится на стадии планирования и начальной реализации.

## Быстрый старт

### Предварительные требования

- CMake 3.20+
- C++20 компилятор (GCC 10+, Clang 12+)
- Системные библиотеки: PipeWire, PulseAudio

### Установка зависимостей (Arch Linux)

```bash
# Системные пакеты
sudo pacman -S pipewire-pulse libpipewire libpulse cmake pkg-config

# Vosk (AUR)
yay -S vosk-api-bin

# ONNX Runtime (опционально, для GigaAM)
yay -S onnxruntime
```

### Инициализация git submodules

```bash
# Клонируем header-only библиотеки в ThirdParty
git submodule add https://github.com/ToruNiina/toml11.git ThirdParty/toml11
git submodule add https://github.com/gabime/spdlog.git ThirdParty/spdlog
git submodule add https://github.com/CLIUtils/CLI11.git ThirdParty/CLI11
git submodule add https://gitlab.com/libeigen/eigen.git ThirdParty/eigen

# Инициализация всех submodules
git submodule update --init --recursive
```

### Сборка

```bash
mkdir build && cd build
cmake ..
make -j$(nproc)
```

### Запуск

```bash
./nerd-dictation-ex --help
```

## Структура проекта

```
nerd-dictation/
├── CMakeLists.txt          # Главный CMake файл
├── README.md               # Этот файл
├── ThirdParty/             # Git submodules
│   ├── toml11/            # TOML парсер
│   ├── spdlog/            # Логирование
│   ├── CLI11/             # CLI парсинг
│   └── eigen/             # Линейная алгебра
├── include/                # Публичные заголовки
│   └── nerd_dictation/
├── src/                    # Исходный код
│   └── main.cpp
└── tests/                  # Тесты (опционально)
```

## Документация

- [CPP_MIGRATION_PLAN.md](CPP_MIGRATION_PLAN.md) — Детальный план миграции
- [CPP_DEPENDENCIES.md](CPP_DEPENDENCIES.md) — Подробности по зависимостям
- [CPP_MIGRATION_SUMMARY_RU.md](CPP_MIGRATION_SUMMARY_RU.md) — Краткая сводка
- [CPP_CODE_EXAMPLES.md](CPP_CODE_EXAMPLES.md) — Примеры кода

## Зависимости

### Системные (через pkg-config)
- **libpipewire-0.3** — Аудио захват (основной)
- **libpulse** — Аудио захват (fallback)
- **libvosk** — Vosk ASR бэкенд (опционально)

### Git Submodules (ThirdParty/)
- **toml11** — Парсинг TOML конфигурации
- **spdlog** — Логирование
- **CLI11** — CLI парсинг
- **eigen** — Линейная алгебра (обязательно, для шумоподавления)

### Опциональные
- **onnxruntime** — Для GigaAM бэкенда

## Опции сборки

```bash
cmake -DENABLE_VOSK_BACKEND=ON \
      -DENABLE_GIGAAM_BACKEND=OFF \
      -DENABLE_TESTS=OFF \
      ..
```

## Лицензия

GPL-3.0 (как оригинальный nerd-dictation)
