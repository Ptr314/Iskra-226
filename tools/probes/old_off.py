# -*- coding: utf-8 -*-
import sys, os, glob
sys.stdout.reconfigure(encoding='utf-8')
from detok import *
for f in sorted(glob.glob(os.path.join(DOCS,'*_bin.txt'))):
    nm=os.path.basename(f)[:-8]
    P=prog(nm)
    if (P['hdr']['attr']&1)==0: continue
    st=P['stream']; s0=P['start']
    best=None
    for d in range(0,17):
        recs=split_records(st,s0+d)
        n=sum(1 for r in recs if r[1] is not None)
        bad=sum(1 for r in recs if r[1] is None)
        if best is None or (n,-bad)>(best[1],-best[2]): best=(d,n,bad)
    mark = '' if best[0]==0 else '   <<<< +%d' % best[0]
    print('%-11s attr=%02X  best=+%-2d recs=%-5d %s | first4=%s' % (nm,P['hdr']['attr'],best[0],best[1],mark,hx(st[s0:s0+4])))
