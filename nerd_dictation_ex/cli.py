#!/usr/bin/env python3
"""
nerd-dictation-ex CLI — главная точка входа.

Использование:
    # Запуск daemon через systemd (рекомендуется)
    systemctl --user start nerd-dictation-ex
    systemctl --user enable nerd-dictation-ex  # автозапуск

    # Диктовка
    nerd-dictation-ex toggle             # Переключить диктовку
    nerd-dictation-ex begin              # Начать диктовку
    nerd-dictation-ex end                # Завершить диктовку
    nerd-dictation-ex cancel             # Отменить диктовку

    # Утилиты
    nerd-dictation-ex list-backends      # Показать бэкенды
"""

import argparse
import fcntl
import json
import os
import signal
import socket
import subprocess
import sys
import threading
import time
from enum import Enum
from pathlib import Path
from collections import deque
from typing import Optional, Generator

# Пути
RUNTIME_DIR = Path(os.environ.get("XDG_RUNTIME_DIR", "/tmp"))
SOCKET_PATH = RUNTIME_DIR / "nerd-dictation-ex.sock"
PID_FILE = RUNTIME_DIR / "nerd-dictation-ex.pid"
COOKIE_PATH = Path("/tmp/nerd-dictation-ex.cookie")

CONFIG_DIRS = [
    Path.home() / ".config" / "nerd-dictation-ex",
    Path(__file__).parent.parent.parent.parent / "config",  # Для dev-режима
]


def load_config() -> dict:
    """Загрузить конфигурацию из TOML файла."""
    try:
        import tomllib
    except ImportError:
        import tomli as tomllib

    for config_dir in CONFIG_DIRS:
        config_file = config_dir / "config.toml"
        if config_file.exists():
            with open(config_file, "rb") as f:
                return tomllib.load(f)

    # Проверяем default.toml
    for config_dir in CONFIG_DIRS:
        default_config = config_dir / "default.toml"
        if default_config.exists():
            with open(default_config, "rb") as f:
                return tomllib.load(f)

    # Встроенный дефолт
    return {
        "general": {"backend": "vosk", "input_method": "xdotool"},
        "audio": {"source": "pw-cat", "sample_rate": 16000, "chunk_size": 8000},
        "vosk": {"model_path": "~/.local/share/vosk/vosk-model-ru"},
        "gigaam": {"model_name": "v3_ctc", "device": "cuda"},
        "output": {"trailing_space": True},
    }


# ============================================================================
# Daemon
# ============================================================================

class DaemonState(Enum):
    """Состояние daemon'а."""
    IDLE = "idle"
    LISTENING = "listening"
    STOPPING = "stopping"


class DictationDaemon:
    """Daemon для nerd-dictation-ex. Держит модель в памяти."""

    # Размер кольцевого буфера в секундах
    AUDIO_BUFFER_SECONDS = 1.5

    # Wake word настройки
    WAKE_WORD = "запись"
    WAKE_WORD_ENABLED = True

    def __init__(self, config: dict):
        self.config = config
        self.state = DaemonState.IDLE
        self.backend = None
        self.backend_name = None
        self._audio_proc: Optional[subprocess.Popen] = None
        self._running = True
        self._dictation_thread: Optional[threading.Thread] = None
        self._stop_dictation = threading.Event()

        # Кольцевой буфер аудио
        sample_rate = config.get("audio", {}).get("sample_rate", 16000)
        chunk_size = config.get("audio", {}).get("chunk_size", 8000)
        # Сколько чанков помещается в буфер
        bytes_per_second = sample_rate * 2  # 16-bit = 2 bytes per sample
        buffer_bytes = int(bytes_per_second * self.AUDIO_BUFFER_SECONDS)
        self._buffer_max_chunks = max(1, buffer_bytes // chunk_size)
        self._audio_buffer: deque = deque(maxlen=self._buffer_max_chunks)
        self._audio_buffer_lock = threading.Lock()
        self._audio_listener_thread: Optional[threading.Thread] = None

        # Очередь для wake word (без лимита, wake word loop потребляет данные)
        from queue import Queue
        self._wake_word_queue: Queue = Queue()

        # Wake word detection
        self._wake_word_enabled = config.get("wake_word", {}).get("enabled", self.WAKE_WORD_ENABLED)
        self._wake_word = config.get("wake_word", {}).get("word", self.WAKE_WORD)
        self._wake_word_model_path = os.path.expanduser(
            config.get("wake_word", {}).get("model_path", "~/.local/share/vosk/vosk-model-small-ru")
        )
        self._wake_word_variants = config.get("wake_word", {}).get("variants", ["записи"])
        self._wake_word_module = None  # Нативный wake word модуль
        self._vad_module = None  # Нативный VAD модуль
        self._vad_iterator = None
        self._wake_word_thread: Optional[threading.Thread] = None
        self._wake_word_running = False

        # Noise reduction
        from .noise_reduction import create_noise_reducer
        self._noise_reducer = create_noise_reducer(config)

    def load_backend(self, backend_name: str = None) -> None:
        """Загрузить бэкенд и модель в память."""
        from . import get_backend

        backend_name = backend_name or self.config.get("general", {}).get("backend", "vosk")

        if self.backend and self.backend_name == backend_name:
            print(f"Бэкенд {backend_name} уже загружен", file=sys.stderr)
            return

        print(f"Загрузка бэкенда: {backend_name}...", file=sys.stderr)
        self.backend = get_backend(backend_name)
        self.backend_name = backend_name

        backend_config = self.config.get(backend_name, {})
        sample_rate = self.config.get("audio", {}).get("sample_rate", 16000)

        if backend_name == "vosk":
            model_path = os.path.expanduser(backend_config.get("model_path", ""))
            self.backend.load_model(model_path, sample_rate=sample_rate)
        elif backend_name == "gigaam":
            self.backend.load_model(
                model_name=backend_config.get("model_name", "v3_ctc"),
                device=backend_config.get("device", "cuda"),
            )
        else:
            self.backend.load_model(**backend_config)

        print(f"Бэкенд {backend_name} загружен и готов", file=sys.stderr)

    def _load_wake_word_models(self) -> bool:
        """Загрузить модели для wake word detection (Silero VAD + Vosk small)."""
        if not self._wake_word_enabled:
            return False

        # Загрузить Silero VAD через нативный C++ VADManager
        try:
            from . import vad
            print("Загрузка нативного Silero VAD (ONNX)...", file=sys.stderr)

            # Инициализируем VAD один раз при старте daemon
            if not vad.is_initialized():
                sample_rate = self.config.get("audio", {}).get("sample_rate", 16000)
                if not vad.init(sample_rate=sample_rate, threshold=0.5):
                    print(f"Ошибка инициализации VAD: {vad.get_error()}", file=sys.stderr)
                    return False

            # Создаём NativeVADIterator для wake word detection
            sample_rate = self.config.get("audio", {}).get("sample_rate", 16000)
            self._vad_iterator = vad.NativeVADIterator(
                threshold=0.5,
                sampling_rate=sample_rate,
                min_silence_duration_ms=300,
                speech_pad_ms=100
            )
            # Сохраняем ссылку на модуль для использования в dictation
            self._vad_module = vad
            print("Нативный Silero VAD загружен", file=sys.stderr)
        except Exception as e:
            print(f"Ошибка загрузки нативного VAD: {e}", file=sys.stderr)
            import traceback
            traceback.print_exc()
            return False

        # Загрузить нативный Vosk для wake word (C++ реализация)
        try:
            from . import wake_word
            print(f"Загрузка нативного Vosk для wake word: {self._wake_word_model_path}...", file=sys.stderr)

            sample_rate = self.config.get("audio", {}).get("sample_rate", 16000)

            # Инициализируем wake word manager
            if not wake_word.init(
                model_path=self._wake_word_model_path,
                wake_word=self._wake_word,
                variants=self._wake_word_variants,
                sample_rate=sample_rate,
                log_level=-1  # Тихий режим
            ):
                error = wake_word.get_error()
                print(f"Ошибка инициализации wake word: {error}", file=sys.stderr)
                if "not found" in error.lower():
                    print("Скачайте: https://alphacephei.com/vosk/models", file=sys.stderr)
                return False

            self._wake_word_module = wake_word
            print("Нативный Vosk для wake word загружен", file=sys.stderr)
            return True
        except Exception as e:
            print(f"Ошибка загрузки нативного wake word: {e}", file=sys.stderr)
            import traceback
            traceback.print_exc()
            return False

    def _start_wake_word_detection(self) -> None:
        """Запустить поток wake word detection."""
        if not self._wake_word_enabled:
            return

        if self._wake_word_thread and self._wake_word_thread.is_alive():
            return

        if not self._load_wake_word_models():
            print("Wake word detection отключён из-за ошибок загрузки", file=sys.stderr)
            return

        self._wake_word_running = True
        self._wake_word_thread = threading.Thread(
            target=self._wake_word_loop, daemon=True
        )
        self._wake_word_thread.start()
        print(f"Wake word detection запущен (слово: '{self._wake_word}')", file=sys.stderr)

    def _wake_word_loop(self) -> None:
        """Цикл распознавания wake word."""
        import numpy as np
        from queue import Empty

        sample_rate = self.config.get("audio", {}).get("sample_rate", 16000)

        # VAD требует чанки по 512 сэмплов при 16kHz (или 256 при 8kHz)
        vad_chunk_samples = 512 if sample_rate == 16000 else 256
        vad_buffer = np.array([], dtype=np.int16)

        # Буфер для накопления речи перед отправкой в Vosk
        speech_buffer = b''
        is_speech_active = False

        while self._wake_word_running and self._running:
            # Пропускаем, если диктовка уже активна
            if self.state == DaemonState.LISTENING:
                # Сбросить состояние и очистить очередь
                vad_buffer = np.array([], dtype=np.int16)
                speech_buffer = b''
                is_speech_active = False
                # Очистить очередь
                while not self._wake_word_queue.empty():
                    try:
                        self._wake_word_queue.get_nowait()
                    except Empty:
                        break
                # НЕ вызываем _vad_iterator.reset_states() - вызывает heap corruption
                # VAD состояние сбрасывается только в начале диктовки
                if self._wake_word_module:
                    self._wake_word_module.reset()
                time.sleep(0.1)
                continue

            # Получить аудио из очереди (блокирующий вызов с таймаутом)
            try:
                chunk = self._wake_word_queue.get(timeout=0.1)
            except Empty:
                continue

            # Конвертировать в numpy
            audio_np = np.frombuffer(chunk, dtype=np.int16)
            vad_buffer = np.concatenate([vad_buffer, audio_np])

            # Обрабатываем VAD чанками по 512 сэмплов (16kHz) или 256 (8kHz)
            while len(vad_buffer) >= vad_chunk_samples:
                vad_chunk = vad_buffer[:vad_chunk_samples]
                vad_buffer = vad_buffer[vad_chunk_samples:]

                # Проверяем VAD (NativeVADIterator принимает int16 напрямую)
                speech_dict = self._vad_iterator(vad_chunk, return_seconds=False)

                if speech_dict:
                    if 'start' in speech_dict:
                        is_speech_active = True
                        speech_buffer = vad_chunk.tobytes()
                    elif 'end' in speech_dict:
                        is_speech_active = False
                        speech_buffer += vad_chunk.tobytes()

                        # Распознаём накопленную речь через нативный wake word manager
                        if speech_buffer and self._wake_word_module:
                            result = self._wake_word_module.process_speech(speech_buffer)

                            if result.text:
                                print(f"[wake word] распознано: '{result.text}'", file=sys.stderr)

                                # Проверка на wake word уже сделана в нативном коде
                                if result.detected:
                                    print(f"[wake word] активация по слову '{self._wake_word}'!", file=sys.stderr)
                                    # Сбросить состояние распознавателя
                                    self._wake_word_module.reset()
                                    # Очистить аудио-буфер чтобы wake word не попал в диктовку
                                    with self._audio_buffer_lock:
                                        self._audio_buffer.clear()
                                    self.begin_dictation()

                        speech_buffer = b''
                        if self._wake_word_module:
                            self._wake_word_module.reset()
                elif is_speech_active:
                    speech_buffer += vad_chunk.tobytes()

    def _start_audio_listener(self) -> None:
        """Запустить постоянный слушатель аудио."""
        if self._audio_listener_thread and self._audio_listener_thread.is_alive():
            return

        self._audio_listener_thread = threading.Thread(
            target=self._audio_listener_loop, daemon=True
        )
        self._audio_listener_thread.start()
        print("Аудио-слушатель запущен", file=sys.stderr)

    def _audio_listener_loop(self) -> None:
        """Постоянно слушать микрофон и заполнять кольцевой буфер."""
        cmd = _get_audio_command(self.config)
        chunk_size = self.config.get("audio", {}).get("chunk_size", 8000)

        while self._running:
            try:
                self._audio_proc = subprocess.Popen(cmd, stdout=subprocess.PIPE)

                # Блокирующее чтение — ждём пока накопится chunk_size байт
                while self._running and self._audio_proc.poll() is None:
                    chunk = self._audio_proc.stdout.read(chunk_size)
                    if chunk:
                        with self._audio_buffer_lock:
                            self._audio_buffer.append(chunk)
                        # Дублируем в очередь для wake word
                        if self._wake_word_running and self.state != DaemonState.LISTENING:
                            try:
                                self._wake_word_queue.put_nowait(chunk)
                            except:
                                pass  # Очередь переполнена - пропускаем
                    elif not chunk:
                        # EOF или процесс завершился
                        break
            except Exception as e:
                print(f"Ошибка аудио-слушателя: {e}", file=sys.stderr)
            finally:
                if self._audio_proc:
                    self._audio_proc.terminate()
                    self._audio_proc.wait()
                    self._audio_proc = None

            if self._running:
                time.sleep(0.5)  # Пауза перед перезапуском

    def _audio_stream(self) -> Generator[bytes, None, None]:
        """Генератор аудио-чанков: сначала буфер, потом живой поток."""
        # Сначала отдаём накопленный буфер
        with self._audio_buffer_lock:
            buffered = list(self._audio_buffer)
            self._audio_buffer.clear()

        for chunk in buffered:
            if self._stop_dictation.is_set():
                return
            # Применяем шумоподавление если включено
            if self._noise_reducer:
                chunk = self._noise_reducer.process_chunk(chunk)
            yield chunk

        # Затем читаем живой поток
        while not self._stop_dictation.is_set():
            with self._audio_buffer_lock:
                if self._audio_buffer:
                    chunk = self._audio_buffer.popleft()
                    # Применяем шумоподавление если включено
                    if self._noise_reducer:
                        chunk = self._noise_reducer.process_chunk(chunk)
                    yield chunk
                    continue
            time.sleep(0.01)

    def _type_text(self, text: str) -> None:
        """Ввести текст через выбранный метод."""
        from .input import type_text, press_keys
        from .postprocess import process_text, TextAction, KeyAction
        from .sounds import play_execute

        actions = process_text(text)
        for action in actions:
            if isinstance(action, TextAction):
                type_text(action.text, self.config)
            elif isinstance(action, KeyAction):
                play_execute()
                press_keys(action.keys, self.config)

    # VAD параметры для offline бэкендов (как в оригинальном GigaAM backend)
    VAD_THRESHOLD = 0.5  # Порог вероятности речи
    MIN_SILENCE_MS = 500  # Минимальная пауза для сегментации (мс)
    MIN_SPEECH_MS = 100  # Минимальная длина речи (мс)
    MAX_BUFFER_SECONDS = 25  # Максимум аудио в буфере

    def _dictation_loop(self) -> None:
        """Основной цикл диктовки (в отдельном потоке)."""
        sample_rate = self.config.get("audio", {}).get("sample_rate", 16000)

        # Проверяем, поддерживает ли бэкенд потоковое распознавание
        is_streaming = getattr(self.backend, 'supports_streaming', True)

        if is_streaming:
            self._dictation_loop_streaming(sample_rate)
        else:
            self._dictation_loop_buffered(sample_rate)

    def _dictation_loop_streaming(self, sample_rate: int) -> None:
        """Цикл диктовки для потоковых бэкендов."""
        try:
            for result in self.backend.transcribe_stream(
                self._audio_stream(),
                sample_rate=sample_rate,
            ):
                if self._stop_dictation.is_set():
                    break
                if result.is_final and result.text:
                    self._type_text(result.text)

            print("[daemon] вызываю finalize", file=sys.stderr)
            final = self.backend.finalize()
            if final and final.text:
                self._type_text(final.text)
        except Exception as e:
            print(f"Ошибка диктовки: {e}", file=sys.stderr)
        finally:
            self.backend.reset()
            self.state = DaemonState.IDLE

    def _dictation_loop_buffered(self, sample_rate: int) -> None:
        """
        Цикл диктовки для offline бэкендов с VAD-сегментацией.

        Использует нативный Silero VAD (ONNX) для определения пауз в речи.
        Распознаёт накопленный буфер при обнаружении паузы.
        """
        import numpy as np

        # Получаем ссылку на VAD модуль (должен быть инициализирован от wake word)
        vad = getattr(self, '_vad_module', None)
        if vad is None:
            from . import vad as vad_module
            vad = vad_module
            self._vad_module = vad

        # Инициализируем VAD если ещё не инициализирован
        if not vad.is_initialized():
            print("[daemon] Инициализирую нативный VAD для сегментации...", file=sys.stderr)
            if not vad.init(sample_rate=sample_rate, threshold=self.VAD_THRESHOLD):
                print(f"[daemon] Ошибка инициализации VAD: {vad.get_error()}", file=sys.stderr)
                return

        # Сбрасываем состояние VAD в начале каждой сессии диктовки
        # Это безопасно - C++ VADManager корректно обрабатывает reset
        vad.reset_states()

        # VAD состояние
        vad_chunk_samples = vad.get_chunk_size()  # 512 для 16kHz, 256 для 8kHz
        vad_buffer = np.array([], dtype=np.int16)
        silence_samples = 0
        speech_samples = 0
        in_speech = False

        # Аудио буфер для распознавания
        audio_buffer = bytearray()
        max_buffer_bytes = self.MAX_BUFFER_SECONDS * sample_rate * 2

        # Пороги в samples
        min_silence = int(self.MIN_SILENCE_MS * sample_rate / 1000)
        min_speech = int(self.MIN_SPEECH_MS * sample_rate / 1000)

        def reset_vad_state():
            nonlocal silence_samples, speech_samples, in_speech, vad_buffer
            silence_samples = 0
            speech_samples = 0
            in_speech = False
            vad_buffer = np.array([], dtype=np.int16)
            # НЕ вызываем reset_states() - это вызывает heap corruption в torch
            # Silero VAD корректно обрабатывает непрерывный поток без сброса

        # Минимум 0.5 секунды аудио для распознавания
        min_audio_bytes = int(0.5 * sample_rate * 2)

        def transcribe_and_type():
            """Распознать буфер и вывести текст."""
            nonlocal audio_buffer
            if len(audio_buffer) < min_audio_bytes:
                audio_buffer.clear()
                return

            # Подаём всё аудио в бэкенд и финализируем
            self.backend._backend.feed(bytes(audio_buffer))
            result = self.backend._backend.finalize()
            audio_buffer.clear()

            # Фильтруем пустые результаты
            text = result.text.strip() if result.text else ""
            if text:
                print(f"[daemon] распознано: '{text}'", file=sys.stderr)
                self._type_text(text)

        try:
            for chunk in self._audio_stream():
                if self._stop_dictation.is_set():
                    break

                if not chunk:
                    continue

                # Добавляем в аудио буфер
                audio_buffer.extend(chunk)

                # Конвертируем для VAD
                audio_np = np.frombuffer(chunk, dtype=np.int16)
                vad_buffer = np.concatenate([vad_buffer, audio_np])

                # Обрабатываем VAD чанками
                while len(vad_buffer) >= vad_chunk_samples:
                    vad_chunk = vad_buffer[:vad_chunk_samples]
                    vad_buffer = vad_buffer[vad_chunk_samples:]

                    # Вызываем нативный VAD (принимает int16 bytes)
                    vad_result = vad.process_chunk(vad_chunk.tobytes())
                    prob = vad_result.speech_probability

                    if prob >= self.VAD_THRESHOLD:
                        speech_samples += vad_chunk_samples
                        silence_samples = 0
                        in_speech = True
                    else:
                        silence_samples += vad_chunk_samples

                # Распознаём при паузе после речи или переполнении буфера
                should_transcribe = (
                    (in_speech and silence_samples >= min_silence and speech_samples >= min_speech) or
                    len(audio_buffer) >= max_buffer_bytes
                )

                if should_transcribe:
                    transcribe_and_type()
                    reset_vad_state()

            # Финальный результат
            print("[daemon] вызываю finalize (buffered)", file=sys.stderr)
            transcribe_and_type()

        except Exception as e:
            print(f"Ошибка диктовки (buffered): {e}", file=sys.stderr)
            import traceback
            traceback.print_exc()
        finally:
            # Очищаем состояние VAD (только если он был использован)
            reset_vad_state()
            self.backend.reset()
            self.state = DaemonState.IDLE

    def begin_dictation(self) -> dict:
        """Начать диктовку."""
        if self.state == DaemonState.LISTENING:
            return {"status": "error", "message": "Диктовка уже запущена"}

        if not self.backend:
            return {"status": "error", "message": "Бэкенд не загружен"}

        self.state = DaemonState.LISTENING
        self._stop_dictation.clear()

        # Сбросить постобработчик для новой диктовки
        from .postprocess import reset_processor
        from .sounds import play_begin
        reset_processor()
        # Сбросить фильтр шумоподавления для новой сессии
        if self._noise_reducer:
            self._noise_reducer.reset()
        play_begin()

        self._dictation_thread = threading.Thread(target=self._dictation_loop, daemon=True)
        self._dictation_thread.start()

        return {"status": "ok", "message": "Диктовка запущена"}

    def end_dictation(self) -> dict:
        """Завершить диктовку с выводом текста."""
        if self.state != DaemonState.LISTENING:
            return {"status": "error", "message": "Диктовка не запущена"}

        from .sounds import play_end
        play_end()

        self._stop_dictation.set()

        if self._dictation_thread:
            self._dictation_thread.join(timeout=2.0)

        self.state = DaemonState.IDLE
        return {"status": "ok", "message": "Диктовка завершена"}

    def cancel_dictation(self) -> dict:
        """Отменить диктовку без вывода."""
        if self.state != DaemonState.LISTENING:
            return {"status": "error", "message": "Диктовка не запущена"}

        self._stop_dictation.set()
        self.backend.reset()
        self.state = DaemonState.IDLE

        if self._dictation_thread:
            self._dictation_thread.join(timeout=1.0)

        return {"status": "ok", "message": "Диктовка отменена"}

    def get_status(self) -> dict:
        """Получить статус daemon'а."""
        return {
            "status": "ok",
            "state": self.state.value,
            "backend": self.backend_name,
            "pid": os.getpid(),
        }

    def handle_command(self, cmd: dict) -> dict:
        """Обработать команду от клиента."""
        action = cmd.get("action")
        print(f"Команда: {action}", file=sys.stderr)

        handlers = {
            "begin": self.begin_dictation,
            "end": self.end_dictation,
            "cancel": self.cancel_dictation,
            "status": self.get_status,
            "reload": lambda: self._reload_backend(cmd.get("backend")),
            "shutdown": self._shutdown,
        }

        if action not in handlers:
            return {"status": "error", "message": f"Неизвестная команда: {action}"}

        result = handlers[action]()
        print(f"  -> {result.get('status')}: {result.get('message', result.get('state', ''))}", file=sys.stderr)
        return result

    def _reload_backend(self, backend_name: str = None) -> dict:
        """Перезагрузить бэкенд."""
        if self.state == DaemonState.LISTENING:
            return {"status": "error", "message": "Остановите диктовку перед перезагрузкой"}

        try:
            self.backend = None
            self.backend_name = None
            self.load_backend(backend_name)
            return {"status": "ok", "message": f"Бэкенд {self.backend_name} перезагружен"}
        except Exception as e:
            return {"status": "error", "message": str(e)}

    def _shutdown(self) -> dict:
        """Остановить daemon."""
        self._running = False
        self._wake_word_running = False
        return {"status": "ok", "message": "Daemon останавливается"}

    def run(self) -> None:
        """Запустить daemon."""
        if SOCKET_PATH.exists():
            SOCKET_PATH.unlink()

        server = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        server.bind(str(SOCKET_PATH))
        server.listen(5)
        server.settimeout(1.0)

        PID_FILE.write_text(str(os.getpid()))

        # Запустить постоянный аудио-слушатель
        self._start_audio_listener()

        # Запустить wake word detection если включено
        self._start_wake_word_detection()

        print(f"Daemon запущен, socket: {SOCKET_PATH}", file=sys.stderr)

        try:
            while self._running:
                try:
                    client, _ = server.accept()
                    self._handle_client(client)
                except socket.timeout:
                    continue
        finally:
            server.close()
            SOCKET_PATH.unlink(missing_ok=True)
            PID_FILE.unlink(missing_ok=True)
            print("Daemon остановлен", file=sys.stderr)

    def _handle_client(self, client: socket.socket) -> None:
        """Обработать подключение клиента."""
        try:
            data = client.recv(4096).decode()
            if data:
                cmd = json.loads(data)
                response = self.handle_command(cmd)
                client.send(json.dumps(response).encode())
        except Exception as e:
            error_response = {"status": "error", "message": str(e)}
            client.send(json.dumps(error_response).encode())
        finally:
            client.close()


class DaemonClient:
    """Клиент для общения с daemon'ом."""

    @staticmethod
    def send_command(cmd: dict, timeout: float = 5.0) -> dict:
        """Отправить команду daemon'у."""
        if not SOCKET_PATH.exists():
            return {"status": "error", "message": "Daemon не запущен"}

        try:
            client = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            client.settimeout(timeout)
            client.connect(str(SOCKET_PATH))
            client.send(json.dumps(cmd).encode())
            response = client.recv(4096).decode()
            client.close()
            return json.loads(response)
        except socket.timeout:
            return {"status": "error", "message": "Таймаут соединения с daemon"}
        except ConnectionRefusedError:
            return {"status": "error", "message": "Daemon не отвечает"}
        except Exception as e:
            return {"status": "error", "message": str(e)}

    @staticmethod
    def is_running() -> bool:
        """Проверить, запущен ли daemon."""
        if not PID_FILE.exists():
            return False

        try:
            pid = int(PID_FILE.read_text())
            os.kill(pid, 0)
            return True
        except (ProcessLookupError, ValueError):
            PID_FILE.unlink(missing_ok=True)
            SOCKET_PATH.unlink(missing_ok=True)
            return False


# ============================================================================
# Команды для работы через daemon
# ============================================================================

def cmd_begin(args, config: dict) -> None:
    """Начать диктовку (через daemon или напрямую)."""
    if DaemonClient.is_running():
        response = DaemonClient.send_command({"action": "begin"})
        if response.get("status") == "ok":
            print("Диктовка запущена", file=sys.stderr)
        else:
            print(f"Ошибка: {response.get('message')}", file=sys.stderr)
            sys.exit(1)
    else:
        print("Daemon не запущен, загрузка модели...", file=sys.stderr)
        print("Для быстрого отклика: systemctl --user start nerd-dictation-ex", file=sys.stderr)
        _cmd_begin_direct(args, config)


def cmd_end(args, config: dict) -> None:
    """Завершить диктовку."""
    if DaemonClient.is_running():
        response = DaemonClient.send_command({"action": "end"})
        if response.get("status") == "ok":
            print("Диктовка завершена", file=sys.stderr)
        else:
            print(f"Ошибка: {response.get('message')}", file=sys.stderr)
    else:
        _cmd_end_direct(args, config)


def cmd_cancel(args, config: dict) -> None:
    """Отменить диктовку."""
    if DaemonClient.is_running():
        response = DaemonClient.send_command({"action": "cancel"})
        if response.get("status") == "ok":
            print("Диктовка отменена", file=sys.stderr)
        else:
            print(f"Ошибка: {response.get('message')}", file=sys.stderr)
    else:
        _cmd_cancel_direct(args, config)


def cmd_toggle(args, config: dict) -> None:
    """Переключить диктовку (begin/end)."""
    if DaemonClient.is_running():
        status = DaemonClient.send_command({"action": "status"})
        if status.get("state") == "listening":
            cmd_end(args, config)
        else:
            cmd_begin(args, config)
    else:
        if COOKIE_PATH.exists():
            _cmd_end_direct(args, config)
        else:
            _cmd_begin_direct(args, config)


# ============================================================================
# Fallback: прямой режим (без daemon)
# ============================================================================

def _get_audio_command(config: dict) -> list:
    """Получить команду для записи аудио."""
    source = config.get("audio", {}).get("source", "pw-cat")
    sample_rate = config.get("audio", {}).get("sample_rate", 16000)

    commands = {
        "pw-cat": ["pw-cat", "--record", "--format", "s16",
                   "--rate", str(sample_rate), "--channels", "1", "-"],
        "parec": ["parec", f"--format=s16ne", f"--rate={sample_rate}",
                  "--channels=1", "--raw"],
        "sox": ["sox", "-q", "-d", "-t", "raw", "-b", "16",
                "-e", "signed-integer", "-r", str(sample_rate), "-c", "1", "-"],
    }

    if source not in commands:
        raise ValueError(f"Неизвестный источник аудио: {source}")
    return commands[source]


def _audio_stream(config: dict) -> Generator[bytes, None, None]:
    """Генератор аудио-чанков."""
    from .noise_reduction import create_noise_reducer
    
    cmd = _get_audio_command(config)
    chunk_size = config.get("audio", {}).get("chunk_size", 8000)
    noise_reducer = create_noise_reducer(config)

    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE)

    fd = proc.stdout.fileno()
    fl = fcntl.fcntl(fd, fcntl.F_GETFL)
    fcntl.fcntl(fd, fcntl.F_SETFL, fl | os.O_NONBLOCK)

    try:
        while proc.poll() is None:
            try:
                chunk = proc.stdout.read(chunk_size)
                if chunk:
                    # Применяем шумоподавление если включено
                    if noise_reducer:
                        chunk = noise_reducer.process_chunk(chunk)
                    yield chunk
            except BlockingIOError:
                time.sleep(0.01)
    finally:
        proc.terminate()
        proc.wait()


def _cmd_begin_direct(args, config: dict) -> None:
    """Начать диктовку напрямую (без daemon)."""
    from . import get_backend, get_available_backends
    from .input import type_text
    from .sounds import play_begin, play_end

    # Проверка: если daemon запущен — не запускать direct mode
    if DaemonClient.is_running():
        print("Daemon уже запущен! Используйте daemon для диктовки.", file=sys.stderr)
        print("Команда: nerd-dictation begin", file=sys.stderr)
        sys.exit(1)

    if COOKIE_PATH.exists():
        print("Диктовка уже запущена. Используйте 'end' или 'cancel'.", file=sys.stderr)
        sys.exit(1)

    backend_name = getattr(args, 'backend', None) or config.get("general", {}).get("backend", "vosk")

    available = get_available_backends()
    if not available:
        print("Ошибка: нет установленных бэкендов.", file=sys.stderr)
        print("\nУстановите бэкенд:", file=sys.stderr)
        print("  pip install nerd-dictation-vosk    # Vosk (CPU)", file=sys.stderr)
        print("  pip install nerd-dictation-gigaam  # GigaAM (GPU)", file=sys.stderr)
        sys.exit(1)

    if backend_name not in available:
        print(f"Ошибка: бэкенд '{backend_name}' не установлен.", file=sys.stderr)
        print(f"Доступные: {', '.join(available.keys())}", file=sys.stderr)
        sys.exit(1)

    print(f"Загрузка бэкенда: {backend_name}...", file=sys.stderr)
    backend = get_backend(backend_name)

    backend_config = config.get(backend_name, {})
    sample_rate = config.get("audio", {}).get("sample_rate", 16000)

    if backend_name == "vosk":
        model_path = os.path.expanduser(backend_config.get("model_path", ""))
        backend.load_model(model_path, sample_rate=sample_rate)
    elif backend_name == "gigaam":
        backend.load_model(
            model_name=backend_config.get("model_name", "v3_ctc"),
            device=backend_config.get("device", "cuda"),
        )
    else:
        backend.load_model(**backend_config)

    COOKIE_PATH.write_text(str(os.getpid()))

    running = True

    def handle_signal(signum, frame):
        nonlocal running
        running = False

    signal.signal(signal.SIGTERM, handle_signal)
    signal.signal(signal.SIGINT, handle_signal)

    print("Диктовка запущена. Говорите...", file=sys.stderr)
    play_begin()

    try:
        with backend:
            for result in backend.transcribe_stream(
                _audio_stream(config),
                sample_rate=sample_rate,
            ):
                if not running:
                    break
                if result.is_final and result.text:
                    type_text(result.text, config)

            if running:
                final = backend.finalize()
                if final and final.text:
                    type_text(final.text, config)
    finally:
        play_end()
        COOKIE_PATH.unlink(missing_ok=True)
        print("\nДиктовка завершена.", file=sys.stderr)


def _cmd_end_direct(args, config: dict) -> None:
    """Завершить диктовку (прямой режим)."""
    if not COOKIE_PATH.exists():
        print("Диктовка не запущена.", file=sys.stderr)
        sys.exit(1)

    pid = int(COOKIE_PATH.read_text())
    try:
        os.kill(pid, signal.SIGTERM)
    except ProcessLookupError:
        COOKIE_PATH.unlink(missing_ok=True)
        print("Процесс диктовки не найден.", file=sys.stderr)


def _cmd_cancel_direct(args, config: dict) -> None:
    """Отменить диктовку (прямой режим)."""
    if not COOKIE_PATH.exists():
        print("Диктовка не запущена.", file=sys.stderr)
        sys.exit(1)

    pid = int(COOKIE_PATH.read_text())
    try:
        os.kill(pid, signal.SIGKILL)
    except ProcessLookupError:
        pass
    COOKIE_PATH.unlink(missing_ok=True)


# ============================================================================
# Утилиты
# ============================================================================

def cmd_list_backends(args, config: dict) -> None:
    """Показать доступные бэкенды."""
    from . import list_backends
    list_backends()


# ============================================================================
# Main
# ============================================================================

def _run_daemon(config: dict) -> None:
    """Запустить daemon (для systemd)."""
    # Lockfile для предотвращения запуска нескольких экземпляров
    runtime_dir = os.environ.get("XDG_RUNTIME_DIR", f"/run/user/{os.getuid()}")
    lockfile_path = os.path.join(runtime_dir, "nerd-dictation-ex.lock")

    try:
        lockfile = open(lockfile_path, "w")
        fcntl.flock(lockfile.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
        lockfile.write(str(os.getpid()))
        lockfile.flush()
    except (IOError, OSError) as e:
        print(f"FATAL: Daemon уже запущен (lockfile: {lockfile_path})", file=sys.stderr)
        print(f"Убейте существующий процесс или удалите lockfile.", file=sys.stderr)
        sys.exit(1)

    daemon = DictationDaemon(config)

    def handle_signal(signum, frame):
        daemon._running = False

    signal.signal(signal.SIGTERM, handle_signal)
    signal.signal(signal.SIGINT, handle_signal)

    daemon.load_backend()
    daemon.run()


def main():
    """Точка входа для nerd-dictation-ex."""
    parser = argparse.ArgumentParser(
        description="nerd-dictation-ex — диктовка с подключаемыми бэкендами ASR",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Примеры:
  # Запуск через systemd (рекомендуется):
  systemctl --user start nerd-dictation-ex   # Запустить daemon
  nerd-dictation-ex toggle                   # Переключить диктовку

  # Без daemon (медленный старт):
  nerd-dictation-ex begin --no-daemon        # Загружать модель каждый раз
        """
    )
    subparsers = parser.add_subparsers(dest="command", required=True)

    # daemon (для systemd)
    subparsers.add_parser("daemon", help="Запустить daemon (для systemd)")

    # begin
    begin_parser = subparsers.add_parser("begin", help="Начать диктовку")
    begin_parser.add_argument("--backend", "-b", help="Бэкенд (переопределяет конфиг)")
    begin_parser.add_argument("--no-daemon", action="store_true",
                              help="Не использовать daemon (медленно)")

    # end
    subparsers.add_parser("end", help="Завершить диктовку")

    # cancel
    subparsers.add_parser("cancel", help="Отменить диктовку")

    # toggle
    subparsers.add_parser("toggle", help="Переключить диктовку (begin/end)")

    # list-backends
    subparsers.add_parser("list-backends", help="Показать доступные бэкенды")

    # test-postprocess (для отладки)
    subparsers.add_parser("test-postprocess", help="Тест постобработки текста")

    # test-audio (для отладки)
    subparsers.add_parser("test-audio", help="Тест шумоподавления аудио")

    args = parser.parse_args()
    config = load_config()

    if args.command == "daemon":
        _run_daemon(config)
    elif args.command == "begin":
        if getattr(args, "no_daemon", False):
            _cmd_begin_direct(args, config)
        else:
            cmd_begin(args, config)
    elif args.command == "end":
        cmd_end(args, config)
    elif args.command == "cancel":
        cmd_cancel(args, config)
    elif args.command == "toggle":
        cmd_toggle(args, config)
    elif args.command == "list-backends":
        cmd_list_backends(args, config)
    elif args.command == "test-postprocess":
        cmd_test_postprocess()
    elif args.command == "test-audio":
        cmd_test_audio()


def cmd_test_audio():
    """Тест шумоподавления аудио."""
    import struct
    import math
    from nerd_dictation_ex.noise_reduction import NoiseReducer, AudioPreprocessorConfig

    print("=" * 50)
    print("Testing audio preprocessing (WebRTC noise suppression)")
    print("=" * 50)

    # Test 1: Create NoiseReducer
    print("\nTest 1 - Create NoiseReducer:")
    try:
        reducer = NoiseReducer(
            sample_rate=16000,
            enabled=True,
            noise_level="high",
            agc_enabled=True,
        )
        print("  ✓ NoiseReducer created successfully")
    except Exception as e:
        print(f"  ✗ Failed to create NoiseReducer: {e}")
        sys.exit(1)

    # Test 2: Process silence (should work)
    print("\nTest 2 - Process silence:")
    try:
        # Generate 10ms of silence (160 samples at 16kHz)
        silence = bytes(320)  # 160 samples * 2 bytes
        processed = reducer.process_chunk(silence)
        print(f"  Input:  {len(silence)} bytes")
        print(f"  Output: {len(processed)} bytes")
        print("  ✓ PASS")
    except Exception as e:
        print(f"  ✗ FAIL: {e}")
        sys.exit(1)

    # Test 3: Process sine wave
    print("\nTest 3 - Process sine wave (440Hz):")
    try:
        # Generate 100ms of 440Hz sine wave
        num_samples = 1600  # 100ms at 16kHz
        samples = []
        for i in range(num_samples):
            # 440Hz sine wave at half amplitude
            sample = int(16384 * math.sin(2 * math.pi * 440 * i / 16000))
            samples.append(sample)

        audio_bytes = struct.pack(f"<{len(samples)}h", *samples)
        processed = reducer.process_chunk(audio_bytes)

        # Decode processed
        processed_samples = struct.unpack(f"<{len(processed)//2}h", processed)

        # Check output is not empty and has reasonable amplitude
        max_in = max(abs(s) for s in samples)
        max_out = max(abs(s) for s in processed_samples) if processed_samples else 0

        print(f"  Input:  {len(samples)} samples, max={max_in}")
        print(f"  Output: {len(processed_samples)} samples, max={max_out}")

        if len(processed_samples) > 0 and max_out > 0:
            print("  ✓ PASS")
        else:
            print("  ✗ FAIL: Empty or zero output")
            sys.exit(1)
    except Exception as e:
        print(f"  ✗ FAIL: {e}")
        sys.exit(1)

    # Test 4: Reset
    print("\nTest 4 - Reset:")
    try:
        reducer.reset()
        print("  ✓ PASS")
    except Exception as e:
        print(f"  ✗ FAIL: {e}")
        sys.exit(1)

    # Test 5: Disabled reducer
    print("\nTest 5 - Disabled reducer:")
    try:
        disabled = NoiseReducer(enabled=False)
        test_data = b"\x00\x10" * 100
        result = disabled.process_chunk(test_data)
        assert result == test_data, "Disabled reducer should pass through unchanged"
        print("  ✓ PASS")
    except Exception as e:
        print(f"  ✗ FAIL: {e}")
        sys.exit(1)

    print("\n" + "=" * 50)
    print("All audio tests PASSED!")
    print("=" * 50)


def cmd_test_postprocess():
    """Тест постобработки текста."""
    from nerd_dictation_ex import postprocess

    print("=" * 50)
    print("Testing postprocess native module")
    print("=" * 50)

    def test(name, text, expected_check):
        """Run a single test."""
        postprocess.reset_processor()
        actions = postprocess.process_text(text)
        print(f'\n{name}: "{text}"')
        for a in actions:
            print(f'  -> {a}')
        try:
            expected_check(actions)
            print("  ✓ PASS")
            return True
        except AssertionError as e:
            print(f"  ✗ FAIL: {e}")
            return False

    passed = 0
    total = 0

    # Test 1: Basic punctuation
    total += 1
    if test("Test 1 - Punctuation",
            "привет запятая мир точка",
            lambda a: (
                a[0].text.startswith("Привет") and
                "," in a[0].text and
                "." in a[0].text
            )):
        passed += 1

    # Test 2: Key action
    total += 1
    if test("Test 2 - Key action",
            "привет выполнить мир",
            lambda a: (
                len(a) == 3 and
                postprocess.is_text_action(a[0]) and
                postprocess.is_key_action(a[1]) and
                a[1].keys == "ctrl+Return" and
                postprocess.is_text_action(a[2])
            )):
        passed += 1

    # Test 3: Capitalization
    total += 1
    if test("Test 3 - Capitalization",
            "это тест",
            lambda a: a[0].text.startswith("Э")):  # Capitalized Э
        passed += 1

    # Test 4: Emoji
    total += 1
    if test("Test 4 - Emoji",
            "привет смайлик улыбка",
            lambda a: "😊" in a[0].text):
        passed += 1

    # Test 5: Multiple fragments
    postprocess.reset_processor()
    a1 = postprocess.process_text("первое точка")
    a2 = postprocess.process_text("второе")
    print(f'\nTest 5 - Multi-fragment: "первое точка" + "второе"')
    for a in a1 + a2:
        print(f'  -> {a}')
    total += 1
    try:
        # Second fragment should be capitalized after period
        assert a2[0].text.strip().startswith("В"), f"Expected 'В', got '{a2[0].text}'"
        print("  ✓ PASS")
        passed += 1
    except AssertionError as e:
        print(f"  ✗ FAIL: {e}")

    print("\n" + "=" * 50)
    print(f"Results: {passed}/{total} tests passed")
    print("=" * 50)

    if passed < total:
        sys.exit(1)


if __name__ == "__main__":
    main()
