# -*- coding: utf-8 -*-
import sys
sys.stdout.reconfigure(encoding='utf-8')
from detok import *

def build_ctx2(P):
    """Variable model from tables 2/3 + 1 only."""
    ctx=Ctx(); st=P['stream']; L1,L2,L3=P['L1'],P['L2'],P['L3']
    n2=L2//4; n3=L3//4; N=n2+n3
    recs=[]
    for i in range(n2):
        o=6+L1+i*4; recs.append((st[o]|st[o+1]<<8, st[o+2]))
    for i in range(n3):
        o=6+L1+L2+i*4; recs.append((st[o]|st[o+1]<<8, st[o+2]))
    T1=table1_recs(P); k=0; desc={}
    for pos,(ad,fl) in enumerate(recs):
        idx=N-1-pos
        if fl & 0x20: ctx.strv.add(idx)
        elif not (fl & 0x10): ctx.short.add(idx)
        if fl & 1 and k < len(T1):
            desc[idx]=T1[k]; k+=1
    for idx,r in desc.items():
        ad,typ,n,sz = r
        if idx in ctx.strv:
            slen=(sz-1)//2 if (sz&1) else sz//2
            # array iff a second dimension present or n plausible; use table-1 record presence + heuristic
            if (typ>>8)!=0x08: ctx.arr.add(idx)           # 2-D
            else: pass                                     # decided below
        else:
            ctx.arr.add(idx)
    return ctx, desc, N

def dim_text(idx, desc, ctx):
    if idx not in desc: return ''
    ad,typ,n,sz = desc[idx]
    s=''
    if idx in ctx.short: s+='%'
    if idx in ctx.strv: s+='¤'
    if (typ>>8)==0x08: dims='(%d)'%n
    else: dims='(%d,%d)'%(n,typ)
    if idx in ctx.strv:
        ln = (sz-1)//2 if (sz&1) else sz//2
        return s+dims+(str(ln) if (sz&1) else '')
    return s+dims

nm=sys.argv[1]
P=prog(nm); ctx,desc,N=build_ctx2(P); T=text_lines(nm) or {}
print('N=%d, descriptors=%d (L1/8=%d)' % (N,len(desc),P['L1']//8))
print('Variables with a table-1 descriptor:')
for idx in sorted(desc):
    print('  V%02X%s' % (idx, dim_text(idx,desc,ctx)))
print('Integer (%%) variables:', ' '.join('V%02X'%i for i in sorted(ctx.short)))
print('String variables:', ' '.join('V%02X'%i for i in sorted(ctx.strv)))
for ln in sorted(T):
    if 'DIM' in T[ln] or 'COM' in T[ln]: print('  TXT %d: %s' % (ln,T[ln]))
