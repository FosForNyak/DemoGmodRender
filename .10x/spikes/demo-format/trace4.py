import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
exec(open('baseline.py').read().split('def enc_tf2')[0])
import ents as E
from bits import BitReader
want = sys.argv[2]; n = int(sys.argv[3])
for s, ud in snap['instancebaseline']:
    if ud and classes[int(s)][1] == want: data_ud = ud; cid = int(s)
flat = E.flatten(tables, classes[cid][2])
b = BitReader(data_ud); idx = -1; log = []
try:
    while b.bit():
        d = E.read_prop_delta(b); idx += 1 + d
        p = flat[idx]; st = b.pos; v = E.dec(b, p)
        vv = repr(v if not isinstance(v, list) else ('arr', len(v), v[:4])).encode('ascii', 'replace').decode()[:60]
        log.append(f'  {idx:4d} +{d:<3d} {p["name"]:32s} {p["table"]:24s} t{p["type"]} {hex(p["flags"]):7s} b={p.get("bits")} used={b.pos-st:4d} {vv}')
except Exception as e: log.append(f'ERR {e}')
print('\n'.join(log[-n:])); print('left', b.left(), 'total props', len(flat))
