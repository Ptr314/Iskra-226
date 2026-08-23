# -*- coding: utf-8 -*-
import sys, os
sys.stdout.reconfigure(encoding='utf-8')
from nb import *

def try_parse(st, start):
    """returns (#records, bytes consumed) parsing from start"""
    recs = split_records(st, start)
    n = sum(1 for r in recs if r[1] is not None)
    bad = sum(1 for r in recs if r[1] is None)
    end = recs[-1][0] if recs else start
    return n, bad, end

names = sorted(os.listdir(NB))
print('%-12s %-5s %6s %6s %6s | best offset  recs' % ('file','attr','L1','L2','L3'))
for nm in names:
    try:
        P = progn(nm)
    except Exception as e:
        print('%-12s ERR %s' % (nm, e)); continue
    st = P['stream']; s0 = P['start']
    best = None
    for d in range(0, 17):
        n, bad, end = try_parse(st, s0 + d)
        if best is None or (n, -bad) > (best[1], -best[2]):
            best = (d, n, bad)
    print('%-12s %02X   %6d %6d %6d | +%-2d  recs=%-5d desync=%d' %
          (nm, P['hdr']['attr'], P['L1'], P['L2'], P['L3'], best[0], best[1], best[2]))
