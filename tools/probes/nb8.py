# -*- coding: utf-8 -*-
import sys, os
sys.stdout.reconfigure(encoding='utf-8')
from nb import *
print('%-10s %-9s %s' % ('file','extra','first program record'))
for nm in sorted(os.listdir(NB)):
    P=progn(nm); st=P['stream']; s=P['start']
    if hx(st[s:s+2])!='0A 19': continue
    print('%-10s %-9s %s  -> line %d, len %d' % (nm, hx(st[s+2:s+4]), hx(st[s+4:s+11]),
          fromBCD(st[s+4])*100+fromBCD(st[s+5]), st[s+6]))
