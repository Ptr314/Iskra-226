# -*- coding: utf-8 -*-
import sys
sys.stdout.reconfigure(encoding='utf-8')
from detok import *
nm=sys.argv[1]
P=prog(nm); T=text_lines(nm) or {}
d,T1,D=describe(P)
print('L1 recs=%d  DIM order=%s' % (len(T1), [hex(x) for x in D]))
for i,r in enumerate(T1):
    print('  rec %2d: ad=%04X typ=%04X n=%-5d sz=%-5d' % (i,r[0],r[1],r[2],r[3]))
for idx in D:
    if idx in d:
        e=d[idx]
        print('  V%02X -> rec%2d ad=%04X typ=%04X n=%d sz=%d delta=%s' % (idx,e['rec'],e['ad'],e['typ'],e['n'],e['sz'],e['delta']))
for ln in sorted(T):
    if 'DIM' in T[ln] or 'COM' in T[ln]: print('  TXT %d: %s' % (ln,T[ln]))
