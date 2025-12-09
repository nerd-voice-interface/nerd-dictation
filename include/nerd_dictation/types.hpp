#pragma once

#include <string>

namespace nerd_dictation {

/**
 * Speech recognition result
 */
struct ASRResult {
    std::string text;
    bool is_final = false;
    float confidence = 0.0f;
    
    ASRResult() = default;
    ASRResult(const std::string& t, bool final = false, float conf = 0.0f)
        : text(t), is_final(final), confidence(conf) {}
};

} // namespace nerd_dictation
