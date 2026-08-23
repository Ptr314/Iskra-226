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
print('N=%d (T2=%d, T3=%d)' % (N,n2,n3))
for k,(ad,fl,b) in enumerate(recs):
    idx=N-1-k
    typ = 'STR' if (fl & 0x20) else ('NUM' if (fl&0x10) else '???')
    nxt = recs[k+1][0] if k+1<len(recs) else None
    d = (nxt-ad) if (nxt is not None and nxt>ad) else ''
    print('  rec%3d -> V%02X  ad=%04X flag=%02X(%s) b=%02X  delta=%s' % (k,idx,ad,fl,typ,b,d))
