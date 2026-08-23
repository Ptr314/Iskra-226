# -*- coding: utf-8 -*-
import sys, os, glob, re, collections
sys.stdout.reconfigure(encoding='utf-8')
from detok import *
targets={0xF0,0xF9,0xFA,0xFB,0xFC,0xFD,0xFF,0xE4,0xDA}
seen=collections.defaultdict(list)
for f in sorted(glob.glob(os.path.join(DOCS,'*_bin.txt'))):
    nm=os.path.basename(f)[:-8]
    try: P=prog(nm)
    except Exception: continue
    if (P['hdr']['attr']&1)==0: continue
    ctx=build_ctx(P)
    for off,ln,body in split_records(P['stream'],P['start']):
        if ln is None: continue
        for v,ops in split_statements(body):
            if v in (0x3F,0x56,0x54,0x82,0x1E,0x0600,0x3A): continue
            if v==0x29: ops=ops[:-2]   # DATA chain pointer
            render(ctx,v,ops)
            for (t,exp,pp) in LAST_TOKS:
                if t in targets and exp:
                    seen[t].append((nm,ln,verb_name(v),hx(ops[max(0,pp-4):pp+8]),hx(ops)))
for t in sorted(seen):
    print('=== %02X : %d occurrences' % (t,len(seen[t])))
    for e in seen[t][:10]:
        print('   %-10s %-6s %-14s ...%s' % (e[0],e[1],e[2],e[3]))
