# -*- coding: utf-8 -*-
import sys, os, glob, collections
sys.stdout.reconfigure(encoding='utf-8')
from detok import *
c=collections.Counter(); ex=collections.defaultdict(list)
for f in sorted(glob.glob(os.path.join(DOCS,'*_bin.txt'))):
    nm=os.path.basename(f)[:-8]
    try: P=prog(nm)
    except Exception: continue
    if (P['hdr']['attr']&1)==0: continue
    for r in table1_recs(P):
        ad,typ,n,sz=r
        hi=typ>>8
        if hi==0x08: continue
        c[(typ,n)]+=1
        if len(ex[typ])<3: ex[typ].append((nm,ad,n,sz))
tot=sum(c.values())
print('records with typ high byte != 08 :',tot)
byn=collections.Counter()
for (typ,n),v in c.items(): byn[n]+=v
print('by first dimension n:', byn.most_common(12))
print()
print('typ values seen with n==10:')
for (typ,n),v in sorted(c.items()):
    if n==10: print('  typ=%04X x%d  ex=%s' % (typ,v,ex[typ][:2]))
