"""
Spell correction module - JamSpell context-aware spell correction.

Uses native C++ JamSpell implementation via pybind11 bindings.

Usage:
    from nerd_dictation_ex import spelling

    # Initialize with model path
    if spelling.init("~/.local/share/jamspell/ru.bin"):
        corrected = spelling.correct("привет мер")
        # corrected = "привет мир"

    # Or use SpellCorrector directly
    corrector = spelling.SpellCorrector("~/.local/share/jamspell/ru.bin")
    if corrector.is_loaded():
        corrected = corrector.correct("текст с ошибками")
"""

import os
import sys
from typing import Optional

# Try to import native module
_native_spelling = None
_global_corrector = None


def _get_native_module():
    """Get native spelling module from _nerd_dictation_native."""
    global _native_spelling
    if _native_spelling is not None:
        return _native_spelling

    try:
        import _nerd_dictation_native
        if hasattr(_nerd_dictation_native, 'spelling'):
            _native_spelling = _nerd_dictation_native.spelling
            return _native_spelling
    except ImportError as e:
        print(f"[spelling] Native module not available: {e}", file=sys.stderr)

    return None


def is_available() -> bool:
    """Check if JamSpell spell correction is available."""
    mod = _get_native_module()
    if mod is None:
        return False
    return getattr(mod, 'available', False)


def init(model_path: str,
         known_words_penalty: float = 20.0,
         unknown_words_penalty: float = 5.0) -> bool:
    """
    Initialize global spell corrector with model.

    Args:
        model_path: Path to JamSpell .bin model file
        known_words_penalty: Penalty for known words (higher = less likely to correct)
        unknown_words_penalty: Penalty for unknown words (higher = more likely to correct)

    Returns:
        True if initialization succeeded
    """
    global _global_corrector

    if not is_available():
        print("[spelling] JamSpell not available (not compiled)", file=sys.stderr)
        return False

    mod = _get_native_module()
    if mod is None:
        return False

    # Expand ~ in path
    model_path = os.path.expanduser(model_path)

    if not os.path.exists(model_path):
        print(f"[spelling] Model file not found: {model_path}", file=sys.stderr)
        return False

    try:
        config = mod.SpellCorrectorConfig()
        config.model_path = model_path
        config.known_words_penalty = known_words_penalty
        config.unknown_words_penalty = unknown_words_penalty

        _global_corrector = mod.SpellCorrector(config)

        if _global_corrector.is_loaded():
            print(f"[spelling] JamSpell model loaded: {model_path}", file=sys.stderr)
            return True
        else:
            print(f"[spelling] Failed to load model: {model_path}", file=sys.stderr)
            _global_corrector = None
            return False

    except Exception as e:
        print(f"[spelling] Error initializing: {e}", file=sys.stderr)
        _global_corrector = None
        return False


def is_initialized() -> bool:
    """Check if global spell corrector is initialized."""
    return _global_corrector is not None and _global_corrector.is_loaded()


def correct(text: str) -> str:
    """
    Correct text using global spell corrector.

    Args:
        text: Input text (UTF-8)

    Returns:
        Corrected text, or original if not initialized
    """
    if _global_corrector is None or not _global_corrector.is_loaded():
        return text

    try:
        return _global_corrector.correct(text)
    except Exception as e:
        print(f"[spelling] Error correcting text: {e}", file=sys.stderr)
        return text


def word_is_known(word: str) -> bool:
    """
    Check if word is in vocabulary.

    Args:
        word: Word to check (UTF-8)

    Returns:
        True if word is known, or True if corrector not initialized
    """
    if _global_corrector is None or not _global_corrector.is_loaded():
        return True

    try:
        return _global_corrector.word_is_known(word)
    except Exception:
        return True


def get_error() -> str:
    """Get last error message."""
    if not is_available():
        return "JamSpell not compiled"
    if _global_corrector is None:
        return "Not initialized"
    if not _global_corrector.is_loaded():
        return "Model not loaded"
    return ""


# Expose SpellCorrector and SpellCorrectorConfig classes
def SpellCorrector(model_path: Optional[str] = None):
    """
    Create a new SpellCorrector instance.

    Args:
        model_path: Optional path to model file

    Returns:
        SpellCorrector instance or None if not available
    """
    mod = _get_native_module()
    if mod is None or not is_available():
        return None

    if model_path:
        model_path = os.path.expanduser(model_path)
        return mod.SpellCorrector(model_path)
    else:
        return mod.SpellCorrector()


def SpellCorrectorConfig():
    """Create a new SpellCorrectorConfig instance."""
    mod = _get_native_module()
    if mod is None or not is_available():
        return None
    return mod.SpellCorrectorConfig()
