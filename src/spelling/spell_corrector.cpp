/**
 * Spell Corrector - JamSpell wrapper implementation
 */

#include "nerd_dictation/spelling/spell_corrector.hpp"

#ifdef HAVE_JAMSPELL
#include <jamspell/spell_corrector.hpp>
#endif

#include <spdlog/spdlog.h>
#include <codecvt>
#include <locale>

namespace nerd_dictation {
namespace spelling {

// ============================================================================
// UTF-8 / wstring conversion
// ============================================================================

std::wstring utf8_to_wstring(const std::string& str) {
    if (str.empty()) {
        return {};
    }

    try {
        std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
        return converter.from_bytes(str);
    } catch (const std::exception& e) {
        spdlog::warn("UTF-8 to wstring conversion failed: {}", e.what());
        return {};
    }
}

std::string wstring_to_utf8(const std::wstring& wstr) {
    if (wstr.empty()) {
        return {};
    }

    try {
        std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
        return converter.to_bytes(wstr);
    } catch (const std::exception& e) {
        spdlog::warn("wstring to UTF-8 conversion failed: {}", e.what());
        return {};
    }
}

// ============================================================================
// SpellCorrector implementation
// ============================================================================

SpellCorrector::SpellCorrector() = default;

SpellCorrector::SpellCorrector(const std::string& model_path) {
    config_.model_path = model_path;
    load_model(model_path);
}

SpellCorrector::SpellCorrector(const SpellCorrectorConfig& config)
    : config_(config) {
    if (!config_.model_path.empty()) {
        load_model(config_.model_path);
    }
}

SpellCorrector::~SpellCorrector() = default;

bool SpellCorrector::load_model([[maybe_unused]] const std::string& model_path) {
#ifdef HAVE_JAMSPELL
    std::lock_guard<std::mutex> lock(mutex_);

    corrector_ = std::make_unique<NJamSpell::TSpellCorrector>();

    spdlog::info("Loading JamSpell model: {}", model_path);

    if (!corrector_->LoadLangModel(model_path)) {
        spdlog::error("Failed to load JamSpell model: {}", model_path);
        corrector_.reset();
        loaded_ = false;
        return false;
    }

    // Apply configuration
    corrector_->SetPenalty(config_.known_words_penalty, config_.unknown_words_penalty);
    corrector_->SetMaxCandidatesToCheck(config_.max_candidates);

    config_.model_path = model_path;
    loaded_ = true;

    spdlog::info("JamSpell model loaded successfully");
    return true;
#else
    spdlog::warn("JamSpell support not compiled in");
    return false;
#endif
}

bool SpellCorrector::is_loaded() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return loaded_;
}

std::string SpellCorrector::correct([[maybe_unused]] const std::string& text) {
#ifdef HAVE_JAMSPELL
    std::lock_guard<std::mutex> lock(mutex_);

    if (!loaded_ || !corrector_) {
        return text;
    }

    // Convert UTF-8 to wstring
    std::wstring wtext = utf8_to_wstring(text);
    if (wtext.empty() && !text.empty()) {
        // Conversion failed, return original
        return text;
    }

    // Apply spell correction
    std::wstring corrected = corrector_->FixFragment(wtext);

    // Convert back to UTF-8
    std::string result = wstring_to_utf8(corrected);
    if (result.empty() && !corrected.empty()) {
        // Conversion failed, return original
        return text;
    }

    return result;
#else
    return text;
#endif
}

bool SpellCorrector::word_is_known([[maybe_unused]] const std::string& word) {
#ifdef HAVE_JAMSPELL
    std::lock_guard<std::mutex> lock(mutex_);

    if (!loaded_ || !corrector_) {
        return true; // Assume known if no model
    }

    std::wstring wword = utf8_to_wstring(word);
    return corrector_->WordIsKnown(wword);
#else
    return true;
#endif
}

void SpellCorrector::set_penalties(
    [[maybe_unused]] double known_penalty,
    [[maybe_unused]] double unknown_penalty) {
#ifdef HAVE_JAMSPELL
    std::lock_guard<std::mutex> lock(mutex_);

    config_.known_words_penalty = known_penalty;
    config_.unknown_words_penalty = unknown_penalty;

    if (corrector_) {
        corrector_->SetPenalty(known_penalty, unknown_penalty);
    }
#endif
}

} // namespace spelling
} // namespace nerd_dictation
