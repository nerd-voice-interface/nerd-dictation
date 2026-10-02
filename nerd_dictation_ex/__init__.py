"""
nerd-dictation-ex — диктовка для Linux.

Python-демон поверх нативных модулей (_nerd_dictation_native), встроенных
в бинарь nerd-dictation. Запуск: systemd user-сервис nerd-dictation
(`nerd-dictation daemon`), управление: nerd-dictation begin|end|cancel|toggle.
"""

__version__ = "0.1.0"

from .backends import NativeBackend, get_backend, get_available_backends, list_backends

__all__ = [
    "__version__",
    "NativeBackend",
    "get_backend",
    "get_available_backends",
    "list_backends",
]
