"""
Модуль ввода текста через различные методы (xdotool, ydotool, wtype, dotool).

Использует нативную C++ реализацию через _nerd_dictation_native.input.
"""

from _nerd_dictation_native.input import InputSimulator

# Кэшированный симулятор (создаётся при первом вызове)
_simulator = None


def _get_simulator(config: dict) -> InputSimulator:
    """Получить или создать симулятор ввода."""
    global _simulator
    method = config.get("general", {}).get("input_method", "auto")

    if _simulator is None:
        _simulator = InputSimulator(method)

    return _simulator


def type_text(text: str, config: dict) -> None:
    """
    Ввести текст через выбранный метод.

    Args:
        text: Текст для ввода
        config: Конфигурация с настройками input_method
    """
    if not text:
        return

    simulator = _get_simulator(config)
    simulator.type_text(text)


def press_keys(keys: str, config: dict) -> None:
    """
    Нажать комбинацию клавиш.

    Args:
        keys: Комбинация клавиш (формат xdotool: "ctrl+Return")
        config: Конфигурация с настройками input_method
    """
    simulator = _get_simulator(config)
    simulator.press_keys(keys)
