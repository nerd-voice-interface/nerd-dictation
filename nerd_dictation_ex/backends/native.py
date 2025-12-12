"""
Нативный бэкенд на основе C++ через pybind11.

Загружает .so бэкенды (GigaAM, T-One и др.) через нативную обёртку.
"""

import sys
from pathlib import Path
from typing import Generator, Optional

from .base import ASRBackend, ASRResult

# Попытка импорта нативного модуля
_native = None
try:
    import _nerd_dictation_native as _native
except ImportError:
    pass


# Пути поиска бэкендов (системные пакеты приоритетнее)
_BACKEND_SEARCH_PATHS = [
    Path("/usr/lib"),
    Path("/usr/local/lib"),
    Path.home() / ".local/lib",
]


def _find_backend_library(name: str) -> Optional[Path]:
    """Найти .so файл бэкенда по имени."""
    lib_names = [
        f"libnerd-dictation-{name}.so",
        f"libnerd-dictation-{name}.so.3",
        f"libnerd-dictation-{name}.so.3.0.0",
    ]

    for search_path in _BACKEND_SEARCH_PATHS:
        if not search_path.exists():
            continue
        for lib_name in lib_names:
            lib_path = search_path / lib_name
            if lib_path.exists():
                # Resolve symlinks to get the actual file
                return lib_path.resolve()

    return None


class NativeBackend(ASRBackend):
    """
    Нативный ASR бэкенд через C++.

    Загружает .so модули (GigaAM, T-One и др.) через dlopen.
    Поддерживает потоковое распознавание.
    """

    name: str = "native"
    supports_streaming: bool = True

    def __init__(self, backend_name: str = "gigaam", library_path: Optional[str] = None):
        """
        Инициализировать нативный бэкенд.

        Args:
            backend_name: Имя бэкенда (gigaam, t-one, dummy)
            library_path: Явный путь к .so файлу (опционально)
        """
        if _native is None:
            raise RuntimeError(
                "Нативный модуль _nerd_dictation_native недоступен.\n"
                "Убедитесь, что используете nerd-dictation с поддержкой Python."
            )

        self._backend_name = backend_name

        # Найти библиотеку
        if library_path:
            lib_path = Path(library_path)
        else:
            lib_path = _find_backend_library(backend_name)

        if lib_path is None or not lib_path.exists():
            raise FileNotFoundError(
                f"Бэкенд '{backend_name}' не найден.\n"
                f"Искали в: {[str(p) for p in _BACKEND_SEARCH_PATHS]}\n"
                f"Установите бэкенд или укажите library_path явно."
            )

        print(f"[NativeBackend] Loading: {lib_path}", file=sys.stderr)

        # Загрузить нативный бэкенд
        self._backend = _native.Backend(str(lib_path))

        # Получить информацию
        info = self._backend.get_info()
        self.name = info.name
        self.supports_streaming = info.streaming
        self._sample_rate = info.sample_rate

        print(f"[NativeBackend] Loaded '{self.name}' (streaming={self.supports_streaming}, rate={self._sample_rate})", file=sys.stderr)

    def load_model(self, model_path: str = "", **kwargs) -> None:
        """Модель встроена в .so, этот метод ничего не делает."""
        # Нативные бэкенды обычно имеют встроенную модель
        pass

    def transcribe_stream(
        self,
        audio_stream: Generator[bytes, None, None],
        sample_rate: int = 16000,
    ) -> Generator[ASRResult, None, None]:
        """
        Потоковое распознавание аудио.

        Args:
            audio_stream: Генератор байтов аудио (PCM 16-bit)
            sample_rate: Частота дискретизации

        Yields:
            ASRResult с распознанным текстом
        """
        if sample_rate != self._sample_rate:
            print(f"[NativeBackend] Warning: sample rate mismatch ({sample_rate} vs {self._sample_rate})", file=sys.stderr)

        last_text = ""

        for chunk in audio_stream:
            if not chunk:
                continue

            # Подать аудио в бэкенд
            self._backend.feed(chunk)

            # Получить результат
            result = self._backend.result()

            # Отдать результат если текст изменился или это финальный
            if result.is_final or (result.text and result.text != last_text):
                yield ASRResult(
                    text=result.text,
                    is_final=result.is_final,
                    confidence=result.confidence if result.confidence >= 0 else None,
                )

                if result.is_final:
                    last_text = ""
                else:
                    last_text = result.text

    def transcribe_file(self, audio_path: str) -> str:
        """Распознать аудиофайл целиком."""
        import wave

        with wave.open(audio_path, 'rb') as wf:
            if wf.getnchannels() != 1:
                raise ValueError("Только моно аудио поддерживается")
            if wf.getsampwidth() != 2:
                raise ValueError("Только 16-bit аудио поддерживается")

            sample_rate = wf.getframerate()
            if sample_rate != self._sample_rate:
                print(f"[NativeBackend] Warning: sample rate mismatch ({sample_rate} vs {self._sample_rate})", file=sys.stderr)

            # Читаем всё аудио
            audio_data = wf.readframes(wf.getnframes())

        # Подаём всё аудио
        self._backend.feed(audio_data)

        # Финализируем и получаем результат
        result = self._backend.finalize()
        return result.text

    def finalize(self) -> Optional[ASRResult]:
        """Завершить распознавание и получить финальный результат."""
        result = self._backend.finalize()
        if result.text:
            return ASRResult(
                text=result.text,
                is_final=True,
                confidence=result.confidence if result.confidence >= 0 else None,
            )
        return None

    def reset(self) -> None:
        """Сбросить состояние распознавателя."""
        self._backend.reset()


class GigaAMNativeBackend(NativeBackend):
    """GigaAM бэкенд через нативную C++ реализацию."""

    name: str = "gigaam-native"

    def __init__(self, **kwargs):
        super().__init__(backend_name="gigaam", **kwargs)


class TOneNativeBackend(NativeBackend):
    """T-One бэкенд через нативную C++ реализацию."""

    name: str = "t-one-native"

    def __init__(self, **kwargs):
        super().__init__(backend_name="t-one", **kwargs)


class DummyNativeBackend(NativeBackend):
    """Тестовый бэкенд через нативную C++ реализацию."""

    name: str = "dummy-native"

    def __init__(self, **kwargs):
        # Dummy бэкенд в директории build проекта
        build_path = Path(__file__).parent.parent.parent / "build" / "libnerd-dictation-dummy.so"
        if build_path.exists():
            kwargs.setdefault("library_path", str(build_path))
        super().__init__(backend_name="dummy", **kwargs)
