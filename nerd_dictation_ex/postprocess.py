"""
Постобработка распознанного текста через нативную C++ реализацию.

- Замена слов на знаки препинания
- Замена слов на нажатия клавиш
- Капитализация после точки/!/?
- Капитализация в начале диктовки

Поддерживает корректную работу с UTF-8 русским текстом.
"""

from typing import List, Union

from _nerd_dictation_native import postprocess as _native_postprocess

# Экспортируем нативные классы
TextAction = _native_postprocess.TextAction
KeyAction = _native_postprocess.KeyAction
TextPostProcessor = _native_postprocess.TextPostProcessor

# Тип для удобства
Action = Union[TextAction, KeyAction]

# Кэшированная ссылка на singleton
_processor = None


def get_processor() -> TextPostProcessor:
    """Получить singleton экземпляр TextPostProcessor."""
    global _processor
    if _processor is None:
        _processor = TextPostProcessor.instance()
    return _processor


def process_text(text: str) -> List[Action]:
    """
    Обработать текст: заменить слова на пунктуацию/клавиши, капитализировать.

    Args:
        text: Распознанный текст

    Returns:
        Список действий (TextAction или KeyAction)
    """
    return get_processor().process(text)


def reset_processor() -> None:
    """Сбросить состояние постобработчика (начать новую диктовку)."""
    get_processor().reset()


def is_text_action(action: Action) -> bool:
    """Проверить является ли действие TextAction."""
    return isinstance(action, TextAction)


def is_key_action(action: Action) -> bool:
    """Проверить является ли действие KeyAction."""
    return isinstance(action, KeyAction)
