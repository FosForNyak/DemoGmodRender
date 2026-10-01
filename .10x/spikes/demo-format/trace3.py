import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
exec(open('flatvar2.py').read().split('def decode_ok')[0])
want = sys.argv[2]; mode = sys.argv[3]; skip_zero = len(sys.argv) > 4
for s, ud in snap['instancebaseline']:
    if ud and classes[int(s)][1] == want: data_ud = ud; cid = int(s)
flat = sort(unsorted(classes[cid][2], ('DT_PredictableId',)), mode)
b = BitReader(data_ud); idx = -1
try:
    while b.bit():
        d = E.read_ubitvar(b); idx += 1 + d
        p = flat[idx]; st = b.pos; v = E.dec(b, p)
        z = v in (0, 0.0, '', (0.0, 0.0, 0.0), [])
        if not (skip_zero and z and d == 0):
            vv = repr(v if not isinstance(v, list) else ('arr', len(v), v[:3])).encode('ascii', 'replace').decode()[:70]
            print(f'  {idx:4d} +{d:<3d} {p["name"]:34s} {p["table"]:26s} t{p["type"]} {hex(p["flags"]):7s} b={p.get("bits")} used={b.pos-st:3d} {vv}')
except Exception as e: print('ERR', e)
print('left', b.left())
