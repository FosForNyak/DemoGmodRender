import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
exec(open('tail.py').read().split("print('after'")[0])
base = b.pos; target = int(sys.argv[4]); maxn = int(sys.argv[5])
allbits = ''.join(str((data_ud[i >> 3] >> (i & 7)) & 1) for i in range(len(data_ud) * 8))
def hdr(p):
    t = BitReader(data_ud); t.pos = base + p
    if not t.bit(): return None
    return E.read_ubitvar(t), t.pos - base
res = []
def rec(p, chain):
    if len(res) > 40: return
    if p == target: res.append(list(chain)); return
    if p > target or len(chain) >= maxn: return
    h = hdr(p)
    if not h: return
    d, vp = h
    if d > 4: return
    for w in range(1, target - vp + 1):
        if vp + w == target or (hdr(vp + w) is not None):
            chain.append((d, w)); rec(vp + w, chain); chain.pop()
rec(0, [])
print(len(res), 'chains')
for c in sorted(res, key=len)[:15]: print(c)
