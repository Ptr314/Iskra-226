# -*- coding: utf-8 -*-
import sys, os, glob
sys.stdout.reconfigure(encoding='utf-8')
from nb import *
from detok import prog as oprog, DOCS

def valid_first(st, s):
    if s+3 > len(st): return False
    for b in (st[s], st[s+1]):
        if (b>>4)>9 or (b&15)>9: return False       # BCD digits
    ln = st[s+2]
    if ln < 1: return False
    e = s+2+ln
    if e >= len(st): return False
    return st[e] == 0xFE

bad=[]
print('--- new files ---')
for nm in sorted(os.listdir(NB)):
    P=progn(nm); st=P['stream']; s=P['start']
    v0=valid_first(st,s); v4=valid_first(st,s+4)
    exp = 4 if P['hdr']['attr']==0x25 and hx(st[s:s+2])=='0A 19' else 0
    ok = (not v0 and v4) if exp==4 else v0
    if not ok: bad.append(nm)
    print('%-12s v0=%-5s v4=%-5s expected=+%d %s' % (nm,v0,v4,exp,'' if ok else '  <<< MISMATCH'))
print('--- old files ---')
for f in sorted(glob.glob(os.path.join(DOCS,'*_bin.txt'))):
    nm=os.path.basename(f)[:-8]
    P=oprog(nm)
    if (P['hdr']['attr']&1)==0: continue
    st=P['stream']; s=P['start']
    if not valid_first(st,s): print('%-12s v0=False  <<< MISMATCH' % nm); bad.append(nm)
print('mismatches:', bad)
