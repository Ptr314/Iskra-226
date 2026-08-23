# -*- coding: utf-8 -*-
import sys, os, glob, re, collections
sys.stdout.reconfigure(encoding='utf-8')
from detok import *
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
            if v in (0x3F,0x56,0x54,0x82,0x1E,0x0600): continue
            out=render(ctx,v,ops)
            for m in set(re.findall(r'\?([0-9A-F]{2})\?',out)):
                cnt[m]+=1
                if len(ex[m])<5: ex[m].append((nm,ln,verb_name(v),hx(ops),out,T.get(ln)))
for k in sorted(cnt, key=lambda x:-cnt[x]):
    print('=== %s : %d ===' % (k,cnt[k]))
    for e in ex[k]:
        print('  %s %s %s | %s' % (e[0],e[1],e[2],e[3]))
        print('     -> %s' % e[4])
        if e[5]: print('     TXT %s' % e[5][:160])
