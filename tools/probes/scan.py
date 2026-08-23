# -*- coding: utf-8 -*-
import sys, os, glob, collections
sys.stdout.reconfigure(encoding='utf-8')
from iskra import *

files = sorted(glob.glob(os.path.join(DOCS, '*_bin.txt')))
verbstat = collections.Counter()
verbfiles = collections.defaultdict(set)
bad = []
summary = []
for f in files:
    nm = os.path.basename(f)[:-8]
    try:
        data = load_hexdump(f)
        hi = header_info(data)
        if hi['sig'] != 1:
            summary.append((nm, 'sig=%02X' % hi['sig'])); continue
        if (hi['attr'] & 1) == 0:
            summary.append((nm, 'TEXT attr=%02X' % hi['attr'])); continue
        st, marks = build_stream(data)
        if len(st) < 8:
            summary.append((nm, 'short stream')); continue
        L1 = st[0]<<8|st[1]; L2=st[2]<<8|st[3]; L3=st[4]<<8|st[5]
        start = 6+L1+L2+L3
        recs = split_records(st, start)
        nd = sum(1 for r in recs if r[1] is None)
        summary.append((nm, 'attr=%02X L1=%d L2=%d L3=%d start=%d len=%d recs=%d desync=%d' % (hi['attr'],L1,L2,L3,start,len(st),len(recs),nd)))
        for off, ln, body in recs:
            if ln is None: continue
            for v, ops in split_statements(body):
                verbstat[v]+=1
                verbfiles[v].add(nm)
    except Exception as e:
        summary.append((nm, 'ERR %s' % e))

for nm, s in summary: print('%-12s %s' % (nm, s))
print()
print('=== VERB STATS ===')
for v in sorted(verbstat):
    known = verb_name(v)
    mark = '' if not known.startswith('?') else '   <<< UNKNOWN'
    print('%04X %-22s %6d  %s%s' % (v, known, verbstat[v], ','.join(sorted(verbfiles[v])[:6]), mark))
