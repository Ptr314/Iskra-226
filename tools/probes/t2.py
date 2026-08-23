# -*- coding: utf-8 -*-
import sys
sys.stdout.reconfigure(encoding='utf-8')
from iskra import *
name = sys.argv[1]
P = prog(name)
print(P['hdr'], 'L1=%d L2=%d L3=%d start=%d streamlen=%d' % (P['L1'],P['L2'],P['L3'],P['start'],len(P['stream'])))
T = text_lines(name)
recs = split_records(P['stream'], P['start'])
for off, ln, body in recs:
    if T and ln in T: print('TXT %d: %s' % (ln, T[ln]))
    else: print('TXT %s: <none>' % ln)
    for v, ops in split_statements(body):
        print('    %-22s %s' % (verb_name(v), hx(ops)))
