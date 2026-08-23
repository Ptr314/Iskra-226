# -*- coding: utf-8 -*-
"""
psf2c.py — PSF v1 (консольный шрифт Linux) → исходник C++ с массивами глифов.

    py tools/psf2c.py

Читает src/font/koi8-8x{8,14,16}.psf и пишет src/font/font_data.cpp.
Данные переносятся дословно; поправки, специфичные для «Искры»
(например, знак ¤ вместо $ в позиции 0x24), делает src/font/font.cpp.
"""

import os

_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FONT_DIR = os.path.join(_ROOT, "src", "font")

FONTS = ["koi8-8x8", "koi8-8x14", "koi8-8x16"]

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
    out.append("} // namespace font_data\n} // namespace iskra\n")

    dst = os.path.join(FONT_DIR, "font_data.cpp")
    with open(dst, "w", encoding="utf-8", newline="\n") as f:
        f.write("".join(out))
    print("записано %s (%d байт)" % (os.path.relpath(dst, _ROOT),
                                     os.path.getsize(dst)))


if __name__ == "__main__":
    main()