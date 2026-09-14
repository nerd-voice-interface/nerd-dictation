"""Определение BT-профиля и выбор fallback-микрофона."""

import subprocess
import sys
from typing import Optional


def detect_bt_a2dp_active() -> bool:
    """Проверяет, активен ли профиль A2DP на Bluetooth-карте.

    Парсит вывод ``pactl list cards`` и ищет карты bluez
    с активным профилем a2dp.
    """
    try:
        result = subprocess.run(
            ["pactl", "list", "cards"],
            capture_output=True, text=True, timeout=5,
        )
        if result.returncode != 0:
            return False
    except (FileNotFoundError, subprocess.TimeoutExpired):
        return False

    is_bluez_card = False
    for line in result.stdout.splitlines():
        stripped = line.strip()
        if stripped.startswith("Name:") and "bluez" in stripped.lower():
            is_bluez_card = True
        elif stripped.startswith("Name:"):
            is_bluez_card = False
        elif is_bluez_card and stripped.startswith("Active Profile:"):
            if "a2dp" in stripped.lower():
                return True

    return False


def find_builtin_source() -> Optional[str]:
    """Находит первый встроенный (не-bluetooth) source PulseAudio/PipeWire.

    Ищет source с ``alsa_input`` в имени, без ``bluez``.
    """
    try:
        result = subprocess.run(
            ["pactl", "list", "sources", "short"],
            capture_output=True, text=True, timeout=5,
        )
        if result.returncode != 0:
            return None
    except (FileNotFoundError, subprocess.TimeoutExpired):
        return None

    for line in result.stdout.splitlines():
        parts = line.split("\t")
        if len(parts) < 2:
            continue
        name = parts[1]
        if "alsa_input" in name and "bluez" not in name.lower():
            return name

    return None


def resolve_audio_source(config: dict) -> Optional[str]:
    """Определяет аудио-источник с учётом BT A2DP fallback.

    Returns:
        Имя source для использования, или None (использовать default).
    """
    audio_cfg = config.get("audio", {})

    if not audio_cfg.get("bt_fallback", True):
        return None

    # Явно заданный fallback source
    explicit = audio_cfg.get("fallback_source")
    if explicit:
        if detect_bt_a2dp_active():
            print(f"BT A2DP активен, используем fallback source: {explicit}", file=sys.stderr)
            return explicit
        return None

    # Автоопределение
    if detect_bt_a2dp_active():
        builtin = find_builtin_source()
        if builtin:
            print(f"BT A2DP активен, fallback на встроенный микрофон: {builtin}", file=sys.stderr)
        else:
            print("BT A2DP активен, но встроенный микрофон не найден", file=sys.stderr)
        return builtin

    return None
