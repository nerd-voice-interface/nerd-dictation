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
      -DENABLE_VOSK_BACKEND=ON \
      -DENABLE_GIGAAM_BACKEND=OFF \
      -DENABLE_TESTS=OFF \
      ..
```

### Опции:

- `CMAKE_BUILD_TYPE`: `Debug`, `Release`, `RelWithDebInfo`
- `ENABLE_VOSK_BACKEND`: Включить Vosk бэкенд (по умолчанию ON)
- `ENABLE_GIGAAM_BACKEND`: Включить GigaAM бэкенд (по умолчанию OFF)
- `ENABLE_TESTS`: Собрать тесты (по умолчанию OFF)

## Структура зависимостей

### Системные (через pkg-config)
- `libpipewire-0.3` — **обязательно**
- `libpulse` — опционально (fallback)

### Git Submodules (ThirdParty/)
- `toml11` — **обязательно**
- `spdlog` — **обязательно**
- `CLI11` — **обязательно**
- `eigen` — опционально (для продвинутого шумоподавления)

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

### Ошибка: "Vosk not found"
Vosk бэкенд опционален. Если не нужен, отключите:
```bash
cmake -DENABLE_VOSK_BACKEND=OFF ..
```

### Ошибка: "ONNX Runtime not found"
GigaAM бэкенд опционален. По умолчанию отключён.

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
