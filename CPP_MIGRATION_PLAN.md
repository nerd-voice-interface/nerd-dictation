# План миграции nerd-dictation-ex на C++

## Обзор проекта

**nerd-dictation-ex** — система диктовки речи для Linux с модульной архитектурой:
- Daemon режим с моделями в памяти
- Подключаемые бэкенды ASR (Vosk, GigaAM)
- Захват аудио (PipeWire/PulseAudio)
- Симуляция ввода (ydotool/xdotool)
- Шумоподавление
- Wake word detection
- Постобработка текста

## Цели миграции на C++

1. **Производительность**: Минимизация задержек, оптимизация обработки аудио
2. **Память**: Эффективное управление памятью для больших моделей
3. **Многопоточность**: Нативная поддержка параллельной обработки
4. **Интеграция**: Прямые биндинги к C/C++ библиотекам (Vosk, ONNX Runtime, PyTorch C++)

---

## Архитектура C++ проекта

### Структура директорий

```
nerd-dictation-ex-cpp/
├── CMakeLists.txt                    # Главный CMake файл
├── README.md
├── LICENSE
├── cmake/                            # CMake модули
│   ├── FindVosk.cmake
│   ├── FindONNXRuntime.cmake
│   └── FindLibTorch.cmake
├── include/                          # Публичные заголовки
│   └── nerd_dictation/
│       ├── backend.hpp               # Интерфейс бэкенда
│       ├── audio.hpp                 # Аудио захват
│       ├── input.hpp                 # Симуляция ввода
│       ├── config.hpp                # Конфигурация
│       ├── daemon.hpp                # Daemon
│       └── types.hpp                 # Общие типы
├── src/                              # Исходный код
│   ├── main.cpp                      # Точка входа CLI
│   ├── daemon/
│   │   ├── daemon.cpp
│   │   ├── daemon.hpp
│   │   └── socket_server.cpp
│   ├── audio/
│   │   ├── audio_capture.cpp         # PipeWire/PulseAudio захват
│   │   ├── audio_buffer.cpp          # Кольцевой буфер
│   │   └── noise_reduction.cpp       # Шумоподавление
│   ├── backends/
│   │   ├── base.cpp                  # Базовый класс
│   │   ├── vosk_backend.cpp          # Vosk бэкенд
│   │   └── gigaam_backend.cpp        # GigaAM бэкенд (ONNX/PyTorch)
│   ├── input/
│   │   ├── input_simulator.cpp       # ydotool/xdotool/wtype
│   │   └── postprocess.cpp           # Постобработка текста
│   ├── config/
│   │   └── config.cpp                # TOML парсинг
│   └── utils/
│       ├── logger.cpp
│       └── signals.cpp
├── backends/                         # Плагины бэкендов (опционально)
│   ├── vosk/
│   │   ├── CMakeLists.txt
│   │   └── src/
│   └── gigaam/
│       ├── CMakeLists.txt
│       └── src/
├── tests/                            # Unit тесты
│   ├── CMakeLists.txt
│   ├── test_audio.cpp
│   ├── test_backend.cpp
│   └── test_postprocess.cpp
└── third_party/                      # Субмодули (опционально)
    └── toml11/                       # TOML парсер
```

---

## Компоненты и их C++ реализации

### 1. Конфигурация (config/)

**Зависимости:**
- `toml11` (header-only TOML парсер) или `cpptoml`

**Реализация:**
```cpp
// include/nerd_dictation/config.hpp
namespace nerd_dictation {
    struct AudioConfig {
        std::string source = "pw-cat";
        int sample_rate = 16000;
        size_t chunk_size = 8000;
    };
    
    struct VoskConfig {
        std::string model_path;
    };
    
    struct GigaAMConfig {
        std::string model_name = "v3_ctc";
        std::string device = "cuda";
    };
    
    struct Config {
        std::string backend = "vosk";
        std::string input_method = "ydotool";
        AudioConfig audio;
        VoskConfig vosk;
        GigaAMConfig gigaam;
        // ...
    };
    
    Config load_config(const std::filesystem::path& path);
}
```

**Производительность:**
- Парсинг один раз при старте
- Использование `std::string_view` где возможно
- Кэширование конфигурации в памяти

---

### 2. Аудио захват (audio/)

**Зависимости:**
- **PipeWire**: `libpipewire-0.3` (C API)
- **PulseAudio**: `libpulse` (C API)
- **ALSA**: `libasound` (fallback)

**Реализация:**
```cpp
// include/nerd_dictation/audio.hpp
namespace nerd_dictation::audio {
    class AudioCapture {
    public:
        AudioCapture(const AudioConfig& config);
        ~AudioCapture();
        
        // Неблокирующее чтение
        std::optional<std::vector<int16_t>> read_chunk();
        
        // Callback-based (для PipeWire)
        void set_callback(std::function<void(const int16_t*, size_t)> cb);
        
    private:
        // PipeWire/PulseAudio handle
        void* handle_;
        AudioConfig config_;
    };
    
    // Кольцевой буфер (lock-free где возможно)
    template<typename T>
    class RingBuffer {
    public:
        RingBuffer(size_t capacity);
        bool push(const T* data, size_t count);
        bool pop(T* data, size_t count);
        size_t available() const;
    };
}
```

**Производительность:**
- Lock-free ring buffer для минимальной задержки
- Прямой доступ к PipeWire/PulseAudio API (без subprocess)
- Zero-copy где возможно
- Использование `std::vector` с pre-allocation

**Оптимизации:**
- Memory-mapped буферы для аудио
- SIMD для обработки (если нужно)
- Batch processing чанков

---

### 3. Шумоподавление (audio/noise_reduction.cpp)

**Зависимости:**
- `Eigen` или нативный C++ для спектральной обработки
- Опционально: `libtorch` для продвинутых методов

**Реализация:**
```cpp
namespace nerd_dictation::audio {
    class NoiseReducer {
    public:
        enum class Method {
            None,
            SpectralGating,
            Noisereduce  // Если интегрируем C++ версию
        };
        
        NoiseReducer(Method method, int sample_rate);
        
        // Обработка чанка (in-place где возможно)
        void process_chunk(int16_t* audio, size_t samples);
        
        void reset();  // Сброс профиля шума
        
    private:
        Method method_;
        int sample_rate_;
        std::vector<float> noise_profile_;
        bool profile_collected_ = false;
        
        void collect_noise_profile(const int16_t* audio, size_t samples);
        void apply_spectral_gating(int16_t* audio, size_t samples);
    };
}
```

**Производительность:**
- In-place обработка (избегаем копирования)
- Использование `std::vector<float>` для спектральных операций
- SIMD инструкции для FFT/фильтрации (через библиотеки)

---

### 4. Бэкенды ASR (backends/)

#### 4.1 Базовый интерфейс

```cpp
// include/nerd_dictation/backend.hpp
namespace nerd_dictation {
    struct ASRResult {
        std::string text;
        bool is_final;
        float confidence = 0.0f;
    };
    
    class ASRBackend {
    public:
        virtual ~ASRBackend() = default;
        
        virtual void load_model(const std::string& path) = 0;
        
        // Потоковое распознавание (callback-based)
        virtual void transcribe_stream(
            std::function<void(const int16_t*, size_t)> audio_callback,
            std::function<void(const ASRResult&)> result_callback
        ) = 0;
        
        virtual ASRResult transcribe_file(const std::filesystem::path& path) = 0;
        virtual ASRResult finalize() = 0;
        virtual void reset() = 0;
        
        virtual bool supports_streaming() const = 0;
        virtual std::string name() const = 0;
    };
}
```

#### 4.2 Vosk бэкенд

**Зависимости:**
- `libvosk` (C API, уже на C++)

**Реализация:**
```cpp
// src/backends/vosk_backend.cpp
#include <vosk/vosk.h>

class VoskBackend : public ASRBackend {
private:
    vosk_model_t* model_ = nullptr;
    vosk_recognizer_t* recognizer_ = nullptr;
    int sample_rate_ = 16000;
    
public:
    void load_model(const std::string& path) override {
        model_ = vosk_model_new(path.c_str());
        recognizer_ = vosk_recognizer_new(model_, sample_rate_);
    }
    
    void transcribe_stream(...) override {
        // Прямой вызов Vosk C API
        // Минимальные накладные расходы
    }
};
```

**Производительность:**
- Прямые вызовы C API (без Python bindings)
- Переиспользование модели в daemon режиме
- Zero-copy передача аудио данных

#### 4.3 GigaAM бэкенд

**Зависимости:**
- **ONNX Runtime**: `onnxruntime` (C++ API) — **рекомендуется**
- Или **LibTorch**: `libtorch` (PyTorch C++)

**Реализация (ONNX Runtime):**
```cpp
// src/backends/gigaam_backend.cpp
#include <onnxruntime/core/session/onnxruntime_cxx_api.h>

class GigaAMBackend : public ASRBackend {
private:
    Ort::Env env_;
    Ort::Session session_;
    Ort::MemoryInfo memory_info_;
    std::vector<const char*> input_names_;
    std::vector<const char*> output_names_;
    
public:
    void load_model(const std::string& path) override {
        Ort::SessionOptions session_options;
        session_options.SetIntraOpNumThreads(4);
        session_options.SetGraphOptimizationLevel(
            GraphOptimizationLevel::ORT_ENABLE_ALL
        );
        
        session_ = Ort::Session(env_, path.c_str(), session_options);
        // Настройка входов/выходов
    }
    
    void transcribe_stream(...) override {
        // Batch обработка через ONNX Runtime
        // GPU ускорение через CUDA provider
    }
};
```

**Производительность:**
- ONNX Runtime оптимизирован для инференса
- Поддержка GPU через CUDA/TensorRT providers
- Batch processing для эффективности
- Оптимизация графа ONNX

**Альтернатива (LibTorch):**
```cpp
#include <torch/torch.h>

class GigaAMBackendLibTorch : public ASRBackend {
private:
    torch::jit::script::Module model_;
    torch::Device device_;
    
public:
    void load_model(const std::string& path) override {
        device_ = torch::cuda::is_available() 
            ? torch::kCUDA 
            : torch::kCPU;
        model_ = torch::jit::load(path, device_);
        model_.eval();
    }
};
```

---

### 5. Симуляция ввода (input/)

**Зависимости:**
- Нет внешних библиотек (только subprocess)

**Реализация:**
```cpp
// include/nerd_dictation/input.hpp
namespace nerd_dictation::input {
    enum class InputMethod {
        YDotool,
        XDotool,
        WType,
        Dotool,
        Stdout
    };
    
    class InputSimulator {
    public:
        InputSimulator(InputMethod method);
        
        void type_text(const std::string& text);
        void press_keys(const std::string& keys);
        
    private:
        InputMethod method_;
        std::string get_command(const std::string& text) const;
    };
}
```

**Производительность:**
- Минимальные накладные расходы (subprocess вызовы)
- Batch отправка текста где возможно

---

### 6. Постобработка текста (input/postprocess.cpp)

**Реализация:**
```cpp
namespace nerd_dictation {
    struct TextAction {
        std::string text;
    };
    
    struct KeyAction {
        std::string keys;
    };
    
    using Action = std::variant<TextAction, KeyAction>;
    
    class TextPostProcessor {
    public:
        std::vector<Action> process(const std::string& text);
        void reset();
        
    private:
        bool capitalize_next_ = true;
        bool pending_space_ = false;
        
        std::string replace_punctuation(const std::string& text);
        std::string apply_capitalization(const std::string& text);
    };
}
```

**Производительность:**
- Использование `std::regex` или ручной парсинг (быстрее)
- `std::string_view` для избежания копирований
- Lookup tables для замены пунктуации

---

### 7. Daemon (daemon/)

**Реализация:**
```cpp
// include/nerd_dictation/daemon.hpp
namespace nerd_dictation {
    class DictationDaemon {
    public:
        DictationDaemon(const Config& config);
        ~DictationDaemon();
        
        void run();  // Запуск daemon loop
        
        // Команды через Unix socket
        void begin_dictation();
        void end_dictation();
        void cancel_dictation();
        DaemonStatus get_status() const;
        
    private:
        Config config_;
        std::unique_ptr<ASRBackend> backend_;
        
        // Потоки
        std::thread audio_thread_;
        std::thread dictation_thread_;
        std::thread wake_word_thread_;
        
        // Синхронизация
        std::atomic<DaemonState> state_;
        std::mutex audio_buffer_mutex_;
        RingBuffer<int16_t> audio_buffer_;
        
        // Unix socket server
        void handle_client(int client_fd);
        void socket_server_loop();
        
        // Аудио обработка
        void audio_listener_loop();
        void dictation_loop();
        void wake_word_loop();
    };
}
```

**Производительность:**
- Lock-free структуры данных где возможно
- Минимальное копирование данных между потоками
- Эффективная синхронизация (condition variables вместо busy-wait)

---

### 8. Wake Word Detection

**Зависимости:**
- Silero VAD: через ONNX Runtime или LibTorch
- Vosk small model для распознавания wake word

**Реализация:**
```cpp
class WakeWordDetector {
private:
    std::unique_ptr<ASRBackend> vosk_small_;  // Для wake word
    std::unique_ptr<VADModel> vad_model_;     // Silero VAD
    
public:
    bool detect(const int16_t* audio, size_t samples);
};
```

---

### 9. CLI (main.cpp)

**Зависимости:**
- `CLI11` или `argparse` (C++ библиотеки для CLI)

**Реализация:**
```cpp
int main(int argc, char* argv[]) {
    CLI::App app{"nerd-dictation-ex"};
    
    auto begin_cmd = app.add_subcommand("begin", "Начать диктовку");
    auto end_cmd = app.add_subcommand("end", "Завершить диктовку");
    // ...
    
    CLI11_PARSE(app, argc, argv);
    
    Config config = load_config();
    
    if (begin_cmd->parsed()) {
        // Подключение к daemon или прямой запуск
    }
    
    return 0;
}
```

---

## Зависимости и их подключение

### Основные зависимости

| Библиотека | Версия | Способ подключения | Назначение |
|------------|--------|-------------------|------------|
| **toml11** | latest | Git submodule или vcpkg | Парсинг TOML конфигурации |
| **libvosk** | latest | Системный пакет или сборка из исходников | Vosk ASR бэкенд |
| **onnxruntime** | 1.16+ | Pre-built binaries или vcpkg | GigaAM бэкенд (ONNX) |
| **libtorch** | 2.1+ | Pre-built или vcpkg | Альтернатива для GigaAM |
| **libpipewire** | 0.3+ | Системный пакет | Аудио захват (PipeWire) |
| **libpulse** | latest | Системный пакет | Аудио захват (PulseAudio) |
| **Eigen** | 3.4+ | Git submodule или vcpkg | Линейная алгебра (шумоподавление) |
| **CLI11** | latest | Git submodule или header-only | CLI парсинг |
| **spdlog** | latest | Git submodule или vcpkg | Логирование |

### Менеджеры зависимостей

#### Вариант 1: vcpkg (рекомендуется)

```bash
# Установка vcpkg
git clone https://github.com/Microsoft/vcpkg.git
./vcpkg/bootstrap-vcpkg.sh

# Установка зависимостей
./vcpkg install toml11 onnxruntime eigen3 spdlog cli11
```

**CMake интеграция:**
```cmake
# CMakeLists.txt
set(CMAKE_TOOLCHAIN_FILE "${CMAKE_SOURCE_DIR}/vcpkg/scripts/buildsystems/vcpkg.cmake")

find_package(toml11 REQUIRED)
find_package(onnxruntime REQUIRED)
find_package(Eigen3 REQUIRED)
find_package(spdlog REQUIRED)
```

#### Вариант 2: Conan

```python
# conanfile.txt
[requires]
toml11/3.7.0
onnxruntime/1.16.0
eigen/3.4.0
spdlog/1.12.0
cli11/2.3.2
```

#### Вариант 3: Системные пакеты (Arch Linux)

```bash
# Arch Linux
sudo pacman -S pipewire-pulse libpipewire libpulse
yay -S vosk-api-bin  # или собрать из AUR

# ONNX Runtime через AUR или вручную
yay -S onnxruntime
```

### Подключение в CMake

```cmake
# CMakeLists.txt
cmake_minimum_required(VERSION 3.20)
project(nerd-dictation-ex VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Зависимости
find_package(PkgConfig REQUIRED)

# PipeWire
pkg_check_modules(PIPEWIRE REQUIRED libpipewire-0.3)

# PulseAudio (fallback)
pkg_check_modules(PULSE REQUIRED libpulse)

# Vosk (через pkg-config или вручную)
find_library(VOSK_LIB vosk)
find_path(VOSK_INCLUDE vosk/vosk.h)

# ONNX Runtime
find_package(onnxruntime REQUIRED)

# TOML11 (header-only)
find_path(TOML11_INCLUDE toml11/toml.hpp)
include_directories(${TOML11_INCLUDE})

# Eigen (header-only)
find_package(Eigen3 REQUIRED)

# spdlog
find_package(spdlog REQUIRED)

# CLI11 (header-only)
find_path(CLI11_INCLUDE CLI11/CLI.hpp)
include_directories(${CLI11_INCLUDE})

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
    VOSK_LIB
    onnxruntime::onnxruntime
    Eigen3::Eigen
    spdlog::spdlog
)

target_include_directories(nerd-dictation-ex
    PRIVATE
    ${PIPEWIRE_INCLUDE_DIRS}
    ${PULSE_INCLUDE_DIRS}
    ${VOSK_INCLUDE}
    include
)
```

---

## План миграции (поэтапно)

### Этап 1: Базовая инфраструктура (1-2 недели)

1. ✅ Настройка CMake проекта
2. ✅ Интеграция зависимостей (vcpkg/Conan)
3. ✅ Реализация конфигурации (TOML парсинг)
4. ✅ Базовый CLI (begin/end/cancel)
5. ✅ Логирование (spdlog)

**Результат:** Работающий CLI без функциональности

---

### Этап 2: Аудио захват (1 неделя)

1. ✅ PipeWire интеграция
2. ✅ PulseAudio fallback
3. ✅ Кольцевой буфер
4. ✅ Тестирование захвата аудио

**Результат:** Захват аудио работает

---

### Этап 3: Vosk бэкенд (1 неделя)

1. ✅ Интеграция libvosk
2. ✅ Реализация VoskBackend
3. ✅ Потоковое распознавание
4. ✅ Тестирование с реальными моделями

**Результат:** Работает диктовка через Vosk

---

### Этап 4: Симуляция ввода и постобработка (3-5 дней)

1. ✅ InputSimulator (ydotool/xdotool)
2. ✅ TextPostProcessor
3. ✅ Интеграция с CLI

**Результат:** Полный цикл: аудио → текст → ввод

---

### Этап 5: Daemon режим (1 неделя)

1. ✅ Unix socket server
2. ✅ Многопоточность (аудио + распознавание)
3. ✅ Управление состоянием
4. ✅ Команды begin/end/cancel через socket

**Результат:** Daemon работает, CLI подключается

---

### Этап 6: Шумоподавление (3-5 дней)

1. ✅ Spectral gating реализация
2. ✅ Интеграция с аудио потоком
3. ✅ Опционально: интеграция noisereduce C++ версии

**Результат:** Шумоподавление работает

---

### Этап 7: GigaAM бэкенд (1-2 недели)

1. ✅ ONNX Runtime интеграция
2. ✅ Загрузка GigaAM моделей
3. ✅ Batch обработка
4. ✅ GPU поддержка (CUDA)
5. ✅ Тестирование

**Результат:** GigaAM бэкенд работает

---

### Этап 8: Wake Word Detection (1 неделя)

1. ✅ Silero VAD интеграция (ONNX)
2. ✅ Vosk small для wake word
3. ✅ Интеграция с daemon
4. ✅ Тестирование

**Результат:** Wake word detection работает

---

### Этап 9: Оптимизация и полировка (1-2 недели)

1. ✅ Профилирование (perf, valgrind)
2. ✅ Оптимизация hot paths
3. ✅ Уменьшение задержек
4. ✅ Тестирование производительности
5. ✅ Документация

**Результат:** Оптимизированная версия

---

## Оценка производительности

### Ожидаемые улучшения

| Метрика | Python | C++ | Улучшение |
|---------|--------|-----|-----------|
| **Задержка начала диктовки** | ~50-100ms | ~10-20ms | **5x** |
| **Задержка распознавания** | ~200-500ms | ~50-150ms | **3-4x** |
| **Потребление памяти** | ~500MB-2GB | ~200MB-1GB | **2x** |
| **CPU использование** | ~20-40% | ~5-15% | **3-4x** |
| **Задержка аудио буфера** | ~50ms | ~10ms | **5x** |

### Критические пути оптимизации

1. **Аудио буфер**: Lock-free ring buffer
2. **ASR инференс**: Batch processing, GPU ускорение
3. **Копирование данных**: Zero-copy где возможно
4. **Память**: Pre-allocation, object pooling

---

## Совместимость с Python версией

### Стратегия миграции

1. **Параллельная разработка**: C++ версия разрабатывается параллельно
2. **Совместимость конфигурации**: Одинаковый формат TOML
3. **Совместимость CLI**: Те же команды и флаги
4. **Постепенный переход**: Можно использовать обе версии

### Обратная совместимость

- ✅ Тот же формат конфигурации (`config.toml`)
- ✅ Те же команды CLI
- ✅ Те же пути к моделям
- ✅ Совместимость с systemd service файлом

---

## Риски и митигация

| Риск | Вероятность | Влияние | Митигация |
|------|-------------|---------|-----------|
| **Сложность интеграции ONNX Runtime** | Средняя | Высокое | Использовать pre-built binaries, документация |
| **Проблемы с PipeWire API** | Низкая | Среднее | Fallback на PulseAudio |
| **Производительность хуже ожидаемой** | Низкая | Среднее | Профилирование, оптимизация |
| **Несовместимость с моделями** | Низкая | Высокое | Тестирование на реальных моделях |

---

## Дополнительные улучшения (опционально)

1. **WebSocket API**: Для интеграции с другими приложениями
2. **REST API**: HTTP сервер для управления
3. **Плагинная система**: Динамическая загрузка бэкендов (dlopen)
4. **Метрики**: Prometheus метрики для мониторинга
5. **Профилирование**: Встроенный профилировщик

---

## Заключение

Миграция на C++ даст значительный прирост производительности и эффективности использования ресурсов. План разбит на управляемые этапы с четкими результатами. Основные преимущества:

- ✅ **Производительность**: 3-5x улучшение задержек
- ✅ **Память**: 2x меньше потребление
- ✅ **Интеграция**: Прямые биндинги к C/C++ библиотекам
- ✅ **Многопоточность**: Нативная поддержка

**Общее время разработки**: 8-12 недель (зависит от опыта команды)
