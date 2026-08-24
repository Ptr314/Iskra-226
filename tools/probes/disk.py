# -*- coding: utf-8 -*-
"""Разбор образа дискеты «Искры» прямо из сырых секторов.

Не зависит от dsk_tools: образы 345_dk_spb — плоские, 1001 сектор по 256 байт
(77 дорожек по 13 секторов). Нужно, чтобы добраться до файлов данных, которые
`iskra --cat` не отдаёт, и до дисков без каталога.

    py tools/probes/disk.py kind   ОБРАЗ             — каталог, загрузочный, без каталога
    py tools/probes/disk.py list   ОБРАЗ
    py tools/probes/disk.py get    ОБРАЗ ИМЯ ФАЙЛ
    py tools/probes/disk.py data   ОБРАЗ ИМЯ         — файл данных записями и значениями
    py tools/probes/disk.py sect   ОБРАЗ N [СКОЛЬКО]
"""
import os
import sys
from decimal import Decimal

sys.stdout.reconfigure(encoding='utf-8')

SEC = 256

KOI = {}
for i in range(0x20, 0x80):
    KOI[i] = chr(i)
_UP = 'ЮАБЦДЕФГХИЙКЛМНОПЯРСТУЖВЬЫЗШЭЩЧЪ'
for i, c in enumerate(_UP):
    KOI[0xE0 + i] = c
KOI[0x24] = '¤'


def kstr(bs):
    return ''.join(KOI.get(b, '.') for b in bs)


def hexs(bs):
    return ' '.join('%02X' % b for b in bs)


class Image(object):
    def __init__(self, path):
        self.path = path
        self.name = os.path.basename(path)
        with open(path, 'rb') as f:
            self.data = f.read()
        self.n = len(self.data) // SEC

    def sector(self, i):
        return self.data[i * SEC:(i + 1) * SEC]

    # --- каталог -----------------------------------------------------------
    def params(self):
        """Первые 16 байт нулевого сектора."""
        return self.sector(0)[:16]

    def index_sectors(self):
        """Число секторов указателя. Берётся из параметров нулевого сектора."""
        p = self.params()
        return p[1] | (p[0] << 8), p

    def entries(self):
        """(сектор_указателя, номер_записи, статус, тип, начало, конец, имя)."""
        ls, p = self.index_sectors()
        if not (1 <= ls <= 255):
            return None
        out = []
        for s in range(ls):
            sec = self.sector(s)
            first = 16 if s == 0 else 0
            for off in range(first, SEC, 16):
                r = sec[off:off + 16]
                if r[0] == 0:
                    continue
                out.append((s, off // 16, r[0], r[1],
                            (r[2] << 8) | r[3], (r[4] << 8) | r[5],
                            kstr(r[8:16]).rstrip()))
        return out

    def raw_name(self, s, i):
        """Восемь байт имени записи (s, i) как они лежат на диске."""
        return self.sector(s)[i * 16 + 8:i * 16 + 16]

    def find(self, name):
        for e in self.entries() or []:
            if e[6] == name:
                return e
        return None

    def file_bytes(self, e):
        """Сырые секторы файла целиком, без снятия служебных байт."""
        return self.data[e[4] * SEC:(e[5] + 1) * SEC]


TYPE = {0x80: 'ПФ', 0x00: 'ФД'}
BOOT = bytes([0x06, 0x90, 0x09, 0x90, 0x07, 0x90])
SECKIND = {0x8B: 'запись', 0x02: 'начало', 0x8F: 'продолжение',
           0x03: 'конец', 0x1C: 'концевая'}


def status(b):
    # Точное сравнение с 10/11 не годится: встречается и 21. Значим бит 0.
    return 'вычеркнут' if (b & 1) else 'жив'


def number(v):
    """8 байт → Decimal. Мантисса 13 цифр, точка после третьей, порядок —
    знаковый байт в степенях 1000 (см. docs/format.md, разд. 2)."""
    digits = ''.join('%02X' % b for b in v[:6]) + '%X' % (v[7] >> 4)
    exp = v[6] - 256 if v[6] > 127 else v[6]
    out = Decimal(digits).scaleb(-10 + 3 * exp)
    return -out if (v[7] & 15) else out


def values(sec):
    """Значения сектора записи: [(тип, байты)]."""
    out = []
    p = 1
    while p + 2 <= SEC:
        t, ln = sec[p], sec[p + 1]
        if (t == 0 and ln == 0) or ln == 0 or p + 2 + ln > SEC:
            break
        out.append((t, sec[p + 2:p + 2 + ln]))
        p += 2 + ln
    return out


def cmd_list(img):
    ls, p = img.index_sectors()
    print('=== %s  секторов %d' % (img.name, img.n))
    print('    параметры сектора 0: %s' % hexs(p))
    ents = img.entries()
    if ents is None:
        print('    указатель не читается (LS=%d)' % ls)
        return
    print('    LS=%d' % ls)
    print('    %-9s %-4s %-9s %6s %6s %6s  %s'
          % ('имя', 'тип', 'статус', 'нач', 'кон', 'сект', 'ук.'))
    for s, i, st, tp, a, b, nm in ents:
        print('    %-9s %-4s %-9s %6d %6d %6d  %d.%d'
              % (nm, TYPE.get(tp, '%02X' % tp), status(st),
                 a, b, b - a + 1, s, i))
    print('    всего %d' % len(ents))


def cmd_kind(img):
    d = img.data
    if d[:6] == BOOT:
        print('%s: загрузочный, «%s»' % (img.name, kstr(d[6:24])))
        return
    if d[:5] == b'\x7e\xe1\xf3\xed\xe2':
        print('%s: пакет %s' % (img.name, kstr(d[:16])))
        return
    if img.entries():
        ls, _ = img.index_sectors()
        print('%s: каталог, LS=%d, файлов %d' % (img.name, ls,
                                                 len(img.entries())))
        return
    def looks_like_header(sec):
        if sec[0] != 0x01 or sec[9] not in (0x20, 0x21, 0x24, 0x25):
            return False
        # Имя — печатные знаки КОИ-8; иначе это случайное совпадение.
        return all(b in KOI and KOI[b] != '.' for b in sec[1:9])

    hdrs = [s for s in range(img.n) if looks_like_header(img.sector(s))]
    if hdrs:
        print('%s: без каталога, абсолютная адресация; программ %d: %s'
              % (img.name, len(hdrs),
                 ', '.join('%s@%d' % (kstr(img.sector(s)[1:9]).strip(), s)
                           for s in hdrs)))
        return
    print('%s: %s' % (img.name,
                      'пустой' if not any(d) else 'не опознан'))


def cmd_data(img, name):
    e = img.find(name)
    if not e:
        print('нет такого файла')
        return
    rec = 0
    for s in range(e[4], e[5] + 1):
        sec = img.sector(s)
        if not any(sec):
            continue
        kind = SECKIND.get(sec[0])
        if kind is None:
            print('  сектор %d: не запись (первый байт %02X)' % (s, sec[0]))
            continue
        if sec[0] == 0x1C:
            print('  сектор %d: концевая запись, использовано %d'
                  % (s, (sec[1] << 8) | sec[2]))
            continue
        if sec[0] in (0x8B, 0x02):
            rec += 1
            print('  --- запись %d, сектор %d (%s)' % (rec, s, kind))
        else:
            print('  --- сектор %d (%s)' % (s, kind))
        for t, v in values(sec):
            if t == 0x40:
                print('      стр(%3d) [%s]' % (len(v), kstr(v)))
            elif t == 0x00:
                print('      чис      %s' % number(v))
            else:
                print('      тип %02X длина %d: %s' % (t, len(v), hexs(v)))


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return
    cmd, path = sys.argv[1], sys.argv[2]
    if cmd == 'list':
        cmd_list(Image(path))
    elif cmd == 'kind':
        cmd_kind(Image(path))
    elif cmd == 'data':
        cmd_data(Image(path), sys.argv[3])
    elif cmd == 'get':
        img = Image(path)
        e = img.find(sys.argv[3])
        if not e:
            print('нет такого файла')
            return
        with open(sys.argv[4], 'wb') as f:
            f.write(img.file_bytes(e))
        print('%s: секторы %d…%d, %d байт' % (e[6], e[4], e[5],
                                              (e[5] - e[4] + 1) * SEC))
    elif cmd == 'sect':
        img = Image(path)
        a = int(sys.argv[3])
        n = int(sys.argv[4]) if len(sys.argv) > 4 else 1
        for s in range(a, min(a + n, img.n)):
            sec = img.sector(s)
            print('--- сектор %d' % s)
            for off in range(0, SEC, 16):
                print('%04X  %-47s  %s' % (off, hexs(sec[off:off + 16]),
                                           kstr(sec[off:off + 16])))


if __name__ == '__main__':
    main()