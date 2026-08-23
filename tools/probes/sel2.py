# -*- coding: utf-8 -*-
import sys, os, glob, re, collections
sys.stdout.reconfigure(encoding='utf-8')
from detok import *
seen=collections.Counter()
for f in sorted(glob.glob(os.path.join(DOCS,'*_text.txt'))):
    nm=os.path.basename(f)[:-9]
    if not os.path.exists(os.path.join(DOCS,nm+'_bin.txt')): continue
    P=prog(nm); T=text_lines(nm)
    for off,ln,body in split_records(P['stream'],P['start']):
        if ln is None or ln not in T: continue
        sts=split_statements(body)
        # split text
        parts=[]; cur=''; inq=False
        for ch in T[ln]:
            if ch=='"': inq=not inq
            if ch==':' and not inq: parts.append(cur); cur=''
            else: cur+=ch
        parts.append(cur)
        if len(parts)!=len(sts): continue
        for (v,ops),tp in zip(sts,parts):
            if v in (0x54,0x82,0x7C,0x80,0x81,0x83,0x7D,0x2A,0x2D,0x7B,0x79,0x7A,0x76,0x77):
                key=(v,tp.strip()[:40])
                if seen[key]: continue
                seen[key]+=1
                print('%-24s %-42s %s' % (verb_name(v), tp.strip()[:42], hx(ops)))
