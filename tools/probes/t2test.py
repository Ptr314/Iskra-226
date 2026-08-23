# -*- coding: utf-8 -*-
import sys, os, glob
sys.stdout.reconfigure(encoding='utf-8')
from detok import *
print('%-11s %5s %5s %5s | %5s %5s | %5s %5s %s' % ('file','L1/8','L2/4','L3/4','maxix','#vars','n2+n3','diff','ok'))
for f in sorted(glob.glob(os.path.join(DOCS,'*_bin.txt'))):
    nm=os.path.basename(f)[:-8]
    try: P=prog(nm)
    except Exception: continue
    if (P['hdr']['attr']&1)==0: continue
    ctx=build_ctx(P)
    mx=-1; used=set()
    for off,ln,body in split_records(P['stream'],P['start']):
        if ln is None: continue
        for v,ops in split_statements(body):
            if v in (0x3F,0x56,0x54,0x82,0x1E): continue
            if v==0x29: ops=ops[:-2]
            render(ctx,v,ops)
            for (t,exp,pp) in LAST_TOKS:
                if t<=VARMAX and exp: used.add(t); mx=max(mx,t)
                if t==0xE0 and exp and pp<len(ops) and ops[pp]<=VARMAX:
                    used.add(ops[pp]); mx=max(mx,ops[pp])
    n2=P['L2']//4; n3=P['L3']//4
    print('%-11s %5d %5d %5d | %5d %5d | %5d %5d %s' % (nm,P['L1']//8,n2,n3,mx+1,len(used),n2+n3,(mx+1)-(n2+n3),'OK' if mx+1==n2+n3 else ''))
