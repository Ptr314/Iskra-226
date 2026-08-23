# -*- coding: utf-8 -*-
import sys
sys.stdout.reconfigure(encoding='utf-8')
from iskra import *
nm=sys.argv[1]; want=set(int(x) for x in sys.argv[2].split(','))
P=prog(nm); T=text_lines(nm) or {}
for off,ln,body in split_records(P['stream'],P['start']):
    if ln in want:
        print('%d: %s' % (ln, T.get(ln,'<none>')))
        for v,ops in split_statements(body):
            print('    %-20s %s' % (verb_name(v), hx(ops)))
