# -*- coding: utf-8 -*-
import sys, os, glob, re, collections
sys.stdout.reconfigure(encoding='utf-8')
from iskra import *
targets=[int(x,16) for x in sys.argv[1].split(',')]
maxn=int(sys.argv[2]) if len(sys.argv)>2 else 6
onlypairs = (len(sys.argv)<4)
files=[]
for f in sorted(glob.glob(os.path.join(DOCS,'*_bin.txt'))):
    nm=os.path.basename(f)[:-8]
    if onlypairs and not os.path.exists(os.path.join(DOCS,nm+'_text.txt')): continue
    files.append(nm)
found=collections.defaultdict(list)
for nm in files:
    try: P=prog(nm)
    except Exception: continue
    T=text_lines(nm) or {}
    for off,ln,body in split_records(P['stream'],P['start']):
        if ln is None: continue
        for v,ops in split_statements(body):
            toks=[t for _,t,_ in walk_tokens(v,ops)]
            for t in targets:
                if t in toks and len(found[t])<maxn:
                    found[t].append((nm,ln,T.get(ln,'<no text>'),verb_name(v),hx(ops)))
for t in targets:
    print('===== %02X =====' % t)
    for e in found[t]:
        print('%s %s: %s' % (e[0],e[1],e[2]))
        print('    %s | %s' % (e[3],e[4]))
