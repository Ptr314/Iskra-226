# -*- coding: utf-8 -*-
import sys, os, glob, collections
sys.stdout.reconfigure(encoding='utf-8')
from nb import *
from detok import *
nxt=collections.Counter(); ex=[]
def scan(P, ctx, tag):
    for off,ln,body in split_records(P['stream'],P['start']):
        if ln is None: continue
        for v,o in split_statements(body):
            if v in (0x3F,0x56,0x54,0x82,0x1E,0x43,0x61,0x62,0x63): continue
            if v==0x29: o=o[:-2]
            render(ctx,v,o)
            for (t,exp,pp) in LAST_TOKS:
                if t==0xF0 and exp:
                    b = o[pp] if pp < len(o) else None
                    nxt['end' if b is None else ('var' if b<=0xC9 else '%02X'%b)]+=1
                    if len(ex)<8: ex.append((tag,ln,hx(o)[:90]))
for nm in sorted(os.listdir(NB)):
    P=progn(nm); scan(P, build_ctx_n(P), nm)
for f in sorted(glob.glob(os.path.join(DOCS,'*_bin.txt'))):
    nm=os.path.basename(f)[:-8]
    P=prog(nm)
    if (P['hdr']['attr']&1)==0: continue
    scan(P, build_ctx(P), nm)
print('token right after F0:', dict(nxt))
for e in ex: print('  %-10s %-5s %s' % e)
