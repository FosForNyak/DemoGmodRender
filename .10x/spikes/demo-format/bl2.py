import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
exec(open('baseline.py').read().split('def enc_tf2')[0])
import ents as E
from bits import BitReader
want = sys.argv[2]; stop_after = int(sys.argv[3])
for s, ud in snap['instancebaseline']:
    if ud is None: continue
    cid = int(s)
    if classes[cid][1] != want: continue
    dt = classes[cid][2]; flat = E.flatten(tables, dt)
    b = BitReader(ud); idx = -1
    while b.bit():
        d = E.read_ubitvar(b); idx += 1 + d
        E.dec(b, flat[idx])
        if idx == stop_after: break
    print('after idx', idx, 'bitpos', b.pos)
    bits = ''.join(str(b.bit()) for _ in range(260))
    print('\n'.join(bits[i:i+65] for i in range(0, 260, 65)))
