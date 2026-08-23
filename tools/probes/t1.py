# -*- coding: utf-8 -*-
import sys
sys.stdout.reconfigure(encoding='utf-8')
from iskra import *
P = prog('STAT04')
print(P['hdr'], P['L1'], P['L2'], P['L3'], P['start'], len(P['stream']))
T = text_lines('STAT04')
recs = split_records(P['stream'], P['start'])
for off, ln, body in recs:
    print('---', ln, hx(body))
    if T and ln in T: print('   TXT:', T[ln])
    for v, ops in split_statements(body):
        print('   ', verb_name(v), '|', hx(ops))
