"""Collect every instancebaseline entry (create + updates) across the whole demo."""
import struct, sys, os, pickle, collections
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from bits import *
import msgs
UDBITS = int(os.environ.get("UDBITS", "19"))

def read_entries(b, n, ebits, fixed, ud, hist):
    out = []; last = -1
    for _ in range(n):
        idx = last + 1
        if not b.bit(): idx = b.ubit(ebits)
        last = idx; s = None
        if b.bit():
            if b.bit():
                h = b.ubit(5); c = b.ubit(5); s = hist[h][:c] + b.string()
            else: s = b.string()
        u = None
        if b.bit():
            if fixed: u = b.ubit(ud[1])
            else: nb = b.ubit(UDBITS); u = bytes(b.ubit(8) for _ in range(nb))
        hist.append(s or ''); del hist[:-32]
        out.append((idx, s, u))
    return out

data = open(sys.argv[1], 'rb').read()
pos = 1072
tables = []  # (name, ebits, fixed, ud)
names = {}   # instancebaseline index -> class string
found = collections.OrderedDict()
while True:
    if pos >= len(data): break
    cmd = data[pos]; tick = struct.unpack_from('<i', data, pos + 1)[0]; pos += 5
    if cmd in (1, 2):
        pos += 84; ln = struct.unpack_from('<i', data, pos)[0]; pos += 4
        b = BitReader(data, pos, pos + ln); pos += ln
        try:
            while b.left() >= 6:
                t = b.ubit(6)
                if t == 12:
                    name = b.string(); eb = b.ubit(5); ne = b.ubit(eb + 1); L = b.varint32()
                    fixed = b.bit(); ud = (b.ubit(12), b.ubit(4)) if fixed else None; comp = b.bit()
                    tables.append((name, eb, fixed, ud))
                    if name == 'instancebaseline' and not comp:
                        tb = BitReader(data); tb.pos = b.pos; tb.end = b.pos + L
                        for idx, s, u in read_entries(tb, ne, eb, fixed, ud, []):
                            if s is not None: names[idx] = s
                            if u: found[(names.get(idx), len(u), tick)] = u
                    b.skip(L); continue
                if t == 13:
                    tid = b.ubit(5); ch = b.word() if b.bit() else 1; L = b.ubit(20)
                    if tid < len(tables) and tables[tid][0] == 'instancebaseline':
                        name, eb, fixed, ud = tables[tid]
                        tb = BitReader(data); tb.pos = b.pos; tb.end = b.pos + L
                        try:
                            for idx, s, u in read_entries(tb, ch, eb, fixed, ud, []):
                                if s is not None: names[idx] = s
                                if u: found[(names.get(idx), len(u), tick)] = u
                        except EOFError:
                            pass
                    b.skip(L); continue
                msgs.parse_msg(b, t, {})
        except Exception:
            pass
    elif cmd == 3: pass
    elif cmd in (4, 6, 8): L = struct.unpack_from('<i', data, pos)[0]; pos += 4 + L
    elif cmd == 5: pos += 4; L = struct.unpack_from('<i', data, pos)[0]; pos += 4 + L
    else: break
print('baseline blobs', len(found))
cls = collections.Counter(k[0] for k in found)
print(cls.most_common(60))
pickle.dump(found, open(sys.argv[2], 'wb'))
