import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
exec(open('tail.py').read().split("print('after'")[0])
base = b.pos
end_rel = int(sys.argv[4]); first_idx = int(sys.argv[5]); last_idx = int(sys.argv[6])
# enumerate all header-consistent parses from base to base+end_rel using model props first_idx..last_idx in order, allowing index skips
import functools
sys.setrecursionlimit(5000)
sols = []
deep = [[]]
def rec(pos, idx, path):
    if len(path) > len(deep[0]): deep[0] = list(path)
    if len(sols) > 5: return
    if pos == base + end_rel:
        if idx == last_idx: sols.append(list(path))
        return
    if pos > base + end_rel: return
    t = BitReader(data_ud); t.pos = pos
    if not t.bit(): return
    d = E.read_ubitvar(t); ni = idx + 1 + d
    if ni > last_idx: return
    if flat[ni]['type'] == 7:
        for w in range(0, 80):
            path.append((ni, flat[ni]['name'], t.pos - base, f'w={w}'))
            rec(t.pos + w, ni, path); path.pop()
        return
    try:
        v = E.dec(t, flat[ni])
    except Exception: return
    path.append((ni, flat[ni]['name'], t.pos - base, v if not isinstance(v, list) else ('arr', len(v))))
    rec(t.pos, ni, path); path.pop()
rec(base, 240, [])
print('solutions', len(sols)); [print('  deep', r) for r in deep[0]]
for s in sols[:3]:
    for r in s: print('  ', r)
    print('--')
