# -*- coding: utf-8 -*-
import sys
sys.stdout.reconfigure(encoding='utf-8')
from detok import *
nm=sys.argv[1]; want=set(int(x) for x in sys.argv[2].split(','))
P=prog(nm)
for off,ln,body in split_records(P['stream'],P['start']):
    if ln in want:
        print('%d RAW: %s' % (ln, hx(body)))
