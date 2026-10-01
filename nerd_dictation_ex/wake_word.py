"""
Модуль Wake Word Detection через нативную C++ реализацию.

Использует Vosk ASR для распознавания речи и детекции wake word.
Работает совместно с VAD для сегментации речи.

Пример использования:
    from nerd_dictation_ex import wake_word

    # Инициализация (один раз при старте)
    wake_word.init(
        model_path="~/.local/share/vosk/vosk-model-small-ru",
        wake_word="запись",
        variants=["записи"]
    )

    # Обработка накопленной речи (после VAD детекции конца речи)
    result = wake_word.process_speech(speech_buffer_bytes)
    if result.detected:
        print(f"Wake word detected in: '{result.text}'")
        wake_word.reset()
"""

from _nerd_dictation_native import wake_word as _native_wake_word

# Экспортируемые классы
WakeWordResult = _native_wake_word.WakeWordResult
WakeWordConfig = _native_wake_word.WakeWordConfig
WakeWordManager = _native_wake_word.WakeWordManager

# Кэшированная ссылка на singleton
_manager = None


def get_manager() -> WakeWordManager:
    """Получить singleton экземпляр WakeWordManager."""
    global _manager
    if _manager is None:
        _manager = WakeWordManager.instance()
    return _manager


def init(
    model_path: str = "",
    wake_word: str = "запись",
    variants: list = None,
    sample_rate: int = 16000,
    log_level: int = -1
) -> bool:
    """
    Инициализировать Wake Word Manager.

    Args:
        model_path: Путь к Vosk модели (пустой = автопоиск)
        wake_word: Слово для активации (по умолчанию "запись")
        variants: Альтернативные формы слова (например ["записи"])
        sample_rate: Частота дискретизации
        log_level: Уровень логирования Vosk (-1 = тихий режим)

    Returns:
        True если инициализация успешна
    """
    import os

    config = WakeWordConfig()

    # Расширяем путь если указан
    if model_path:
        config.model_path = os.path.expanduser(model_path)
    else:
        config.model_path = ""  # Автопоиск

    config.wake_word = wake_word
    config.variants = variants or ["записи"]
    config.sample_rate = sample_rate
    config.log_level = log_level

    manager = get_manager()
    return manager.init(config)




def process_speech(audio_bytes: bytes) -> WakeWordResult:
    """
    Обработать накопленную речь и проверить наличие wake word.

    Args:
        audio_bytes: Аудио данные (raw int16 PCM bytes)

    Returns:
        WakeWordResult с текстом и флагом detected
    """
    return get_manager().process_speech(audio_bytes)




def reset() -> None:
    """
    Сбросить состояние распознавателя.

    Вызывать после детекции wake word или при начале новой сессии.
    """
    get_manager().reset()








def get_error() -> str:
    """Получить последнее сообщение об ошибке."""
    return get_manager().get_error()
