import struct, sys
sys.path.insert(0, sys.argv[0].rsplit('/',1)[0])
from bits import *
data = open(sys.argv[1],'rb').read()
p = 1072+5+84+4; ln = struct.unpack_from('<i', data, 1072+5+84)[0]
b = BitReader(data, p, p+ln)
b.ubit(6); b.string(); b.ubit(6)
b.word(); b.long(); b.bit(); b.bit(); b.ubit(32); b.word(); b.bytes_(16); b.byte(); b.byte(); b.float(); b.byte()
for _ in range(4): b.string()
url = b.string(); print('url', url)
tailstart = b.pos
for skip in range(0, 12):
    t = BitReader(data, p, p+ln); t.pos = tailstart + skip
    try:
        s = t.string()
        if s and all(32 <= ord(c) < 127 for c in s):
            rest = []
            while t.left() > 0: rest.append(t.bit())
            print('skip', skip, 'string', repr(s), 'rest', len(rest), ''.join(map(str, rest)))
    except Exception as e: pass
