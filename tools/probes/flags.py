# -*- coding: utf-8 -*-
import sys, os, glob, collections
sys.stdout.reconfigure(encoding='utf-8')
from detok import *
c=collections.Counter(); zero_by_type=collections.Counter(); per=collections.defaultdict(collections.Counter)
for f in sorted(glob.glob(os.path.join(DOCS,'*_bin.txt'))):
    nm=os.path.basename(f)[:-8]
    try: P=prog(nm)
    except Exception: continue
    if (P['hdr']['attr']&1)==0: continue
    st=P['stream']; L1,L2,L3=P['L1'],P['L2'],P['L3']
    n1=L1//8
    recs=[]
    for i in range(L2//4):
        o=6+L1+i*4; recs.append((st[o]|st[o+1]<<8, st[o+2], st[o+3]))
    for i in range(L3//4):
        o=6+L1+L2+i*4; recs.append((st[o]|st[o+1]<<8, st[o+2], st[o+3]))
    n_t1flag=sum(1 for a,fl,b in recs if fl&1)
    n_int=sum(1 for a,fl,b in recs if (fl&0x30)==0)
    n_int_zero=sum(1 for a,fl,b in recs if (fl&0x30)==0 and a==0)
    print('%-11s recs=%-4d t1recs=%-3d flag_bit0=%-3d int=%-3d int_ad0=%-3d' % (nm,len(recs),n1,n_t1flag,n_int,n_int_zero))
    for a,fl,b in recs: c[fl]+=1; per[nm][fl]+=1
print()
print('flag histogram:', ' '.join('%02X:%d'%(k,c[k]) for k in sorted(c)))
