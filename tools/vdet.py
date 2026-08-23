# -*- coding: utf-8 -*-
import sys, pickle, re, html, os
sys.stdout.reconfigure(encoding='utf-8')
from verify import load_mine, load_ref, skeleton
detail=pickle.load(open('verify.pkl','rb'))
which=sys.argv[1]; nm=sys.argv[2]; k=int(sys.argv[3]) if len(sys.argv)>3 else 6
idx={'skel':3,'lits':4,'nums':5}[which]
mine=load_mine(nm); ref=load_ref(nm)
for ln in detail[nm][idx][:k]:
    print('%s %d' % (nm,ln))
    print('   MINE: %s' % mine[ln][:200])
    print('   REF : %s' % ref[ln][:200])
