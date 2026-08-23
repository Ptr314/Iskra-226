# -*- coding: utf-8 -*-
import sys, os
sys.stdout.reconfigure(encoding='utf-8')
from nb import *
for nm in ['BAM1','BAM10','S_BAM01','BAM00','LBAM']:
    P=progn(nm); st=P['stream']; L1,L2,L3=P['L1'],P['L2'],P['L3']; s=P['start']
    print('=== %s  n2=%d n3=%d' % (nm,L2//4,L3//4))
    print('  last 5 records of the last table:')
    base = 6+L1+L2 if L3 else 6+L1
    cnt  = L3//4 if L3 else L2//4
    for i in range(max(0,cnt-5),cnt):
        o=base+i*4
        print('    [%3d] %s   addr=%04X flag=%02X b=%02X' % (i,hx(st[o:o+4]),st[o]|st[o+1]<<8,st[o+2],st[o+3]))
    print('  EXTRA : %s' % hx(st[s:s+4]))
    print('  program: %s' % hx(st[s+4:s+20]))
