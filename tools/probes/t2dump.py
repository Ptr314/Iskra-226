# -*- coding: utf-8 -*-
import sys
sys.stdout.reconfigure(encoding='utf-8')
from detok import *
nm=sys.argv[1]
P=prog(nm); st=P['stream']; L1,L2,L3=P['L1'],P['L2'],P['L3']
names=eval(sys.argv[2]) if len(sys.argv)>2 else {}
base3=6+L1+L2
base2=6+L1
n2=L2//4; n3=L3//4
print('TABLE 3 (%d recs) then TABLE 2 (%d recs)' % (n3,n2))
recs=[]
for i in range(n3):
    o=base3+i*4; recs.append(('T3',i,st[o]|st[o+1]<<8, st[o+2], st[o+3]))
for i in range(n2):
    o=base2+i*4; recs.append(('T2',i,st[o]|st[o+1]<<8, st[o+2], st[o+3]))
for k,(tb,i,ad,fl,b) in enumerate(recs):
    print(' idx %02X  %s[%2d]  ad=%04X flag=%02X b=%02X   %s' % (k,tb,i,ad,fl,b,names.get(k,'')))
