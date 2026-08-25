# -*- coding: utf-8 -*-
"""
font2c.py — знакогенератор «Искры» → исходник C++ с массивом глифов.

    py tools/font2c.py

Читает src/font/koi7-5x8.txt, пишет src/font/font_data.cpp.

Дамп устроен как «КОД:строка», по восемь строк на глиф; `*` — светящаяся
точка, `.` — тёмная. Запись ровно по глифу, пять знаков, и глифов 96 —
коды 20-7F кодировки КОИ-7 Н2. Байт хранится прижатым влево: старший бит —
левая точка глифа, три младших не заняты вовсе.

Данные переносятся дословно, править их нечем: и знак ¤ в позиции 24, и Ъ
в позиции 5F стоят так у самой машины. Сам дамп снят со скана рис. 3.1
руководства по БОСГИ — `tools/probes/bosgi_font.py`.
"""

import io
import os

_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FONT_DIR = os.path.join(_ROOT, "src", "font")

KOI7_FONT = "koi7-5x8.txt"      # глиф 5x8, запись по глифу

HEADER = """// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
//
// СГЕНЕРИРОВАНО tools/font2c.py из src/font/koi7-5x8.txt — не править руками.
//
// Раскладка КОИ-7 Н2: 20-5F совпадает с ASCII, только в позиции 24 знак ¤,
// а в позиции 5F буква Ъ; 60-7F — прописная кириллица; 7F пуст. Индекс
// глифа равен семибитному коду символа, коды ниже пробела — управляющие,
// глифов у них нет.

#include "font/font.h"

namespace iskra {
namespace font_data {

"""


def load_koi7(path):
    """Знакогенератор «Искры»: 96 глифов 5x8, коды 20-7F, запись по глифу."""
    rows = {}
    for line in io.open(path, encoding="utf-8"):
        line = line.strip()
        if not line:
            continue
        code, _, bits = line.partition(":")
        rows.setdefault(int(code, 16), []).append(bits)

    if sorted(rows) != list(range(0x20, 0x80)):
        raise SystemExit("%s: ожидались коды 20-7F" % path)

    # Таблица глифов всё равно на 128 записей: экран сбрасывает старший бит,
    # и индексом служит семибитный код целиком.
    data = bytearray(0x20 * 8)
    for c in range(0x20, 0x80):
        lines = rows[c]
        if len(lines) != 8 or any(len(r) != 5 for r in lines):
            raise SystemExit("%s: код %02X — не восемь строк по пять" % (path, c))
        for r in lines:
            b = 0
            for x, ch in enumerate(r):
                if ch == "*":
                    b |= 0x80 >> x          # прижимаем влево
                elif ch != ".":
                    raise SystemExit("%s: код %02X — знак %r" % (path, c, ch))
            data.append(b)
    return 8, bytes(data)


def main():
    out = [HEADER]

    h, data = load_koi7(os.path.join(FONT_DIR, KOI7_FONT))
    out.append("// %s — 128 глифов 5x8, прижатых влево (биты 7-3)\n" % KOI7_FONT)
    # extern обязателен: без него const на уровне пространства имён получает
    # внутреннее связывание и не виден из font.cpp
    out.append("extern const unsigned char glyphs_5x8[128 * 8];\n")
    out.append("extern const unsigned char glyphs_5x8[128 * 8] = {\n")
    for c in range(128):
        row = data[c * h:(c + 1) * h]
        out.append("    " + " ".join("0x%02X," % b for b in row)
                   + "  // %02X\n" % c)
    out.append("};\n\n")

    out.append("} // namespace font_data\n} // namespace iskra\n")

    dst = os.path.join(FONT_DIR, "font_data.cpp")
    with open(dst, "w", encoding="utf-8", newline="\n") as f:
        f.write("".join(out))
    print("записано %s (%d байт)" % (os.path.relpath(dst, _ROOT),
                                     os.path.getsize(dst)))


if __name__ == "__main__":
    main()
