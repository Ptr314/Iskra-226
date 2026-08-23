# -*- coding: utf-8 -*-
import sys, os, glob, collections
sys.stdout.reconfigure(encoding='utf-8')
from nb import *
from detok import *

HARD = {
 'ASMB':      {0x0625},
 '$GIO':      {0x40},
 'graphics':  {0x060F,0x0613,0x0614,0x0615,0x0619,0x061C,0x061E,0x061F,0x0600,0x0623},
 'unknown':   {0x33,0x37,0x63,0x72,0x0609,0x060D,0x060E,0x0603,0x0604},
}
files=[]
for f in sorted(glob.glob(os.path.join(DOCS,'*_bin.txt'))):
    nm=os.path.basename(f)[:-8]
    P=prog(nm)
    if (P['hdr']['attr']&1)==0: continue
    files.append((nm,P))
for nm in sorted(os.listdir(NB)):
    files.append((nm,progn(nm)))

tot_stmt=0; tot_lines=0
cat=collections.Counter(); clean=0; per=collections.Counter()
for nm,P in files:
    used=set()
    for off,ln,body in split_records(P['stream'],P['start']):
        if ln is None: continue
        tot_lines+=1
        for v,o in split_statements(body):
            tot_stmt+=1
            for k,s in HARD.items():
                if v in s: used.add(k); per[k]+=1
    if not used: clean+=1
    for k in used: cat[k]+=1
print('files: %d, lines: %d, statements: %d' % (len(files),tot_lines,tot_stmt))
print('files using nothing "hard": %d (%.0f%%)' % (clean, 100*clean/len(files)))
for k in HARD:
    print('  %-9s: %3d files, %5d statements (%.2f%% of all)' % (k,cat[k],per[k],100*per[k]/tot_stmt))
