# -*- coding: utf-8 -*-
import sys
sys.stdout.reconfigure(encoding='utf-8')
from nb import *
P=progn('S_BAM03'); st=P['stream']; s=P['start']
print('S_BAM03 start=%d  bytes: %s' % (s, hx(st[s:s+40])))
print('  at +4: line=%d len=%d  byte at end=%02X' % (fromBCD(st[s+4])*100+fromBCD(st[s+5]), st[s+6], st[s+6+2+st[s+6]-2]))
e=s+4+2+st[s+6]
print('  expected FE at %d -> %02X ; around: %s' % (e, st[e], hx(st[e-6:e+6])))
print('  sector chunk pos of e: %d (254*%d)' % (e%254, e//254))
