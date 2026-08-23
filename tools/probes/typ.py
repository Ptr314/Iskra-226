# -*- coding: utf-8 -*-
import sys, os, glob, collections
sys.stdout.reconfigure(encoding='utf-8')
from detok import *
c=collections.Counter()
for f in sorted(glob.glob(os.path.join(DOCS,'*_bin.txt'))):
    nm=os.path.basename(f)[:-8]
    try: P=prog(nm)
    except Exception: continue
    if (P['hdr']['attr']&1)==0: continue
    st=P['stream']; L1,L2,L3=P['L1'],P['L2'],P['L3']
    n2=L2//4; n3=L3//4; N=n2+n3
    recs=[]
    for i in range(n2):
        o=6+L1+i*4; recs.append((st[o]|st[o+1]<<8, st[o+2], st[o+3]))
    for i in range(n3):
        o=6+L1+L2+i*4; recs.append((st[o]|st[o+1]<<8, st[o+2], st[o+3]))
    T1=table1_recs(P); k=0
    for ad,fl,b in recs:
        if fl&1:
            if k>=len(T1): break
            r=T1[k]; k+=1
            t='STR' if fl&0x20 else ('REAL' if fl&0x10 else 'INT')
            hi=r[1]>>8
            c[(t, '%04X'%r[1] if hi in (0x08,) else ('hi=%02X'%hi), r[3])]+=1
for k in sorted(c, key=lambda x:(-c[x])):
    print('%-6s typ=%-8s sz=%-5d : %d' % (k[0],k[1],k[2],c[k]))
