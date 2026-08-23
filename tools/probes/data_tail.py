# -*- coding: utf-8 -*-
import sys, os, glob
sys.stdout.reconfigure(encoding='utf-8')
from iskra import *
for nm in ['DEM6','DIG_DEM','VICT','EDITOR','M0']:
    P=prog(nm)
    print('===',nm,'start',P['start'])
    prev=None
    for off,ln,body in split_records(P['stream'],P['start']):
        if ln is None: continue
        p=0
        while p<len(body):
            v=body[p]; sp=p; p+=1
            if v==0x06: v=0x600|body[p]; p+=1
            L=body[p]; p+=1
            ops=body[p:p+L]; p+=L
            if v==0x29:
                tail=ops[-2:]
                stmt_off = off+3+sp
                print('line %-6d off=%-6d tail=%s  LE=%04X BE=%04X  len=%d' % (ln, stmt_off, hx(tail), tail[0]|tail[1]<<8, tail[0]<<8|tail[1], L))
