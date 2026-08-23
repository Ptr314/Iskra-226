# -*- coding: utf-8 -*-
import sys, os, collections
sys.stdout.reconfigure(encoding='utf-8')
from nb import *
from detok import *
vs=collections.Counter(); vf=collections.defaultdict(set)
ops=collections.Counter(); of=collections.defaultdict(set)
bad=0
for nm in sorted(os.listdir(NB)):
    P=progn(nm); ctx=Ctx()
    for off,ln,body in split_records(P['stream'],P['start']):
        if ln is None: bad+=1; continue
        for v,o in split_statements(body):
            vs[v]+=1; vf[v].add(nm)
            if v in (0x3F,0x56,0x54,0x82,0x1E): continue
            if v==0x29: o=o[:-2]
            render(ctx,v,o)
            for (t,exp,pp) in LAST_TOKS:
                if t>0xC9: ops[t]+=1; of[t].add(nm)
print('desync records:',bad)
print('=== VERBS not known ===')
for v in sorted(vs):
    if verb_name(v).startswith('?'):
        print('  %04X x%-4d %s' % (v,vs[v],','.join(sorted(vf[v])[:5])))
print('=== all verbs present ===')
print('  ', ' '.join('%04X'%v for v in sorted(vs)))
print('=== operand tokens ===')
for t in sorted(ops):
    print('  %02X x%-5d %s' % (t,ops[t],','.join(sorted(of[t])[:4])))
