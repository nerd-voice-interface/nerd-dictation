"""
Нативный бэкенд на основе C++ через pybind11.

Загружает .so бэкенды (GigaAM, T-One и др.) через нативную обёртку.
"""

import sys
from pathlib import Path
from typing import Optional

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


class NativeBackend:
    """
    ASR-бэкенд в нативной .so (GigaAM, T-One и др.), загружаемый через dlopen.

    Методы повторяют C API бэкенда (include/nerd_dictation/backend_api.h):
    feed копит аудио, result отдаёт текущую гипотезу потокового бэкенда,
    finalize дожимает остаток, reset начинает заново. Звать их можно только
    из одного потока — воркера распознавания демона.

    supports_streaming и sample_rate известны только после загрузки
    (nerd_dictation_get_info требует созданного экземпляра).
    """

    name: str = "native"

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

    @property
    def sample_rate(self) -> int:
        """Частота, которую ждёт модель; аудио другой частоты демон ресемплирует."""
        return self._sample_rate

    def feed(self, audio: bytes) -> None:
        """Подать int16 PCM на частоте sample_rate."""
        self._backend.feed(audio)

    def result(self):
        """Текущий результат потокового бэкенда (text, is_final, confidence)."""
        return self._backend.result()

    def finalize(self):
        """Распознать накопленное и вернуть финальный результат."""
        return self._backend.finalize()

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
