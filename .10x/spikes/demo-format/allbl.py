import sys, os, pickle
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
exec(open('baseline.py').read().split('def enc_tf2')[0])
import ents as E
from bits import BitReader
found = pickle.load(open(sys.argv[2], 'rb'))
ok = bad = 0; badl = []
seen = set()
for k, ud in found.items():
    if (k[0], len(ud)) in seen: continue
    seen.add((k[0], len(ud)))
    cid = int(k[0]); flat = E.flatten(tables, classes[cid][2])
    st = {}; b = BitReader(ud)
    try:
        E.read_props(b, flat, st)
        good = b.left() < 8
    except Exception as e:
        good = False
    if good: ok += 1
    else: bad += 1; badl.append((classes[cid][1], b.left()))
print('baselines ok', ok, 'bad', bad, badl[:10])
