# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
# Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
# Описание: сопоставление графических глаголов `06 xx` с их именами.
#
# Разовая проба. Работает на паре «текст + токены»: считает, сколько раз
# каждое имя встречается в листинге и сколько раз каждый глагол — в
# оттранслированной копии, и сводит их по числу аргументов.

import collections
import os
import re
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..'))
import iskra                                                # noqa: E402

CURRENCY = '¤'

NAMES = ['PLOT', 'NPLOT', 'DNPLOT', 'DRAW', 'DDRAW', 'NDRAW', 'DOT', 'LABEL',
         'FRAME', 'WINDOW', 'STRETCH', 'TURN', 'ORIGIN',
         CURRENCY + 'OPEN', CURRENCY + 'COPY', CURRENCY + 'LET',
         CURRENCY + 'MOVE']

# Подкоды 06 xx, которые точно графические либо ещё не опознаны.
CODES = [0x00, 0x0F, 0x13, 0x14, 0x15, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E,
         0x1F, 0x22, 0x23]


def statements(body):
    """Разбить строку листинга на операторы по двоеточиям вне скобок."""
    body = re.sub(r'"[^"\n]*"', '""', body)
    out = []
    cur = ''
    depth = 0
    for ch in body:
        if ch == '(':
            depth += 1
        elif ch == ')':
            depth -= 1
        if ch == ':' and depth == 0:
            out.append(cur)
            cur = ''
        else:
            cur += ch
    out.append(cur)
    return out


def arity(tail):
    """Число операндов верхнего уровня."""
    tail = tail.strip()
    if not tail:
        return 0
    n = 1
    depth = 0
    for ch in tail:
        if ch == '(':
            depth += 1
        elif ch == ')':
            depth -= 1
        elif ch == ',' and depth == 0:
            n += 1
    return n


def text_ops(path):
    """{номер строки: [(имя, арность)]} и общий счётчик."""
    per = collections.defaultdict(list)
    for line in open(path, encoding='utf-8', errors='replace'):
        m = re.match(r'^(\d+)\s?(.*)$', line.rstrip('\n'))
        if not m:
            continue
        num = int(m.group(1))
        for st in statements(m.group(2)):
            st = st.strip()
            for w in NAMES:
                mm = re.match(r'^' + re.escape(w) + r'(?![A-Z0-9])(.*)$', st)
                if mm:
                    per[num].append((w, arity(mm.group(1))))
                    break
    return per


def bin_ops(path):
    data = open(path, 'rb').read()
    stream, _ = iskra.build_stream(data)
    l1 = stream[0] << 8 | stream[1]
    l2 = stream[2] << 8 | stream[3]
    l3 = stream[4] << 8 | stream[5]
    per = collections.defaultdict(list)
    for _off, num, body in iskra.split_records(stream, 6 + l1 + l2 + l3):
        if num is None:
            continue
        for verb, ops in iskra.split_statements(body):
            if verb > 0xFF and (verb & 0xFF) in CODES:
                n = 0
                if ops:
                    n = 1 + sum(1 for t in iskra.walk_tokens(verb, ops)
                                if t[1] == 0xDE)
                per[num].append(('06 %02X' % (verb & 0xFF), n))
    return per


def main():
    if len(sys.argv) < 3:
        print('graphverbs.py ЛИСТИНГ ОТТРАНСЛИРОВАННЫЙ')
        return
    tp, bp = sys.argv[1], sys.argv[2]
    tper, bper = text_ops(tp), bin_ops(bp)

    tc = collections.Counter()
    ta = collections.defaultdict(collections.Counter)
    for lst in tper.values():
        for w, n in lst:
            tc[w] += 1
            ta[w][n] += 1
    bc = collections.Counter()
    ba = collections.defaultdict(collections.Counter)
    for lst in bper.values():
        for v, n in lst:
            bc[v] += 1
            ba[v][n] += 1

    print('=== имена в тексте')
    for w, n in tc.most_common():
        print('   %-8s x%-4d арность %s' % (w, n, dict(ta[w])))
    print('=== глаголы в токенах')
    for v, n in bc.most_common():
        print('   %-6s x%-4d арность %s' % (v, n, dict(ba[v])))

    # Совпадения на строках с одним номером, где и число операторов, и
    # набор арностей совпали целиком: только такие строки надёжны, редакции
    # программы всё-таки разные.
    pair = collections.Counter()
    for num, tl in tper.items():
        bl = bper.get(num)
        if not bl or len(bl) != len(tl):
            continue
        if [n for _, n in tl] != [n for _, n in bl]:
            continue
        for (w, _), (v, _) in zip(tl, bl):
            pair[(v, w)] += 1
    print('=== пары «глагол ↔ имя» на построчно совпавших операторах')
    for (v, w), c in sorted(pair.items(), key=lambda kv: -kv[1]):
        print('   %-6s = %-8s x%d' % (v, w, c))


if __name__ == '__main__':
    main()
