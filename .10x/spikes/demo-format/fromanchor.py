import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
exec(open('tail.py').read().split("print('after'")[0])
base = b.pos
rel = int(sys.argv[4]); model_idx = int(sys.argv[5])
t = BitReader(data_ud); t.pos = base + rel   # position of the value
idx = model_idx
out = []
p = flat[idx]; v = E.dec(t, p); out.append((idx, p['name'], v))
try:
    while t.bit():
        idx += 1 + E.read_ubitvar(t); p = flat[idx]; v = E.dec(t, p); out.append((idx, p['name'], v))
except Exception as e: out.append(('ERR', str(e)))
for r in out: print(r)
print('left', t.left())
