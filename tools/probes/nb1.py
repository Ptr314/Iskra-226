# -*- coding: utf-8 -*-
import sys
sys.stdout.reconfigure(encoding='utf-8')
from nb import *
for nm in ['S_BAM01','BAM','BAM00','LBAM','BAM1','L0']:
    d=load(nm)
    print('=== %s  size=%d  hdr sig=%02X name=%r attr=%02X' % (nm,len(d),d[0],kstr(d[1:9]),d[9]))
    print('   sector markers:', ' '.join('%02X%02X'%(d[o],d[o+1]) for o in range(0x100,min(len(d),0x100+256*8),256)))
    P=progn(nm)
    print('   L1=%d L2=%d L3=%d start=%d streamlen=%d' % (P['L1'],P['L2'],P['L3'],P['start'],len(P['stream'])))
    st=P['stream']
    print('   prologue+start:', hx(st[0:6]))
    s=P['start']
    print('   at start:', hx(st[s:s+48]))
