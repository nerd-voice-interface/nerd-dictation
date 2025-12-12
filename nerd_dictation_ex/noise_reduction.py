"""
Модуль для шумоподавления аудио через нативную C++ реализацию.

Использует WebRTC Audio Processing для:
- Noise Suppression (шумоподавление)
- Automatic Gain Control (автоматическая регулировка усиления)
- High-pass filtering (фильтрация низких частот)

Пример использования:
    from nerd_dictation_ex.noise_reduction import create_noise_reducer

    reducer = create_noise_reducer(config)
    if reducer:
        processed = reducer.process_chunk(audio_bytes)
"""

from typing import Generator, Optional

from _nerd_dictation_native import audio as _native_audio

# Экспортируем нативные классы
AudioPreprocessorConfig = _native_audio.AudioPreprocessorConfig
AudioPreprocessor = _native_audio.AudioPreprocessor


class NoiseReducer:
    """
    Класс для шумоподавления аудио потока.

    Обёртка над нативным AudioPreprocessor с WebRTC noise suppression.
    """

    # Маппинг уровней шумоподавления
    NOISE_LEVELS = {
        "low": 0,
        "moderate": 1,
        "high": 2,
        "veryhigh": 3,
    }

    def __init__(
        self,
        sample_rate: int = 16000,
        enabled: bool = True,
        noise_level: str = "high",
        agc_enabled: bool = True,
        high_pass_filter: bool = True,
    ):
        """
        Инициализировать фильтр шумоподавления.

        Args:
            sample_rate: Частота дискретизации (8000 или 16000)
            enabled: Включено ли шумоподавление
            noise_level: Уровень шумоподавления ("low", "moderate", "high", "veryhigh")
            agc_enabled: Включить автоматическую регулировку усиления
            high_pass_filter: Включить high-pass фильтр
        """
        self.sample_rate = sample_rate
        self.enabled = enabled
        self._preprocessor: Optional[AudioPreprocessor] = None

        if not enabled:
            return

        # Создаём нативный препроцессор
        config = AudioPreprocessorConfig()
        config.input_sample_rate = sample_rate
        config.process_sample_rate = 16000  # WebRTC требует минимум 16kHz
        config.noise_suppression_level = self.NOISE_LEVELS.get(noise_level.lower(), 2)
        config.agc_enabled = agc_enabled
        config.high_pass_filter_enabled = high_pass_filter

        self._preprocessor = AudioPreprocessor()
        if not self._preprocessor.initialize(config):
            raise RuntimeError("Failed to initialize AudioPreprocessor")

    def process_chunk(self, chunk: bytes) -> bytes:
        """
        Обработать один чанк аудио.

        Args:
            chunk: Байты PCM 16-bit аудио

        Returns:
            Обработанные байты
        """
        if not self.enabled or self._preprocessor is None:
            return chunk

        try:
            return self._preprocessor.process(chunk)
        except Exception as e:
            import sys
            print(f"[noise_reduction] Ошибка обработки: {e}", file=sys.stderr)
            return chunk

    def process_stream(
        self, audio_stream: Generator[bytes, None, None]
    ) -> Generator[bytes, None, None]:
        """
        Обработать поток аудио.

        Args:
            audio_stream: Генератор байтов аудио

        Yields:
            Обработанные байты
        """
        for chunk in audio_stream:
            yield self.process_chunk(chunk)

    def reset(self) -> None:
        """Сбросить состояние (например, для новой сессии диктовки)."""
        if self._preprocessor is not None:
            self._preprocessor.reset()


def create_noise_reducer(config: dict) -> Optional[NoiseReducer]:
    """
    Создать фильтр шумоподавления из конфигурации.

    Args:
        config: Конфигурация с секцией [noise_reduction]

    Returns:
        NoiseReducer или None если отключено
    """
    noise_config = config.get("noise_reduction", {})

    if not noise_config.get("enabled", False):
        return None

    sample_rate = config.get("audio", {}).get("sample_rate", 16000)

    # Маппинг старых методов на уровни
    method = noise_config.get("method", "high")
    if method in ("noisereduce", "spectral_gating"):
        # Старые методы -> high level
        noise_level = "high"
    else:
        noise_level = method

    return NoiseReducer(
        sample_rate=sample_rate,
        enabled=True,
        noise_level=noise_level,
        agc_enabled=noise_config.get("agc_enabled", True),
        high_pass_filter=noise_config.get("high_pass_filter", True),
    )
