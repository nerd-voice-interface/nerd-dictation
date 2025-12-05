# Бэкенды nerd-dictation

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
# Vosk бэкенд (ОБЯЗАТЕЛЕН для wake word detection)
add_subdirectory(backends/vosk)
target_link_libraries(nerd-dictation PRIVATE nerd-dictation-vosk)

# GigaAM бэкенд (опциональный)
if(ENABLE_GIGAAM_BACKEND)
    add_subdirectory(backends/gigaam)
    target_link_libraries(nerd-dictation PRIVATE nerd-dictation-gigaam)
endif()
```

**Важно:**
- **Vosk всегда включён** - требуется для wake word detection
- GigaAM опционален - используется только для основного распознавания
- Даже при использовании GigaAM, Vosk нужен для активации по слову "запись"

**Примечание:** Бэкенды собираются как динамические библиотеки (`.so` файлы), что позволяет:
- Загружать их во время выполнения
- Обновлять без пересборки основного проекта
- Использовать плагинную архитектуру в будущем

### Создание бэкенда в коде

```cpp
#include "nerd_dictation_vosk/backend.hpp"

auto backend = nerd_dictation::backends::create_vosk_backend();
backend->load_model("/path/to/model");
```

## Vosk бэкенд (ОБЯЗАТЕЛЕН)

**Статус:** Обязательный компонент

**Причина:** Требуется для wake word detection (активация по слову "запись")

**Зависимости:**
- `libvosk` (C библиотека) - **обязательна**

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

**Использование:**
1. **Wake word detection:** Использует Vosk small модель (`vosk-model-small-ru`)
   - Детектирует слово "запись" для активации диктовки
   - Работает постоянно в фоне через отдельный поток
   - Использует Silero VAD для детекции речи

2. **Основное распознавание:** Может использоваться как основной бэкенд
   - Потоковое распознавание
   - CPU только
   - Низкая задержка

**Особенности:**
- Всегда включён (требуется для wake word)
- Может использоваться как основной бэкенд для диктовки
- Для wake word используется маленькая модель (~45MB)

## GigaAM бэкенд (ОПЦИОНАЛЕН)

**Статус:** Опциональный компонент

**Причина:** Используется только для основного распознавания речи (не для wake word)

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
- Batch обработка (не потоковый)
- Поддержка GPU через CUDA
- Высокое качество распознавания (~5-7% WER для русского)
- С пунктуацией (e2e модели)

**Примечание:** Даже если используется GigaAM как основной бэкенд, Vosk всё равно требуется для wake word detection.

## GigaAM-ONNX бэкенд (ОПЦИОНАЛЕН, планируется)

Отдельный вариант GigaAM бэкенда с оптимизацией через ONNX Runtime.

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
