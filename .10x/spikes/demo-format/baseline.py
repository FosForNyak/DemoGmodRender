"""Try candidate prop-index encodings on instance baselines from dem_stringtables."""
import struct, sys
sys.path.insert(0, sys.argv[0].rsplit('\\', 1)[0])
from bits import *
import ents as E

data = open(sys.argv[1], 'rb').read()
pos = 1072
tables = classes = None
snap = {}
while True:
    cmd = data[pos]; pos += 5
    if cmd in (1, 2): pos += 84; L = struct.unpack_from('<i', data, pos)[0]; pos += 4 + L
    elif cmd == 3: pass
    elif cmd == 6:
        L = struct.unpack_from('<i', data, pos)[0]; pos += 4
        tables, classes = E.read_tables(BitReader(data, pos, pos + L)); pos += L
    elif cmd == 8:
        L = struct.unpack_from('<i', data, pos)[0]; pos += 4
        sb = BitReader(data, pos, pos + L); pos += L
        ntab = sb.byte()
        for _ in range(ntab):
            tname = sb.string(); ns = sb.word(); items = []
            for _ in range(ns):
                s = sb.string(); ud = None
                if sb.bit(): n = sb.word(); ud = bytes(sb.ubit(8) for _ in range(n))
                items.append((s, ud))
            if sb.bit():
                for _ in range(sb.word()):
                    sb.string()
                    if sb.bit(): n = sb.word(); sb.skip(n * 8)
            snap[tname] = items
            if tname == 'instancebaseline': break
        break
    elif cmd == 4: L = struct.unpack_from('<i', data, pos)[0]; pos += 4 + L
    elif cmd == 5: pos += 4; L = struct.unpack_from('<i', data, pos)[0]; pos += 4 + L

def enc_tf2(b):
    idx = -1
    while b.bit():
        idx += 1 + E.read_ubitvar(b); yield idx

def read_field_index(b, last, newway):
    if newway and b.bit(): return last + 1
    if newway and b.bit():
        ret = b.ubit(3)
    else:
        ret = b.ubit(7)
        s = ret & (32 | 64)
        if s == 32: ret = (ret & ~96) | (b.ubit(2) << 5)
        elif s == 64: ret = (ret & ~96) | (b.ubit(4) << 5)
        elif s == 96: ret = (ret & ~96) | (b.ubit(7) << 5)
    if ret == 0xFFF: return -1
    return last + 1 + ret

def try_csgo(b, flat):
    newway = b.bit()
    idxs = []; last = -1
    while True:
        last = read_field_index(b, last, newway)
        if last == -1: break
        if last >= len(flat): raise ValueError('idx %d' % last)
        idxs.append(last)
        if len(idxs) > 2000: raise ValueError('too many')
    for i in idxs: E.dec(b, flat[i])
    return idxs

def try_interleaved(b, flat, reader):
    idxs = []
    for i in reader(b):
        if i >= len(flat): raise ValueError('idx %d' % i)
        E.dec(b, flat[i]); idxs.append(i)
    return idxs

def old_ob(b):
    # guess: while bit: idx = last+1 if bit else ReadUBitLong(numprop bits)
    idx = -1
    while b.bit():
        if b.bit(): idx += 1
        else: idx = b.ubit(10)
        yield idx

cands = {
    'csgo_newway': lambda b, fl: try_csgo(b, fl),
    'tf2_ubitvar': lambda b, fl: try_interleaved(b, fl, enc_tf2),
    'old_ob': lambda b, fl: try_interleaved(b, fl, old_ob),
}
ok = {k: 0 for k in cands}
for s, ud in snap['instancebaseline']:
    if ud is None: continue
    cid = int(s); dt = classes[cid][2]; flat = E.flatten(tables, dt)
    for k, fn in cands.items():
        b = BitReader(ud)
        try:
            idxs = fn(b, flat)
            if b.left() < 8:
                ok[k] += 1
                if ok[k] <= 2: print(k, 'OK', classes[cid][1], 'props', len(idxs), 'left', b.left(), 'first', [flat[i]['name'] for i in idxs[:6]])
        except Exception as e:
            pass
print('baselines', sum(1 for s, u in snap['instancebaseline'] if u), 'ok per encoding', ok)
