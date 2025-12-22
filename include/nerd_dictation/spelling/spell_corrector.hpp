/**
 * Spell Corrector - JamSpell wrapper for context-aware spell correction
 *
 * Features:
 * - Load JamSpell language model
 * - Context-aware spell correction using n-grams
 * - Thread-safe correction interface
 * - UTF-8/wstring conversion
 *
 * JamSpell uses n-gram language models for context-aware corrections,
 * providing better results than simple edit-distance based approaches.
 */

#pragma once

#include <memory>
#include <mutex>
#include <string>

#ifdef HAVE_JAMSPELL
// Forward declaration to avoid exposing JamSpell headers
namespace NJamSpell {
class TSpellCorrector;
}
#endif

namespace nerd_dictation {
namespace spelling {

/**
 * Spell Corrector configuration
 */
struct SpellCorrectorConfig {
    // Path to JamSpell model file (.bin)
    std::string model_path;

    // Penalty for known words (higher = less likely to correct known words)
    double known_words_penalty = 20.0;

    // Penalty for unknown words (higher = more likely to correct unknown words)
    double unknown_words_penalty = 5.0;

    // Maximum candidates to check for each word
    size_t max_candidates = 14;
};

/**
 * Spell Corrector using JamSpell
 *
 * Usage:
 *   SpellCorrector corrector;
 *   if (corrector.load_model("/path/to/ru.bin")) {
 *       std::string corrected = corrector.correct("привет мер");
 *       // corrected = "привет мир"
 *   }
 */
class SpellCorrector {
public:
    /**
     * Constructor (does not load model)
     */
    SpellCorrector();

    /**
     * Constructor with model loading
     */
    explicit SpellCorrector(const std::string& model_path);

    /**
     * Constructor with full configuration
     */
    explicit SpellCorrector(const SpellCorrectorConfig& config);

    /**
     * Destructor
     */
    ~SpellCorrector();

    /**
     * Load language model from file
     *
     * @param model_path Path to .bin model file
     * @return true if loaded successfully
     */
    bool load_model(const std::string& model_path);

    /**
     * Check if model is loaded
     */
    bool is_loaded() const;

    /**
     * Correct text using loaded model
     *
     * @param text Input text (UTF-8)
     * @return Corrected text (UTF-8)
     */
    std::string correct(const std::string& text);

    /**
     * Check if a word is known in the vocabulary
     *
     * @param word Word to check (UTF-8)
     * @return true if word is in vocabulary
     */
    bool word_is_known(const std::string& word);

    /**
     * Set correction penalties
     *
     * @param known_penalty Penalty for known words
     * @param unknown_penalty Penalty for unknown words
     */
    void set_penalties(double known_penalty, double unknown_penalty);

    /**
     * Get current configuration
     */
    const SpellCorrectorConfig& config() const { return config_; }

    // Non-copyable, non-movable (contains mutex)
    SpellCorrector(const SpellCorrector&) = delete;
    SpellCorrector& operator=(const SpellCorrector&) = delete;
    SpellCorrector(SpellCorrector&&) = delete;
    SpellCorrector& operator=(SpellCorrector&&) = delete;

private:
    SpellCorrectorConfig config_;

#ifdef HAVE_JAMSPELL
    std::unique_ptr<NJamSpell::TSpellCorrector> corrector_;
#endif

    bool loaded_ = false;
    mutable std::mutex mutex_;
};

// ============================================================================
// UTF-8 / wstring conversion utilities
// ============================================================================

/**
 * Convert UTF-8 string to wstring
 */
std::wstring utf8_to_wstring(const std::string& str);

/**
 * Convert wstring to UTF-8 string
 */
std::string wstring_to_utf8(const std::wstring& wstr);

} // namespace spelling
} // namespace nerd_dictation
