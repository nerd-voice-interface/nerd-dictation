"""
Модуль воспроизведения звуков для обратной связи.

Использует нативную C++ реализацию через PulseAudio если доступна,
иначе fallback на subprocess.
"""

import subprocess
import sys
from pathlib import Path
from typing import Optional

# Попытка импорта нативного модуля
_native_sounds = None
try:
    from _nerd_dictation_native import sounds as _native_sounds
    _native_sounds.init()
except ImportError:
    pass

# Путь к звукам (fallback)
SOUNDS_DIR = Path(__file__).parent.parent.parent / "sounds"

# Типы звуков
SOUND_BEGIN = "begin"
SOUND_END = "end"
SOUND_EXECUTE = "execute"


def _get_sound_path(sound_type: str) -> Optional[Path]:
    """Получить путь к звуковому файлу."""
    sound_file = SOUNDS_DIR / f"{sound_type}.wav"
    if sound_file.exists():
        return sound_file
    return None


def _find_player() -> Optional[list]:
    """Найти доступный аудио-плеер."""
    players = [
        ["paplay"],      # PulseAudio
        ["pw-play"],     # PipeWire
        ["aplay"],       # ALSA
    ]

    for player in players:
        try:
            result = subprocess.run(
                ["which", player[0]],
                capture_output=True,
                timeout=1
            )
            if result.returncode == 0:
                return player
        except Exception:
            pass

    return None


def _play_sound_fallback(sound_type: str) -> None:
    """Fallback воспроизведение через subprocess."""
    sound_path = _get_sound_path(sound_type)
    if not sound_path:
        return

    player = _find_player()
    if not player:
        return

    try:
        subprocess.Popen(
            player + [str(sound_path)],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
    except Exception as e:
        print(f"[sounds] ошибка воспроизведения: {e}", file=sys.stderr)


def play_sound(sound_type: str) -> None:
    """
    Воспроизвести звук асинхронно (не блокирует).

    Args:
        sound_type: Тип звука (begin, end, execute)
    """
    if _native_sounds is not None:
        # Используем нативную реализацию
        if sound_type == SOUND_BEGIN:
            _native_sounds.play_begin()
        elif sound_type == SOUND_END:
            _native_sounds.play_end()
        elif sound_type == SOUND_EXECUTE:
            _native_sounds.play_execute()
    else:
        # Fallback на subprocess
        _play_sound_fallback(sound_type)


def play_begin() -> None:
    """Воспроизвести звук начала диктовки."""
    if _native_sounds is not None:
        _native_sounds.play_begin()
    else:
        _play_sound_fallback(SOUND_BEGIN)


def play_end() -> None:
    """Воспроизвести звук окончания диктовки."""
    if _native_sounds is not None:
        _native_sounds.play_end()
    else:
        _play_sound_fallback(SOUND_END)


def play_execute() -> None:
    """Воспроизвести звук выполнения команды."""
    if _native_sounds is not None:
        _native_sounds.play_execute()
    else:
        _play_sound_fallback(SOUND_EXECUTE)
