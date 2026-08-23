# -*- coding: utf-8 -*-
import sys, os, glob, re, collections
sys.stdout.reconfigure(encoding='utf-8')
from iskra import *
targets = [int(x,16) for x in sys.argv[1].split(',')]
maxn = int(sys.argv[2]) if len(sys.argv)>2 else 6
PAIRS=[]
for f in sorted(glob.glob(os.path.join(DOCS,'*_text.txt'))):
    nm=os.path.basename(f)[:-9]
    if os.path.exists(os.path.join(DOCS,nm+'_bin.txt')): PAIRS.append(nm)
found=collections.defaultdict(list)
for nm in PAIRS:
    P=prog(nm); T=text_lines(nm)
    if not T: continue
    for off,ln,body in split_records(P['stream'],P['start']):
        if ln is None or ln not in T: continue
        sts=split_statements(body)
        for v,ops in sts:
            if v in (0x3f,0x56): continue
            for t in targets:
                if t in ops and len(found[t])<maxn:
                    found[t].append((nm,ln,T[ln],verb_name(v),hx(ops)))
for t in targets:
    print('===== %02X =====' % t)
    for e in found[t]:
        print('%s %d: %s' % (e[0],e[1],e[2]))
        print('    %s | %s' % (e[3],e[4]))
