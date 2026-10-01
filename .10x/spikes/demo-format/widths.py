import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
exec(open('baseline.py').read().split('def enc_tf2')[0])
import ents as E
from bits import BitReader
want = sys.argv[2]; start_idx = int(sys.argv[3]); cands = [int(x) for x in sys.argv[4].split(',')]
for s, ud in snap['instancebaseline']:
    if ud and classes[int(s)][1] == want: data_ud = ud; cid = int(s)
flat = E.flatten(tables, classes[cid][2])
# replay a known-good prefix using the CO/specSurr/bools layout: walk markers blindly with candidate widths
b = BitReader(data_ud); idx = -1; out = []
def peek_ok(pos):
    t = BitReader(data_ud); t.pos = pos
    if t.left() < 7: return True
    if not t.bit(): return t.left() < 8
    d = E.read_ubitvar(t); return d < 40
while b.bit():
    idx += 1 + E.read_ubitvar(b)
    here = b.pos
    w = None
    for c in cands:
        if peek_ok(here + c):
            # disambiguate: prefer the smallest width that also keeps the following marker valid
            t = BitReader(data_ud); t.pos = here + c
            w = c; break
    if w is None: out.append((idx, '?')); break
    val = BitReader(data_ud); val.pos = here; v = val.ubit(min(w, 32))
    out.append((idx, w, v)); b.pos = here + w
    if idx > 400: break
runs = []
for r in out:
    if runs and runs[-1][2] == r[1] and runs[-1][1] == r[0] - 1: runs[-1][1] = r[0]; runs[-1][3] += (1 if r[2] else 0) if len(r) > 2 else 0
    else: runs.append([r[0], r[0], r[1], (1 if len(r) > 2 and r[2] else 0)])
for a, z, w, nz in runs: print(f'{a:4d}-{z:4d} ({z-a+1:3d}) width={w} nonzero={nz}')
print('left', b.left())
