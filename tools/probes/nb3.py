# -*- coding: utf-8 -*-
import sys, os
sys.stdout.reconfigure(encoding='utf-8')
from nb import *
print('%-12s %-4s %5s %5s %5s | bit0  n1 | extra4 bytes' % ('file','attr','n1','n2','n3'))
for nm in sorted(os.listdir(NB)):
    P=progn(nm); st=P['stream']; L1,L2,L3=P['L1'],P['L2'],P['L3']
    n1=L1//8; n2=L2//4; n3=L3//4
    bit0=0
    for i in range(n2):
        o=6+L1+i*4
        if o+3 < len(st) and st[o+2]&1: bit0+=1
    for i in range(n3):
        o=6+L1+L2+i*4
        if o+3 < len(st) and st[o+2]&1: bit0+=1
    s=P['start']
    print('%-12s %02X   %5d %5d %5d | %4d %3d %s | %s' % (nm,P['hdr']['attr'],n1,n2,n3,bit0,n1,'OK' if bit0==n1 else '**',hx(st[s:s+4])))
