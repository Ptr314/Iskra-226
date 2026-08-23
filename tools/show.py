# -*- coding: utf-8 -*-
import sys
sys.stdout.reconfigure(encoding='utf-8')
from detok import *
nm=sys.argv[1]
only=set(int(x) for x in sys.argv[2].split(',')) if len(sys.argv)>2 else None
for ln,txt,orig in listing(nm,only):
    print('%5d %s' % (ln,txt))
    if orig is not None: print('      TXT: %s' % orig)
