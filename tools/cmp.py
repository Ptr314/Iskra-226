# -*- coding: utf-8 -*-
import sys, re, html, os
sys.stdout.reconfigure(encoding='utf-8')
from iskra import DOCS
nm=sys.argv[1]
n=int(sys.argv[2]) if len(sys.argv)>2 else 12
s=html.unescape(open('out/%s.txt'%nm,encoding='utf-8',errors='replace').read())
s=re.sub(r'\n-> [0-9A-F ]*\n','',s)
mine={}
for line in s.split('\n'):
    m=re.match(r'^(\d{4}) (.*)$', line)
    if m: mine[int(m.group(1))]=m.group(2)
ref={}
p=os.path.join(DOCS,nm+'_text.txt')
if os.path.exists(p):
    for line in open(p,encoding='utf-8'):
        m=re.match(r'^(\d+)\s(.*)$', line.rstrip('\r\n'))
        if m: ref[int(m.group(1))]=m.group(2)
c=0
for ln in sorted(set(mine) & set(ref)):
    a=mine[ln]; b=ref[ln]
    # normalise: drop spaces, map my V?? placeholders and their names to 'v'
    na=re.sub(r'V[0-9A-F]{2}','v',a).replace(' ','')
    nb=re.sub(r'(?<![A-Z0-9])[A-Z][0-9]?(?![A-Z])','v',b).replace(' ','')
    if na==nb: continue
    c+=1
    if c>n: break
    print('%d MINE: %s' % (ln,a))
    print('%d REF : %s' % (ln,b))
print('--- differing lines shown: %d of %d common' % (min(c,n), len(set(mine)&set(ref))))
