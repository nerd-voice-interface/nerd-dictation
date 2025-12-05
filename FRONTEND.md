# Фронтенд nerd-dictation

## Обзор

Реализован полный фронтенд системы диктовки с dummy бэкендом для тестирования.

## Компоненты

### 1. CLI интерфейс
- `nerd-dictation daemon` - запуск daemon
- `nerd-dictation begin` - начать диктовку
- `nerd-dictation end` - завершить диктовку
- `nerd-dictation cancel` - отменить диктовку
- `nerd-dictation toggle` - переключить диктовку (begin/end)

### 2. Daemon
- Unix socket сервер для IPC (`$XDG_RUNTIME_DIR/nerd-dictation.sock`)
- Держит модель в памяти
- Обрабатывает команды от CLI клиента

### 3. Dummy бэкенд
- Генерирует случайный текст из предопределённого списка
- Симулирует потоковое распознавание
- Подходит для тестирования фронтенда без реальных моделей

### 4. Конфигурация
- Загрузка из TOML файла (`~/.config/nerd-dictation/config.toml`)
- Поддержка секций: `[general]`, `[audio]`, `[dummy]`

### 5. Симуляция ввода
- Поддержка методов: `ydotool`, `xdotool`, `wtype`, `dotool`, `stdout`
- Настраивается через `input_method` в конфигурации

### 6. Постобработка текста
- Замена слов на знаки препинания ("точка" → ".")
- Замена слов на команды клавиш ("выполнить" → Ctrl+Return)
- Автоматическая капитализация после точки/!/?

### 7. Звуки
- Воспроизведение звуков начала/окончания диктовки
- Звук выполнения команды

## Использование

### 1. Запуск daemon

```bash
nerd-dictation daemon
```

Daemon запустится и будет слушать команды через Unix socket.

### 2. Управление диктовкой

В другом терминале:

```bash
# Начать диктовку
nerd-dictation begin

# Завершить диктовку
nerd-dictation end

# Переключить (begin/end)
nerd-dictation toggle
```

### 3. Конфигурация

Создайте `~/.config/nerd-dictation/config.toml`:

```toml
[general]
backend = "dummy"
input_method = "stdout"  # или "ydotool", "xdotool", etc.

[audio]
source = "pw-cat"
sample_rate = 16000
chunk_size = 8000

[dummy]
model_path = "dummy"
```

## Тестирование

Dummy бэкенд генерирует случайный текст из списка:
- "привет мир"
- "это тестовый текст"
- "распознавание речи работает"
- "dummy бэкенд генерирует текст"
- "можно тестировать фронтенд"

Текст будет обработан постобработчиком и введён через выбранный метод.

## Структура кода

```
include/nerd_dictation/
├── types.hpp              # Общие типы (ASRResult)
├── config.hpp             # Конфигурация
├── backends/
│   ├── base.hpp           # Базовый интерфейс бэкенда
│   └── dummy_backend.hpp  # Dummy бэкенд
├── daemon/
│   └── daemon.hpp         # Daemon сервер
├── input/
│   ├── input_simulator.hpp  # Симуляция ввода
│   └── postprocess.hpp      # Постобработка текста
└── sounds/
    └── sounds.hpp         # Воспроизведение звуков

src/
├── main.cpp               # Точка входа, CLI
├── config/config.cpp      # Загрузка конфигурации
├── backends/dummy_backend.cpp
├── daemon/daemon.cpp
├── input/
│   ├── input_simulator.cpp
│   └── postprocess.cpp
└── sounds/sounds.cpp
```

## Следующие шаги

После тестирования фронтенда можно добавить реальные бэкенды:
- Vosk бэкенд (CPU, потоковый)
- GigaAM бэкенд (GPU, ONNX Runtime)
