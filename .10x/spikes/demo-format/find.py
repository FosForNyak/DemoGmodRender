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
pkt = data[pos:pos + ln]
nbits = ln * 8
# build bit array (LSB-first)
bits = bytearray(nbits)
for i in range(nbits):
    bits[i] = (pkt[i >> 3] >> (i & 7)) & 1
def byte_at(i):
    v = 0
    for k in range(8): v |= bits[i + k] << k
    return v
names = [b'modelprecache', b'genericprecache', b'soundprecache', b'decalprecache', b'instancebaseline', b'lightstyles', b'userinfo', b'server_query_info', b'ParticleEffectNames', b'EffectDispatch', b'VguiScreen', b'Materials', b'InfoPanel', b'Scenes', b'networkstring', b'downloadables', b'DynamicModels']
first = [nm[0] for nm in names]
for i in range(0, nbits - 8 * 20):
    c = byte_at(i)
    if c not in first: continue
    for nm in names:
        ok = True
        for k, ch in enumerate(nm):
            if byte_at(i + 8 * k) != ch: ok = False; break
        if ok and byte_at(i + 8 * len(nm)) == 0:
            print('found', nm.decode(), 'at bit', i, 'type bits before:', ''.join(str(bits[j]) for j in range(i - 6, i)), 'val', sum(bits[i - 6 + k] << k for k in range(6)))
