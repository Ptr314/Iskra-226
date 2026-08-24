# -*- coding: utf-8 -*-
"""Перепись файлов данных по всем образам: коды секторов и коды значений.

Гипотеза, проверяемая скриптом (книга, разд. 18.6):
    сектор записи = <код сектора> затем пары <тип> <длина> <данные>
"""
import collections
import os
import sys

sys.stdout.reconfigure(encoding='utf-8')

SEC = 256
DIR = (r'C:\Storages\GoogleDrive\Projects\Emulation\DSKCommander'
       r'\docs\samples\Iskra226\345_dk_spb')

KOI = dict((i, chr(i)) for i in range(0x20, 0x80))
for i, c in enumerate('ЮАБЦДЕФГХИЙКЛМНОПЯРСТУЖВЬЫЗШЭЩЧЪ'):
    KOI[0xE0 + i] = c
KOI[0x24] = '¤'


def kstr(bs):
    return ''.join(KOI.get(b, '.') for b in bs)


def images():
    for f in sorted(os.listdir(DIR)):
        if f.lower().endswith('.dsk'):
            yield f, open(os.path.join(DIR, f), 'rb').read()


def catalog(data):
    """Список (статус, тип, нач, кон, имя) или None, если указатель не читается."""
    ls = (data[0] << 8) | data[1]
    if not (1 <= ls <= 255) or ls * SEC > len(data):
        return None
    out = []
    for s in range(ls):
        sec = data[s * SEC:(s + 1) * SEC]
        for off in range(16 if s == 0 else 0, SEC, 16):
            r = sec[off:off + 16]
            if r[0] == 0:
                continue
            if r[0] not in (0x10, 0x11):
                return None                       # не каталог
            a = (r[2] << 8) | r[3]
            b = (r[4] << 8) | r[5]
            if a > b or b * SEC >= len(data):
                return None
            out.append((r[0], r[1], a, b, kstr(r[8:16]).rstrip()))
    return out


def parse_record_sector(sec):
    """Разобрать сектор как <код> + пары <тип><длина><данные>.

    Возвращает (код, [(тип, длина)], хвост_ненулевой). Хвост после последнего
    значения должен быть нулевым — иначе разбор считается неудачным.
    """
    vals = []
    p = 1
    while p + 2 <= SEC:
        t, ln = sec[p], sec[p + 1]
        if t == 0 and ln == 0:
            break
        if ln == 0 or p + 2 + ln > SEC:
            return sec[0], vals, True
        vals.append((t, ln))
        p += 2 + ln
    tail = any(sec[p:])
    return sec[0], vals, tail


REC_CODES = (0x02, 0x8F, 0x03, 0x1C, 0x81, 0x82, 0x83, 0x8B, 0x84)


def classify(data, a, b):
    """Похож ли файл на записанный операторами DATA SAVE DC/DA."""
    used = [s for s in range(a, b + 1) if any(data[s * SEC:(s + 1) * SEC])]
    if not used:
        return 'пустой', [], used
    vals, off = [], 0
    for s in used:
        sec = data[s * SEC:(s + 1) * SEC]
        if sec[0] == 0x1C:
            continue
        c, v, tail = parse_record_sector(sec)
        if tail or not v:
            off += 1
        vals.extend(v)
    return ('записи' if off == 0 else
            'сырой' if off == len(used) else 'смесь(%d/%d)' % (off, len(used))
            ), vals, used


def main():
    val_shape = collections.Counter()
    sec_codes = collections.Counter()
    kinds = collections.Counter()
    rows = []
    for nm, data in images():
        cat = catalog(data)
        if cat is None:
            continue
        for st, tp, a, b, fn in cat:
            if tp != 0x00:
                continue
            kind, vals, used = classify(data, a, b)
            kinds[kind] += 1
            rows.append((nm, fn, b - a + 1, len(used), kind))
            if kind == 'записи':
                for t, ln in vals:
                    val_shape[(t, ln)] += 1
                for s in used:
                    sec_codes[data[s * SEC]] += 1

    print('=== файлы данных по устройству содержимого ===')
    for k, v in kinds.most_common():
        print('  %-12s %3d' % (k, v))
    print()
    print('=== файлы, разложившиеся на записи ===')
    for nm, fn, n, u, k in rows:
        if k == 'записи':
            print('  %-18s %-10s выделено %4d, занято %4d' % (nm, fn, n, u))
    print()
    print('=== первый байт сектора (только в них) ===')
    for k, v in sec_codes.most_common():
        print('  %02X  %6d' % (k, v))
    print()
    print('=== <тип> <длина> (только в них) ===')
    for (t, ln), v in sorted(val_shape.items(), key=lambda x: -x[1])[:25]:
        print('  %02X %3d   %6d' % (t, ln, v))
    print()
    print('=== сырые и смешанные ===')
    for nm, fn, n, u, k in rows:
        if k not in ('записи', 'пустой'):
            print('  %-18s %-10s выделено %4d, занято %4d  %s'
                  % (nm, fn, n, u, k))


if __name__ == '__main__':
    main()