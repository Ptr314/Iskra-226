# -*- coding: utf-8 -*-
import sys, os, collections
sys.stdout.reconfigure(encoding='utf-8')
from nb import *
from detok import *
want=[int(x,16) for x in sys.argv[1].split(',')]
per=int(sys.argv[2]) if len(sys.argv)>2 else 4
found=collections.defaultdict(list)
for nm in sorted(os.listdir(NB)):
    P=progn(nm); ctx=build_ctx_n(P)
    for off,ln,body in split_records(P['stream'],P['start']):
        if ln is None: continue
        sts=split_statements(body)
        for v,o in sts:
            if v in want and len(found[v])<per:
                whole=':'.join((verb_name(vv)+' '+render(ctx,vv,oo)).strip() for vv,oo in sts)
                found[v].append((nm,ln,whole[:240],hx(o)[:150]))
for v in want:
    print('===== %04X (%d) =====' % (v,len(found[v])))
    for e in found[v]:
        print('  %s %s: %s' % (e[0],e[1],e[2]))
        print('     ops: %s' % e[3])
