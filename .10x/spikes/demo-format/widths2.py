import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
exec(open('baseline.py').read().split('def enc_tf2')[0])
import ents as E
from bits import BitReader
want = sys.argv[2]; prefix = sys.argv[3].split(',') if sys.argv[3] != '-' else []
cands = [int(x) for x in '1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,20,21,23,24,31,32,72,96'.split(',')]
for s, ud in snap['instancebaseline']:
    if ud and classes[int(s)][1] == want: data_ud = ud; cid = int(s)
D = {'i8': dict(type=0, flags=1, bits=8), 'cvec': dict(type=2, flags=2, bits=0, low=0, high=0), 'v24': dict(type=2, flags=8, bits=24, low=0.0, high=360.0),
     'vi': dict(type=0, flags=0x20, bits=32), 'f32': dict(type=1, flags=4, bits=0), 'f15': dict(type=1, flags=8, bits=15, low=-1.0, high=1.0), 'v96': dict(type=2, flags=4, bits=0)}
b = BitReader(data_ud); idx = -1; out = []
def peek_ok(pos):
    t = BitReader(data_ud); t.pos = pos
    if t.left() < 7: return True
    if not t.bit(): return t.left() < 8
    return E.read_ubitvar(t) < 40
k = 0
while b.bit():
    idx += 1 + E.read_ubitvar(b); here = b.pos
    if k < len(prefix):
        v = E.dec(b, D[prefix[k]]); out.append((idx, prefix[k], v)); k += 1; continue
    w = next((c for c in cands if peek_ok(here + c)), None)
    if w is None: out.append((idx, '?', None)); break
    val = BitReader(data_ud); val.pos = here; v = val.ubit(min(w, 32))
    out.append((idx, w, v)); b.pos = here + w
for r in out: print(r)
print('left', b.left())
