# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
# Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
# Описание: проверка вики-статей — таблицы токенов должны быть отсортированы
# по возрастанию токена, а сами токены не должны противоречить docs/format.md.

import glob
import io
import os
import re
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(_HERE, '..', '..'))
DOCS = os.path.join(ROOT, 'docs')

# Ячейка вида <code>7F</code> либо <code>F2</code>–<code>F8</code>: берём первый байт.
CELL = re.compile(r'^<code>([0-9A-F]{2})</code>')


# Колонка проверяется, только если её заголовок назван так. Иначе под правило
# попали бы буквы приставок (`DC`, `DA`, `BA`) и маркеры чужой системы.
TOKEN_HEADS = ('токен', 'подкод', 'код', 'байт')


def header(lines):
    """Заголовки колонок таблицы, в нижнем регистре."""
    for ln in lines:
        if ln.startswith('!'):
            return [c.strip().lower() for c in ln[1:].split('!!')]
    return []


def rows(lines):
    """Разбить строки вики-таблицы на ячейки."""
    for ln in lines:
        if not ln.startswith('|') or ln.startswith('|-') or ln.startswith('|}'):
            continue
        if ln.startswith('|+') or ln.startswith('!'):
            continue
        yield [c.strip() for c in ln[1:].split('||')]


def tables(text):
    """Выдать таблицы файла как списки строк."""
    cur = None
    for ln in text.split('\n'):
        s = ln.strip()
        if s.startswith('{|'):
            cur = []
        elif s.startswith('|}'):
            if cur is not None:
                yield cur
            cur = None
        elif cur is not None:
            cur.append(s)


def check(path):
    text = io.open(path, encoding='utf-8').read()
    bad = 0
    for n, tbl in enumerate(tables(text), 1):
        head = header(tbl)
        # По колонкам: в двухколоночных таблицах каждая колонка своя.
        cols = {}
        for cells in rows(tbl):
            for i, c in enumerate(cells):
                if i >= len(head) or head[i] not in TOKEN_HEADS:
                    continue
                m = CELL.match(c)
                if m:
                    cols.setdefault(i, []).append(int(m.group(1), 16))
        for i, seq in cols.items():
            if len(seq) < 2:
                continue
            if seq != sorted(seq):
                bad += 1
                print('  таблица %d, колонка %d: не по возрастанию' % (n, i))
                print('   ', ' '.join('%02X' % v for v in seq))
    return bad


def main():
    paths = sorted(glob.glob(os.path.join(DOCS, 'ARTICLE-wiki*.txt')))
    if not paths:
        print('статей не найдено в %s' % DOCS)
        return 1
    total = 0
    for path in paths:
        bad = check(path)
        print('%-28s %s' % (os.path.basename(path),
                            'порядок токенов верен' if not bad
                            else '%d нарушений' % bad))
        total += bad
    return 1 if total else 0


if __name__ == '__main__':
    sys.exit(main())
