#!/usr/bin/env python3
"""Проверка конвейера диктовки: распознавание не должно терять речь.

Прогоняется через НАСТОЯЩИЙ путь данных: реальный _audio_listener_loop с
подставленной командой записи, реальный _audio_stream, реальный
_dictation_loop_buffered. Заглушками подменены только нативные модули,
недоступные вне хост-процесса (_nerd_dictation_native embedded в C++ бинарь).

Каждый 0.25-секундный чанк заполнен своим номером, поэтому потерю речи
видно побайтово, а не по косвенным признакам.

Правила:
  R1  ни один чанк не потерян при RTF>1
  R2  сегменты распознаны строго в порядке произнесения
  R3  хвост (недособранный буфер на момент end) не теряется
  R4  cancel выбрасывает сегменты отменённой сессии, но не следующей
  R5  LISTENING -> FINALIZING -> IDLE, в IDLE переводит только воркер
  R6  вытеснение из кольцевого буфера фиксируется счётчиком

Запуск:  python3 tests/test_dictation_pipeline.py
"""

import os
import sys
import time
import types
import threading

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, REPO)

# --- Заглушки нативных модулей (до импорта cli) -----------------------------

TYPED = []          # текст, дошедший до "печати"
TYPED_LOCK = threading.Lock()


def _stub(name, **attrs):
    m = types.ModuleType(f"nerd_dictation_ex.{name}")
    for k, v in attrs.items():
        setattr(m, k, v)
    sys.modules[f"nerd_dictation_ex.{name}"] = m
    return m


_stub("noise_reduction", create_noise_reducer=lambda config: None)
_stub("sounds", play_begin=lambda: None, play_end=lambda: None,
      play_execute=lambda: None)


class TextAction:
    def __init__(self, text):
        self.text = text


class KeyAction:
    def __init__(self, keys):
        self.keys = keys


_stub("postprocess", process_text=lambda t: [TextAction(t)],
      reset_processor=lambda: None, TextAction=TextAction, KeyAction=KeyAction)


def _type_text(text, config):
    with TYPED_LOCK:
        TYPED.append(text)


_stub("input", type_text=_type_text, press_keys=lambda k, c: None)

# VAD: режет сегмент каждые SEG_CHUNKS аудио-чанков, детерминированно
SEG_CHUNKS = 4          # сегмент = 4 чанка = 1.0 s аудио
_vad_calls = {"n": 0}


class _VadResult:
    def __init__(self, p):
        self.speech_probability = p


def _vad_process(chunk_bytes):
    # 512 сэмплов на вызов; 8000-байтный аудио-чанк = 4000 сэмплов ≈ 7.8 вызова
    _vad_calls["n"] += 1
    period = int(SEG_CHUNKS * 4000 / 512)
    # речь почти весь период, тишина в конце — чтобы сработал min_silence
    return _VadResult(0.9 if (_vad_calls["n"] % period) < period - 20 else 0.0)


_stub("vad", is_initialized=lambda: True,
      init=lambda sample_rate, threshold: True,
      reset_states=lambda: None, get_chunk_size=lambda: 512,
      process_chunk=_vad_process, get_error=lambda: "")

from nerd_dictation_ex import cli  # noqa: E402

# --- Параметры симуляции ----------------------------------------------------

SR = 16000
CHUNK = 8000                 # байт = 0.25 s аудио
CHUNK_S = CHUNK / (SR * 2)
SPEEDUP = 10.0               # прогоняем в 10 раз быстрее реального времени
N_CHUNKS = 80                # 20 s аудио
RTF = 3.0                    # фейковый бэкенд втрое медленнее реального времени

PRODUCER = f"""
import sys, time
for i in range({N_CHUNKS}):
    sys.stdout.buffer.write(i.to_bytes(2,'little')*{CHUNK // 2})
    sys.stdout.buffer.flush()
    time.sleep({CHUNK_S / SPEEDUP})
"""


def chunk_ids(blob):
    """Вернуть номера 0.25-секундных чанков, попавшие в сегмент.

    Шагаем по CHUNK байт, а не по 2: чанк заполнен одним значением целиком,
    а посэмплово этот цикл держал бы GIL и сам душил бы потребителя,
    подменяя измеряемый эффект артефактом теста.
    """
    ids = []
    for off in range(0, len(blob) - 1, CHUNK):
        v = int.from_bytes(blob[off:off + 2], 'little')
        if not ids or ids[-1] != v:
            ids.append(v)
    return ids


class FakeNative:
    """Имитация нативного распознавателя: медленный, но честный."""

    def __init__(self):
        self.buf = b""
        self.seen = []

    def feed(self, data):
        self.buf += data

    def finalize(self):
        ids = chunk_ids(self.buf)
        time.sleep(RTF * len(self.buf) / (SR * 2) / SPEEDUP)
        self.buf = b""
        self.seen.append(ids)
        r = types.SimpleNamespace()
        r.text = " ".join(str(i) for i in ids)
        r.confidence = 1.0
        return r

    def reset(self):
        self.buf = b""


class FakeBackend:
    supports_streaming = False

    def __init__(self):
        self._backend = FakeNative()

    def reset(self):
        self._backend.reset()


def make_daemon():
    cfg = {"audio": {"sample_rate": SR, "chunk_size": CHUNK, "source": "pw-cat"},
           "wake_word": {"enabled": False}, "general": {}}
    cli._get_audio_command = lambda config: ["python3", "-c", PRODUCER]
    d = cli.DictationDaemon(cfg)
    d.backend = FakeBackend()
    d.backend_name = "fake"
    return d


def wait_idle(d, timeout):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        if d.state == cli.DaemonState.IDLE:
            return True
        time.sleep(0.02)
    return False


# --- Сценарии ---------------------------------------------------------------

def run_main():
    """R1, R2, R3, R5, R6 — один прогон полного конвейера."""
    del TYPED[:]
    d = make_daemon()
    d._start_audio_listener()
    # Старая версия воркера не имеет — тест должен уметь мерить и её,
    # иначе сравнение "до/после" невозможно
    if hasattr(d, "_start_recognizer"):
        d._start_recognizer()

    d.begin_dictation()
    states = [d.state]

    # Даём продюсеру выдать весь материал
    time.sleep(N_CHUNKS * CHUNK_S / SPEEDUP + 0.5)
    d.end_dictation()
    states.append(d.state)

    drained = wait_idle(d, timeout=60.0)
    # Старая версия объявляет IDLE, не дождавшись работы. Даём осесть всему,
    # что ещё в полёте, иначе сравнение мерило бы расторопность, а не потерю речи.
    time.sleep(5.0)
    d._running = False

    seen = [i for seg in d.backend._backend.seen for i in seg]
    return {
        "seen": seen,
        "segments": list(d.backend._backend.seen),
        "states": states,
        "drained": drained,
        "dropped": getattr(d, "_dropped_chunks", None),
        "typed": list(TYPED),
    }


def run_cancel():
    """R4 — cancel выбрасывает НЕраспознанный остаток своей сессии.

    Уже напечатанное отменить нельзя и не нужно — проверяем, что накопленная
    при RTF>1 очередь не досчитывается, а следующая сессия при этом работает.
    """
    del TYPED[:]
    d = make_daemon()
    d._start_audio_listener()
    if hasattr(d, "_start_recognizer"):
        d._start_recognizer()

    d.begin_dictation()
    time.sleep(N_CHUNKS * CHUNK_S / SPEEDUP / 2)
    backlog = d._segment_queue.qsize() if hasattr(d, "_segment_queue") else 0
    typed_at_cancel = len(TYPED)
    d.cancel_dictation()
    cancelled_ok = wait_idle(d, timeout=30.0)
    # Сколько сегментов старой сессии допечаталось после отмены
    leaked = len(TYPED) - typed_at_cancel
    typed_after_cancel = len(TYPED)

    d.begin_dictation()
    time.sleep(N_CHUNKS * CHUNK_S / SPEEDUP / 2)
    d.end_dictation()
    second_ok = wait_idle(d, timeout=60.0)
    d._running = False

    return {
        "cancelled_ok": cancelled_ok,
        "backlog_at_cancel": backlog,
        "leaked": leaked,
        "second_session_typed": len(TYPED) - typed_after_cancel,
        "second_ok": second_ok,
    }


def run_vad_failure():
    """R7 — отказ VAD не должен заклинивать демон в FINALIZING навсегда.

    Ранний return в _dictation_loop_buffered проходит мимо try/finally, поэтому
    маркер конца сессии обязан ставиться выше — в целевой функции потока.
    """
    del TYPED[:]
    d = make_daemon()
    d._start_audio_listener()
    if hasattr(d, "_start_recognizer"):
        d._start_recognizer()

    vad_mod = sys.modules["nerd_dictation_ex.vad"]
    saved_init, saved_ready = vad_mod.init, vad_mod.is_initialized
    vad_mod.is_initialized = lambda: False
    vad_mod.init = lambda sample_rate, threshold: False
    try:
        d.begin_dictation()
        time.sleep(0.5)
        d.end_dictation()
        recovered = wait_idle(d, timeout=15.0)
    finally:
        vad_mod.init, vad_mod.is_initialized = saved_init, saved_ready

    # Демон обязан снова принимать диктовку
    accepts = d.begin_dictation().get("status") == "ok"
    d._stop_dictation.set()
    d._running = False
    return {"recovered": recovered, "accepts_next": accepts}


def main():
    ok = True
    r = run_main()

    seen, segs = r["seen"], r["segments"]
    produced = set(range(N_CHUNKS))
    # Продюсер мог не успеть отдать последние чанки до end — учитываем фактические
    covered = set(seen)
    missing = sorted(x for x in produced if x < (max(covered) if covered else -1)
                     and x not in covered)

    print(f"сегментов: {len(segs)}, чанков распознано: {len(covered)}/{N_CHUNKS}")
    print(f"вытеснено из кольцевого буфера: {r['dropped']}")

    if missing:
        print(f"R1 FAIL: потеряно {len(missing)} чанков внутри распознанного "
              f"диапазона: {missing[:20]}{'...' if len(missing) > 20 else ''}")
        ok = False
    elif len(covered) < N_CHUNKS * 0.9:
        print(f"R1 FAIL: распознано лишь {len(covered)} из {N_CHUNKS} чанков")
        ok = False
    else:
        print(f"R1 PASS: пропусков внутри распознанного нет ({len(covered)} чанков)")

    if seen != sorted(seen):
        print(f"R2 FAIL: порядок нарушен")
        ok = False
    else:
        print("R2 PASS: порядок сохранён")

    if segs and max(seen) >= N_CHUNKS - 3:
        print(f"R3 PASS: хвост дошёл (последний чанк {max(seen)})")
    else:
        print(f"R3 FAIL: хвост потерян (последний чанк {max(seen) if seen else None})")
        ok = False

    finalizing = getattr(cli.DaemonState, "FINALIZING", None)
    if r["states"][0] == cli.DaemonState.LISTENING and r["drained"] and \
       (finalizing is None or r["states"][1] == finalizing):
        print("R5 PASS: LISTENING -> FINALIZING -> IDLE")
    else:
        print(f"R5 FAIL: переходы {r['states']}, дождались IDLE: {r['drained']}")
        ok = False

    if r["dropped"] is None:
        print("R6 SKIP: в этой версии счётчика вытеснений нет")
    elif r["dropped"] == 0:
        print("R6 PASS: вытеснений нет, счётчик на месте")
    else:
        print(f"R6 FAIL: потребитель не успевал, вытеснено {r['dropped']} чанков")
        ok = False

    c = run_cancel()
    # При RTF=3 очередь на момент отмены обязана быть непустой, иначе
    # проверка ничего не проверяет
    if c["backlog_at_cancel"] == 0:
        print(f"R4 FAIL: нечего было отменять, очередь пуста — тест не показателен")
        ok = False
    elif c["leaked"] > 1:
        print(f"R4 FAIL: после отмены допечаталось {c['leaked']} сегментов "
              f"старой сессии (допустим максимум 1, уже бывший в работе)")
        ok = False
    elif not (c["cancelled_ok"] and c["second_ok"] and c["second_session_typed"] > 0):
        print(f"R4 FAIL: {c}")
        ok = False
    else:
        print(f"R4 PASS: отменено {c['backlog_at_cancel']} сегментов из очереди, "
              f"следующая сессия напечатала {c['second_session_typed']}")

    v = run_vad_failure()
    if v["recovered"] and v["accepts_next"]:
        print("R7 PASS: отказ VAD не заклинил демон, следующая диктовка принята")
    else:
        print(f"R7 FAIL: демон завис после отказа VAD: {v}")
        ok = False

    print("ALL PASS" if ok else "FAILED")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
