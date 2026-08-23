# -*- coding: utf-8 -*-
import sys, os, glob, collections
sys.stdout.reconfigure(encoding='utf-8')
from detok import *
c=collections.Counter(); ex=collections.defaultdict(list)
for f in sorted(glob.glob(os.path.join(DOCS,'*_bin.txt'))):
    nm=os.path.basename(f)[:-8]
    try: P=prog(nm)
    except Exception: continue
    if (P['hdr']['attr']&1)==0: continue
    T=text_lines(nm) or {}
    for off,ln,body in split_records(P['stream'],P['start']):
        if ln is None: continue
        for v,ops in split_statements(body):
            if v==0x54 and ops:
                c[ops[0]]+=1
                if len(ex[ops[0]])<4: ex[ops[0]].append((nm,ln,hx(ops),T.get(ln)))
for k in sorted(c):
    print('class %02X : %d' % (k,c[k]))
    for e in ex[k]:
        print('    %s %s  %s' % (e[0],e[1],e[2]))
        if e[3]: print('       TXT: %s' % e[3][:120])
