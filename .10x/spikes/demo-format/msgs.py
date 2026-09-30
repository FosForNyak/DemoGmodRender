import struct, sys, collections
sys.path.insert(0, sys.argv[0].rsplit('/',1)[0])
from bits import *
EDICT_BITS = 13
C = dict(ple_len=24, csl_len='20', um_len=11, em_len=11, ge_len=11, te_len='var')
def parse_msg(b, t, info):
    if t == 0: return
    if t == 1: b.string(); return
    if t == 2: b.ubit(32); b.string(); b.bit(); return
    if t == 3: b.ubit(32); b.ubit(16); b.ubit(16); return
    if t == 4: b.string(); return
    if t == 5:
        c = b.byte()
        for _ in range(c): b.string(); b.string()
        return
    if t == 6: b.byte(); b.long(); return
    if t == 7: b.string(); return
    if t == 8:
        b.word(); b.long(); b.bit(); b.bit(); b.ubit(32); b.word(); b.bytes_(16); b.byte(); b.byte(); b.float(); b.byte()
        for _ in range(6): b.string()
        b.ubit(16); return
    if t == 9: b.bit(); n = b.word(); b.skip(n); return
    if t == 10:
        n = b.word(); bits = log2(n) + 1; create = b.bit()
        if not create:
            for _ in range(n): b.ubit(bits); b.string(); b.string()
        info['classes'] = n; return
    if t == 11: b.bit(); return
    if t == 12:
        name = b.string(); eb = b.ubit(5); mx = 1 << eb; ne = b.ubit(eb+1)
        ln = b.varint32()
        fixed = b.bit()
        if fixed: b.ubit(12); b.ubit(4)
        comp = b.bit()
        info.setdefault('tables', []).append((name, mx, ne, ln, fixed, comp))
        b.skip(ln); return
    if t == 13:
        tid = b.ubit(5); ch = b.word() if b.bit() else 1; ln = b.ubit(20); b.skip(ln); return
    if t == 14:
        codec = b.string(); q = b.byte()
        if q == 255: b.word()
        info['voiceinit']=(codec,q); return
    if t == 15: b.byte(); b.byte(); ln = b.word(); b.skip(ln); return
    if t == 17:
        rel = b.bit()
        if rel: ln = b.ubit(8)
        else: b.ubit(8); ln = b.ubit(16)
        b.skip(ln); return
    if t == 18: b.ubit(EDICT_BITS); return
    if t == 19: b.bit(); b.ubit(16); b.ubit(16); b.ubit(16); return
    if t == 20: b.ubit(16); b.ubit(16); b.ubit(16); return
    if t == 21:
        read_vec_coord(b); b.ubit(9)
        if b.bit(): b.ubit(EDICT_BITS); b.ubit(13)
        b.bit(); return
    if t == 23: b.byte(); ln = b.ubit(C['um_len']); b.skip(ln); return
    if t == 24: b.ubit(EDICT_BITS); b.ubit(9); ln = b.ubit(C['em_len']); b.skip(ln); return
    if t == 25: ln = b.ubit(C['ge_len']); b.skip(ln); return
    if t == 26:
        b.ubit(EDICT_BITS); d = b.bit()
        if d: b.long()
        b.bit(); b.ubit(EDICT_BITS); ln = b.ubit(C['ple_len']); b.bit(); b.skip(ln); return
    if t == 27:
        b.byte(); ln = b.varint32() if C['te_len']=='var' else b.ubit(17); b.skip(ln); return
    if t == 28: b.ubit(14); return
    if t == 29: b.word(); ln = b.word(); b.skip(ln*8); return
    if t == 30: b.ubit(9); ln = b.ubit(20); b.skip(ln); return
    if t == 31: b.ubit(32); b.string(); return
    if t == 32: ln = b.ubit(32); b.skip(ln*8); return
    if t == 33:
        ln = b.ubit(20); b.skip(ln); return
    raise ValueError('unknown msg %d' % t)
def read_coord(b):
    i = b.bit(); f = b.bit()
    if i or f:
        b.bit()
        if i: b.ubit(14)
        if f: b.ubit(5)
def read_vec_coord(b):
    x, y, z = b.bit(), b.bit(), b.bit()
    for h in (x, y, z):
        if h: read_coord(b)
if __name__ == '__main__':
    fn = sys.argv[1]; data = open(fn,'rb').read(); limit = int(sys.argv[2]) if len(sys.argv) > 2 else 10**9
    for a in sys.argv[3:]:
        k, v = a.split('='); C[k] = int(v) if v.isdigit() else v
    pos = 1072; npk = 0; cnt = collections.Counter(); info = {}; errors = 0
    while True:
        cmd = data[pos]; tick = struct.unpack_from('<i', data, pos+1)[0]; pos += 5
        if cmd in (1,2):
            pos += 84; ln = struct.unpack_from('<i', data, pos)[0]; pos += 4
            b = BitReader(data, pos, pos+ln); pos += ln; npk += 1
            seq = []
            try:
                while b.left() >= 6:
                    t = b.ubit(6); seq.append(t); parse_msg(b, t, info); cnt[t] += 1
                if b.left() > 0:
                    nl = b.left(); rest = b.ubit(nl)
                    if rest != 0:
                        info.setdefault('nonzero_pad', 0); info['nonzero_pad'] += 1
                        pl = info.setdefault('pad_last', collections.Counter()); pl[(seq[-1] if seq else -1, nl, rest)] += 1
            except Exception as e:
                errors += 1
                errtypes = info.setdefault('errtypes', collections.Counter()); errtypes[seq[-1] if seq else -1] += 1
                if errors <= 8: print('ERR packet', npk, 'cmd', cmd, 'tick', tick, 'len', ln, 'seq', seq[-8:], e)
            if npk >= limit: break
        elif cmd == 3: pass
        elif cmd in (4,6,8): ln = struct.unpack_from('<i', data, pos)[0]; pos += 4+ln
        elif cmd == 5: pos += 4; ln = struct.unpack_from('<i', data, pos)[0]; pos += 4+ln
        else: break
    print('packets', npk, 'errors', errors); print(sorted(cnt.items()))
    for tb in info.get('tables', []): print('  table', tb)
    print('classes', info.get('classes'), 'voice', info.get('voiceinit'))
    print('errtypes', info.get('errtypes'), 'nonzero_pad', info.get('nonzero_pad'))
    print('pad (lastmsg, nbits, value):', info.get('pad_last', collections.Counter()).most_common(12))
