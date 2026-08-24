# -*- coding: utf-8 -*-
"""Есть ли в корпусе образов переполнение сектора указателя каталога.

Признаки переполнения, которые ищутся:
  1. запись лежит не в том секторе, который даёт хеш имени;
  2. сектор указателя занят целиком (15 записей в нулевом, 16 в прочих);
  3. дырки в последовательности слотов — след удалённой записи.
"""
import collections
import os
import sys

sys.stdout.reconfigure(encoding='utf-8')
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from disk import Image, kstr, SEC                                # noqa: E402
from hash import DIR, index_sector                               # noqa: E402


def capacity(s):
    return 15 if s == 0 else 16


def main():
    misplaced = []
    full = []
    gaps = []
    hottest = []
    total = scratched = 0

    for f in sorted(os.listdir(DIR)):
        if not f.lower().endswith('.dsk'):
            continue
        img = Image(os.path.join(DIR, f))
        ents = img.entries()
        if not ents:
            continue
        ls, _ = img.index_sectors()

        slots = collections.defaultdict(list)
        for s, i, st, tp, a, b, nm in ents:
            total += 1
            if st & 1:
                scratched += 1
            slots[s].append(i)
            raw = bytes(img.raw_name(s, i))
            want = index_sector(raw, ls)
            if want != s:
                misplaced.append((f, kstr(raw), ls, s, want))

        # Занятость и дырки. Слот занят, если статус не ноль; слот со стёртым
        # заголовком, но уцелевшим именем — след прежней записи.
        worst = (0, 0, 0)
        for s in range(ls):
            sec = img.sector(s)
            first = 16 if s == 0 else 0
            used, ghost = [], []
            for off in range(first, SEC, 16):
                if sec[off]:
                    used.append(off // 16)
                elif any(sec[off + 8:off + 16]):
                    ghost.append(off // 16)
            if not used and not ghost:
                continue
            cap = capacity(s)
            if len(used) == cap:
                full.append((f, s, cap))
            # Живые и следы вместе должны идти подряд от первого слота.
            both = sorted(used + ghost)
            expect = list(range(first // 16, first // 16 + len(both)))
            if both != expect:
                gaps.append((f, s, used, ghost))
            globals()['GHOSTS'] = globals().get('GHOSTS', 0) + len(ghost)
            if len(used) > worst[1]:
                worst = (s, len(used), cap)
        if worst[1]:
            hottest.append((worst[1] / float(worst[2]), f, ls) + worst)

    print('записей всего %d, из них вычеркнутых %d' % (total, scratched))
    print()
    print('1. Записи не на своём месте (прямой признак переполнения): %d'
          % len(misplaced))
    for m in misplaced[:10]:
        print('   %s [%s] LS=%d: лежит в %d, хеш даёт %d' % m)
    print()
    print('2. Секторы указателя, занятые целиком: %d' % len(full))
    for x in full:
        print('   %s сектор %d (вместимость %d)' % x)
    print()
    print('3. Слотов со стёртым заголовком, но уцелевшим именем: %d'
          % globals().get('GHOSTS', 0))
    print('   секторов, где живые и следы не идут подряд от первого слота: %d'
          % len(gaps))
    for x in gaps[:10]:
        print('   %s сектор %d: живые %s, следы %s' % x)
    print()
    print('Насколько близко подходили к переполнению:')
    for r, f, ls, s, n, cap in sorted(hottest, reverse=True)[:8]:
        print('   %-18s LS=%-3d сектор %2d: %2d из %2d  (%3.0f%%, свободно %d)'
              % (f, ls, s, n, cap, 100 * r, cap - n))


if __name__ == '__main__':
    main()