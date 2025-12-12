/**
 * VAD Manager Implementation using ONNX Runtime
 */

#include "nerd_dictation/vad/vad_manager.hpp"
#include <onnxruntime/core/session/onnxruntime_cxx_api.h>
#include <spdlog/spdlog.h>
#include <cstdlib>
#include <filesystem>
#include <cmath>
#include <algorithm>

namespace fs = std::filesystem;

namespace nerd_dictation {
namespace vad {

// PIMPL implementation for ONNX Runtime
struct VADManager::OnnxImpl {
    std::unique_ptr<Ort::Env> env;
    std::unique_ptr<Ort::Session> session;
    Ort::MemoryInfo memory_info{nullptr};

    // Input/output names (Silero VAD specific)
    std::vector<const char*> input_names = {"input", "state", "sr"};
    std::vector<const char*> output_names = {"output", "stateN"};

    OnnxImpl() : memory_info(Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault)) {}
};

VADManager& VADManager::instance() {
    static VADManager instance;
    return instance;
}

VADManager::VADManager()
    : onnx_(std::make_unique<OnnxImpl>())
{
    init_state();
}

VADManager::~VADManager() = default;

void VADManager::init_state() {
    // Initialize state to zeros
    state_.fill(0.0f);
    context_.clear();
    last_sample_rate_ = 0;
}

size_t VADManager::context_size() const {
    return (config_.sample_rate == 16000) ? 64 : 32;
}

size_t VADManager::chunk_size() const {
    return (config_.sample_rate == 16000) ? 512 : 256;
}

bool VADManager::init(const VADConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);

    config_ = config;

    // Find model if not specified
    if (config_.model_path.empty()) {
        config_.model_path = find_vad_model();
        if (config_.model_path.empty()) {
            last_error_ = "Silero VAD model not found";
            spdlog::error("VADManager: {}", last_error_);
            return false;
        }
    }

    // Validate sample rate
    if (config_.sample_rate != 8000 && config_.sample_rate != 16000) {
        last_error_ = "Invalid sample rate. Supported: 8000, 16000";
        spdlog::error("VADManager: {}", last_error_);
        return false;
    }

    return load_model();
}

bool VADManager::load_model() {
    try {
        // Create ONNX Runtime environment
        onnx_->env = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "VADManager");

        // Session options
        Ort::SessionOptions session_options;
        session_options.SetIntraOpNumThreads(1);
        session_options.SetInterOpNumThreads(1);
        session_options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

        // Try GPU provider if requested
        if (config_.use_gpu) {
            try {
                OrtCUDAProviderOptions cuda_options;
                cuda_options.device_id = 0;
                session_options.AppendExecutionProvider_CUDA(cuda_options);
                spdlog::info("VADManager: Using CUDA execution provider");
            } catch (const Ort::Exception& e) {
                spdlog::warn("VADManager: CUDA not available, using CPU: {}", e.what());
            }
        }

        // Load model
        onnx_->session = std::make_unique<Ort::Session>(
            *onnx_->env,
            config_.model_path.c_str(),
            session_options
        );

        spdlog::info("VADManager: Loaded Silero VAD model from {}", config_.model_path);
        spdlog::info("VADManager: Sample rate={}, chunk_size={}, threshold={}",
                     config_.sample_rate, chunk_size(), config_.threshold);

        // Initialize context buffer
        context_.resize(context_size(), 0.0f);
        last_sample_rate_ = config_.sample_rate;

        initialized_ = true;
        return true;

    } catch (const Ort::Exception& e) {
        last_error_ = std::string("ONNX Runtime error: ") + e.what();
        spdlog::error("VADManager: {}", last_error_);
        initialized_ = false;
        return false;
    } catch (const std::exception& e) {
        last_error_ = std::string("Error loading model: ") + e.what();
        spdlog::error("VADManager: {}", last_error_);
        initialized_ = false;
        return false;
    }
}

bool VADManager::is_initialized() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return initialized_;
}

VADResult VADManager::process_chunk(const int16_t* samples, size_t num_samples) {
    return process_chunk(std::vector<int16_t>(samples, samples + num_samples));
}

VADResult VADManager::process_chunk(const std::vector<int16_t>& samples) {
    std::lock_guard<std::mutex> lock(mutex_);

    VADResult result;

    if (!initialized_) {
        last_error_ = "VADManager not initialized";
        return result;
    }

    const size_t expected_samples = chunk_size();
    if (samples.size() != expected_samples) {
        last_error_ = "Invalid chunk size. Expected " + std::to_string(expected_samples) +
                      ", got " + std::to_string(samples.size());
        spdlog::warn("VADManager: {}", last_error_);
        return result;
    }

    try {
        // Convert int16 to float32 and normalize to [-1, 1]
        std::vector<float> audio_float(samples.size());
        for (size_t i = 0; i < samples.size(); ++i) {
            audio_float[i] = static_cast<float>(samples[i]) / 32768.0f;
        }

        // Prepare input: context + audio chunk
        const size_t ctx_size = context_size();
        std::vector<float> input_data(ctx_size + samples.size());

        // Copy context
        std::copy(context_.begin(), context_.end(), input_data.begin());

        // Copy audio
        std::copy(audio_float.begin(), audio_float.end(), input_data.begin() + ctx_size);

        // Input shapes
        std::array<int64_t, 2> input_shape = {1, static_cast<int64_t>(input_data.size())};
        std::array<int64_t, 3> state_shape = {2, 1, 128};
        std::array<int64_t, 1> sr_shape = {1};

        // Sample rate tensor
        int64_t sr_value = config_.sample_rate;

        // Create input tensors
        Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
            onnx_->memory_info,
            input_data.data(),
            input_data.size(),
            input_shape.data(),
            input_shape.size()
        );

        Ort::Value state_tensor = Ort::Value::CreateTensor<float>(
            onnx_->memory_info,
            state_.data(),
            state_.size(),
            state_shape.data(),
            state_shape.size()
        );

        Ort::Value sr_tensor = Ort::Value::CreateTensor<int64_t>(
            onnx_->memory_info,
            &sr_value,
            1,
            sr_shape.data(),
            sr_shape.size()
        );

        // Run inference
        std::array<Ort::Value, 3> inputs = {
            std::move(input_tensor),
            std::move(state_tensor),
            std::move(sr_tensor)
        };

        auto outputs = onnx_->session->Run(
            Ort::RunOptions{nullptr},
            onnx_->input_names.data(),
            inputs.data(),
            inputs.size(),
            onnx_->output_names.data(),
            onnx_->output_names.size()
        );

        // Get output probability
        float* output_data = outputs[0].GetTensorMutableData<float>();
        result.speech_probability = output_data[0];
        result.is_speech = result.speech_probability >= config_.threshold;

        // Update state from output
        float* new_state = outputs[1].GetTensorMutableData<float>();
        std::copy(new_state, new_state + state_.size(), state_.begin());

        // Update context (last ctx_size samples of input)
        std::copy(input_data.end() - ctx_size, input_data.end(), context_.begin());

    } catch (const Ort::Exception& e) {
        last_error_ = std::string("Inference error: ") + e.what();
        spdlog::error("VADManager: {}", last_error_);
    } catch (const std::exception& e) {
        last_error_ = std::string("Processing error: ") + e.what();
        spdlog::error("VADManager: {}", last_error_);
    }

    return result;
}

void VADManager::reset_states() {
    std::lock_guard<std::mutex> lock(mutex_);

    init_state();
    if (initialized_) {
        context_.resize(context_size(), 0.0f);
        last_sample_rate_ = config_.sample_rate;
    }

    spdlog::debug("VADManager: States reset");
}

void VADManager::set_threshold(float threshold) {
    std::lock_guard<std::mutex> lock(mutex_);
    config_.threshold = std::clamp(threshold, 0.0f, 1.0f);
}

std::string VADManager::get_error() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return last_error_;
}

std::string find_vad_model() {
    // Search paths in order of priority
    std::vector<std::string> search_paths;

    // 1. User cache (torch hub download location)
    if (const char* home = std::getenv("HOME")) {
        search_paths.push_back(
            std::string(home) +
            "/.cache/torch/hub/snakers4_silero-vad_master/src/silero_vad/data/silero_vad.onnx"
        );
        // Also check for 16k optimized version
        search_paths.push_back(
            std::string(home) +
            "/.cache/torch/hub/snakers4_silero-vad_master/src/silero_vad/data/silero_vad_16k_op15.onnx"
        );
    }

    // 2. XDG data home
    if (const char* data_home = std::getenv("XDG_DATA_HOME")) {
        search_paths.push_back(std::string(data_home) + "/nerd-dictation/silero_vad.onnx");
    } else if (const char* home = std::getenv("HOME")) {
        search_paths.push_back(std::string(home) + "/.local/share/nerd-dictation/silero_vad.onnx");
    }

    // 3. System paths
    search_paths.push_back("/usr/share/nerd-dictation/silero_vad.onnx");
    search_paths.push_back("/usr/local/share/nerd-dictation/silero_vad.onnx");

    // Search for model
    for (const auto& path : search_paths) {
        if (fs::exists(path)) {
            spdlog::debug("VADManager: Found model at {}", path);
            return path;
        }
    }

    spdlog::warn("VADManager: Silero VAD model not found in any search path");
    return "";
}

} // namespace vad
} // namespace nerd_dictation
