# -*- coding: utf-8 -*-
import sys, os, glob, collections, re
sys.stdout.reconfigure(encoding='utf-8')
from iskra import *

PAIRS = []
for f in sorted(glob.glob(os.path.join(DOCS,'*_text.txt'))):
    nm = os.path.basename(f)[:-9]
    if os.path.exists(os.path.join(DOCS, nm+'_bin.txt')):
        PAIRS.append(nm)

def split_text_stmts(s):
    out=[]; cur=''; i=0; inq=False
    while i < len(s):
        c=s[i]
        if c=='"': inq = not inq; cur+=c; i+=1; continue
        if not inq and c==':':
            out.append(cur); cur=''; i+=1; continue
        cur+=c; i+=1
        # REM/% swallow rest
        st=cur.lstrip()
        if not inq and (st=='%' or st.startswith('% ') or st=='REM ' or st=='REM'):
            pass
    out.append(cur)
    # re-merge: if a statement starts with REM or %, everything after belongs to it
    res=[]
    k=0
    while k < len(out):
        t=out[k].strip()
        if t.startswith('%') or t.startswith('REM'):
            res.append(':'.join(out[k:])); break
        res.append(out[k]); k+=1
    return res

def first_word(s):
    s=s.strip()
    m=re.match(r"^([A-Za-z\u00a4$']+)", s)
    return m.group(1).upper() if m else s[:12]

votes = collections.defaultdict(collections.Counter)
examples = collections.defaultdict(list)
for nm in PAIRS:
    try:
        P=prog(nm); T=text_lines(nm)
    except Exception as e:
        print('skip',nm,e); continue
    if not T: continue
    recs = split_records(P['stream'], P['start'])
    for off, ln, body in recs:
        if ln is None or ln not in T: continue
        sts = split_statements(body)
        tsts = split_text_stmts(T[ln])
        if len(sts)!=len(tsts): continue
        for (v,ops),ts in zip(sts,tsts):
            votes[v][first_word(ts)] += 1
            if len(examples[v])<3: examples[v].append((nm,ln,ts.strip(),hx(ops)))

print('=== VERB -> TEXT WORD ===')
for v in sorted(votes):
    tot=sum(votes[v].values())
    top=votes[v].most_common(4)
    print('%04X %-22s n=%-5d %s' % (v, verb_name(v), tot, top))
print()
print('=== EXAMPLES FOR UNKNOWN ===')
for v in sorted(votes):
    if verb_name(v).startswith('?'):
        for e in examples[v]: print('%04X %-9s %4d  %s\n           %s' % (v,e[0],e[1],e[2],e[3]))
