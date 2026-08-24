# -*- coding: utf-8 -*-
"""
psf2c.py — исходные знакогенераторы → исходник C++ с массивами глифов.

    py tools/psf2c.py

Читает и пишет src/font/font_data.cpp:

  koi8-8x{8,14,16}.psf — PSF v1, консольные шрифты Linux, крупные и читаемые;
  koi8-7x8.txt         — дамп ПЗУ советского терминала, глиф 7x8 в поле 9x10.

Текстовый дамп устроен как «КОД:строка», по восемь строк на глиф, запись
шириной девять знаков: крайние столбцы у всех 256 глифов пусты — это поля
знакоместа, а сам глиф занимает семь средних. Байт хранится прижатым влево
(старший бит — левая точка глифа), как и у PSF.

Данные переносятся дословно; поправки, специфичные для «Искры» (знак ¤
вместо $ в позиции 0x24, дорисованный Ъ), делает src/font/font.cpp.
"""

import io
import os

_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FONT_DIR = os.path.join(_ROOT, "src", "font")

FONTS = ["koi8-8x8", "koi8-8x14", "koi8-8x16"]
TEXT_FONT = "koi8-7x8.txt"      # глиф 7x8, запись в девять столбцов

HEADER = """// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
//
// СГЕНЕРИРОВАНО tools/psf2c.py из src/font/*.psf — не править руками.
//
// Раскладка КОИ-8: 00-7F совпадает с ASCII, C0-DF строчная кириллица,
// E0-FF прописная. Индекс глифа равен коду символа.

#include "font/font.h"

namespace iskra {
namespace font_data {

"""


def load_psf(path):
    d = open(path, "rb").read()
    if len(d) < 4 or d[0] != 0x36 or d[1] != 0x04:
        raise SystemExit("%s: не PSF v1" % path)
    mode, h = d[2], d[3]
    if mode & 1:
        raise SystemExit("%s: 512 глифов не поддерживается" % path)
    if len(d) != 4 + 256 * h:
        raise SystemExit("%s: неожиданный размер" % path)
    return h, d[4:]


def load_text(path):
    """Дамп ПЗУ: 256 глифов по восемь строк, девять столбцов в записи."""
    rows = {}
    for line in io.open(path, encoding="utf-8"):
        line = line.strip()
        if not line.strip():
            continue
        code, _, bits = line.partition(":")
        rows.setdefault(int(code, 16), []).append(bits)

    if sorted(rows) != list(range(256)):
        raise SystemExit("%s: ожидались коды 00-FF" % path)

    data = bytearray()
    for c in range(256):
        lines = rows[c]
        if len(lines) != 8 or any(len(r) != 9 for r in lines):
            raise SystemExit("%s: код %02X — не восемь строк по девять" % (path, c))
        for r in lines:
            # Крайние столбцы — поля знакоместа, а не глиф: если там что-то
            # есть, значит формат понят неверно, и молчать об этом нельзя.
            if r[0] != "." or r[8] != ".":
                raise SystemExit("%s: код %02X — занят крайний столбец" % (path, c))
            b = 0
            for x, ch in enumerate(r[1:8]):
                if ch == "*":
                    b |= 0x80 >> x          # прижимаем влево, как у PSF
                elif ch != ".":
                    raise SystemExit("%s: код %02X — знак %r" % (path, c, ch))
            data.append(b)
    return 8, bytes(data)


def main():
    out = [HEADER]
    for name in FONTS:
        h, data = load_psf(os.path.join(FONT_DIR, name + ".psf"))
        ident = "glyphs_8x%d" % h
        out.append("// %s.psf — 256 глифов 8x%d\n" % (name, h))
        # extern обязателен: без него const на уровне пространства имён
        # получает внутреннее связывание и не виден из font.cpp
        out.append("extern const unsigned char %s[256 * %d];\n" % (ident, h))
        out.append("extern const unsigned char %s[256 * %d] = {\n" % (ident, h))
        for c in range(256):
            row = data[c * h:(c + 1) * h]
            out.append("    " + " ".join("0x%02X," % b for b in row)
                       + "  // %02X\n" % c)
        out.append("};\n\n")

    h, data = load_text(os.path.join(FONT_DIR, TEXT_FONT))
    out.append("// %s — 256 глифов 7x8, прижатых влево (биты 7-1)\n" % TEXT_FONT)
    out.append("extern const unsigned char glyphs_7x8[256 * 8];\n")
    out.append("extern const unsigned char glyphs_7x8[256 * 8] = {\n")
    for c in range(256):
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