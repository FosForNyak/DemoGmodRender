import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
exec(open('baseline.py').read().split('def enc_tf2')[0])
import ents as E
from bits import BitReader
want = sys.argv[2]
for s, ud in snap['instancebaseline']:
    if ud is None: continue
    cid = int(s)
    if classes[cid][1] != want: continue
    dt = classes[cid][2]; flat = E.flatten(tables, dt)
    b = BitReader(ud); idx = -1
    print(want, 'userdata bytes', len(ud))
    try:
        while b.bit():
            d = E.read_ubitvar(b); idx += 1 + d
            p = flat[idx]; st = b.pos; v = E.dec(b, p)
            print(f'  {idx:4d} +{d:<3d} {p["name"]:40s} t{p["type"]} {hex(p["flags"]):7s} bits={p.get("bits")} used={b.pos-st:3d} v={v if not isinstance(v, list) else ("arr", len(v), v[:4])}')
    except Exception as e: print('ERR', e)
    print('left', b.left())
