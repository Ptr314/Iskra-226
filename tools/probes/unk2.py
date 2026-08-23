# -*- coding: utf-8 -*-
import sys, os, glob, re, collections
sys.stdout.reconfigure(encoding='utf-8')
from detok import *
targets=set(['F0','F4','F9','FA','FB','FC','FD','FF','E4','DA','D4','D9','CE','CF','C0'])
cnt=collections.Counter(); ex=collections.defaultdict(list)
for f in sorted(glob.glob(os.path.join(DOCS,'*_bin.txt'))):
    nm=os.path.basename(f)[:-8]
    try: P=prog(nm)
    except Exception: continue
    if (P['hdr']['attr']&1)==0: continue
    ctx=build_ctx(P); T=text_lines(nm) or {}
    for off,ln,body in split_records(P['stream'],P['start']):
        if ln is None: continue
        for v,ops in split_statements(body):
            if v in (0x3F,0x56,0x54,0x82,0x1E,0x0600,0x3A,0x2A,0x2D,0x80): continue
            out=render(ctx,v,ops)
            u=re.findall(r'\?([0-9A-F]{2})\?',out)
            if len(u)!=1: continue
            k=u[0]
            if k not in targets: continue
            cnt[k]+=1
            if len(ex[k])<8: ex[k].append((nm,ln,verb_name(v),hx(ops),out,T.get(ln)))
for k in sorted(cnt, key=lambda x:-cnt[x]):
    print('=== %s : %d ===' % (k,cnt[k]))
    for e in ex[k]:
        print('  %s %s %s | %s' % (e[0],e[1],e[2],e[3]))
        print('     -> %s' % e[4])
        if e[5]: print('     TXT %s' % e[5][:150])
