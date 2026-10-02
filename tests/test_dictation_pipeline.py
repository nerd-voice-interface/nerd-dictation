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
  R7  отказ VAD не заклинивает демон в FINALIZING
  R8  toggle в демоне: IDLE -> LISTENING -> FINALIZING, в FINALIZING отказ
  R9  поток кнопки: PRESSED переключает, REJECTED нет, после обрыва X
      и неудачного подключения — переподключение
  R10 потоковый бэкенд на 8 кГц: все чанки дошли до feed по порядку, без
      потерь, уже ресемплированными
  R11 потоковый: завершённые фразы печатаются по ходу, остаток — из finalize
      на end, промежуточные гипотезы не печатаются никогда
  R12 потоковый cancel: ничего не печатается, finalize не зовут, reset — да
  R13 бэкенд на частоте записи ресемплер не создаёт

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

# Заглушка нативного модуля кнопки: настоящий XCB-код проверяется в gtest
# (tests/test_button_trigger.cpp), здесь — только логика потока демона
class _TriggerEvent:
    NONE, PRESSED, REJECTED, DISCONNECTED = "NONE", "PRESSED", "REJECTED", "DISCONNECTED"


class _FakeButtonTrigger:
    connect_plan = []   # результаты connect() по очереди; пусто — True
    wait_plan = []      # события wait() по очереди; пусто — NONE
    instances = []

    def __init__(self, button):
        self.button = button
        self._connected = False
        self.connects = 0
        _FakeButtonTrigger.instances.append(self)

    def connect(self):
        self.connects += 1
        plan = _FakeButtonTrigger.connect_plan
        self._connected = plan.pop(0) if plan else True
        return self._connected

    def connected(self):
        return self._connected

    def wait(self, timeout_ms):
        plan = _FakeButtonTrigger.wait_plan
        if not plan:
            time.sleep(0.01)
            return _TriggerEvent.NONE
        ev = plan.pop(0)
        if ev == _TriggerEvent.DISCONNECTED:
            self._connected = False
        return ev

    def last_source(self):
        return "fake device"

    def get_error(self):
        return "fake error"


_native_pkg = types.ModuleType("_nerd_dictation_native")
_native_trigger = types.ModuleType("_nerd_dictation_native.trigger")
_native_trigger.ButtonTrigger = _FakeButtonTrigger
_native_trigger.TriggerEvent = _TriggerEvent
_native_pkg.trigger = _native_trigger
_native_audio = types.ModuleType("_nerd_dictation_native.audio")  # Resampler — ниже
_native_pkg.audio = _native_audio
sys.modules.setdefault("_nerd_dictation_native", _native_pkg)
sys.modules.setdefault("_nerd_dictation_native.trigger", _native_trigger)
sys.modules.setdefault("_nerd_dictation_native.audio", _native_audio)

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
    """Оффлайновый бэкенд (как GigaAM): частота записи, распознаёт сегмент целиком."""
    supports_streaming = False
    sample_rate = SR

    def __init__(self):
        self._backend = FakeNative()

    def feed(self, data):
        self._backend.feed(data)

    def finalize(self):
        return self._backend.finalize()

    def result(self):
        raise AssertionError("оффлайновому бэкенду result() не зовут")

    def reset(self):
        self._backend.reset()


class FakeStreamingBackend:
    """Потоковый бэкенд (как t-one): 8 кГц, фраза завершается каждые 4 чанка.

    Промежуточная гипотеза помечена "partial" — она не должна печататься.
    """
    supports_streaming = True
    sample_rate = SR // 2
    FINAL_EVERY = 4

    def __init__(self):
        self.fed = []          # номера чанков в порядке feed
        self.fed_sizes = []    # длины поданных байт (уже после ресемплинга)
        self.pending = []
        self.finalize_calls = 0
        self.reset_calls = 0

    def feed(self, data):
        # Чанк однороден: номер — первые два байта, остальное — проверка целостности
        assert data == data[:2] * (len(data) // 2), "чанк повреждён"
        self.fed.append(int.from_bytes(data[:2], "little"))
        self.fed_sizes.append(len(data))
        self.pending.append(self.fed[-1])

    def result(self):
        if len(self.pending) >= self.FINAL_EVERY:
            text, self.pending = " ".join(map(str, self.pending)), []
            return types.SimpleNamespace(text=text, is_final=True, confidence=1.0)
        return types.SimpleNamespace(text="partial", is_final=False, confidence=1.0)

    def finalize(self):
        self.finalize_calls += 1
        text, self.pending = " ".join(map(str, self.pending)), []
        return types.SimpleNamespace(text=text, is_final=True, confidence=1.0)

    def reset(self):
        self.reset_calls += 1
        self.pending = []


class _FakeResampler:
    """Заглушка нативного ресемплера: децимация ×2. Качество проверяет gtest
    (tests/test_resampler.cpp), здесь — только что демон его вызывает."""
    created = []

    def __init__(self, in_rate, out_rate):
        assert (in_rate, out_rate) == (SR, SR // 2), (in_rate, out_rate)
        self._out = out_rate
        self.calls = 0
        self.resets = 0
        _FakeResampler.created.append(self)

    def process(self, data):
        self.calls += 1
        return memoryview(data).cast("h")[::2].tobytes()

    def reset(self):
        self.resets += 1

    def out_rate(self):
        return self._out


_native_audio.Resampler = _FakeResampler


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
    _FakeResampler.created.clear()
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
        "resamplers": len(_FakeResampler.created),
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


def run_toggle():
    """R8 — единый toggle в демоне."""
    del TYPED[:]
    d = make_daemon()
    d._start_audio_listener()
    d._start_recognizer()

    first = d.toggle_dictation()
    after_first = d.state
    time.sleep(0.3)
    second = d.toggle_dictation()
    after_second = d.state
    drained = wait_idle(d, timeout=30.0)

    # FINALIZING выставляем явно: ждать его по времени значило бы гонку в тесте
    d.state = cli.DaemonState.FINALIZING
    third = d.toggle_dictation()
    after_third = d.state
    d.state = cli.DaemonState.IDLE

    via_socket = d.handle_command({"action": "toggle"})
    after_socket = d.state
    d.end_dictation()
    wait_idle(d, timeout=30.0)
    d._running = False
    return {
        "first": (first.get("status"), after_first),
        "second": (second.get("status"), after_second),
        "drained": drained,
        "third": (third.get("status"), after_third),
        "socket": (via_socket.get("status"), after_socket),
    }


def run_trigger_loop():
    """R9 — поток кнопки на заглушке нативного модуля."""
    d = make_daemon()
    d._trigger_button = 2
    d.TRIGGER_RETRY_SECONDS = 0.05
    toggles = []
    d.toggle_dictation = lambda: toggles.append(1) or {"status": "ok", "message": "fake"}

    _FakeButtonTrigger.instances.clear()
    # Первое подключение неудачно (X ещё нет), затем успешно, после обрыва — снова
    _FakeButtonTrigger.connect_plan[:] = [False, True, True]
    _FakeButtonTrigger.wait_plan[:] = [
        _TriggerEvent.REJECTED, _TriggerEvent.PRESSED,
        _TriggerEvent.DISCONNECTED, _TriggerEvent.PRESSED,
    ]
    d._start_button_trigger()
    end = time.monotonic() + 5.0
    while _FakeButtonTrigger.wait_plan and time.monotonic() < end:
        time.sleep(0.02)
    time.sleep(0.1)
    d._running = False
    d._trigger_thread.join(timeout=2.0)

    t = _FakeButtonTrigger.instances[0] if _FakeButtonTrigger.instances else None
    return {
        "toggles": len(toggles),
        "connects": t.connects if t else 0,
        "plan_left": len(_FakeButtonTrigger.wait_plan),
        "thread_stopped": not d._trigger_thread.is_alive(),
    }


def run_streaming(cancel_midway=False):
    """R10-R12 — потоковый бэкенд через тот же конвейер."""
    del TYPED[:]
    _FakeResampler.created.clear()
    d = make_daemon()
    d.backend = FakeStreamingBackend()
    d._start_audio_listener()
    d._start_recognizer()

    d.begin_dictation()
    if cancel_midway:
        time.sleep(N_CHUNKS * CHUNK_S / SPEEDUP / 2)
        typed_before = len(TYPED)
        fed_before = len(d.backend.fed)
        d.cancel_dictation()
    else:
        time.sleep(N_CHUNKS * CHUNK_S / SPEEDUP + 0.5)
        typed_before = fed_before = None
        d.end_dictation()
    drained = wait_idle(d, timeout=30.0)
    time.sleep(0.3)
    d._running = False

    b = d.backend
    return {
        "fed": list(b.fed), "fed_sizes": set(b.fed_sizes), "typed": list(TYPED),
        "finalize_calls": b.finalize_calls, "reset_calls": b.reset_calls,
        "resamplers": len(_FakeResampler.created),
        "resampler_resets": sum(r.resets for r in _FakeResampler.created),
        "drained": drained, "typed_before": typed_before, "fed_before": fed_before,
        "state": d.state,
    }


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

    if hasattr(cli.DictationDaemon, "toggle_dictation"):
        t = run_toggle()
        L, F = cli.DaemonState.LISTENING, getattr(cli.DaemonState, "FINALIZING", None)
        if (t["first"] == ("ok", L) and t["second"] == ("ok", F) and t["drained"]
                and t["third"] == ("error", F) and t["socket"] == ("ok", L)):
            print("R8 PASS: toggle IDLE->LISTENING->FINALIZING->IDLE, в FINALIZING отказ, "
                  "команда сокета работает")
        else:
            print(f"R8 FAIL: {t}")
            ok = False

        g = run_trigger_loop()
        if g == {"toggles": 2, "connects": 3, "plan_left": 0, "thread_stopped": True}:
            print("R9 PASS: PRESSED переключает, REJECTED нет, переподключение после "
                  "неудачи и обрыва, поток останавливается")
        else:
            print(f"R9 FAIL: {g}")
            ok = False
    else:
        print("R8/R9 SKIP: в этой версии нет toggle в демоне")

    if hasattr(cli.DictationDaemon, "_recognize"):
        st = run_streaming()
        fed = st["fed"]
        contiguous = fed == list(range(fed[0], fed[0] + len(fed))) if fed else False
        if (contiguous and fed[0] <= 1 and len(fed) >= N_CHUNKS * 0.9
                and st["fed_sizes"] == {CHUNK // 2} and st["resamplers"] == 1):
            print(f"R10 PASS: {len(fed)} чанков в feed подряд, каждый {CHUNK // 2} байт "
                  f"после ресемплинга 16->8 кГц")
        else:
            print(f"R10 FAIL: первые {fed[:5]}, всего {len(fed)}, размеры {st['fed_sizes']}, "
                  f"ресемплеров {st['resamplers']}")
            ok = False

        typed = st["typed"]
        printed_ids = [int(x) for t in typed for x in t.split() if x.isdigit()]
        if ("partial" not in " ".join(typed) and printed_ids == fed
                and len(typed) >= 2 and st["finalize_calls"] == 1 and st["drained"]):
            print(f"R11 PASS: {len(typed) - 1} фраз по ходу + остаток из finalize, "
                  f"напечатано ровно то, что подано; гипотезы не печатались")
        else:
            print(f"R11 FAIL: напечатано {typed[:4]}..., finalize {st['finalize_calls']}, "
                  f"совпадает с поданным: {printed_ids == fed}")
            ok = False

        c = run_streaming(cancel_midway=True)
        leaked = len(c["typed"]) - c["typed_before"]
        if (c["fed_before"] > 0 and c["finalize_calls"] == 0 and c["reset_calls"] >= 1
                and leaked <= 1 and c["drained"] and c["state"] == cli.DaemonState.IDLE
                and c["resampler_resets"] >= 1):
            print(f"R12 PASS: отмена после {c['fed_before']} чанков: finalize не звали, "
                  f"reset и сброс ресемплера были, демон в IDLE")
        else:
            print(f"R12 FAIL: {dict((k, c[k]) for k in ('fed_before', 'finalize_calls', 'reset_calls', 'drained', 'state', 'resampler_resets'))}, "
                  f"допечатано после отмены {leaked}")
            ok = False

        if r["resamplers"] == 0:
            print("R13 PASS: бэкенд на частоте записи — ресемплер не создавался")
        else:
            print(f"R13 FAIL: для 16 кГц бэкенда создано ресемплеров: {r['resamplers']}")
            ok = False
    else:
        print("R10-R13 SKIP: в этой версии нет потокового режима в воркере")

    print("ALL PASS" if ok else "FAILED")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
