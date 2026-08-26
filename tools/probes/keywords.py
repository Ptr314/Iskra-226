# SPDX-License-Identifier: GPL-3.0-or-later
# Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
"""Таблица ключевых слов BASIC 02 из самого интерпретатора.

На загрузочной дискете, начиная с 0x1840, лежат три вещи подряд:

  1. алфавитный список однословных ключевых слов, склеенных без разделителей;
  2. список сочетаний: байт «индекс первого слова + 0x60» и текст продолжения;
  3. массив перестановки: по байту на запись обоих списков — её токен.

Границы слов в первом списке машина знает по своему разбору, а нам их
приходится задать руками: список алфавитный, и разбивка проверяется тем, что
склейка обратно совпадает байт в байт. Разбор целиком — docs/format.md,
разд. 4.

    py tools/probes/keywords.py [ОБРАЗ]
"""

import os
import sys

# Список 1: алфавитный, 51 слово. Проверяется склейкой (см. ниже).
WORDS = (
    'ADD AND( BACKSPACE BIN( BOOL CLEAR COM CONVERT COPY DATA DBACKSPACE '
    'DEFFN DIM DSKIP END FOR GOSUB GOTO HEXPRINT IF INIT INPUT KEYIN LET '
    'LIMITS LIST LOAD MOVE NEXT ON OR( PACK( PRINT READ REM RENUMBER RES '
    'RETURN REWIND ROTATE RUN SAVE SCRATCH SELECT SKIP STOP TRACE '
    'UNPACK( VERIFY XOR( $GIO'
).split()

START = 0x1840          # начало списка 1
COMBO_END = 0x19B5      # за последним сочетанием
PERM = 0x19B6           # массив перестановки

DEFAULT = os.path.join('..', 'DSKCommander', 'docs', 'samples', 'Iskra226',
                       '345_dk_spb', 'disk1side0.dsk')


def decode(data):
    blob = ''.join(WORDS)
    got = data[START:START + len(blob)].decode('latin1')
    if got != blob:
        raise SystemExit('список слов не сходится: %r' % got[:60])

    # Сочетания: код продолжения — байт вне латинских прописных, они и
    # разделяют записи.
    tail = data[START + len(blob):COMBO_END]
    names = list(WORDS)
    combos = []
    i = 0
    while i < len(tail):
        code = tail[i]
        i += 1
        text = ''
        while i < len(tail) and not (tail[i] >= 0x60 and
                                     not 0x41 <= tail[i] <= 0x5A):
            text += chr(tail[i])
            i += 1
        combos.append((code - 0x60, text))
        names.append(names[code - 0x60] + ' ' + text)

    perm = data[PERM:PERM + len(names)]
    if len(perm) != len(names):
        raise SystemExit('перестановка короче списка')
    return names, combos, perm


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else DEFAULT
    data = open(path, 'rb').read()
    names, combos, perm = decode(data)
    print('слов %d, сочетаний %d, перестановка %d байт'
          % (len(WORDS), len(combos), len(perm)))
    for name, token in sorted(zip(names, perm), key=lambda p: p[1]):
        print('%02X  %s' % (token, name))


if __name__ == '__main__':
    main()
