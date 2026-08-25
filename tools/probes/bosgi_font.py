# -*- coding: utf-8 -*-
"""
bosgi_font.py — знакогенератор «Искры» с рис. 3.1 руководства по БОСГИ.

    py tools/probes/bosgi_font.py [--check]

Читает docs/BOSGI-FONT-fix.jpg («Номенклатура символов», рис. 3.1) и пишет
src/font/koi7-5x8.txt. С ключом --check ничего не пишет, а сверяет то, что
получилось, с уже записанным файлом.

Нужен Pillow (`py -m pip install pillow`) — единственное место в проекте,
где сторонний пакет требуется; проба одноразовая, в сборку не входит.

**Скан в git не попадает** (`docs/*.jpg` в `.gitignore`, как и книга), так
что из чистой копии проба не запустится. Результат её работы —
`src/font/koi7-5x8.txt` — в репозитории лежит готовым.

Что на рисунке
--------------

96 рамок, 12 рядов по восемь: слева шесть рядов и справа шесть. Внутри
каждой рамки решётка **5 столбцов на 8 строк**; белый квадратик — тёмная
точка экрана, чёрное поле — светящаяся. Так и должно быть: обычное
состояние экрана «Искры» негативное, светлые знаки на тёмном поле
(руководство, разд. 2.1).

Порядок кодов — КОИ-7 Н2, но начиная с 0x40 и с переносом после 0x7F:

    левый  блок, ряды 1-6   →  40-47 48-4F 50-57 58-5F 60-67 68-6F
    правый блок, ряды 1-2   →  70-77 78-7F
    правый блок, ряды 3-6   →  20-27 28-2F 30-37 38-3F

Порядок доказан содержимым: левый ряд 1 читается `@ABCDEFG`, левый ряд 3 —
`PQRSTUVW`, правый ряд 5 — `01234567`, правый ряд 4 — `()*+,-./`. Ошибиться
в раскладке нельзя — любой сдвиг ломает и цифры, и латиницу разом.

Два отличия от КОИ-7 Н2, которые видны прямо на рисунке:

  * **0x5F — не подчёркивание, а Ъ**: та же буква, что Ь, с перекладиной
    слева вверху;
  * **0x7F пуст** — все 40 клеток рамки белые. В КОИ-7 Н2 там Ъ, и он
    переехал на 0x5F, освободив позицию РЗБ (DEL).

Верхняя строка знакоместа пуста у всех 96 знаков: сам глиф 5x7, а восьмая
строка — межстрочный просвет сверху. Это и служит проверкой разметки.

Как разбирается
---------------

1. Порог по локальному фону (уменьшенная копия картинки) — скан неровно
   освещён, глобальный порог тут не годится.
2. Связные области краски: рамка знака — самая крупная, шум отсеивается по
   площади и размеру.
3. Рамка внутри области: строка считается рамочной, если краски в ней не
   меньше 30 точек, столбец — если не меньше 70% высоты.
4. Разметка решётки: шаг берётся от рамки, а смещение подгоняется по
   гистограмме центров белых просветов. Затем смещение по вертикали
   двигается, пока верхняя строка не выйдет пустой.
5. Каждая клетка опрашивается 45 раз — пять сдвигов по горизонтали, три по
   вертикали, три размера окна, — и решает большинство. Одиночная выборка
   на этом скане врёт: краска растекается, а сетка местами пропадает.

Что пришлось поправить руками
-----------------------------

Восемь знаков голосование не вытянуло: там скан повреждён — то краска
затекла в верхнюю строку рамки, то потерян крайний столбец. Каждая правка
ниже прочитана глазами на увеличении и сверена с парной буквой: латинская и
кириллическая A, B и В, E и Е — один глиф.
"""

import io
import os
import sys

try:
    from PIL import Image
except ImportError:                                  # pragma: no cover
    raise SystemExit("нужен Pillow: py -m pip install pillow")

_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SRC = os.path.join(_ROOT, "docs", "BOSGI-FONT-fix.jpg")
DST = os.path.join(_ROOT, "src", "font", "koi7-5x8.txt")

COLS, ROWS = 5, 8

# Правки по прочтению на увеличении; ключ — код, значение — «строка: вид».
FIXES = {
    0x41: {5: "#...#"},                 # A: скан размыл левый столбец, срав. 61 А
    0x60: {7: "#.###"},                 # Ю: нижняя строка как верхняя
    0x70: {0: "....."},                 # П: в верхнюю строку затекла подпись
    0x75: {0: "....."},                 # У: то же
    0x76: {0: ".....", 4: ".###."},     # Ж: то же; перекладина сплошная
    0x77: {0: ".....", 1: "####.",      # В: рамка съехала, глиф тот же, что 42 B
           2: "#...#", 3: "#...#", 4: "####.",
           5: "#...#", 6: "#...#", 7: "####."},
    0x7D: {0: "....."},                 # Щ: пятно над рамкой
    0x36: {5: "#...#", 6: "#...#"},     # 6: правый столбец потерян сканом
}


def load(path):
    im = Image.open(path).convert("L")
    w, h = im.size
    px = im.load()
    bg = im.resize((w // 40, h // 40), Image.BOX).resize((w, h), Image.BILINEAR)
    bp = bg.load()
    return w, h, px, bp


def components(w, h, px, bp):
    """Связные области краски, отсеянные по площади и размеру."""
    def ink(x, y):
        return px[x, y] < bp[x, y] - 28

    seen = [[0] * w for _ in range(h)]
    out = []
    for y0 in range(h):
        for x0 in range(w):
            if not ink(x0, y0) or seen[y0][x0]:
                continue
            stack = [(x0, y0)]
            seen[y0][x0] = 1
            mnx = mxx = x0
            mny = mxy = y0
            n = 0
            while stack:
                x, y = stack.pop()
                n += 1
                if x < mnx: mnx = x
                if x > mxx: mxx = x
                if y < mny: mny = y
                if y > mxy: mxy = y
                for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                    nx, ny = x + dx, y + dy
                    if 0 <= nx < w and 0 <= ny < h and ink(nx, ny) and not seen[ny][nx]:
                        seen[ny][nx] = 1
                        stack.append((nx, ny))
            if n > 200 and mxx - mnx > 15 and mxy - mny > 25:
                out.append((mnx, mny, mxx, mxy))
    return out


def in_rows(comps):
    """Двенадцать рядов по восемь: слева шесть и справа шесть, парами."""
    comps = sorted(comps, key=lambda c: c[1])
    rows, cur = [], [comps[0]]
    for c in comps[1:]:
        if c[1] - cur[0][1] < 40:
            cur.append(c)
        else:
            rows.append(sorted(cur, key=lambda z: z[0]))
            cur = [c]
    rows.append(sorted(cur, key=lambda z: z[0]))
    if [len(r) for r in rows] != [16] * 6:
        raise SystemExit("рисунок разобран не на 6x16 рамок: %s"
                         % [len(r) for r in rows])
    return rows


class Sheet(object):
    def __init__(self, path):
        self.w, self.h, self.px, self.bp = load(path)
        self.rows = in_rows(components(self.w, self.h, self.px, self.bp))

    def ink(self, x, y):
        if x < 0 or y < 0 or x >= self.w or y >= self.h:
            return False
        return self.px[x, y] < self.bp[x, y] - 28

    def frame(self, c):
        x0, y0, x1, y1 = c
        ys = [y for y in range(y0, y1 + 1)
              if sum(1 for x in range(x0, x1 + 1) if self.ink(x, y)) >= 30]
        t, b = (ys[0], ys[-1]) if ys else (y0, y1)
        xs = [x for x in range(x0, x1 + 1)
              if sum(1 for y in range(t, b + 1) if self.ink(x, y)) >= 0.70 * (b - t + 1)]
        l, r = (xs[0], xs[-1]) if xs else (x0, x1)
        # Рамка бывает разорвана краской соседа: тогда берём саму область.
        if not (33 <= r - l <= 46):
            l, r = x0, x1
        if not (70 <= b - t <= 90):
            t, b = y0, y1
        return l, t, r, b

    def gaps(self, l, t, r, b, axis):
        """Гистограмма центров белых просветов — по ней ловится решётка."""
        if axis == "x":
            hist = [0.0] * (r - l + 1)
            for y in range(t, b + 1):
                run = 0
                for x in range(l, r + 2):
                    if x <= r and not self.ink(x, y):
                        run += 1
                        continue
                    if 2 <= run <= 9:
                        hist[int(round((2 * x - 1 - run) / 2.0 - l))] += 1
                    run = 0
            return hist
        hist = [0.0] * (b - t + 1)
        for x in range(l, r + 1):
            run = 0
            for y in range(t, b + 2):
                if y <= b and not self.ink(x, y):
                    run += 1
                    continue
                if 2 <= run <= 11:
                    hist[int(round((2 * y - 1 - run) / 2.0 - t))] += 1
                run = 0
        return hist

    @staticmethod
    def offset(hist, n, span):
        """Гребёнка из n зубьев с шагом span/n: подбираем смещение."""
        step = span / float(n)
        best = None
        d = -3.0
        while d <= 3.0:
            s = 0.0
            for k in range(n):
                t = step * (k + 0.5) + d
                i = int(t)
                fr = t - i
                if 0 <= i < len(hist):
                    s += hist[i] * (1 - fr)
                if 0 <= i + 1 < len(hist):
                    s += hist[i + 1] * fr
            if best is None or s > best[0]:
                best = (s, d)
            d += 0.25
        return best[1]

    def cell(self, geom, r, k, jx, jy, frac):
        l, t, cw, ch, dx, dy = geom
        x = int(round(l + cw * (k + 0.5) + dx + jx))
        y = int(round(t + ch * (r + 0.5) + dy + jy))
        rx = max(1, int(cw * frac))
        ry = max(1, int(ch * frac))
        white = total = 0
        for ddy in range(-ry, ry + 1):
            for ddx in range(-rx, rx + 1):
                total += 1
                if not self.ink(x + ddx, y + ddy):
                    white += 1
        return white * 3 < total          # светится, если белого мало

    def glyph(self, comp):
        l, t, r, b = self.frame(comp)
        cw = (r - l + 1) / float(COLS)
        ch = (b - t + 1) / float(ROWS)
        dx = self.offset(self.gaps(l, t, r, b, "x"), COLS, r - l + 1)
        dy = self.offset(self.gaps(l, t, r, b, "y"), ROWS, b - t + 1)
        # Верхняя строка знакоместа пуста у всех 96 знаков: если она вышла
        # засвеченной, разметка съехала вверх — двигаем, пока не сойдётся.
        for shift in (0, 0.5, 1.0, 1.5, 2.0, 2.5, 3.0, -0.5, -1.0):
            g = (l, t, cw, ch, dx, dy + shift)
            if not any(self.cell(g, 0, k, 0, 0, 0.28) for k in range(COLS)):
                dy += shift
                break
        geom = (l, t, cw, ch, dx, dy)

        votes = [[0] * COLS for _ in range(ROWS)]
        total = 0
        for jx in (-1.0, -0.5, 0.0, 0.5, 1.0):
            for jy in (-0.5, 0.0, 0.5):
                for frac in (0.20, 0.28, 0.34):
                    total += 1
                    for row in range(ROWS):
                        for k in range(COLS):
                            if self.cell(geom, row, k, jx, jy, frac):
                                votes[row][k] += 1
        return ["".join("#" if votes[row][k] * 2 > total else "."
                        for k in range(COLS)) for row in range(ROWS)]


def code_at(row, col):
    """Раскладка рисунка: с 0x40, перенос после 0x7F на 0x20."""
    code = 0x40 + row * 8 + col if col < 8 else 0x70 + row * 8 + (col - 8)
    return code if code <= 0x7F else 0x20 + (code - 0x80)


def build():
    sheet = Sheet(SRC)
    glyphs = {}
    for row in range(6):
        for col in range(16):
            glyphs[code_at(row, col)] = sheet.glyph(sheet.rows[row][col])
    for code, fix in FIXES.items():
        for row, bits in fix.items():
            glyphs[code][row] = bits

    bad = [c for c in range(0x20, 0x80) if glyphs[c][0] != "." * COLS]
    if bad:
        raise SystemExit("верхняя строка занята у %s"
                         % " ".join("%02X" % c for c in bad))

    out = io.StringIO()
    for code in range(0x20, 0x80):
        for row in range(ROWS):
            out.write(u"%02X:%s\n" % (code, glyphs[code][row].replace("#", "*")))
        out.write(u"\n")
    return out.getvalue()


def main():
    text = build()
    if "--check" in sys.argv:
        have = io.open(DST, encoding="utf-8").read()
        print("сходится" if have == text else "РАСХОДИТСЯ с " + DST)
        return
    with io.open(DST, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    print("записано %s" % os.path.relpath(DST, _ROOT))


if __name__ == "__main__":
    main()
