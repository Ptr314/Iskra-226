# -*- coding: utf-8 -*-
"""Full detokenizer for Iskra-226 BASIC 02 tokenized programs (research model)."""
import sys
from iskra import *

VERBS.update({0x76:"DATA SAVE DC", 0x77:"DATA SAVE DC CLOSE", 0x82:"SCRATCH DISK"})
VERBS2.update({0x06:"MAT COPY", 0x22:"¤LET", 0x23:"WINDOW", 0x26:"REPLACE"})

# operand-position meanings
OPD = {
 0xCA:'FROM', 0xCB:'ALL', 0xCC:'GOSUB', 0xCD:'GOTO',
 0xD0:')', 0xD1:'TO', 0xD2:'STEP',
 0xD5:'AT(', 0xD6:'BEG', 0xD7:'END', 0xD8:'ROUND(',
 0xDB:'#', 0xDC:'/',
 0xDF:'TAB(',
 0xE1:'STR(', 0xE9:'-', 0xEB:'(',
 0xEC:'POS(', 0xED:'LEN(', 0xEE:'NUM(', 0xEF:'VAL(',
 0xF1:'#PI',
 0xF2:'ABS(', 0xF3:'INT(', 0xF5:'SGN(', 0xF6:'SQR(', 0xF7:'LOG(', 0xF8:'EXP(',
}
# operation-position meanings
OPR = {
 0xCA:'FROM', 0xCB:'ALL', 0xCC:'GOSUB', 0xCD:'GOTO',
 0xD0:')', 0xD1:'TO', 0xD2:'STEP', 0xD3:'THEN',
 0xD4:'>', 0xD5:'<>', 0xD6:'<=', 0xD7:'<', 0xD8:'>=', 0xD9:'=',
 0xDB:'#', 0xDC:'/', 0xDD:';', 0xDE:',', 0xDF:'*', 0xE0:'^',
 0xE6:'OR', 0xE7:'AND', 0xE9:'-', 0xEA:'+',
}
FUNC_EXPL = {0xD5,0xD8,0xDF,0xE1,0xF2,0xF3,0xF5,0xF6,0xF7,0xF8}   # own '(' , closed by D0
IMPLICIT  = {0xEC,0xED,0xEE,0xEF}                                 # no closing paren

def bcd_digits(b): return '%02X' % b

def num_e5(extra, with_exp):
    d = extra[0]
    before = d >> 4
    total = d & 0x0F
    digs = ''.join('%02X' % b for b in extra[1:1+(total+1)//2])[:total]
    if before == 0:
        s = '.' + digs
    elif before >= total:
        s = digs + '0'*(before-total)
    else:
        s = digs[:before] + '.' + digs[before:]
    if with_exp:
        s += 'E' + str(extra[-1])
    return s

class Ctx:
    def __init__(self):
        self.arr = set()      # var indices known to be arrays
        self.strv = set()
        self.short = set()
    def vname(self, i):
        s = 'V%02X' % i
        if i in self.short: s += '%'
        if i in self.strv: s += '¤'
        return s

class St:
    """statement renderer"""
    def __init__(self, ctx, verb):
        self.ctx=ctx; self.verb=verb
        self.out=[]
        self.expect=True     # expecting an operand
        self.stack=[]        # 'array','func','group'
        self.impl=0          # count of open implicit functions on top
        self.pos_pending=False
        self.val_pending=False
    def emit(self,s): self.out.append(s)
    def text(self): return ''.join(self.out)

    def close_impl(self):
        while self.impl>0:
            self.emit(')'); self.impl-=1

    def operand_start(self):
        """called before emitting an operand; handles implicit list separators"""
        if not self.expect:
            # a new operand where an operation was expected -> implicit list separator
            self.emit(',')
        self.expect=True

LAST_TOKS=[]

def render(ctx, verb, ops, opts=None):
    s = St(ctx, verb)
    p = 0; n = len(ops)
    # --- verb specific preambles ---
    if verb in (0x21,0x22,0x2F) and n>=2:
        s.emit(str(FROM_BCD2(ops,0))); p=2
    elif verb==0x23 and n>=1:
        s.emit(str(ops[0])); p=1; s.expect=False
    elif verb==0x27 and n>=5:
        s.emit(str(ops[0])); p=5; s.expect=False
    elif verb==0x25 and n>=5:
        s.emit(ctx.vname(ops[0])+','+str(FROM_BCD2(ops,1))+','+str(FROM_BCD2(ops,3))); p=5
    elif verb in (0x3F,0x56):
        return kstr(ops)
    LAST_TOKS.clear()
    while p<n:
        t=ops[p]; p+=1
        LAST_TOKS.append((t,s.expect,p))
        if t<=VARMAX:
            s.operand_start()
            s.emit(ctx.vname(t))
            if t in ctx.arr:
                s.emit('('); s.stack.append('array'); s.expect=True
            else:
                s.expect=False
            continue
        if t==0xDE:
            if s.expect:
                if p>=n: s.emit('?DE?'); break
                v=ops[p]; p+=1
                s.emit('{%02X}'%v); s.expect=False
            else:
                s.close_impl(); s.emit(','); s.expect=True
            continue
        if t==0xE0:
            if s.expect:
                if p>=n: s.emit('?E0?'); break
                v=ops[p]; p+=1
                s.operand_start()
                s.emit(ctx.vname(v)+'()'); s.expect=False
            else:
                s.close_impl(); s.emit('^'); s.expect=True
            continue
        if t==0xE2:
            if p>=n: s.emit('?E2?'); break
            L=ops[p]; p+=1
            s.operand_start()
            s.emit('HEX('+''.join('%02X'%b for b in ops[p:p+L])+')'); p+=L; s.expect=False
            continue
        if t==0xE3:
            if p>=n: s.emit('?E3?'); break
            L=ops[p]; p+=1
            s.operand_start()
            s.emit('"'+kstr(ops[p:p+L])+'"'); p+=L; s.expect=False
            continue
        if t==0xE8:
            if p>=n: s.emit('?E8?'); break
            s.operand_start(); s.emit(str(fromBCD(ops[p]))); p+=1; s.expect=False; continue
        if t==0xE7:
            s.operand_start(); s.emit(str(FROM_BCD2(ops,p))); p+=2; s.expect=False; continue
        if t in (0xE5,0xE6):
            if p>=n: s.emit('?%02X?'%t); break
            d=ops[p]; nd=d&0x0F; k=1+(nd+1)//2+(1 if t==0xE6 else 0)
            s.operand_start(); s.emit(num_e5(ops[p:p+k], t==0xE6)); p+=k; s.expect=False; continue
        if t==0xD3:
            s.close_impl(); s.emit('THEN'+str(FROM_BCD2(ops,p))); p+=2; s.expect=True; continue
        if t in (0xCC,0xCD):
            s.close_impl(); s.emit(OPR[t])
            lst=[]
            while p+1<n: lst.append(str(FROM_BCD2(ops,p))); p+=2
            s.emit(','.join(lst)); s.expect=False; continue
        if t==0xD0:
            s.close_impl()
            if s.stack: s.stack.pop()
            s.emit(')'); s.expect=False; continue
        if s.expect:
            nm=OPD.get(t)
            if nm is None:
                s.emit('?%02X?'%t); s.expect=False; continue
            if t in FUNC_EXPL:
                s.operand_start(); s.emit(nm); s.stack.append('func'); s.expect=True; continue
            if t in IMPLICIT:
                s.operand_start(); s.emit(nm); s.impl+=1; s.expect=True
                if t==0xEF: s.val_pending=True
                if t==0xEC: s.pos_pending=True
                continue
            if t==0xEB:
                s.operand_start(); s.emit('('); s.stack.append('group'); s.expect=True; continue
            if t==0xE9:
                s.emit('-'); s.expect=True; continue     # unary, state unchanged
            if t==0xF1:
                s.operand_start(); s.emit('#PI'); s.expect=False; continue
            # keyword-ish in operand slot
            s.emit(nm); s.expect=True; continue
        else:
            nm=OPR.get(t)
            if nm is None:
                s.emit('?%02X?'%t); s.expect=True; continue
            s.close_impl()
            s.emit(nm)
            s.expect = (t!=0xD0)
            continue
    s.close_impl()
    return s.text()

def FROM_BCD2(a,i):
    if i+1>=len(a): return -1
    return fromBCD(a[i])*100+fromBCD(a[i+1])

# ---------------- table 1 / DIM reconstruction ----------------
def table1_recs(P):
    st=P['stream']; L1=P['L1']; res=[]
    for i in range(L1//8):
        o=6+i*8
        r=[st[o]|st[o+1]<<8, st[o+2]|st[o+3]<<8, st[o+4]|st[o+5]<<8, st[o+6]|st[o+7]<<8]
        res.append(r)
    return res

def dim_order(P):
    """indices declared in DIM/COM, in order of appearance"""
    order=[]
    for off,ln,body in split_records(P['stream'],P['start']):
        if ln is None: continue
        for v,ops in split_statements(body):
            if v in (0x46,0x4E):
                for b in ops:
                    if b<=VARMAX: order.append(b)
    return order

def describe(P):
    """returns dict idx -> (kind, text) using table1 + DIM order"""
    T=table1_recs(P); R=len(T); D=dim_order(P); out={}
    for k,idx in enumerate(D):
        t1i=R-1-k
        if t1i<0 or t1i>=R: continue
        ad,typ,ln,sz=T[t1i]
        nxt=T[t1i+1][0] if t1i+1<R else 0xFFFE
        delta=(nxt-ad) if (ad and (t1i+1>=R or nxt)) else None
        out[idx]=dict(rec=t1i, ad=ad, typ=typ, n=ln, sz=sz, delta=delta)
    return out, T, D

def build_ctx(P):
    ctx=Ctx()
    d,T1,D=describe(P)
    for idx,e in d.items():
        typ=e['typ']; sz=e['sz']; ln=e['n']; delta=e['delta']
        if typ==0x0800:
            ctx.strv.add(idx)
            slen=(sz-1)//2 if (sz&1) else sz//2
            if delta is not None:
                if delta==ln*slen+6: ctx.arr.add(idx)
            else:
                if ln>1 and ln<10000 and sz<600: pass
        else:
            ctx.arr.add(idx)
            if sz==4: ctx.short.add(idx)
    # arrays referenced wholesale: two passes, state-aware
    for _ in range(2):
        for off,ln_,body in split_records(P['stream'],P['start']):
            if ln_ is None: continue
            for v,ops in split_statements(body):
                if v in (0x3F,0x56,0x54,0x82): continue
                render(ctx,v,ops)
                for (t,exp,pp) in list(LAST_TOKS):
                    if t==0xE0 and exp and pp<len(ops) and ops[pp]<=VARMAX:
                        ctx.arr.add(ops[pp])
    return ctx

def listing(nm, only=None):
    P=prog(nm); ctx=build_ctx(P); T=text_lines(nm) or {}
    res=[]
    for off,ln,body in split_records(P['stream'],P['start']):
        if ln is None: continue
        if only and ln not in only: continue
        parts=[]
        for v,ops in split_statements(body):
            vb=verb_name(v)
            parts.append((vb+' '+render(ctx,v,ops)).strip())
        res.append((ln, ':'.join(parts), T.get(ln)))
    return res
