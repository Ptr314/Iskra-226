# -*- coding: utf-8 -*-
"""Toolkit for Iskra-226 BASIC 02 tokenized file research."""
import os, re, sys, glob

_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DOCS  = os.path.join(_ROOT, "corpus", "hex")   # hex-дампы *_bin.txt / *_text.txt
BIN   = os.path.join(_ROOT, "corpus", "bin")   # двоичные файлы без текстовых пар
WORK  = os.path.join(_ROOT, "build", "corpus") # производные: hex → bin

def load_hexdump(path):
    """Parse '0000 | xx xx ... | ascii' dump into bytes."""
    data = bytearray()
    with open(path, 'r', encoding='utf-8', errors='replace') as f:
        for line in f:
            line = line.rstrip('\r\n')
            if '|' not in line:
                continue
            parts = line.split('|')
            if len(parts) < 2:
                continue
            off = parts[0].strip()
            try:
                offv = int(off, 16)
            except ValueError:
                continue
            hexpart = parts[1]
            toks = hexpart.split()
            for t in toks:
                if len(t) == 2:
                    try:
                        data.append(int(t, 16))
                    except ValueError:
                        pass
    return bytes(data)

def kchar(b):
    if b == 0x24: return '\u00a4'
    if 0x20 <= b < 0x7f: return chr(b)
    if b >= 0xc0:
        return bytes([b]).decode('koi8_r')
    return '{%02X}' % b

def kstr(bs):
    return ''.join(kchar(b) for b in bs)

def fromBCD(b):
    return (b >> 4) * 10 + (b & 15)

# ---------------- stream assembly ----------------

def sectors(data):
    for off in range(0x100, len(data), 0x100):
        yield off, data[off:off+0x100]

def build_stream(data, strict=True):
    """Concatenate program sectors dropping 2 service bytes. Stops at 1C."""
    out = bytearray()
    marks = []   # (stream_offset_where_sector_content_starts, file_offset)
    for off, sec in sectors(data):
        if len(sec) < 3: break
        if sec[0] == 0x1c: break
        if sec[1] != 0x80: break
        marks.append((len(out), off))
        out += sec[2:]
    return bytes(out), marks

def header_info(data):
    return dict(sig=data[0], name=kstr(data[1:9]).rstrip(), attr=data[9])

# ---------------- verbs ----------------

VERBS = {
0x21:"GOTO",0x22:"GOSUB",0x23:"GOSUB'",0x24:"IF",0x25:"KEYIN",0x26:"ON",0x27:"DEFFN'",
0x28:"PRINTUSING",0x29:"DATA",0x2A:"SAVE",0x2B:"RENUMBER",0x2C:"CLEAR",0x2D:"LOAD",
0x2E:"LIST",0x2F:"RUN",0x30:"RETURN CLEAR",0x34:"ON ERROR",0x35:"LET",0x36:"",
0x3F:"%",0x40:"$GIO",0x41:"INPUT",0x42:"STOP",0x43:"AND(",0x44:"READ",0x45:"BOOL",
0x46:"DIM",0x47:"CONVERT",0x48:"PACK(",0x4A:"ADD",0x4B:"BIN(",0x4C:"PRINT",0x4D:"ROTATE",
0x4E:"COM",0x50:"HEXPRINT",0x51:"RESTORE",0x52:"NEXT",0x53:"REWIND",0x54:"SELECT",
0x55:"BACKSPACE",0x56:"REM",0x57:"FOR",0x58:"SKIP",0x59:"END",0x5A:"DEFFN",0x5C:"RES",
0x5D:"UNPACK(",0x5E:"RETURN",0x5F:"TRACE",0x61:"OR(",0x62:"XOR(",0x64:"INIT",
0x66:"DATA LOAD BT",0x68:"DATA SAVE BT",0x6D:"COPY",0x6E:"DATA SAVE BA",0x6F:"DATA SAVE DA",
0x70:"DATA LOAD BA",0x71:"DATA LOAD DA",0x74:"DATA LOAD DC",0x75:"DATA LOAD DC OPEN T",
0x78:"DATA SAVE DC OPEN T",0x79:"DBACKSPACE",0x7A:"DSKIP",0x7B:"LIMITS",0x7C:"LIST DC",
0x7D:"LOAD DC",0x7E:"MOVE",0x80:"SAVE DC",0x81:"SCRATCH",0x83:"VERIFY",
}
VERBS2 = {
0x01:"MAT",0x02:"MAT REDIM",0x0A:"MAT SEARCH",0x0C:"\u00a4TRAN(",0x0F:"\u00a4OPEN",
0x15:"DRAW",0x19:"NPLOT",0x1E:"LABEL",0x1F:"\u00a4COPY",0x24:"LINPUT",0x25:"ASMB",
}

def verb_name(vid):
    if vid > 0xff:
        return VERBS2.get(vid & 0xff, "?%04X?" % vid)
    return VERBS.get(vid, "?%02X?" % vid)

# ---------------- record/statement splitting ----------------

SECLEN = 254

def skip_pad(stream, p):
    """After FE: if the rest of the current 254-byte sector chunk is all zero, jump to next chunk."""
    b = ((p // SECLEN) + 1) * SECLEN
    if b > len(stream): b = len(stream)
    if b > p and all(v == 0 for v in stream[p:b]):
        return b
    return p

def split_records(stream, start):
    """Yield (offset, line_num, body_bytes). Robust to alignment zeroes."""
    p = start
    recs = []
    while p + 3 <= len(stream):
        p = skip_pad(stream, p)
        if p + 3 > len(stream): break
        ln = fromBCD(stream[p]) * 100 + fromBCD(stream[p+1])
        blen = stream[p+2]
        if blen < 1: break
        body = stream[p+3 : p+2+blen]
        end = p + 2 + blen
        recs.append((p, ln, bytes(body)))
        if end >= len(stream): break
        if stream[end] != 0xFE:
            recs.append((end, None, b'DESYNC'))
            break
        p = end + 1
    return recs

def split_statements(body):
    """Yield (verb_id, operands_bytes)."""
    p = 0
    st = []
    while p < len(body):
        v = body[p]; p += 1
        if v == 0x06:
            if p >= len(body): break
            v = 0x0600 | body[p]; p += 1
        if p >= len(body): break
        ln = body[p]; p += 1
        st.append((v, bytes(body[p:p+ln])))
        p += ln
    return st

# ---------------- convenience ----------------

def load(name):
    p = os.path.join(DOCS, name if name.endswith('.txt') else name + '_bin.txt')
    return load_hexdump(p)

def prog(name):
    data = load(name)
    hi = header_info(data)
    stream, marks = build_stream(data)
    L1 = stream[0] << 8 | stream[1]
    L2 = stream[2] << 8 | stream[3]
    L3 = stream[4] << 8 | stream[5]
    start = 6 + L1 + L2 + L3
    return dict(name=name, hdr=hi, stream=stream, marks=marks, L1=L1, L2=L2, L3=L3, start=start)

def text_lines(name):
    p = os.path.join(DOCS, name + '_text.txt')
    if not os.path.exists(p): return None
    out = {}
    order = []
    with open(p, 'r', encoding='utf-8') as f:
        for line in f:
            line = line.rstrip('\r\n')
            m = re.match(r'^(\d+)\s(.*)$', line)
            if m:
                out[int(m.group(1))] = m.group(2)
                order.append(int(m.group(1)))
            elif order:
                out[order[-1]] += '\n' + line
    return out

def hx(bs):
    return ' '.join('%02X' % b for b in bs)

# ---------------- token-level walker ----------------
VARMAX = 0xC9

def preamble(verb, ops):
    """Return number of leading bytes that are not expression tokens."""
    if verb in (0x21,0x22,0x2F): return 2          # BCD line
    if verb == 0x23: return 1                      # GOSUB' label (binary)
    if verb == 0x27: return 5                      # DEFFN' label + 4 zero
    if verb == 0x25: return 5                      # KEYIN var + 2 BCD lines
    if verb in (0x3F,0x56): return len(ops)        # raw text
    if verb == 0x54: return len(ops)               # SELECT: own format
    if verb == 0x82: return len(ops)
    return 0

def walk_tokens(verb, ops):
    """Yield (pos, tok, extra) for expression tokens; VAR tokens have tok<=0xC9."""
    p = preamble(verb, ops); n = len(ops)
    res = []
    while p < n:
        t = ops[p]; start = p; p += 1
        extra = b''
        if t <= VARMAX:
            pass
        elif t == 0xDE:
            extra = ops[p:p+1]; p += 1
        elif t == 0xE0:
            extra = ops[p:p+1]; p += 1
        elif t == 0xD3:
            extra = ops[p:p+2]; p += 2
        elif t in (0xE2, 0xE3):
            if p < n:
                L = ops[p]; extra = ops[p+1:p+1+L]; p += 1 + L
        elif t == 0xE7:
            extra = ops[p:p+2]; p += 2
        elif t == 0xE8:
            extra = ops[p:p+1]; p += 1
        elif t in (0xE5, 0xE6):
            if p < n:
                d = ops[p]; nd = d & 0x0F
                k = 1 + (nd + 1)//2 + (1 if t == 0xE6 else 0)
                extra = ops[p:p+k]; p += k
        elif t in (0xCC, 0xCD):
            extra = ops[p:]; p = n
        res.append((start, t, bytes(extra)))
    return res
