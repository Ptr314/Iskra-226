# -*- coding: utf-8 -*-
import sys, os, glob, collections
sys.stdout.reconfigure(encoding='utf-8')
from nb import *
from detok import *
stat=collections.Counter(); ex=collections.defaultdict(list)
def scan(P,ctx,tag):
    for off,ln,body in split_records(P['stream'],P['start']):
        if ln is None: continue
        for v,o in split_statements(body):
            if v in (0x3F,0x56,0x54,0x82,0x1E): continue
            if v==0x29: o=o[:-2]
            render(ctx,v,o)
            for (t,exp,pp) in LAST_TOKS:
                if t in (0xE3,0xE4) and pp<len(o):
                    L=o[pp]; txt=bytes(o[pp+1:pp+1+L])
                    q = 0x22 in txt
                    stat[(t,q)]+=1
                    if t==0xE4 and len(ex[q])<6: ex[q].append((tag,ln,kstr(txt)[:70]))
for nm in sorted(os.listdir(NB)):
    P=progn(nm); scan(P,build_ctx_n(P),nm)
for f in sorted(glob.glob(os.path.join(DOCS,'*_bin.txt'))):
    nm=os.path.basename(f)[:-8]
    P=prog(nm)
    if (P['hdr']['attr']&1)==0: continue
    scan(P,build_ctx(P),nm)
print('E3 without quote inside:',stat[(0xE3,False)],'  E3 WITH quote inside:',stat[(0xE3,True)])
print('E4 without quote inside:',stat[(0xE4,False)],'  E4 WITH quote inside:',stat[(0xE4,True)])
for q in (True,False):
    print('--- E4 %s quote:' % ('with' if q else 'without'))
    for e in ex[q]: print('   %-10s %-6s %s' % e)
