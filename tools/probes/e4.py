# -*- coding: utf-8 -*-
import sys, os, glob, collections
sys.stdout.reconfigure(encoding='utf-8')
from nb import *
from detok import *
# treat E4 like E3 for the walk
stat=collections.Counter(); low=collections.Counter()
verbs=collections.defaultdict(collections.Counter)
def scan(P,tag):
    for off,ln,body in split_records(P['stream'],P['start']):
        if ln is None: continue
        for v,o in split_statements(body):
            if v in (0x3F,0x56): continue
            i=0
            while i<len(o):
                t=o[i]
                if t in (0xE3,0xE4) and i+1<len(o):
                    L=o[i+1]; txt=o[i+2:i+2+L]
                    stat[t]+=1
                    verbs[t][v]+=1
                    if any(0xC0<=b<0xE0 for b in txt): low[t]+=1
                    i+=2+L; continue
                i+=1
for nm in sorted(os.listdir(NB)):
    scan(progn(nm),nm)
for f in sorted(glob.glob(os.path.join(DOCS,'*_bin.txt'))):
    nm=os.path.basename(f)[:-8]
    P=prog(nm)
    if (P['hdr']['attr']&1)==0: continue
    scan(P,nm)
for t in (0xE3,0xE4):
    print('%02X  count=%-6d with lowercase KOI8=%-5d verbs=%s' % (t,stat[t],low[t],verbs[t].most_common(6)))
