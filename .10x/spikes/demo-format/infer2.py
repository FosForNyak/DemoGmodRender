"""Infer the real flattened prop order of a class from its instance baseline by search."""
import sys, os, collections
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
exec(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'baseline.py')).read().split('def enc_tf2')[0])
import ents as E
from bits import BitReader
sys.setrecursionlimit(20000)

import pickle
want = sys.argv[2]
if want.endswith('.pkl'):
    found = pickle.load(open(want, 'rb'))
    keysl = [k for k in found if k[0] == sys.argv[4]]
    k = keysl[int(sys.argv[5])]
    cid = int(k[0]); data_ud = found[k]; want = classes[cid][1] + f'@{k[2]}'
else:
    for s, ud in snap['instancebaseline']:
        if ud and classes[int(s)][1] == want:
            cid = int(s); data_ud = ud
dt = classes[cid][2]
flat = E.flatten(tables, dt)

def key(p):
    k = (p['type'], p['flags'] & ~(1 << 10), p.get('bits'), p.get('low'), p.get('high'), p.get('elements'))
    if p['type'] == 5: k += (key(p['elem']),)
    return k
groups = collections.OrderedDict()
for p in flat: groups.setdefault(key(p), []).append(p['name'])
keys = list(groups)
rep = {key(p): p for p in flat}
avail = {k: len(v) for k, v in groups.items()}
print(want, dt, 'props', len(flat), 'descriptor groups', len(keys), 'bytes', len(data_ud))

total_bits = len(data_ud) * 8
best = [None, -1]
path = []
deepest=[None]
nodes=[0]
def dfs(pos, idx, depth):
    nodes[0]+=1
    if nodes[0] > 200000: return False
    b = BitReader(data_ud); b.pos = pos
    if b.left() <= 0: return False
    if not b.bit():
        if b.left() < 8:
            best[0] = list(path); return True
        return False
    d = E.read_ubitvar(b); ni = idx + 1 + d
    if ni >= len(flat) or d > 2000: return False
    here = b.pos
    for k in keys:
        if avail[k] == 0: continue
        bb = BitReader(data_ud); bb.pos = here
        try:
            v = E.dec(bb, rep[k])
        except Exception:
            continue
        # quick check of the next marker
        if bb.left() <= 0: continue
        nxt = BitReader(data_ud); nxt.pos = bb.pos
        if not nxt.bit() and nxt.left() >= 8: continue
        avail[k] -= 1; path.append((ni, k, v))
        if len(path) > best[1]: best[1] = len(path); deepest[0] = list(path)
        if dfs(bb.pos, ni, depth + 1): return True
        path.pop(); avail[k] += 1
    return False

ok = dfs(0, -1, 0)
print('solved' if ok else 'NOT solved', 'props decoded', len(best[0]) if best[0] else None)
res = best[0] or deepest[0]
print('deepest', best[1])
if res:
    for ni, k, v in res[:int(sys.argv[3]) if len(sys.argv) > 3 else 80]:
        names = groups[k]
        vs = v if not isinstance(v, (list, tuple, str)) or len(v) < 5 else ('seq', len(v))
        vs = repr(vs).encode('ascii','replace').decode()
        print(f'  {ni:4d} {names[0]:34s} (+{len(names)-1} same) t{k[0]} fl={hex(k[1])} bits={k[2]} v={vs}')
