#!/usr/bin/env python3
"""Проверка: каждый биндинг в src/main.cpp кем-то используется из Python.

Нативный модуль _nerd_dictation_native встроен в бинарь и нужен только
Python-демону (nerd_dictation_ex/). Биндинг, на который в Python никто не
ссылается, — мёртвая поверхность: её надо поддерживать, проверять на GIL и
держать в голове, но она ничего не делает. Миграция на C++ оставила таких
немало.

Проверка консервативна: имя считается используемым, если оно встречается
в коде пакета как отдельное слово где угодно. Ложных «используется» (общие
имена вроде reset у разных классов) она пропустит, ложных «не используется»
не даст — значит, всё, что она находит, удалять безопасно.

Запуск:  python3 tests/check_unused_bindings.py
"""

import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MAIN = os.path.join(REPO, "src", "main.cpp")
PKG = os.path.join(REPO, "nerd_dictation_ex")

# .def("x"  .def_static("x"  .def_readwrite("x"  .def_property("x"  .value("X")
# m.def("x")  sub.def("x")  sub.attr("x")
BINDING_RE = re.compile(
    r'\.(def|def_static|def_readwrite|def_readonly|def_property|def_property_readonly|value|attr)'
    r'\(\s*"([A-Za-z_]\w*)"'
)
CLASS_RE = re.compile(r'py::(?:class_|enum_)<[^>]*>\(\s*\w+\s*,\s*"([A-Za-z_]\w*)"')
SUBMODULE_RE = re.compile(r'def_submodule\(\s*"([A-Za-z_]\w*)"')

# Не адресуются из Python по имени
IGNORED = {"__init__", "__repr__", "__str__", "__enter__", "__exit__", "__bool__"}

# Биндинги, которые нужны, хотя Python не называет их по имени. Причина обязательна.
REQUIRED = {
    "BackendInfo": "тип, который возвращает Backend.get_info(): без регистрации "
                   "pybind11 не сконвертирует результат",
    "ASRResult": "тип, который возвращают Backend.result() и finalize()",
}


def python_corpus() -> str:
    parts = []
    for root, _, files in os.walk(PKG):
        if "__pycache__" in root:
            continue
        for f in files:
            if f.endswith(".py"):
                with open(os.path.join(root, f), encoding="utf-8") as fh:
                    parts.append(fh.read())
    return "\n".join(parts)


def main() -> int:
    lines = open(MAIN, encoding="utf-8").read().splitlines()
    corpus = python_corpus()

    # Только тело PYBIND11_EMBEDDED_MODULE: дальше main() сам обращается к
    # Python (sys.attr("argv") и т.п.) — это не биндинги
    start = next(i for i, l in enumerate(lines) if "PYBIND11_EMBEDDED_MODULE" in l)
    depth, end = 0, None
    for i in range(start, len(lines)):
        depth += lines[i].count("{") - lines[i].count("}")
        if depth == 0 and i > start and "}" in lines[i]:
            end = i
            break

    names = []  # (line, kind, name)
    for lineno, line in enumerate(lines[start:end + 1], start + 1):
        for m in SUBMODULE_RE.finditer(line):
            names.append((lineno, "submodule", m.group(1)))
        for m in CLASS_RE.finditer(line):
            names.append((lineno, "class", m.group(1)))
        for m in BINDING_RE.finditer(line):
            kind, name = m.group(1), m.group(2)
            if kind == "value":
                continue  # значения enum: функция может вернуть любое, набор обязан быть полным
            names.append((lineno, kind, name))

    unused = []
    for lineno, kind, name in names:
        if name in IGNORED or name in REQUIRED:
            continue
        if not re.search(r"\b" + re.escape(name) + r"\b", corpus):
            unused.append((lineno, kind, name))

    print(f"биндингов проверено: {len(names)}, без единой ссылки из Python: {len(unused)}")
    for lineno, kind, name in unused:
        print(f"  main.cpp:{lineno}  {kind:22} {name}")
    if unused:
        return 1
    print("PASS: каждый биндинг используется из Python")
    return 0


if __name__ == "__main__":
    sys.exit(main())
