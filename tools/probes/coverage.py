# -*- coding: utf-8 -*-
"""
coverage.py — что из языка уже исполняется, а что ещё нет.

    py tools/probes/coverage.py

Сверяет два множества:

  * глаголы, у которых есть ветка в `Interp::exec()` (`src/core/interp.cpp`);
  * глаголы, встречающиеся в корпусе.

и печатает разницу тремя срезами: сколько операторов остаётся неисполнимыми,
сколько программ упирается в каждый глагол и в каком порядке их добавлять,
чтобы быстрее всего довести программы до конца.

Разбор корпуса — питоновской моделью (`tools/iskra.py`), а не эмулятором:
модель даёт все глаголы программы разом, тогда как прогон останавливается на
первом же нереализованном. Файлы, которые модель не разбирает целиком, из
счёта выпадают — иначе мусор от рассинхронизации попал бы в статистику
несуществующими глаголами.
"""

import collections
import glob
import io
import os
import re
import sys

_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(_ROOT, "tools"))
sys.stdout.reconfigure(encoding="utf-8")

import iskra                                                   # noqa: E402
import detok                                                   # noqa: E402

# Не программы на Бейсике в оттранслированном виде: у двух обрезаны дампы,
# третий лежит в корпусе текстом (CLAUDE.md, «Корпус»).
SKIP = ("DIG_DEM", "FS", "STATIST")


def implemented():
    """Глаголы с веткой в Interp::exec() — и `case`, и `verb == ...`."""
    src = io.open(os.path.join(_ROOT, "src", "core", "interp.cpp"),
                  encoding="utf-8").read()
    a = src.index("bool Interp::exec(")
    b = src.index("default: break;", a)
    seg = src[a:b]
    done = set(int(x, 16) for x in re.findall(r"case\s+0x([0-9A-Fa-f]+)\s*:", seg))
    done |= set(int(x, 16) for x in re.findall(r"verb\s*==\s*0x([0-9A-Fa-f]+)", seg))
    return done


def programs():
    """{имя: множество глаголов} по всем разбираемым файлам корпуса."""
    out = {}
    files = (sorted(glob.glob(os.path.join(_ROOT, "corpus", "hex", "*_bin.txt"))) +
             sorted(glob.glob(os.path.join(_ROOT, "corpus", "bin", "*"))))
    for path in files:
        name = os.path.basename(path).replace("_bin.txt", "")
        if name in SKIP:
            continue
        try:
            data = (iskra.load_hexdump(path) if path.endswith(".txt")
                    else open(path, "rb").read())
            if len(data) < 512 or data[0] != 1 or (data[9] & 1) == 0:
                continue                      # не оттранслированная программа
            stream, _ = iskra.build_stream(data)
            l1 = stream[0] << 8 | stream[1]
            l2 = stream[2] << 8 | stream[3]
            l3 = stream[4] << 8 | stream[5]
            recs = iskra.split_records(stream, 6 + l1 + l2 + l3)
        except Exception:
            continue
        if any(body == b"DESYNC" for _, _, body in recs):
            continue                          # разбор развалился — не считаем
        verbs = collections.Counter()
        for _, _, body in recs:
            for verb, _ops in iskra.split_statements(body):
                verbs[verb] += 1
        out[name] = verbs
    return out


def tag(v):
    return ("06 %02X" % (v & 0xFF)) if v > 0xFF else ("%02X" % v)


# Разобрано в docs/format.md, но в таблицы tools/detok.py ещё не попало.
EXTRA = {0x1E: "IF END THEN"}

# Машинозависимые: их здесь не будет вовсе (docs/DECISIONS.md, разд. 1),
# и в порядке по отдаче им делать нечего: проба обещала бы то, чего не
# случится. Счёт операторов выше их по-прежнему учитывает.
NEVER = {0x0625, 0x40}


def name(v):
    if v in EXTRA:
        return EXTRA[v]
    table = detok.VERBS2 if v > 0xFF else detok.VERBS
    return table.get(v & 0xFF if v > 0xFF else v, "?")


def main():
    done = implemented()
    progs = programs()

    total = sum(sum(c.values()) for c in progs.values())
    missing = collections.Counter()
    files_hit = collections.Counter()
    for verbs in progs.values():
        for v, n in verbs.items():
            if v in done:
                continue
            missing[v] += n
            files_hit[v] += 1

    left = sum(missing.values())
    print("файлов учтено: %d" % len(progs))
    print("исполняется глаголов: %d, встречается в корпусе: %d"
          % (len(done), len(set().union(*[set(c) for c in progs.values()]))))
    print("не исполняется: %d глаголов, %d операторов из %d (%.1f%%)"
          % (len(missing), left, total, 100.0 * left / total if total else 0))
    print()

    print("мешают больше всего (файлов / операторов):")
    for v, n in files_hit.most_common(15):
        print("  %3d  %5d  %-6s %s" % (n, missing[v], tag(v), name(v)))
    print()

    # Жадный порядок: на каждом шаге берём глагол, который доводит до конца
    # больше всего программ. Останавливается, когда одиночные добавления
    # перестают что-либо открывать.
    rest = dict((k, set(c) - done) for k, c in progs.items())
    doomed = sum(1 for s in rest.values() if s & NEVER)
    print("порядок по отдаче (глагол -> программ исполняется целиком):")
    print("  %d из %d — сейчас" % (sum(1 for s in rest.values() if not s), len(rest)))
    for step in range(1, 40):
        cand = collections.Counter()
        for s in rest.values():
            if len(s) == 1 and not (s & NEVER):
                cand[next(iter(s))] += 1
        if not cand:
            break
        v = cand.most_common(1)[0][0]
        for s in rest.values():
            s.discard(v)
        whole = sum(1 for s in rest.values() if not s)
        print("  %2d. %-6s %-22s %d из %d" % (step, tag(v), name(v), whole, len(rest)))
    print("  дальше каждой оставшейся программе не хватает минимум двух глаголов")
    print("  потолок: %d из %d — остальным нужны ASMB либо $GIO,"
          % (len(rest) - doomed, len(rest)))
    print("  а их здесь не будет вовсе")


if __name__ == "__main__":
    main()
