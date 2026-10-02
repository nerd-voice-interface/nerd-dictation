#include "nerd_dictation/input/input_simulator.hpp"
#include "nerd_dictation/sounds/sounds.hpp"
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <unistd.h>
#include <cstring>
#include <fstream>
#include <iostream>
#include <csignal>
#include <execinfo.h>
#include <cstdlib>
#include <sys/resource.h>
#include <ctime>
#include <fcntl.h>

#include <pybind11/embed.h>
#include <pybind11/stl.h>
#include <pybind11/functional.h>
#include "nerd_dictation/backend_wrapper.hpp"
#ifdef HAVE_ONNXRUNTIME
#include "nerd_dictation/vad/vad_manager.hpp"
#endif
#ifdef HAVE_VOSK
#include "nerd_dictation/wake_word/wake_word_manager.hpp"
#endif
#ifdef HAVE_XCB_XINPUT
#include "nerd_dictation/trigger/button_trigger.hpp"
#endif
#include "nerd_dictation/postprocess/text_postprocessor.hpp"
#include "nerd_dictation/audio/audio_preprocessor.hpp"
#include "nerd_dictation/audio/resampler.hpp"
#ifdef HAVE_JAMSPELL
#include "nerd_dictation/spelling/spell_corrector.hpp"
#endif
namespace py = pybind11;

/* ============================================================================
 * Native sounds module for Python
 * ============================================================================ */
PYBIND11_EMBEDDED_MODULE(_nerd_dictation_native, m) {
    m.doc() = "Native C++ functions for nerd-dictation";

    // Sounds submodule
    auto sounds = m.def_submodule("sounds", "Native sound playback via PulseAudio");
    sounds.def("init", &nerd_dictation::sounds::init_sounds, "Initialize sound subsystem");
    sounds.def("shutdown", &nerd_dictation::sounds::shutdown_sounds, "Shutdown sound subsystem");
    sounds.def("play_begin", &nerd_dictation::sounds::play_begin, "Play dictation start sound");
    sounds.def("play_end", &nerd_dictation::sounds::play_end, "Play dictation end sound");
    sounds.def("play_execute", &nerd_dictation::sounds::play_execute, "Play command execution sound");

    // Input submodule
    auto input = m.def_submodule("input", "Native input simulation (xdotool, ydotool, wtype, dotool)");
    py::class_<nerd_dictation::input::InputSimulator>(input, "InputSimulator")
        .def(py::init<const std::string&>(), py::arg("method"),
             "Create input simulator with specified method (xdotool, ydotool, wtype, dotool, stdout)")
        .def("type_text", &nerd_dictation::input::InputSimulator::type_text,
             py::call_guard<py::gil_scoped_release>(),
             py::arg("text"), "Type text using configured method")
        .def("press_keys", &nerd_dictation::input::InputSimulator::press_keys,
             py::call_guard<py::gil_scoped_release>(),
             py::arg("keys"), "Press key combination (e.g., 'ctrl+Return')");

    // ASR Result class
    py::class_<nerd_dictation::ASRResult>(m, "ASRResult")
        .def(py::init<>())
        .def(py::init<const std::string&, bool, float>(),
             py::arg("text"), py::arg("is_final") = false, py::arg("confidence") = -1.0f)
        .def_readwrite("text", &nerd_dictation::ASRResult::text)
        .def_readwrite("is_final", &nerd_dictation::ASRResult::is_final)
        .def_readwrite("confidence", &nerd_dictation::ASRResult::confidence)
        .def("__repr__", [](const nerd_dictation::ASRResult& r) {
            return "<ASRResult text='" + r.text + "' is_final=" +
                   (r.is_final ? "True" : "False") + " confidence=" +
                   std::to_string(r.confidence) + ">";
        });

    // Backend Info class
    py::class_<nerd_dictation::BackendInfo>(m, "BackendInfo")
        .def(py::init<>())
        .def_readwrite("name", &nerd_dictation::BackendInfo::name)
        .def_readwrite("streaming", &nerd_dictation::BackendInfo::streaming)
        .def_readwrite("sample_rate", &nerd_dictation::BackendInfo::sample_rate)
        .def_readwrite("channels", &nerd_dictation::BackendInfo::channels)
        .def("__repr__", [](const nerd_dictation::BackendInfo& i) {
            return "<BackendInfo name='" + i.name + "' streaming=" +
                   (i.streaming ? "True" : "False") + " sample_rate=" +
                   std::to_string(i.sample_rate) + ">";
        });

    // Backend class - main ASR interface
    py::class_<nerd_dictation::Backend>(m, "Backend")
        .def(py::init<const std::string&>(), py::arg("library_path"),
             "Load backend from .so file")
        .def("get_info", &nerd_dictation::Backend::get_info,
             "Get backend information")
        .def("name", &nerd_dictation::Backend::name,
             "Get backend name")
        .def("supports_streaming", &nerd_dictation::Backend::supports_streaming,
             "Check if streaming is supported")
        .def("sample_rate", &nerd_dictation::Backend::sample_rate,
             "Get expected sample rate")
        .def("feed", [](nerd_dictation::Backend& self, py::bytes data) {
            // Копируем, чтобы отпустить GIL: py::bytes нельзя трогать без него
            std::string_view sv = data;
            const int16_t* begin = reinterpret_cast<const int16_t*>(sv.data());
            std::vector<int16_t> samples(begin, begin + sv.size() / sizeof(int16_t));
            py::gil_scoped_release unlock;
            self.feed(samples.data(), samples.size());
        }, py::arg("audio_bytes"),
           "Feed audio data (raw int16 PCM bytes)")
        .def("result", &nerd_dictation::Backend::result,
             py::call_guard<py::gil_scoped_release>(),
             "Get current result")
        // Инференс идёт секунды: удерживать GIL значит остановить приём аудио
        .def("finalize", &nerd_dictation::Backend::finalize,
             py::call_guard<py::gil_scoped_release>(),
             "Finalize and get final result")
        .def("reset", &nerd_dictation::Backend::reset,
             py::call_guard<py::gil_scoped_release>(),
             "Reset state for new session")
        .def("get_error", &nerd_dictation::Backend::get_error,
             "Get last error message")
        .def("__bool__", [](const nerd_dictation::Backend& self) {
            return static_cast<bool>(self);
        });

#ifdef HAVE_ONNXRUNTIME
    // VAD submodule - Voice Activity Detection via Silero VAD ONNX
    auto vad = m.def_submodule("vad", "Voice Activity Detection using Silero VAD (ONNX)");

    // VADResult class
    py::class_<nerd_dictation::vad::VADResult>(vad, "VADResult")
        .def(py::init<>())
        .def_readwrite("speech_probability", &nerd_dictation::vad::VADResult::speech_probability)
        .def_readwrite("is_speech", &nerd_dictation::vad::VADResult::is_speech)
        .def("__repr__", [](const nerd_dictation::vad::VADResult& r) {
            return "<VADResult speech_prob=" + std::to_string(r.speech_probability) +
                   " is_speech=" + (r.is_speech ? "True" : "False") + ">";
        });

    // VADConfig class
    py::class_<nerd_dictation::vad::VADConfig>(vad, "VADConfig")
        .def(py::init<>())
        .def_readwrite("model_path", &nerd_dictation::vad::VADConfig::model_path)
        .def_readwrite("sample_rate", &nerd_dictation::vad::VADConfig::sample_rate)
        .def_readwrite("threshold", &nerd_dictation::vad::VADConfig::threshold)
        .def_readwrite("use_gpu", &nerd_dictation::vad::VADConfig::use_gpu);

    // VADManager singleton
    py::class_<nerd_dictation::vad::VADManager, std::unique_ptr<nerd_dictation::vad::VADManager, py::nodelete>>(vad, "VADManager")
        .def_static("instance", &nerd_dictation::vad::VADManager::instance,
                    py::return_value_policy::reference,
                    "Get singleton VADManager instance")
        // Все методы ниже берут VADManager::mutex_, который process_chunk
        // удерживает весь ORT-инференс уже без GIL. Держать здесь GIL значит
        // ждать этот мьютекс с GIL в руках и останавливать все потоки Python.
        .def("init", &nerd_dictation::vad::VADManager::init,
             py::call_guard<py::gil_scoped_release>(),
             py::arg("config"), "Initialize VAD with configuration")
        .def("is_initialized", &nerd_dictation::vad::VADManager::is_initialized,
             py::call_guard<py::gil_scoped_release>(),
             "Check if VAD is initialized")
        .def("process_chunk", [](nerd_dictation::vad::VADManager& self, py::bytes data) {
            std::string_view sv = data;
            const int16_t* begin = reinterpret_cast<const int16_t*>(sv.data());
            std::vector<int16_t> samples(begin, begin + sv.size() / sizeof(int16_t));
            py::gil_scoped_release unlock;
            return self.process_chunk(samples);
        }, py::arg("audio_bytes"),
           "Process audio chunk (raw int16 PCM bytes) and get VAD result")
        .def("reset_states", &nerd_dictation::vad::VADManager::reset_states,
             py::call_guard<py::gil_scoped_release>(),
             "Reset internal states for new session")
        .def_property("threshold",
                      &nerd_dictation::vad::VADManager::threshold,
                      // сеттер берёт mutex_, геттер — нет
                      py::cpp_function(&nerd_dictation::vad::VADManager::set_threshold,
                                       py::call_guard<py::gil_scoped_release>()),
                      "Speech detection threshold (0.0-1.0)")
        .def_property_readonly("sample_rate", &nerd_dictation::vad::VADManager::sample_rate,
                               "Sample rate (8000 or 16000)")
        .def_property_readonly("chunk_size", &nerd_dictation::vad::VADManager::chunk_size,
                               "Required chunk size in samples (512 for 16kHz, 256 for 8kHz)")
        .def("get_error", &nerd_dictation::vad::VADManager::get_error,
             py::call_guard<py::gil_scoped_release>(),
             "Get last error message");

    // Helper function to find default model
#endif

#ifdef HAVE_VOSK
    // Wake Word submodule - Vosk-based wake word detection
    auto wake_word = m.def_submodule("wake_word", "Wake word detection using Vosk ASR");

    // WakeWordResult class
    py::class_<nerd_dictation::wake_word::WakeWordResult>(wake_word, "WakeWordResult")
        .def(py::init<>())
        .def_readwrite("text", &nerd_dictation::wake_word::WakeWordResult::text,
                       "Recognized text")
        .def_readwrite("detected", &nerd_dictation::wake_word::WakeWordResult::detected,
                       "True if wake word was detected")
        .def_readwrite("confidence", &nerd_dictation::wake_word::WakeWordResult::confidence,
                       "Recognition confidence");

    // WakeWordConfig class
    py::class_<nerd_dictation::wake_word::WakeWordConfig>(wake_word, "WakeWordConfig")
        .def(py::init<>())
        .def_readwrite("model_path", &nerd_dictation::wake_word::WakeWordConfig::model_path,
                       "Path to Vosk model directory")
        .def_readwrite("wake_word", &nerd_dictation::wake_word::WakeWordConfig::wake_word,
                       "Wake word to detect")
        .def_readwrite("variants", &nerd_dictation::wake_word::WakeWordConfig::variants,
                       "Alternative forms of wake word")
        .def_readwrite("sample_rate", &nerd_dictation::wake_word::WakeWordConfig::sample_rate,
                       "Audio sample rate")
        .def_readwrite("log_level", &nerd_dictation::wake_word::WakeWordConfig::log_level,
                       "Vosk log level (-1 = quiet)");

    // WakeWordManager singleton
    py::class_<nerd_dictation::wake_word::WakeWordManager,
               std::unique_ptr<nerd_dictation::wake_word::WakeWordManager, py::nodelete>>(wake_word, "WakeWordManager")
        .def_static("instance", &nerd_dictation::wake_word::WakeWordManager::instance,
                    py::return_value_policy::reference,
                    "Get singleton WakeWordManager instance")
        // Как и у VADManager: эти методы берут mutex_, который process_speech
        // держит весь инференс Vosk уже без GIL
        .def("init", &nerd_dictation::wake_word::WakeWordManager::init,
             py::call_guard<py::gil_scoped_release>(),
             py::arg("config"), "Initialize with configuration")
        .def("is_initialized", &nerd_dictation::wake_word::WakeWordManager::is_initialized,
             py::call_guard<py::gil_scoped_release>(),
             "Check if manager is initialized")
        .def("process_speech", [](nerd_dictation::wake_word::WakeWordManager& self, py::bytes data) {
            std::string_view sv = data;
            const uint8_t* begin = reinterpret_cast<const uint8_t*>(sv.data());
            std::vector<uint8_t> audio(begin, begin + sv.size());
            py::gil_scoped_release unlock;
            return self.process_speech(audio.data(), audio.size());
        }, py::arg("audio_bytes"),
           "Process speech audio and check for wake word")
        .def("reset", &nerd_dictation::wake_word::WakeWordManager::reset,
             py::call_guard<py::gil_scoped_release>(),
             "Reset recognizer state")
        .def_property("wake_word",
                      // и геттер, и сеттер берут mutex_
                      py::cpp_function(&nerd_dictation::wake_word::WakeWordManager::wake_word,
                                       py::call_guard<py::gil_scoped_release>()),
                      py::cpp_function(
                          [](nerd_dictation::wake_word::WakeWordManager& self, const std::string& word) {
                              self.set_wake_word(word);
                          },
                          py::call_guard<py::gil_scoped_release>()),
                      "Current wake word")
        .def_property_readonly("sample_rate", &nerd_dictation::wake_word::WakeWordManager::sample_rate,
                               "Audio sample rate")
        .def("get_error", &nerd_dictation::wake_word::WakeWordManager::get_error,
             py::call_guard<py::gil_scoped_release>(),
             "Get last error message");

    // Helper functions
#endif

#ifdef HAVE_XCB_XINPUT
    // ========== Trigger Submodule ==========
    auto trigger = m.def_submodule("trigger", "Mouse button trigger (XInput2)");

    py::enum_<nerd_dictation::trigger::TriggerEvent>(trigger, "TriggerEvent")
        .value("NONE", nerd_dictation::trigger::TriggerEvent::None)
        .value("PRESSED", nerd_dictation::trigger::TriggerEvent::Pressed)
        .value("REJECTED", nerd_dictation::trigger::TriggerEvent::Rejected)
        .value("DISCONNECTED", nerd_dictation::trigger::TriggerEvent::Disconnected);

    // Объект принадлежит одному потоку, мьютекса нет. GIL снимается на
    // сетевых вызовах к X: connect() ждёт ответов сервера, wait() — событий.
    py::class_<nerd_dictation::trigger::ButtonTrigger>(trigger, "ButtonTrigger")
        .def(py::init<int>(), py::arg("button"))
        .def("connect", &nerd_dictation::trigger::ButtonTrigger::connect,
             py::call_guard<py::gil_scoped_release>(),
             "Connect to X, grab the button; False on failure (see get_error)")
        .def("connected", &nerd_dictation::trigger::ButtonTrigger::connected)
        .def("wait", &nerd_dictation::trigger::ButtonTrigger::wait,
             py::call_guard<py::gil_scoped_release>(),
             py::arg("timeout_ms"), "Wait for the next press")
        .def("last_source", &nerd_dictation::trigger::ButtonTrigger::last_source,
             "Device name of the last press")
        .def("get_error", &nerd_dictation::trigger::ButtonTrigger::get_error);
#endif

    // ==========================================================================
    // Text Postprocessor submodule - replace words with punctuation/keys
    // ==========================================================================
    auto postprocess = m.def_submodule("postprocess",
        "Text postprocessing: punctuation replacement, capitalization, key commands");

    // TextAction class
    py::class_<nerd_dictation::postprocess::TextAction>(postprocess, "TextAction")
        .def(py::init<>())
        .def(py::init<const std::string&>(), py::arg("text"))
        .def_readwrite("text", &nerd_dictation::postprocess::TextAction::text,
                       "Text to type")
        .def("__repr__", [](const nerd_dictation::postprocess::TextAction& a) {
            return "<TextAction text='" + a.text + "'>";
        });

    // KeyAction class
    py::class_<nerd_dictation::postprocess::KeyAction>(postprocess, "KeyAction")
        .def(py::init<>())
        .def(py::init<const std::string&>(), py::arg("keys"))
        .def_readwrite("keys", &nerd_dictation::postprocess::KeyAction::keys,
                       "Key combination to press (xdotool format)")
        .def("__repr__", [](const nerd_dictation::postprocess::KeyAction& a) {
            return "<KeyAction keys='" + a.keys + "'>";
        });


    // TextPostProcessor singleton
    py::class_<nerd_dictation::postprocess::TextPostProcessor,
               std::unique_ptr<nerd_dictation::postprocess::TextPostProcessor, py::nodelete>>(
                   postprocess, "TextPostProcessor")
        .def_static("instance", &nerd_dictation::postprocess::TextPostProcessor::instance,
                    py::return_value_policy::reference,
                    "Get singleton TextPostProcessor instance")
        .def("process", [](nerd_dictation::postprocess::TextPostProcessor& self, const std::string& text) {
            // Convert Action variants to Python-friendly format
            auto actions = self.process(text);
            py::list result;
            for (const auto& action : actions) {
                if (nerd_dictation::postprocess::is_text_action(action)) {
                    result.append(nerd_dictation::postprocess::get_text_action(action));
                } else {
                    result.append(nerd_dictation::postprocess::get_key_action(action));
                }
            }
            return result;
        }, py::arg("text"),
           "Process text: replace words with punctuation/keys, apply capitalization")
        .def("reset", &nerd_dictation::postprocess::TextPostProcessor::reset,
             "Reset state (start new dictation)");

    // Helper functions
    postprocess.def("process_text", [](const std::string& text) {
        auto actions = nerd_dictation::postprocess::process_text(text);
        py::list result;
        for (const auto& action : actions) {
            if (nerd_dictation::postprocess::is_text_action(action)) {
                result.append(nerd_dictation::postprocess::get_text_action(action));
            } else {
                result.append(nerd_dictation::postprocess::get_key_action(action));
            }
        }
        return result;
    }, py::arg("text"),
       "Process text using global postprocessor singleton");

    postprocess.def("reset_processor", &nerd_dictation::postprocess::reset_processor,
                    "Reset global postprocessor state");

    // Helper to check action types from Python
    postprocess.def("is_text_action", [](const py::object& obj) {
        return py::isinstance<nerd_dictation::postprocess::TextAction>(obj);
    }, py::arg("action"), "Check if action is TextAction");

    postprocess.def("is_key_action", [](const py::object& obj) {
        return py::isinstance<nerd_dictation::postprocess::KeyAction>(obj);
    }, py::arg("action"), "Check if action is KeyAction");

    // ==========================================================================
    // Audio Preprocessor submodule - WebRTC noise suppression and AGC
    // ==========================================================================
    auto audio = m.def_submodule("audio",
        "Audio preprocessing: noise suppression and AGC via WebRTC");

    // AudioPreprocessor::Config class
    py::class_<nerd_dictation::audio::AudioPreprocessor::Config>(audio, "AudioPreprocessorConfig")
        .def(py::init<>())
        .def_readwrite("input_sample_rate",
                       &nerd_dictation::audio::AudioPreprocessor::Config::input_sample_rate,
                       "Input sample rate (default: 16000)")
        .def_readwrite("process_sample_rate",
                       &nerd_dictation::audio::AudioPreprocessor::Config::process_sample_rate,
                       "Processing sample rate (default: 16000)")
        .def_readwrite("noise_suppression_level",
                       &nerd_dictation::audio::AudioPreprocessor::Config::noise_suppression_level,
                       "Noise suppression level: 0=Low, 1=Moderate, 2=High, 3=VeryHigh")
        .def_readwrite("agc_enabled",
                       &nerd_dictation::audio::AudioPreprocessor::Config::agc_enabled,
                       "Enable Automatic Gain Control")
        .def_readwrite("high_pass_filter_enabled",
                       &nerd_dictation::audio::AudioPreprocessor::Config::high_pass_filter_enabled,
                       "Enable high-pass filter to remove DC offset");

    // Resampler: owned by one thread (the recognizer worker), no mutex.
    // GIL is released only around the conversion itself: the input bytes are
    // copied first and the output bytes are built back under the GIL.
    py::class_<nerd_dictation::audio::Resampler>(audio, "Resampler")
        .def(py::init<int, int>(), py::arg("in_rate"), py::arg("out_rate"))
        .def("process", [](nerd_dictation::audio::Resampler& self, py::bytes data) {
            std::string_view sv = data;
            const int16_t* begin = reinterpret_cast<const int16_t*>(sv.data());
            std::vector<int16_t> input(begin, begin + sv.size() / sizeof(int16_t));
            std::vector<int16_t> output;
            {
                py::gil_scoped_release unlock;
                output = self.process(input.data(), input.size());
            }
            return py::bytes(reinterpret_cast<const char*>(output.data()),
                             output.size() * sizeof(int16_t));
        }, py::arg("audio_bytes"), "Convert raw int16 PCM bytes to the output rate")
        .def("reset", &nerd_dictation::audio::Resampler::reset,
             py::call_guard<py::gil_scoped_release>(),
             "Drop filter history before an unrelated signal")
        .def("out_rate", &nerd_dictation::audio::Resampler::out_rate);

    // AudioPreprocessor class
    py::class_<nerd_dictation::audio::AudioPreprocessor>(audio, "AudioPreprocessor")
        .def(py::init<>())
        .def("initialize", &nerd_dictation::audio::AudioPreprocessor::initialize,
             py::arg("config"), "Initialize preprocessor with configuration")
        .def("is_initialized", &nerd_dictation::audio::AudioPreprocessor::is_initialized,
             "Check if preprocessor is initialized")
        .def("reset", &nerd_dictation::audio::AudioPreprocessor::reset,
             "Reset preprocessor state")
        .def("process", [](nerd_dictation::audio::AudioPreprocessor& self, py::bytes data) {
            // Копия нужна, чтобы отпустить GIL: py::bytes нельзя трогать без него
            std::string_view sv = data;
            const int16_t* begin = reinterpret_cast<const int16_t*>(sv.data());
            size_t num_samples = sv.size() / sizeof(int16_t);
            std::vector<int16_t> input(begin, begin + num_samples);
            std::vector<int16_t> output(num_samples);

            int result;
            {
                // GIL снимаем только на сам вызов: WebRTC APM работает на
                // каждом аудио-чанке, а результат ниже заворачивается в
                // py::bytes — это уже работа с Python-объектом, нужен GIL
                py::gil_scoped_release unlock;
                result = self.process(input.data(), num_samples, output.data());
            }
            if (result < 0) {
                throw std::runtime_error("AudioPreprocessor::process() failed");
            }

            return py::bytes(reinterpret_cast<const char*>(output.data()),
                            result * sizeof(int16_t));
        }, py::arg("audio_bytes"),
           "Process audio chunk (raw int16 PCM bytes)");

#ifdef HAVE_JAMSPELL
    // ==========================================================================
    // Spell Correction submodule - JamSpell context-aware spell correction
    // ==========================================================================
    auto spelling = m.def_submodule("spelling",
        "Spell correction using JamSpell context-aware n-gram model");

    // SpellCorrectorConfig class
    py::class_<nerd_dictation::spelling::SpellCorrectorConfig>(spelling, "SpellCorrectorConfig")
        .def(py::init<>())
        .def_readwrite("model_path",
                       &nerd_dictation::spelling::SpellCorrectorConfig::model_path,
                       "Path to JamSpell model file (.bin)")
        .def_readwrite("known_words_penalty",
                       &nerd_dictation::spelling::SpellCorrectorConfig::known_words_penalty,
                       "Penalty for known words (higher = less likely to correct)")
        .def_readwrite("unknown_words_penalty",
                       &nerd_dictation::spelling::SpellCorrectorConfig::unknown_words_penalty,
                       "Penalty for unknown words (higher = more likely to correct)");

    // SpellCorrector class
    py::class_<nerd_dictation::spelling::SpellCorrector>(spelling, "SpellCorrector")
        .def(py::init<>())
        .def(py::init<const std::string&>(), py::arg("model_path"),
             "Create spell corrector and load model from path")
        .def(py::init<const nerd_dictation::spelling::SpellCorrectorConfig&>(),
             py::arg("config"), "Create spell corrector with configuration")
        // Все пять методов берут SpellCorrector::mutex_, а correct() держит
        // его десятки миллисекунд. Ждать этот мьютекс с GIL в руках значит
        // останавливать все потоки Python.
        .def("is_loaded", &nerd_dictation::spelling::SpellCorrector::is_loaded,
             py::call_guard<py::gil_scoped_release>(),
             "Check if model is loaded")
        .def("correct", &nerd_dictation::spelling::SpellCorrector::correct,
             py::call_guard<py::gil_scoped_release>(),
             py::arg("text"), "Correct text using loaded model (UTF-8)")
        .def("word_is_known", &nerd_dictation::spelling::SpellCorrector::word_is_known,
             py::call_guard<py::gil_scoped_release>(),
             py::arg("word"), "Check if word is in vocabulary")
        .def_property_readonly("config",
                               &nerd_dictation::spelling::SpellCorrector::config,
                               "Get current configuration");

    // Module-level flag to check if JamSpell is available
    spelling.attr("available") = true;
#else
    // Stub module when JamSpell is not compiled
    auto spelling = m.def_submodule("spelling", "Spell correction (NOT AVAILABLE)");
    spelling.attr("available") = false;
#endif
}

/* ============================================================================
 * Crash handler with backtrace
 * ============================================================================ */

static void crash_handler(int sig) {
    const char* sig_name = (sig == SIGSEGV) ? "SIGSEGV" :
                           (sig == SIGABRT) ? "SIGABRT" :
                           (sig == SIGFPE)  ? "SIGFPE" : "UNKNOWN";

    // Print header
    const char header[] = "\n[nerd-dictation] FATAL: Caught signal ";
    write(STDERR_FILENO, header, sizeof(header) - 1);
    write(STDERR_FILENO, sig_name, strlen(sig_name));
    write(STDERR_FILENO, "\n", 1);

    // Get backtrace
    void* bt_buffer[64];
    int bt_size = backtrace(bt_buffer, 64);

    const char bt_header[] = "[nerd-dictation] Backtrace:\n";
    write(STDERR_FILENO, bt_header, sizeof(bt_header) - 1);

    // Print backtrace to stderr
    backtrace_symbols_fd(bt_buffer, bt_size, STDERR_FILENO);

    const char footer[] = "[nerd-dictation] End of backtrace\n";
    write(STDERR_FILENO, footer, sizeof(footer) - 1);

    // Save crash report to file
    const char* home = getenv("HOME");
    if (home) {
        char report_path[512];
        time_t now = time(nullptr);
        struct tm* t = localtime(&now);
        snprintf(report_path, sizeof(report_path),
                 "%s/.local/share/nerd-dictation/crash-%04d%02d%02d-%02d%02d%02d.log",
                 home, t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
                 t->tm_hour, t->tm_min, t->tm_sec);

        int fd = open(report_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd >= 0) {
            write(fd, "Signal: ", 8);
            write(fd, sig_name, strlen(sig_name));
            write(fd, "\nBacktrace:\n", 12);
            backtrace_symbols_fd(bt_buffer, bt_size, fd);
            close(fd);

            const char saved[] = "[nerd-dictation] Crash report saved to: ";
            write(STDERR_FILENO, saved, sizeof(saved) - 1);
            write(STDERR_FILENO, report_path, strlen(report_path));
            write(STDERR_FILENO, "\n", 1);
        }
    }

    // Re-raise the signal to get core dump
    signal(sig, SIG_DFL);
    raise(sig);
}

static void install_crash_handler() {
    // Enable core dumps (unlimited size)
    struct rlimit core_limit;
    core_limit.rlim_cur = RLIM_INFINITY;
    core_limit.rlim_max = RLIM_INFINITY;
    setrlimit(RLIMIT_CORE, &core_limit);

    struct sigaction sa;
    sa.sa_handler = crash_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESETHAND;  // Only handle once, then use default

    sigaction(SIGSEGV, &sa, nullptr);
    sigaction(SIGABRT, &sa, nullptr);
    sigaction(SIGFPE, &sa, nullptr);
}

int main(int argc, char* argv[]) {
    // Install crash handler for better debugging
    install_crash_handler();

    // Setup logging
    auto logger = spdlog::stderr_color_mt("nerd-dictation");
    spdlog::set_default_logger(logger);
    spdlog::set_level(spdlog::level::info);

    // ========================================================================
    // Python-based processing via cli.py
    // ========================================================================

    // Initialize Python interpreter manually for better control
    py::initialize_interpreter();

    int exit_code = 0;

    try {
        // Add scripts directory (priority) and venv site-packages to Python path
        py::module_ sys = py::module_::import("sys");
        py::list path = sys.attr("path");
        // Local scripts first (for development), then venv for dependencies
        path.attr("insert")(0, PYTHON_SCRIPTS_DIR);
        path.attr("insert")(1, PYTHON_VENV_SITE_PACKAGES);

        spdlog::info("Python interpreter initialized");
        spdlog::info("Venv site-packages: {}", PYTHON_VENV_SITE_PACKAGES);
        spdlog::info("Scripts directory: {}", PYTHON_SCRIPTS_DIR);

        // Convert argv to Python list
        py::list py_argv;
        for (int i = 0; i < argc; ++i) {
            py_argv.append(argv[i]);
        }
        sys.attr("argv") = py_argv;

        // Import and run cli module from nerd_dictation_ex package
        py::module_ cli = py::module_::import("nerd_dictation_ex.cli");

        spdlog::info("Running cli.main()...");
        cli.attr("main")();

    } catch (py::error_already_set& e) {
        // Check if it's SystemExit (raised by argparse --help or sys.exit())
        if (e.matches(PyExc_SystemExit)) {
            // Propagate the status passed to sys.exit(), following CPython semantics:
            // None/absent -> 0, int -> that value, anything else -> print it and exit 1.
            // Swallowing it as 0 would hide Python-level failures from systemd.
            exit_code = 0;
            py::object code = py::getattr(e.value(), "code", py::none());
            if (!code.is_none()) {
                if (py::isinstance<py::int_>(code)) {
                    exit_code = code.cast<int>();
                } else {
                    spdlog::error("{}", py::str(code).cast<std::string>());
                    exit_code = 1;
                }
            }
            PyErr_Clear();
        } else {
            spdlog::error("Python error: {}", e.what());
            PyErr_Clear();
            exit_code = 1;
        }
    } catch (const std::exception& e) {
        spdlog::error("Error: {}", e.what());
        exit_code = 1;
    }

    // Clean finalization - clear any pending errors first
    PyErr_Clear();
    py::finalize_interpreter();

    return exit_code;
}
