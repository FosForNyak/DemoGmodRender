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
found = [('downloadables', 76), ('modelprecache', 2773), ('genericprecache', 38096), ('soundprecache', 38285), ('decalprecache', 64969),
         ('instancebaseline', 74264), ('lightstyles', 75756), ('userinfo', 82329), ('DynamicModels', 94303), ('server_query_info', 94464),
         ('ParticleEffectNames', 94775), ('EffectDispatch', 96451), ('VguiScreen', 97089), ('Materials', 97207), ('Scenes', 97989), ('networkstring', 220068)]
for idx, (nm, at) in enumerate(found):
    fstart = at + 8 * (len(nm) + 1)
    nxt = found[idx + 1][1] - 6 if idx + 1 < len(found) else None
    b = BitReader(data, pos, pos + ln); b.pos = pos * 8 + fstart
    raw = ''.join(str(b.bit()) for _ in range(72))
    b.pos = pos * 8 + fstart
    mx = b.word(); eb = log2(mx); ne = b.ubit(eb + 1)
    after_ne = b.pos - pos * 8
    v = b.varint32(); after_v = b.pos - pos * 8
    fixed = b.bit()
    extra = (b.ubit(12), b.ubit(4)) if fixed else None
    comp = b.bit()
    data_start = b.pos - pos * 8
    span = (nxt - data_start) if nxt else None
    print(f'{nm:20s} max={mx:6d} ne={ne:6d} varlen={v:8d} fixed={fixed} {extra} comp={comp} data_start={data_start} bits_to_next={span}')
    print('   ', raw)
