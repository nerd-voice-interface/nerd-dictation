# Инструкции по сборке

## Быстрый старт

### 1. Установка системных зависимостей

#### Arch Linux
```bash
sudo pacman -S pipewire-pulse libpipewire libpulse cmake pkg-config

# Vosk (AUR)
yay -S vosk-api-bin

# ONNX Runtime (опционально, для GigaAM)
yay -S onnxruntime
```

#### Ubuntu/Debian
```bash
sudo apt update
sudo apt install -y libpipewire-0.3-dev libpulse-dev cmake pkg-config build-essential
```

### 2. Инициализация git submodules

```bash
# Автоматически через скрипт
./scripts/init-submodules.sh

# Или вручную
git submodule add https://github.com/ToruNiina/toml11.git ThirdParty/toml11
git submodule add https://github.com/gabime/spdlog.git ThirdParty/spdlog
git submodule add https://github.com/CLIUtils/CLI11.git ThirdParty/CLI11
git submodule add https://gitlab.com/libeigen/eigen.git ThirdParty/eigen

git submodule update --init --recursive
```

### 3. Сборка проекта

```bash
mkdir build && cd build
cmake ..
make -j$(nproc)
```

### 4. Запуск

```bash
./nerd-dictation-ex --help
```

## Опции сборки

```bash
cmake -DCMAKE_BUILD_TYPE=Release \
      -DENABLE_TESTS=OFF \
      ..
```

### Опции:

- `CMAKE_BUILD_TYPE`: `Debug`, `Release`, `RelWithDebInfo`
- `ENABLE_TESTS`: Собрать тесты (по умолчанию OFF)

**Примечание:** Опции бэкендов (Vosk, GigaAM) обрабатываются в отдельных CMake файлах для каждого бэкенда.

## Структура зависимостей

### Системные (через pkg-config)
- `libpipewire-0.3` — **обязательно**
- `libpulse` — опционально (fallback)

### Git Submodules (ThirdParty/)
- `toml11` — **обязательно**
- `spdlog` — **обязательно**
- `CLI11` — **обязательно**
- `eigen` — **обязательно** (для шумоподавления)

### Опциональные системные
- `libvosk` — для Vosk бэкенда
- `onnxruntime` — для GigaAM бэкенда

## Устранение проблем

### Ошибка: "PipeWire not found"
```bash
sudo pacman -S libpipewire  # Arch Linux
sudo apt install libpipewire-0.3-dev  # Ubuntu/Debian
```

### Ошибка: "TOML11 not found"
```bash
./scripts/init-submodules.sh
# или
git submodule update --init --recursive
```

### Ошибка: "Eigen not found"
```bash
# Установить системный пакет
sudo pacman -S eigen  # Arch Linux
sudo apt install libeigen3-dev  # Ubuntu/Debian

# Или инициализировать submodule
./scripts/init-submodules.sh
```

## Проверка конфигурации

После запуска `cmake ..` проверьте вывод:

```
=== Configuration Summary ===
Project: nerd-dictation-ex 1.0.0
Build type: Release
C++ standard: 20

Backends:
  Vosk: ON
  GigaAM: OFF

Dependencies:
  PipeWire: 0.3.x
  PulseAudio: YES
  TOML11: YES
  spdlog: YES
  CLI11: YES
  Eigen: YES
============================
```
