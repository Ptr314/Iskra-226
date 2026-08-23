# -*- coding: utf-8 -*-
"""Line-by-line comparison of the viewer output against reference listings."""
import sys, re, html, os, glob, collections
sys.stdout.reconfigure(encoding='utf-8')
from iskra import DOCS, _ROOT

# вывод C++ детокенизатора, см. tools/README.md
OUT  = os.path.join(_ROOT, "build", "out")    # корпус из hex-дампов
OUT2 = os.path.join(_ROOT, "build", "out2")   # корпус corpus/bin

def strip_literals(s):
    """remove string literals and HEX(...) bodies, return (rest, [literals])"""
    lits=[]; out=''; i=0; n=len(s)
    while i<n:
        c=s[i]
        if c == '"':
            j=s.find(c,i+1)
            if j<0: j=n-1
            lits.append(s[i+1:j]); out+='@'; i=j+1; continue
        if s.startswith('HEX(',i):
            j=s.find(')',i)
            if j<0: j=n-1
            lits.append(s[i+4:j]); out+='H'; i=j+1; continue
        out+=c; i+=1
    return out,lits

WS=re.compile(r'[A-Za-z0-9\u00a4%\u0400-\u04FF ]+')
def skeleton(s):
    body,lits = strip_literals(s)
    return WS.sub('',body), lits

def nums(s):
    body,_=strip_literals(s)
    return re.findall(r'\d+\.?\d*', body)

def load_mine(nm):
    p=os.path.join(OUT, nm+'.txt')
    if not os.path.exists(p): p=os.path.join(OUT2, nm+'.txt')
    s=html.unescape(open(p,encoding='utf-8',errors='replace').read())
    d={}
    for line in s.split('\n'):
        m=re.match(r'^(\d{4}) (.*)$',line)
        if m: d[int(m.group(1))]=m.group(2)
    return d

def load_ref(nm):
    p=os.path.join(DOCS,nm+'_text.txt')
    if not os.path.exists(p): return None
    d={}; order=[]
    for line in open(p,encoding='utf-8',errors='replace'):
        line=line.rstrip('\r\n')
        m=re.match(r'^(\d+)\s(.*)$',line)
        if m: d[int(m.group(1))]=m.group(2); order.append(int(m.group(1)))
        elif order: d[order[-1]]+='\n'+line
    return d

total=collections.Counter()
detail={}
for f in sorted(glob.glob(os.path.join(DOCS,'*_text.txt'))):
    nm=os.path.basename(f)[:-9]
    try: mine=load_mine(nm)
    except Exception: continue
    ref=load_ref(nm)
    if not ref or not mine: continue
    common=sorted(set(mine)&set(ref))
    only_mine=sorted(set(mine)-set(ref)); only_ref=sorted(set(ref)-set(mine))
    bad_sk=[]; bad_lit=[]; bad_num=[]
    for ln in common:
        a,la=skeleton(mine[ln]); b,lb=skeleton(ref[ln])
        if a!=b: bad_sk.append(ln)
        if la!=lb: bad_lit.append(ln)
        if nums(mine[ln])!=nums(ref[ln]): bad_num.append(ln)
    detail[nm]=(len(common),len(only_mine),len(only_ref),bad_sk,bad_lit,bad_num)
    total['common']+=len(common); total['sk']+=len(bad_sk)
    total['lit']+=len(bad_lit); total['num']+=len(bad_num)
    total['onlymine']+=len(only_mine); total['onlyref']+=len(only_ref)

print('%-10s %6s %6s %6s | %6s %6s %6s' % ('file','common','+mine','+ref','skel','lits','nums'))
for nm in sorted(detail):
    c,om,orf,sk,lit,num=detail[nm]
    flag='' if not (sk or lit or num or om or orf) else ''
    print('%-10s %6d %6d %6d | %6d %6d %6d %s' % (nm,c,om,orf,len(sk),len(lit),len(num),flag))
print('%-10s %6d %6d %6d | %6d %6d %6d' % ('TOTAL',total['common'],total['onlymine'],total['onlyref'],
      total['sk'],total['lit'],total['num']))
import pickle
pickle.dump(detail,open('verify.pkl','wb'))
