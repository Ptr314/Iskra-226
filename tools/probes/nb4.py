# -*- coding: utf-8 -*-
import sys, os
sys.stdout.reconfigure(encoding='utf-8')
from nb import *
for nm in sorted(os.listdir(NB)):
    P=progn(nm); st=P['stream']
    off = 4 if P['hdr']['attr']==0x25 else 0
    recs=[r for r in split_records(st,P['start']+off) if r[1] is not None]
    if not recs: continue
    lines=[r[1] for r in recs]
    a=fromBCD(st[P['start']])*100+fromBCD(st[P['start']+1])
    b=fromBCD(st[P['start']+2])*100+fromBCD(st[P['start']+3])
    print('%-12s attr=%02X  extra=%-4s %-4s | first=%-5d last=%-5d  n=%-4d  has1019=%s has%s=%s' %
          (nm,P['hdr']['attr'],a if off else '-',b if off else '-',lines[0],lines[-1],len(lines),
           (1019 in lines) if off else '-', b if off else '-', (b in lines) if off else '-'))
