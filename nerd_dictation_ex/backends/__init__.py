"""
nerd-dictation-ex backends

Бэкенды распознавания — нативные C++ библиотеки (/usr/lib/libnerd-dictation-*.so),
загружаемые через встроенный модуль _nerd_dictation_native:
    gigaam-native  - GigaAM (оффлайн, распознаёт сегмент целиком)
    t-one-native   - T-One (потоковый)
    dummy-native   - тестовый бэкенд
    remote         - распознаёт демон другой машины через ssh ([remote] host)
"""

from typing import Dict, Type

from .native import (NativeBackend, GigaAMNativeBackend, TOneNativeBackend,
                     DummyNativeBackend, RemoteNativeBackend)

_BACKENDS: Dict[str, Type[NativeBackend]] = {
    "gigaam-native": GigaAMNativeBackend,
    "t-one-native": TOneNativeBackend,
    "dummy-native": DummyNativeBackend,
    "remote": RemoteNativeBackend,
}


def get_available_backends() -> Dict[str, Type[NativeBackend]]:
    """Получить словарь доступных бэкендов."""
    return dict(_BACKENDS)


def get_backend(name: str, **kwargs) -> NativeBackend:
    """Получить экземпляр бэкенда по имени."""
    if name not in _BACKENDS:
        raise ValueError(f"Бэкенд '{name}' не найден. Доступны: {', '.join(_BACKENDS)}")
    return _BACKENDS[name](**kwargs)


def list_backends() -> None:
    """Вывести список бэкендов.

    Потоковость не выводится: она известна только после загрузки модели
    (nerd_dictation_get_info требует созданного экземпляра).
    """
    print("Доступные бэкенды:\n")
    for name, cls in _BACKENDS.items():
        print(f"  {name}:")
        if cls.__doc__:
            print(f"    {cls.__doc__.strip().splitlines()[0]}")
        print()


__all__ = [
    "NativeBackend",
    "get_backend",
    "get_available_backends",
    "list_backends",
]
