# -*- coding: utf-8 -*-
import sys, os, glob, collections
sys.stdout.reconfigure(encoding='utf-8')
from iskra import *

# skeleton walker: consumes operand stream, tracks token counts and per-verb usage
def walk(ops, verb, cnt, byverb, ctx, examples):
    p=0
    n=len(ops)
    while p<n:
        t=ops[p]; p+=1
        if t < 0xC0:
            cnt['VAR']+=1
            continue
        cnt[t]+=1
        byverb[t][verb]+=1
        if t==0xDE: p+=1
        elif t==0xE0: p+=1
        elif t==0xD3: p+=2
        elif t==0xE2:
            if p<n: L=ops[p]; p+=1+L
        elif t==0xE3:
            if p<n: L=ops[p]; p+=1+L
        elif t==0xE7: p+=2
        elif t==0xE8: p+=1
        elif t in (0xE5,0xE6):
            if p<n:
                d=ops[p]; p+=1
                nd=d&0x0F
                p += (nd+1)//2
                if t==0xE6: p+=1
        elif t in (0xCC,0xCD):
            p = n  # rest is BCD line list
        if len(examples[t])<4 and t in (0xCB,0xCE,0xCF,0xDA,0xE4,0xF0,0xF1,0xF4,0xF5,0xF9,0xFA,0xFB,0xFC,0xFD,0xFF,0xC0,0xC1,0xC2,0xC3,0xC4,0xC5,0xC6,0xC7,0xC8,0xC9):
            examples[t].append((verb, hx(ops)))
    return

cnt=collections.Counter(); byverb=collections.defaultdict(collections.Counter)
examples=collections.defaultdict(list)
for f in sorted(glob.glob(os.path.join(DOCS,'*_bin.txt'))):
    nm=os.path.basename(f)[:-8]
    data=load_hexdump(f); hi=header_info(data)
    if hi['sig']!=1 or (hi['attr']&1)==0: continue
    st,_=build_stream(data)
    L1=st[0]<<8|st[1]; L2=st[2]<<8|st[3]; L3=st[4]<<8|st[5]
    for off,ln,body in split_records(st,6+L1+L2+L3):
        if ln is None: continue
        for v,ops in split_statements(body):
            if v in (0x3F,0x56): continue  # raw text
            walk(ops, v, cnt, byverb, None, examples)

print('=== OPERAND TOKEN COUNTS ===')
for t in sorted(k for k in cnt if k!='VAR'):
    print('%02X  %7d   verbs: %s' % (t, cnt[t], ', '.join('%04X:%d'%(v,c) for v,c in byverb[t].most_common(5))))
print('VAR %7d' % cnt['VAR'])
print()
print('=== RARE TOKEN EXAMPLES ===')
for t in sorted(examples):
    for e in examples[t][:3]:
        print('%02X after verb %04X : %s' % (t, e[0], e[1]))
