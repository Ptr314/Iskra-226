# -*- coding: utf-8 -*-
import sys, os
sys.stdout.reconfigure(encoding='utf-8')
sys.path.insert(0,'.')
from iskra import *
NB = BIN   # из iskra.py
def load(nm):
    return open(os.path.join(NB,nm),'rb').read()
def progn(nm):
    data=load(nm)
    hi=header_info(data)
    stream,marks=build_stream(data)
    L1=stream[0]<<8|stream[1]; L2=stream[2]<<8|stream[3]; L3=stream[4]<<8|stream[5]
    start=6+L1+L2+L3
    if not _rec_ok(stream,start) and _rec_ok(stream,start+4): start+=4
    return dict(name=nm,hdr=hi,stream=stream,L1=L1,L2=L2,L3=L3,start=start)

def _rec_ok(st,p):
    p=skip_pad(st,p)
    if p+3>len(st): return False
    for i in range(2):
        if (st[p+i]>>4)>9 or (st[p+i]&15)>9: return False
    L=st[p+2]
    if L<1: return False
    e=p+2+L
    return e<len(st) and st[e]==0xFE

def build_ctx_n(P):
    from detok import Ctx, table1_recs, render, LAST_TOKS, VARMAX
    ctx=Ctx(); st=P['stream']; L1,L2,L3=P['L1'],P['L2'],P['L3']
    n2=L2//4; n3=L3//4; N=n2+n3
    recs=[]
    for i in range(n2):
        o=6+L1+i*4; recs.append((st[o]|st[o+1]<<8, st[o+2]))
    for i in range(n3):
        o=6+L1+L2+i*4; recs.append((st[o]|st[o+1]<<8, st[o+2]))
    T1=table1_recs(P); k=0
    for pos,(ad,fl) in enumerate(recs):
        idx=N-1-pos
        if idx>VARMAX: continue
        if fl & 0x20: ctx.strv.add(idx)
        elif not (fl & 0x10): ctx.short.add(idx)
        if fl & 1 and k < len(T1):
            r=T1[k]; k+=1
            if idx not in ctx.strv: ctx.arr.add(idx)
            elif (r[3] & 1)==0 or (r[1]>>8)!=8: ctx.arr.add(idx)
    for _ in range(2):
        for off,ln,body in split_records(st,P['start']):
            if ln is None: continue
            for v,o in split_statements(body):
                if v in (0x3F,0x56,0x54,0x82): continue
                render(ctx,v,o)
                for (t,exp,pp) in LAST_TOKS:
                    if t==0xE0 and exp and pp<len(o) and o[pp]<=VARMAX: ctx.arr.add(o[pp])
    return ctx
