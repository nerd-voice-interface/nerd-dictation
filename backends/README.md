# Бэкенды nerd-dictation-ex

Каждый бэкенд — независимый CMake-проект со своими зависимостями.

## Структура

```
backends/
├── vosk/          # Vosk бэкенд (CPU, потоковый)
│   ├── CMakeLists.txt
│   ├── include/
│   │   └── nerd_dictation_vosk/
│   │       └── backend.hpp
│   └── src/
│       └── backend.cpp
└── gigaam/        # GigaAM бэкенд (GPU, ONNX Runtime)
    ├── CMakeLists.txt
    ├── include/
    │   └── nerd_dictation_gigaam/
    │       └── backend.hpp
    └── src/
        └── backend.cpp
```

## Использование

### Подключение в основном проекте

В основном `CMakeLists.txt`:

```cmake
# Vosk бэкенд
if(ENABLE_VOSK_BACKEND)
    add_subdirectory(backends/vosk)
    target_link_libraries(nerd-dictation-ex PRIVATE nerd-dictation-vosk)
endif()

# GigaAM бэкенд
if(ENABLE_GIGAAM_BACKEND)
    add_subdirectory(backends/gigaam)
    target_link_libraries(nerd-dictation-ex PRIVATE nerd-dictation-gigaam)
endif()
```

### Создание бэкенда в коде

```cpp
#include "nerd_dictation_vosk/backend.hpp"

auto backend = nerd_dictation::backends::create_vosk_backend();
backend->load_model("/path/to/model");
```

## Vosk бэкенд

**Зависимости:**
- `libvosk` (C библиотека)

**Установка:**
```bash
# Arch Linux
yay -S vosk-api-bin

# Или из исходников
git clone https://github.com/alphacephei/vosk-api
cd vosk-api/c
make
sudo make install
```

**Особенности:**
- Потоковое распознавание
- CPU только
- Низкая задержка

## GigaAM бэкенд

**Зависимости:**
- `onnxruntime` (C++ API)

**Установка:**
```bash
# Скачать pre-built binaries
wget https://github.com/microsoft/onnxruntime/releases/download/v1.16.3/onnxruntime-linux-x64-1.16.3.tgz
tar -xzf onnxruntime-linux-x64-1.16.3.tgz
export ONNXRUNTIME_ROOT=/path/to/onnxruntime-linux-x64-1.16.3

# Или через AUR (Arch Linux)
yay -S onnxruntime
```

**Особенности:**
- Batch обработка
- Поддержка GPU через CUDA
- Высокое качество распознавания

## Создание нового бэкенда

1. Создайте директорию `backends/my_backend/`
2. Создайте `CMakeLists.txt` с поиском зависимостей
3. Реализуйте класс, наследующий `ASRBackend`
4. Создайте функцию `create_my_backend()`
5. Подключите в основном проекте через `add_subdirectory`

Пример структуры:

```cpp
// backends/my_backend/include/nerd_dictation_mybackend/backend.hpp
#include "nerd_dictation/backends/base.hpp"

namespace nerd_dictation::backends {
    std::unique_ptr<ASRBackend> create_my_backend();
}
```
