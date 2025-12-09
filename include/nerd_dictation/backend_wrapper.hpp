/**
 * C++ Wrapper for nerd-dictation C API
 *
 * Convenient wrapper for using C API backends in C++ code.
 * Provides RAII, exceptions, and STL compatibility.
 */

#pragma once

#include "nerd_dictation/backend_api.h"
#include <string>
#include <functional>
#include <vector>
#include <stdexcept>
#include <dlfcn.h>

namespace nerd_dictation {

/**
 * Recognition result (C++ version)
 */
struct ASRResult {
    std::string text;
    bool is_final = false;
    float confidence = -1.0f;

    ASRResult() = default;
    ASRResult(const std::string& t, bool final = false, float conf = -1.0f)
        : text(t), is_final(final), confidence(conf) {}

    /* Constructor from C structure */
    explicit ASRResult(const struct nerd_dictation_result& r)
        : text(r.text ? r.text : "")
        , is_final(r.is_final != 0)
        , confidence(r.confidence) {}
};

/**
 * Backend information (C++ version)
 */
struct BackendInfo {
    std::string name;
    bool streaming = false;
    int sample_rate = 16000;
    int channels = 1;
    int bits_per_sample = 16;

    BackendInfo() = default;

    explicit BackendInfo(const nerd_dictation_info& info)
        : name(info.name ? info.name : "")
        , streaming(info.streaming != 0)
        , sample_rate(info.sample_rate)
        , channels(info.channels)
        , bits_per_sample(info.bits_per_sample) {}
};

/**
 * RAII wrapper for backend loaded from .so
 */
class Backend {
public:
    using CreateFunc = nerd_dictation_t (*)();
    using DestroyFunc = void (*)(nerd_dictation_t);
    using GetInfoFunc = int (*)(nerd_dictation_t, nerd_dictation_info*);
    using FeedFunc = int (*)(nerd_dictation_t, const int16_t*, size_t);
    using ResultFunc = int (*)(nerd_dictation_t, struct nerd_dictation_result*);
    using FinalizeFunc = int (*)(nerd_dictation_t, struct nerd_dictation_result*);
    using ResetFunc = int (*)(nerd_dictation_t);
    using GetErrorFunc = const char* (*)(nerd_dictation_t);

private:
    void* lib_handle_ = nullptr;
    nerd_dictation_t backend_ = nullptr;

    /* Function pointers */
    DestroyFunc destroy_func_ = nullptr;
    GetInfoFunc get_info_func_ = nullptr;
    FeedFunc feed_func_ = nullptr;
    ResultFunc result_func_ = nullptr;
    FinalizeFunc finalize_func_ = nullptr;
    ResetFunc reset_func_ = nullptr;
    GetErrorFunc get_error_func_ = nullptr;

    template<typename T>
    T load_symbol(const char* name) {
        void* sym = dlsym(lib_handle_, name);
        if (!sym) {
            throw std::runtime_error(std::string("Failed to load symbol: ") + name);
        }
        return reinterpret_cast<T>(sym);
    }

public:
    /**
     * Load backend from .so file
     * @param library_path Path to backend .so file
     */
    explicit Backend(const std::string& library_path) {
        /* Load shared library */
        lib_handle_ = dlopen(library_path.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (!lib_handle_) {
            throw std::runtime_error(std::string("Failed to load library: ") + dlerror());
        }

        try {
            /* Load function pointers */
            auto create_func = load_symbol<CreateFunc>("nerd_dictation_create");
            destroy_func_ = load_symbol<DestroyFunc>("nerd_dictation_destroy");
            get_info_func_ = load_symbol<GetInfoFunc>("nerd_dictation_get_info");
            feed_func_ = load_symbol<FeedFunc>("nerd_dictation_feed");
            result_func_ = load_symbol<ResultFunc>("nerd_dictation_result");
            finalize_func_ = load_symbol<FinalizeFunc>("nerd_dictation_finalize");
            reset_func_ = load_symbol<ResetFunc>("nerd_dictation_reset");
            get_error_func_ = load_symbol<GetErrorFunc>("nerd_dictation_get_error");

            /* Create backend instance */
            backend_ = create_func();
            if (!backend_) {
                throw std::runtime_error("Failed to create backend instance");
            }
        } catch (...) {
            dlclose(lib_handle_);
            throw;
        }
    }

    ~Backend() {
        if (backend_ && destroy_func_) {
            destroy_func_(backend_);
        }
        if (lib_handle_) {
            dlclose(lib_handle_);
        }
    }

    /* Non-copyable */
    Backend(const Backend&) = delete;
    Backend& operator=(const Backend&) = delete;

    /* Movable */
    Backend(Backend&& other) noexcept
        : lib_handle_(other.lib_handle_)
        , backend_(other.backend_)
        , destroy_func_(other.destroy_func_)
        , get_info_func_(other.get_info_func_)
        , feed_func_(other.feed_func_)
        , result_func_(other.result_func_)
        , finalize_func_(other.finalize_func_)
        , reset_func_(other.reset_func_)
        , get_error_func_(other.get_error_func_)
    {
        other.lib_handle_ = nullptr;
        other.backend_ = nullptr;
    }

    Backend& operator=(Backend&& other) noexcept {
        if (this != &other) {
            if (backend_ && destroy_func_) {
                destroy_func_(backend_);
            }
            if (lib_handle_) {
                dlclose(lib_handle_);
            }

            lib_handle_ = other.lib_handle_;
            backend_ = other.backend_;
            destroy_func_ = other.destroy_func_;
            get_info_func_ = other.get_info_func_;
            feed_func_ = other.feed_func_;
            result_func_ = other.result_func_;
            finalize_func_ = other.finalize_func_;
            reset_func_ = other.reset_func_;
            get_error_func_ = other.get_error_func_;

            other.lib_handle_ = nullptr;
            other.backend_ = nullptr;
        }
        return *this;
    }

    /**
     * Get backend information
     */
    BackendInfo get_info() const {
        nerd_dictation_info info;
        info.struct_size = sizeof(info);

        int ret = get_info_func_(backend_, &info);
        if (ret < 0) {
            throw std::runtime_error("Failed to get backend info");
        }

        return BackendInfo(info);
    }

    /**
     * Backend name
     */
    std::string name() const {
        return get_info().name;
    }

    /**
     * Whether streaming is supported
     */
    bool supports_streaming() const {
        return get_info().streaming;
    }

    /**
     * Expected sample rate
     */
    int sample_rate() const {
        return get_info().sample_rate;
    }

    /**
     * Feed audio data
     */
    void feed(const int16_t* samples, size_t num_samples) {
        int ret = feed_func_(backend_, samples, num_samples);
        if (ret < 0) {
            const char* err = get_error_func_(backend_);
            throw std::runtime_error(err ? err : "Failed to feed audio");
        }
    }

    void feed(const std::vector<int16_t>& samples) {
        feed(samples.data(), samples.size());
    }

    /**
     * Get current result
     */
    ASRResult result() {
        struct nerd_dictation_result r;
        int ret = result_func_(backend_, &r);
        if (ret < 0) {
            const char* err = get_error_func_(backend_);
            throw std::runtime_error(err ? err : "Failed to get result");
        }
        return ASRResult(r);
    }

    /**
     * Finalize and get final result
     */
    ASRResult finalize() {
        struct nerd_dictation_result r;
        int ret = finalize_func_(backend_, &r);
        if (ret < 0) {
            const char* err = get_error_func_(backend_);
            throw std::runtime_error(err ? err : "Failed to finalize");
        }
        return ASRResult(r);
    }

    /**
     * Reset state for new session
     */
    void reset() {
        int ret = reset_func_(backend_);
        if (ret < 0) {
            const char* err = get_error_func_(backend_);
            throw std::runtime_error(err ? err : "Failed to reset");
        }
    }

    /**
     * Streaming recognition with callbacks
     *
     * @param audio_provider Function to get audio. Returns true if data available.
     * @param result_callback Function to process results.
     */
    void transcribe_stream(
        std::function<bool(std::vector<int16_t>&)> audio_provider,
        std::function<void(const ASRResult&)> result_callback
    ) {
        std::vector<int16_t> chunk;
        std::string last_text;

        while (audio_provider(chunk)) {
            if (chunk.empty()) continue;

            feed(chunk);

            auto r = result();

            if (r.is_final || (!r.text.empty() && r.text != last_text)) {
                result_callback(r);

                if (r.is_final) {
                    last_text.clear();
                } else {
                    last_text = r.text;
                }
            }
        }

        /* Finalize */
        auto final_result = finalize();
        if (!final_result.text.empty()) {
            result_callback(final_result);
        }
    }

    /**
     * Get last error
     */
    std::string get_error() const {
        const char* err = get_error_func_(backend_);
        return err ? err : "";
    }

    /**
     * Check validity
     */
    explicit operator bool() const {
        return backend_ != nullptr;
    }
};

} /* namespace nerd_dictation */
