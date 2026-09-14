#!/usr/bin/env python3
"""Проверка правила: биндинг не держит GIL, пока берёт нативный мьютекс.

pybind11 по умолчанию удерживает GIL весь вызов. Если один метод класса
держит mutex_ долго и БЕЗ GIL (снят вручную), а соседний метод того же
класса ждёт тот же mutex_, ДЕРЖА GIL, — все потоки Python встают.
Это инверсия приоритетов, и она незаметна при чтении одного метода.

Скрипт строит список методов, берущих mutex_, и убеждается, что каждый
соответствующий биндинг в main.cpp снимает GIL.

Запуск:  python3 tests/check_gil_bindings.py
"""

import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(REPO, "src")
MAIN = os.path.join(SRC, "main.cpp")

# Классы, чьи методы экспонируются в Python и защищены общим мьютексом
IMPL_FILES = {
    "VADManager": "vad/vad_manager.cpp",
    "WakeWordManager": "wake_word/wake_word_manager.cpp",
    "SpellCorrector": "spelling/spell_corrector.cpp",
    "AudioPreprocessor": "audio/audio_preprocessor.cpp",
}

METHOD_RE = re.compile(r"^[A-Za-z_][\w:<>,\s&*\[\]]*?\b(\w+)::(\w+)\s*\(")


def methods_taking_mutex(path):
    """Вернуть имена методов, в теле которых берётся mutex_."""
    taking = set()
    current = None
    try:
        lines = open(path, encoding="utf-8").read().splitlines()
    except FileNotFoundError:
        return taking
    for line in lines:
        m = METHOD_RE.match(line)
        if m:
            current = m.group(2)
        if current and ("lock_guard" in line or "unique_lock" in line) and "mutex_" in line:
            taking.add(current)
    return taking


def binding_releases_gil(main_src, cls, method):
    """Снимает ли биндинг cls.method GIL?

    Ищем .def("method", ...) внутри блока класса и проверяем наличие
    call_guard<gil_scoped_release> или ручного gil_scoped_release до
    следующего .def.
    """
    # Границы блока класса: от py::class_<...Cls> до следующего py::class_
    cls_start = None
    for m in re.finditer(r"py::class_<[^>]*\b" + re.escape(cls) + r"\b", main_src):
        cls_start = m.start()
        break
    if cls_start is None:
        return None  # класс не экспонирован
    nxt = main_src.find("py::class_<", cls_start + 10)
    block = main_src[cls_start: nxt if nxt != -1 else len(main_src)]

    # Метод может быть экспонирован и как .def("name"), и внутри def_property
    # под другим именем свойства — тогда ищем по ссылке на сам метод.
    m = re.search(r'\.def(?:_property)?\w*\(\s*"' + re.escape(method) + r'"', block)
    if m is None:
        m = re.search(r'\.def_property\w*\([^;]*?::' + re.escape(method) + r'\b', block)
    if m is None:
        return None  # метод действительно не экспонирован
    rest = block[m.start() + 1:]
    nxt_def = rest.find('.def(')
    nxt_def2 = rest.find('.def_')
    ends = [x for x in (nxt_def, nxt_def2) if x != -1]
    chunk = rest[: min(ends)] if ends else rest
    return "gil_scoped_release" in chunk


def main():
    main_src = open(MAIN, encoding="utf-8").read()
    violations = []
    checked = 0

    for cls, rel in sorted(IMPL_FILES.items()):
        impl = os.path.join(SRC, rel)
        locked = methods_taking_mutex(impl)
        if not locked:
            print(f"{cls}: мьютекса нет — правило неприменимо")
            continue
        print(f"{cls}: методов с mutex_ — {len(locked)}: {', '.join(sorted(locked))}")
        for meth in sorted(locked):
            res = binding_releases_gil(main_src, cls, meth)
            if res is None:
                print(f"    {meth}: не экспонирован в Python — пропуск")
                continue
            checked += 1
            if res:
                print(f"    {meth}: GIL снимается — ок")
            else:
                print(f"    {meth}: ДЕРЖИТ GIL, беря mutex_ — НАРУШЕНИЕ")
                violations.append(f"{cls}::{meth}")

    print()
    print(f"проверено биндингов: {checked}, нарушений: {len(violations)}")
    if violations:
        print("НАРУШЕНИЯ: " + ", ".join(violations))
        return 1
    print("PASS: ни один биндинг не держит GIL, беря нативный мьютекс")
    return 0


if __name__ == "__main__":
    sys.exit(main())
