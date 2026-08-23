# -*- coding: utf-8 -*-
"""Конвертирует hex-дампы corpus/hex/*_bin.txt в двоичные файлы build/corpus/."""
import os, sys, glob
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from iskra import load_hexdump, DOCS, WORK

os.makedirs(WORK, exist_ok=True)
n = 0
for f in glob.glob(os.path.join(DOCS, '*_bin.txt')):
    nm = os.path.basename(f)[:-8]
    open(os.path.join(WORK, nm + '.bin'), 'wb').write(load_hexdump(f))
    n += 1
print('%d файлов записано в %s' % (n, WORK))
