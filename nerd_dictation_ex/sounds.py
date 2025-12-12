"""
Модуль воспроизведения звуков для обратной связи.

Использует нативную C++ реализацию через PulseAudio.
Этот модуль работает только внутри C++ daemon (pybind11 embedded module).
"""

from _nerd_dictation_native import sounds as _native_sounds

# Инициализация при импорте
_native_sounds.init()

# Типы звуков (для совместимости)
SOUND_BEGIN = "begin"
SOUND_END = "end"
SOUND_EXECUTE = "execute"


def play_sound(sound_type: str) -> None:
    """
    Воспроизвести звук асинхронно (не блокирует).

    Args:
        sound_type: Тип звука (begin, end, execute)
    """
    if sound_type == SOUND_BEGIN:
        _native_sounds.play_begin()
    elif sound_type == SOUND_END:
        _native_sounds.play_end()
    elif sound_type == SOUND_EXECUTE:
        _native_sounds.play_execute()


def play_begin() -> None:
    """Воспроизвести звук начала диктовки."""
    _native_sounds.play_begin()


def play_end() -> None:
    """Воспроизвести звук окончания диктовки."""
    _native_sounds.play_end()


def play_execute() -> None:
    """Воспроизвести звук выполнения команды."""
    _native_sounds.play_execute()
