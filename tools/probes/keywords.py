# SPDX-License-Identifier: GPL-3.0-or-later
# Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
"""Таблицы ключевых слов BASIC 02 из самого интерпретатора.

На загрузочной дискете лежат две таблицы подряд.

**Глаголы**, с 0x1840, тремя частями:

  1. алфавитный список однословных ключевых слов, склеенных без разделителей;
  2. список сочетаний: байт «индекс первого слова + 0x60» и текст продолжения;
  3. массив перестановки: по байту на запись обоих списков — её токен.

**Операнды**, с 0x1A0C: пул слов и таблица указателей на них с 0x1B18, по
два байта на слово, младшим вперёд. Слово кончается там, где начинается
следующее по указателю; у последней буквы обычно взведён старший бит, но
опираться на это нельзя — у `LEN(` он не взведён.

Токен слова из пула — его порядковый номер плюс смещение, и смещений
несколько: у машины токен двузначен, и один и тот же байт значит разное в
позиции операнда и в позиции операции (docs/format.md, разд. 7). Смещения
подобраны по совпадению с байтами, установленными раньше по корпусу, и
сверка печатается ключом --check.

    py tools/probes/keywords.py [ОБРАЗ]
    py tools/probes/keywords.py --check [ОБРАЗ]
"""

import os
import sys

# --- глаголы ---------------------------------------------------------------

# Список 1: алфавитный, 51 слово. Проверяется склейкой (см. decode_verbs).
WORDS = (
    'ADD AND( BACKSPACE BIN( BOOL CLEAR COM CONVERT COPY DATA DBACKSPACE '
    'DEFFN DIM DSKIP END FOR GOSUB GOTO HEXPRINT IF INIT INPUT KEYIN LET '
    'LIMITS LIST LOAD MOVE NEXT ON OR( PACK( PRINT READ REM RENUMBER RES '
    'RETURN REWIND ROTATE RUN SAVE SCRATCH SELECT SKIP STOP TRACE '
    'UNPACK( VERIFY XOR( $GIO'
).split()

VERB_START = 0x1840     # начало списка 1
COMBO_END = 0x19B5      # за последним сочетанием
VERB_PERM = 0x19B6      # массив перестановки

# --- операнды --------------------------------------------------------------

OPS_PTRS = 0x1B18       # таблица указателей
OPS_PTRS_END = 0x1BFC   # за последним указателем

# Пул делится на прогоны, у каждого своё смещение токена. Границы и смещения
# подтверждены байтами, известными по корпусу: `D6` BEG, `DE` запятая,
# `E3` литерал, `F0` FN, `D0` `)`, `CB` ALL, `06` LIST у SELECT.
RUNS = [
    (0,   42, 0xD6, 'операнд'),      # D6…FF
    (42,  82, 0xA0, 'операция'),     # CA…F1
    (82,  95, -82,  'SELECT'),       # 00…0C
    (95, 104, -82,  'CLEAR'),        # 0D…15
]

DEFAULT = os.path.join('..', 'DSKCommander', 'docs', 'samples', 'Iskra226',
                       '345_dk_spb', 'disk1side0.dsk')

# Что было известно по корпусу до чтения таблиц — на этом она и проверяется.
KNOWN = {
    'операнд': {0xD6: 'BEG', 0xD7: 'END', 0xD8: 'ROUND(', 0xDB: '#',
                0xDC: '/', 0xDD: ';', 0xDE: ',', 0xDF: 'TAB(', 0xE0: '()',
                0xE1: 'STR(', 0xE2: 'HEX(', 0xE3: '"', 0xE4: "'",
                0xE9: '-', 0xEA: '+', 0xEB: '(', 0xEC: 'POS(', 0xED: 'LEN(',
                0xEE: 'NUM(', 0xEF: 'VAL(', 0xF0: 'FN', 0xF1: '#PI',
                0xF2: 'ABS(', 0xF3: 'INT(', 0xF4: 'RND(', 0xF5: 'SGN(',
                0xF6: 'SQR(', 0xF7: 'LOG(', 0xF8: 'EXP('},
    'операция': {0xCB: 'ALL', 0xCC: 'GOSUB', 0xCD: 'GOTO', 0xD0: ')',
                 0xD1: 'TO', 0xD2: 'STEP', 0xD3: 'THEN', 0xD4: '>',
                 0xD5: '<>', 0xD6: '<=', 0xD7: '<', 0xD8: '>=', 0xD9: '=',
                 0xDB: '#', 0xDC: '/', 0xDD: ';', 0xDE: ',', 0xDF: '*',
                 0xE0: '^', 0xE6: 'OR', 0xE7: 'AND', 0xE9: '-', 0xEA: '+',
                 0xEB: '('},
    'SELECT': {0x00: '#', 0x05: 'P', 0x06: 'LIST', 0x07: 'PRINT',
               0x08: 'PLOT', 0x0A: 'DISK'},
    'CLEAR': {0x11: 'V', 0x12: 'N', 0x14: 'P'},
}


def decode_verbs(data):
    blob = ''.join(WORDS)
    got = data[VERB_START:VERB_START + len(blob)].decode('latin1')
    if got != blob:
        raise SystemExit('список слов не сходится: %r' % got[:60])

    # Сочетания: код продолжения — байт вне латинских прописных, они и
    # разделяют записи.
    tail = data[VERB_START + len(blob):COMBO_END]
    names = list(WORDS)
    i = 0
    while i < len(tail):
        code = tail[i]
        i += 1
        text = ''
        while i < len(tail) and not (tail[i] >= 0x60 and
                                     not 0x41 <= tail[i] <= 0x5A):
            text += chr(tail[i])
            i += 1
        names.append(names[code - 0x60] + ' ' + text)

    perm = data[VERB_PERM:VERB_PERM + len(names)]
    if len(perm) != len(names):
        raise SystemExit('перестановка короче списка')
    return list(zip(perm, names))


def decode_ops(data):
    ptrs = []
    for p in range(OPS_PTRS, OPS_PTRS_END, 2):
        ptrs.append(data[p] | (data[p + 1] << 8))
    words = []
    for k, p in enumerate(ptrs):
        end = ptrs[k + 1] if k + 1 < len(ptrs) else p + 1
        raw = data[p:end]
        # Старший бит на последней букве — признак конца слова у машины;
        # нам границы уже дали указатели, бит просто снимаем.
        text = ''.join(chr(b & 0x7F) for b in raw if b & 0x7F >= 0x20)
        words.append(text)
    return words


def main():
    args = [a for a in sys.argv[1:] if a != '--check']
    check = '--check' in sys.argv
    path = args[0] if args else DEFAULT
    data = open(path, 'rb').read()

    verbs = decode_verbs(data)
    words = decode_ops(data)

    print('=== глаголы: %d' % len(verbs))
    for token, name in sorted(verbs):
        print('%02X  %s' % (token, name))

    bad = 0
    for lo, hi, base, kind in RUNS:
        print('\n=== %s: %d' % (kind, hi - lo))
        for i in range(lo, hi):
            token = (i + base) & 0xFF if base > 0 else i + base
            text = words[i]
            mark = ''
            want = KNOWN.get(kind, {}).get(token)
            if want is not None:
                if want == text:
                    mark = ' ✓'
                else:
                    mark = ' ← ждали %r' % want
                    bad += 1
            print('%02X  %-10s%s' % (token, text if text else '—', mark))

    if check:
        print('\nрасхождений с корпусом: %d' % bad)
        return 1 if bad else 0
    return 0


if __name__ == '__main__':
    sys.exit(main())
