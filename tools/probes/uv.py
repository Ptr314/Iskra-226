# -*- coding: utf-8 -*-
import sys, os, glob, collections
sys.stdout.reconfigure(encoding='utf-8')
from detok import *
want=[int(x,16) for x in sys.argv[1].split(',')]
per=int(sys.argv[2]) if len(sys.argv)>2 else 4
found=collections.defaultdict(list)
for f in sorted(glob.glob(os.path.join(DOCS,'*_bin.txt'))):
    nm=os.path.basename(f)[:-8]
    try: P=prog(nm)
    except Exception: continue
    if (P['hdr']['attr']&1)==0: continue
    ctx=build_ctx(P); T=text_lines(nm) or {}
    for off,ln,body in split_records(P['stream'],P['start']):
        if ln is None: continue
        sts=split_statements(body)
        for i,(v,ops) in enumerate(sts):
            if v in want and len(found[v])<per:
                whole=':'.join((verb_name(vv)+' '+render(ctx,vv,oo)).strip() for vv,oo in sts)
                found[v].append((nm,ln,whole,hx(ops),T.get(ln)))
for v in want:
    print('===== %04X (%d shown) =====' % (v,len(found[v])))
    for e in found[v]:
        print('%s %s: %s' % (e[0],e[1],e[2]))
        print('     ops: %s' % e[3])
        if e[4]: print('     TXT: %s' % e[4])
