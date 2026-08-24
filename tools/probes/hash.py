# -*- coding: utf-8 -*-
"""Хеш имени файла в номер сектора указателя каталога — и его проверка.

«Искра» пользуется «старым» указателем Wang 2200. Проверено на всех
303 записях каталога с 32 образов: совпадение полное.

    py tools/probes/hash.py            проверка по всем образам
    py tools/probes/hash.py ИМЯ LS     номер сектора для имени
"""
import collections
import os
import sys

sys.stdout.reconfigure(encoding='utf-8')
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from disk import Image, kstr, KOI                                # noqa: E402

DIR = (r'C:\Storages\GoogleDrive\Projects\Emulation\DSKCommander'
       r'\docs\samples\Iskra226\345_dk_spb')


def index_sector(name8, index_sectors):
    """Номер сектора указателя для имени (8 байт, дополнено пробелами).

        tmp = 0;  для каждого байта имени:  tmp ^= байт
        tmp *= 3
        tmp = (tmp % 256) + (tmp / 256)
        сектор = tmp % число_секторов_указателя
    """
    t = 0
    for c in name8:
        t ^= c
    t *= 3
    t = (t % 256) + t // 256
    return t % index_sectors


def wang_new(name8):
    """«Новый» указатель Wang (MVP OS 2.5). В «Искре» не встречается —
    оставлен, чтобы было видно, что проверялся и он."""
    t = 0
    for i, c in enumerate(name8):
        t += (16 * (t % 16) + t // 16) if i % 2 == 0 else c
    return t % 256


def dataset():
    """[(имя8, LS, сектор, образ)] по всем образам с каталогом."""
    rows = []
    for f in sorted(os.listdir(DIR)):
        if not f.lower().endswith('.dsk'):
            continue
        img = Image(os.path.join(DIR, f))
        ents = img.entries()
        if not ents:
            continue
        ls, _ = img.index_sectors()
        for s, i, st, tp, a, b, nm in ents:
            rows.append((bytes(img.raw_name(s, i)), ls, s, f))
    return rows


def main():
    if len(sys.argv) > 2:
        koi = dict((v, k) for k, v in KOI.items())
        name = ''.join(sys.argv[1])[:8].ljust(8)
        nm = bytes(koi.get(ch, 0x20) for ch in name)
        ls = int(sys.argv[2])
        print('[%s] LS=%d -> сектор %d' % (kstr(nm), ls, index_sector(nm, ls)))
        return

    rows = dataset()
    ok = collections.Counter()
    bad = []
    for nm, ls, sec, f in rows:
        if index_sector(nm, ls) == sec:
            ok[ls] += 1
        else:
            bad.append((f, kstr(nm), ls, sec, index_sector(nm, ls)))
    print('записей %d, образов %d' % (len(rows), len(set(r[3] for r in rows))))
    print('сошлось %d, разошлось %d' % (sum(ok.values()), len(bad)))
    print('по размеру указателя: %s' % dict(sorted(ok.items())))
    for b in bad[:10]:
        print('   %s [%s] LS=%d: на диске %d, посчитано %d' % b)


if __name__ == '__main__':
    main()