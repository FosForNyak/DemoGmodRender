"""Collect PacketEntities messages that update exactly one entity: exact prop-stream bit ranges."""
import struct, sys, os, collections, pickle
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from bits import *
import msgs, ents as E

data = open(sys.argv[1], 'rb').read()
limit = int(sys.argv[2])
pos = 1072; npk = 0
classes = None; server_class_bits = None
idx_class = {}
out = []  # (tick, entity index, class name or None, kind, bit_start, bit_end)
stats = collections.Counter()
while True:
    cmd = data[pos]; tick = struct.unpack_from('<i', data, pos + 1)[0]; pos += 5
    if cmd in (1, 2):
        pos += 84; ln = struct.unpack_from('<i', data, pos)[0]; pos += 4
        b = BitReader(data, pos, pos + ln); pos += ln; npk += 1
        try:
            while b.left() >= 6:
                t = b.ubit(6)
                if t == 26 and classes:
                    b.ubit(13); delta = b.bit()
                    if delta: b.long()
                    b.bit(); upd = b.ubit(13); L = b.ubit(24); b.bit()
                    start = b.pos; b.skip(L)
                    stats['pe'] += 1
                    if upd == 1:
                        eb = BitReader(data); eb.pos = start; eb.end = start + L
                        ix = -1 + 1 + E.read_ubitvar(eb)
                        if not eb.bit():
                            if eb.bit():
                                cid = eb.ubit(server_class_bits); serial = eb.ubit(10)
                                idx_class[ix] = classes[cid][1]; kind = 'enter'
                            else:
                                kind = 'delta'
                            # prop stream starts here; ends 1 bit (end marker) before the tail
                            tail = 1 if delta else 0  # explicit-delete loop terminator
                            out.append((tick, ix, idx_class.get(ix), kind, eb.pos, start + L - tail))
                            stats[kind] += 1
                        else:
                            stats['leave'] += 1
                    elif upd > 1:
                        # we can still learn enters' class ids only if parseable; skip
                        pass
                    continue
                msgs.parse_msg(b, t, {})
        except Exception as ex:
            stats['err'] += 1
        if npk >= limit: break
    elif cmd == 6:
        L = struct.unpack_from('<i', data, pos)[0]; pos += 4
        tables, classes = E.read_tables(BitReader(data, pos, pos + L)); pos += L
        server_class_bits = log2(len(classes)) + 1
    elif cmd == 3: pass
    elif cmd in (4, 8): L = struct.unpack_from('<i', data, pos)[0]; pos += 4 + L
    elif cmd == 5: pos += 4; L = struct.unpack_from('<i', data, pos)[0]; pos += 4 + L
    else: break
print(stats)
by = collections.Counter((c, k) for _, _, c, k, _, _ in out)
print(by.most_common(20))
pickle.dump(out, open(sys.argv[3], 'wb'))
