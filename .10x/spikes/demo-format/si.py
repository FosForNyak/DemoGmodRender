import struct, sys
sys.path.insert(0, sys.argv[0].rsplit('/',1)[0])
from bits import *
data = open(sys.argv[1],'rb').read()
p = 1072+5+84+4; ln = struct.unpack_from('<i', data, 1072+5+84)[0]
b = BitReader(data, p, p+ln)
b.ubit(6); b.string(); b.ubit(6)
b.word(); b.long(); b.bit(); b.bit(); b.ubit(32); b.word(); b.bytes_(16); b.byte(); b.byte(); b.float(); b.byte()
for _ in range(4): b.string()
start = b.pos; end = b.end
print('remaining bits', end-start)
# dump remaining as raw bits and try strings at every offset
raw = []
bb = BitReader(data, p, p+ln); bb.pos = start
while bb.left() > 0: raw.append(bb.bit())
print(''.join(map(str, raw)))
for k in range(0, 40):
    t = BitReader(data, p, p+ln); t.pos = start + k
    try:
        chars = bytearray()
        while t.left() >= 8 and len(chars) < 80:
            c = t.byte()
            if c == 0: break
            chars.append(c)
        if len(chars) >= 3 and all(32 <= c < 127 for c in chars): print('k', k, 'string', chars.decode(), 'then pos rel', t.pos-start)
    except Exception as e: pass
