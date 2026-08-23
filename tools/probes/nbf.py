# -*- coding: utf-8 -*-
import sys, os, collections
sys.stdout.reconfigure(encoding='utf-8')
from nb import *
from detok import *
targets={0xF0,0xF5,0xF9,0xE4,0xDA,0xCE,0xFF}
found=collections.defaultdict(list)
for nm in sorted(os.listdir(NB)):
    P=progn(nm); ctx=build_ctx_n(P)
    for off,ln,body in split_records(P['stream'],P['start']):
        if ln is None: continue
        for v,o in split_statements(body):
            if v in (0x3F,0x56,0x54,0x82,0x1E): continue
            if v==0x29: o=o[:-2]
            txt=render(ctx,v,o)
            for (t,exp,pp) in LAST_TOKS:
                if t in targets and exp and len(found[t])<6:
                    found[t].append((nm,ln,verb_name(v),hx(o)[:110],txt[:130]))
for t in sorted(found):
    print('===== %02X =====' % t)
    for e in found[t]:
        print('  %-10s %-5s %-12s %s' % (e[0],e[1],e[2],e[3]))
        print('        -> %s' % e[4])
