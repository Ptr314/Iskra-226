import sys,html,re
sys.stdout.reconfigure(encoding='utf-8')
s=open(sys.argv[1],encoding='utf-8',errors='replace').read()
s=html.unescape(s)
s=re.sub(r'\n-> [0-9A-F ]*\n','',s)
sys.stdout.write(s)
