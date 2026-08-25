# -*- coding: utf-8 -*-
"""
gbuffer.py — ищет на образах дискет поток команд графического буфера.

    py tools/probes/gbuffer.py            перепись по всем образам
    py tools/probes/gbuffer.py ОБРАЗ N    разбор потока с сектора N

Буфер графики — обычный символьный массив Бейсика: `¤OPEN` объявляет его,
`NPLOT`/`DRAW`/`LABEL` дописывают в него записи, `¤COPY /адрес` выкладывает
всё разом на устройство. Коды записей выведены из `SLIDE` 160 и 1400
(`docs/format.md`, разд. 5, «Коды операций графического буфера»); здесь они
проверяются на живых байтах.

Запись: код `80`-`86`. У рисующих — ровно пять байт: код и две координаты
двухбайтовыми двоичными числами, старший байт первым. У надписей — код, байт
длины (считая себя), четыре байта приращения точки, три байта признаков и
дальше текст в КОИ-8.
"""

import os
import sys
import glob

DSK = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__)))), "..", "DSKCommander", "docs", "samples",
    "Iskra226")

NAMES = {0x80: "NPLOT", 0x81: "NDRAW", 0x82: "NLAB.", 0x83: "???",
         0x84: "DOT", 0x85: "DRAW", 0x86: "LABEL"}
SHORT = (0x80, 0x81, 0x84, 0x85)
LONG = (0x82, 0x86)


def walk(b, p):
    """Сколько записей подряд разбирается с позиции p и где встало."""
    n = 0
    while p < len(b):
        op = b[p]
        if op in SHORT:
            k = 5
        elif op in LONG:
            if p + 1 >= len(b):
                break
            k = 1 + b[p + 1]
            if k < 8:              # код, длина, 4 байта точки, 3 признака
                break
        else:
            break
        if p + k > len(b):
            break
        p += k
        n += 1
    return n, p


def runs(d):
    """Куски: подряд идущие секторы с одной строковой записью 8B 40 LL."""
    out, cur, first = [], bytearray(), None
    for s in range(len(d) // 256):
        b = d[s * 256:(s + 1) * 256]
        if b[0] == 0x8B and b[1] == 0x40 and b[2] > 0:
            if first is None:
                first = s
            cur += b[3:3 + b[2]]
        else:
            if cur:
                out.append((first, cur))
            cur, first = bytearray(), None
    if cur:
        out.append((first, cur))
    return out


def best_start(b):
    """Смещение, с которого поток разбирается длиннее всего."""
    best = (0, 0, 0)
    for off in range(0, min(64, len(b))):
        if b[off] not in NAMES:
            continue
        n, end = walk(b, off)
        if n > best[0]:
            best = (n, off, end)
    return best


def survey():
    for f in sorted(glob.glob(os.path.join(DSK, "**", "*.dsk"), recursive=True)):
        for first, b in runs(open(f, "rb").read()):
            n, off, end = best_start(b)
            if n >= 20:
                print("%-22s сектор %4d  байт %5d  записей %4d "
                      "со смещения %d, конец %d" % (
                          os.path.basename(f), first, len(b), n, off + 1, end))


def dump(path, sector):
    d = open(path, "rb").read()
    b = bytearray()
    s = sector
    while s < len(d) // 256:
        r = d[s * 256:(s + 1) * 256]
        if r[0] != 0x8B or r[1] != 0x40:
            break
        b += r[3:3 + r[2]]
        s += 1
    n, off, end = best_start(b)
    print("собрано %d байт с секторов %d-%d; поток с байта %d по %d, "
          "записей %d" % (len(b), sector, s - 1, off + 1, end, n))
    p = off
    while p < end:
        op = b[p]
        k = 1 + b[p + 1] if op in LONG else 5
        r = b[p:p + k]
        if op in LONG:
            txt = "".join(chr(c) if 32 <= c < 127 else "." for c in r[9:])
            tail = " приращение %d,%d признаки %s текст %r" % (
                (r[2] << 8) | r[3], (r[4] << 8) | r[5],
                " ".join("%02X" % x for x in r[6:9]), txt)
        else:
            tail = " точка %d,%d" % ((r[1] << 8) | r[2], (r[3] << 8) | r[4])
        print("%5d %-6s %-32s%s" % (p + 1, NAMES[op],
                                    " ".join("%02X" % x for x in r), tail))
        p += k
    if end < len(b):
        print("хвост %d байт: %s" % (
            len(b) - end, " ".join("%02X" % x for x in b[end:])))


if __name__ == "__main__":
    if len(sys.argv) == 3:
        dump(sys.argv[1], int(sys.argv[2]))
    else:
        survey()
