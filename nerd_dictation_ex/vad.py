"""
Модуль Voice Activity Detection (VAD) через нативную C++ реализацию.

Использует Silero VAD через ONNX Runtime для определения речи в аудио.
Этот модуль работает только внутри C++ daemon (pybind11 embedded module).

Пример использования:
    from nerd_dictation_ex import vad

    # Инициализация (один раз при старте)
    vad.init()

    # Обработка аудио чанков
    result = vad.process_chunk(audio_bytes)
    if result.is_speech:
        print(f"Речь детектирована: {result.speech_probability:.2f}")

    # Сброс состояния для новой сессии
    vad.reset_states()
"""

from _nerd_dictation_native import vad as _native_vad

# Экспортируемые классы
VADResult = _native_vad.VADResult
VADConfig = _native_vad.VADConfig
VADManager = _native_vad.VADManager

# Кэшированная ссылка на singleton
_manager = None


def get_manager() -> VADManager:
    """Получить singleton экземпляр VADManager."""
    global _manager
    if _manager is None:
        _manager = VADManager.instance()
    return _manager


def init(
    model_path: str = "",
    sample_rate: int = 16000,
    threshold: float = 0.5,
    use_gpu: bool = False
) -> bool:
    """
    Инициализировать VAD.

    Args:
        model_path: Путь к silero_vad.onnx (пустой = автопоиск)
        sample_rate: Частота дискретизации (8000 или 16000)
        threshold: Порог детекции речи (0.0-1.0)
        use_gpu: Использовать CUDA если доступен

    Returns:
        True если инициализация успешна
    """
    config = VADConfig()
    config.model_path = model_path
    config.sample_rate = sample_rate
    config.threshold = threshold
    config.use_gpu = use_gpu

    manager = get_manager()
    return manager.init(config)


def is_initialized() -> bool:
    """Проверить инициализирован ли VAD."""
    return get_manager().is_initialized()


def process_chunk(audio_bytes: bytes) -> VADResult:
    """
    Обработать аудио чанк и получить вероятность речи.

    Args:
        audio_bytes: Аудио данные (raw int16 PCM bytes)
                     Размер должен быть 1024 байт (512 samples) для 16kHz
                     или 512 байт (256 samples) для 8kHz

    Returns:
        VADResult с speech_probability и is_speech
    """
    return get_manager().process_chunk(audio_bytes)


def process_samples(samples: list) -> VADResult:
    """
    Обработать аудио семплы и получить вероятность речи.

    Args:
        samples: Список int16 значений
                 Размер должен быть 512 для 16kHz или 256 для 8kHz

    Returns:
        VADResult с speech_probability и is_speech
    """
    return get_manager().process_chunk_samples(samples)


def reset_states() -> None:
    """
    Сбросить внутреннее состояние VAD.

    ВАЖНО: Вызывать только в начале новой сессии диктовки,
    НЕ во время idle или wake word детекции!
    """
    get_manager().reset_states()


def get_threshold() -> float:
    """Получить текущий порог детекции речи."""
    return get_manager().threshold


def set_threshold(threshold: float) -> None:
    """Установить порог детекции речи (0.0-1.0)."""
    get_manager().threshold = threshold


def get_sample_rate() -> int:
    """Получить частоту дискретизации."""
    return get_manager().sample_rate


def get_chunk_size() -> int:
    """
    Получить требуемый размер чанка в семплах.

    Returns:
        512 для 16kHz или 256 для 8kHz
    """
    return get_manager().chunk_size


def get_error() -> str:
    """Получить последнее сообщение об ошибке."""
    return get_manager().get_error()


def find_model() -> str:
    """
    Найти путь к Silero VAD модели.

    Поиск в порядке приоритета:
    1. ~/.cache/torch/hub/snakers4_silero-vad_master/src/silero_vad/data/silero_vad.onnx
    2. ~/.local/share/nerd-dictation/silero_vad.onnx
    3. /usr/share/nerd-dictation/silero_vad.onnx

    Returns:
        Путь к модели или пустая строка если не найдена
    """
    return _native_vad.find_vad_model()


class NativeVADIterator:
    """
    Итератор для потоковой обработки VAD на базе C++ VADManager.

    Имитирует интерфейс Silero VADIterator, но использует нативную реализацию.
    Это позволяет использовать один и тот же VAD для wake word и dictation
    без проблем с heap corruption при reset_states().

    Использование:
        vad.init()  # Один раз при старте
        iterator = vad.NativeVADIterator(threshold=0.5, min_silence_duration_ms=300)

        for chunk in audio_stream:
            result = iterator(chunk)
            if result and 'start' in result:
                print("Speech started")
            elif result and 'end' in result:
                print("Speech ended")
    """

    def __init__(
        self,
        threshold: float = 0.5,
        sampling_rate: int = 16000,
        min_silence_duration_ms: int = 100,
        speech_pad_ms: int = 30
    ):
        """
        Инициализировать VAD итератор.

        Args:
            threshold: Порог детекции речи (0.0-1.0)
            sampling_rate: Частота дискретизации (8000 или 16000)
            min_silence_duration_ms: Мин. длительность тишины для завершения речи (мс)
            speech_pad_ms: Отступ в начале/конце речи (мс)
        """
        self.threshold = threshold
        self.sampling_rate = sampling_rate

        if sampling_rate not in [8000, 16000]:
            raise ValueError('VADIterator supports only 8000 and 16000 sample rates')

        self.min_silence_samples = sampling_rate * min_silence_duration_ms / 1000
        self.speech_pad_samples = sampling_rate * speech_pad_ms / 1000

        # Состояние итератора (НЕ вызываем reset_states для C++ модели!)
        self.triggered = False
        self.temp_end = 0
        self.current_sample = 0

        # Получаем ссылку на VADManager
        self._manager = get_manager()

        # Negative threshold для hysteresis (как в оригинале)
        self._neg_threshold = threshold - 0.15

    def soft_reset(self):
        """
        Мягкий сброс состояния итератора БЕЗ сброса C++ модели.

        ВАЖНО: Используйте этот метод вместо reset_states() в wake word detection!
        """
        self.triggered = False
        self.temp_end = 0
        self.current_sample = 0

    def __call__(self, audio_data, return_seconds=False, time_resolution: int = 1):
        """
        Обработать аудио чанк и вернуть событие речи.

        Args:
            audio_data: Аудио данные (bytes, numpy int16, numpy float32, или torch tensor)
            return_seconds: Возвращать время в секундах (иначе в samples)
            time_resolution: Разрешение времени для секунд

        Returns:
            dict с 'start' или 'end' или None
        """
        import numpy as np

        # Конвертируем в bytes (int16 PCM)
        if isinstance(audio_data, bytes):
            audio_bytes = audio_data
        elif hasattr(audio_data, 'numpy'):
            # torch tensor - конвертируем в numpy
            arr = audio_data.numpy()
            if arr.dtype == np.float32 or arr.dtype == np.float64:
                # Нормализованный float [-1, 1] -> int16
                arr = (arr * 32768.0).clip(-32768, 32767).astype(np.int16)
            audio_bytes = arr.tobytes()
        elif hasattr(audio_data, 'tobytes'):
            # numpy array
            arr = audio_data
            if arr.dtype == np.float32 or arr.dtype == np.float64:
                # Нормализованный float [-1, 1] -> int16
                arr = (arr * 32768.0).clip(-32768, 32767).astype(np.int16)
            audio_bytes = arr.tobytes()
        else:
            raise TypeError(f"Unsupported audio type: {type(audio_data)}")

        # Размер чанка
        window_size_samples = len(audio_bytes) // 2  # int16 = 2 bytes
        self.current_sample += window_size_samples

        # Вызываем C++ VAD
        result = self._manager.process_chunk(audio_bytes)
        speech_prob = result.speech_probability

        # Hysteresis логика (как в оригинальном VADIterator)
        if speech_prob >= self.threshold and self.temp_end:
            self.temp_end = 0

        if speech_prob >= self.threshold and not self.triggered:
            self.triggered = True
            speech_start = max(0, self.current_sample - self.speech_pad_samples - window_size_samples)
            return {
                'start': int(speech_start) if not return_seconds
                         else round(speech_start / self.sampling_rate, time_resolution)
            }

        if speech_prob < self._neg_threshold and self.triggered:
            if not self.temp_end:
                self.temp_end = self.current_sample
            if self.current_sample - self.temp_end < self.min_silence_samples:
                return None
            else:
                speech_end = self.temp_end + self.speech_pad_samples - window_size_samples
                self.temp_end = 0
                self.triggered = False
                return {
                    'end': int(speech_end) if not return_seconds
                           else round(speech_end / self.sampling_rate, time_resolution)
                }

        return None
