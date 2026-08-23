# -*- coding: utf-8 -*-
import sys
sys.stdout.reconfigure(encoding='utf-8')
from detok import *
nm=sys.argv[1]
P=prog(nm); st=P['stream']; L1,L2,L3=P['L1'],P['L2'],P['L3']
n2=L2//4; n3=L3//4; N=n2+n3
recs=[]
for i in range(n2):
    o=6+L1+i*4; recs.append((st[o]|st[o+1]<<8, st[o+2], st[o+3]))
for i in range(n3):
    o=6+L1+L2+i*4; recs.append((st[o]|st[o+1]<<8, st[o+2], st[o+3]))
T1=table1_recs(P)
k=0
print('idx  flag type   -> table1 rec (ad,typ,n,sz)')
for pos,(ad,fl,b) in enumerate(recs):
    idx=N-1-pos
    typ='STR' if fl&0x20 else ('REAL' if fl&0x10 else 'INT%')
    if fl&1:
        r=T1[k] if k<len(T1) else None; k+=1
        print(' V%02X %02X %-5s -> T1[%2d] ad=%04X typ=%04X n=%-6d sz=%d' % (idx,fl,typ,k-1,r[0],r[1],r[2],r[3]))
T=text_lines(nm)
if T:
    for ln in sorted(T):
        if 'DIM' in T[ln] or 'COM' in T[ln] or 'REDIM' in T[ln]: print('  TXT %d: %s' % (ln,T[ln]))
