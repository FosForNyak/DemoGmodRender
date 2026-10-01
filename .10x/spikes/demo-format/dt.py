import struct, sys, collections
sys.path.insert(0, sys.argv[0].rsplit('\\', 1)[0])
from bits import *
data = open(sys.argv[1], 'rb').read()
FLAGBITS = int(sys.argv[2]) if len(sys.argv) > 2 else 16
pos = 1072
while True:
    cmd = data[pos]; pos += 5
    if cmd in (1, 2): pos += 84; ln = struct.unpack_from('<i', data, pos)[0]; pos += 4 + ln
    elif cmd == 3: pass
    elif cmd == 6: ln = struct.unpack_from('<i', data, pos)[0]; pos += 4; break
    elif cmd in (4, 8): ln = struct.unpack_from('<i', data, pos)[0]; pos += 4 + ln
    elif cmd == 5: pos += 4; ln = struct.unpack_from('<i', data, pos)[0]; pos += 4 + ln
b = BitReader(data, pos, pos + ln)
tables = {}
types = collections.Counter(); flags_seen = 0
while b.bit():
    needs_decoder = b.bit()
    name = b.string()
    nprops = b.ubit(10)
    props = []
    for _ in range(nprops):
        t = b.ubit(5); pname = b.string(); fl = b.ubit(FLAGBITS)
        types[t] += 1; flags_seen |= fl
        p = dict(type=t, name=pname, flags=fl)
        if t == 6: p['dt'] = b.string()
        elif fl & (1 << 6): p['exclude'] = b.string()
        elif t == 5: p['elements'] = b.ubit(10)
        else:
            p['low'] = b.float(); p['high'] = b.float(); p['bits'] = b.ubit(7)
        props.append(p)
    tables[name] = props
print('tables parsed', len(tables), 'bits left before classes', b.left())
nclasses = b.word()
print('nclasses', nclasses)
classes = []
try:
    for _ in range(nclasses):
        cid = b.word(); cname = b.string(); dtname = b.string(); classes.append((cid, cname, dtname))
except EOFError as e:
    print('classes stopped at', len(classes), e)
print('tables', len(tables), 'classes', nclasses, 'bits left', b.left())
print('prop types', sorted(types.items()), 'flags union', bin(flags_seen))
print('first classes', classes[:6], '...', classes[-3:])
for nm in ('DT_BaseEntity', 'DT_BasePlayer', 'DT_GMOD_Player', 'DT_HL2MP_Player'):
    if nm in tables:
        print(nm, [(p['name'], p['type'], hex(p['flags']), p.get('bits'), p.get('dt')) for p in tables[nm][:14]])
gm = [c for c in classes if 'GMOD' in c[2] or 'gmod' in c[1].lower()]
print('gmod classes', gm[:10])
