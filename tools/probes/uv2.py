# -*- coding: utf-8 -*-
import sys, os, glob, collections
sys.stdout.reconfigure(encoding='utf-8')
from detok import *
want=[int(x,16) for x in sys.argv[1].split(',')]
per=int(sys.argv[2]) if len(sys.argv)>2 else 20
cnt=collections.Counter()
for f in sorted(glob.glob(os.path.join(DOCS,'*_bin.txt'))):
    nm=os.path.basename(f)[:-8]
    try: P=prog(nm)
    except Exception: continue
    if (P['hdr']['attr']&1)==0: continue
    for off,ln,body in split_records(P['stream'],P['start']):
        if ln is None: continue
        for v,ops in split_statements(body):
            if v in want and cnt[v]<per:
                cnt[v]+=1
                print('%04X %-10s %5d  %s' % (v,nm,ln,hx(ops)))
