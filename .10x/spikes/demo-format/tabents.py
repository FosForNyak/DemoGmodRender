import struct, sys
sys.path.insert(0, sys.argv[0].rsplit('\\', 1)[0])
from bits import *
data = open(sys.argv[1], 'rb').read()
pos = 1072; n = 0
while True:
    cmd = data[pos]; pos += 5
    pos += 84; ln = struct.unpack_from('<i', data, pos)[0]; pos += 4
    n += 1
    if n == 2: break
    pos += ln
b = BitReader(data, pos, pos + ln)
b.ubit(6); b.ubit(32); b.ubit(32)
while b.left() >= 6:
    t = b.ubit(6)
    if t != 12: print('stop at msg', t); break
    name = b.string(); eb = b.ubit(5); ne = b.ubit(eb + 1); x = 0; L = b.varint32()
    fixed = b.bit(); ud = (b.ubit(12), b.ubit(4)) if fixed else None; comp = b.bit()
    start = b.pos
    tb = BitReader(data); tb.pos = start; tb.end = start + L
    b.skip(L)
    entries = []; last = -1; hist = []; err = None
    if comp:
        print(f'{name:20s} ne={ne:5d} x={x} L={L} fixed={ud} COMPRESSED first bytes:', bytes(tb.ubit(8) for _ in range(12)).hex())
        continue
    try:
        for i in range(ne):
            idx = last + 1
            if not tb.bit(): idx = tb.ubit(eb)
            last = idx; s = None
            if tb.bit():
                if tb.bit():
                    h = tb.ubit(5); c = tb.ubit(5); s = hist[h][:c] + tb.string()
                else: s = tb.string()
            u = None
            if tb.bit():
                if fixed: u = tb.ubit(ud[1])
                else: nb = tb.ubit(14); u = bytes(tb.ubit(8) for _ in range(nb))
            hist.append(s or ''); hist = hist[-32:]
            entries.append((idx, s, u if not isinstance(u, bytes) else len(u)))
    except EOFError as e:
        err = str(e)
    print(f'{name:20s} ne={ne:5d} x={x} L={L} fixed={ud} decoded={len(entries)} left={tb.left()} err={err}')
    print('    ', entries[:4])
