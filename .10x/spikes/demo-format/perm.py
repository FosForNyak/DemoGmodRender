import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
exec(open('tail.py').read().split("print('after'")[0])
base = b.pos
end_rel = int(sys.argv[4]); lo = int(sys.argv[5]); hi = int(sys.argv[6])
cand = list(range(lo, hi + 1))
sys.setrecursionlimit(5000)
best = [[]]; sols = []; nodes = [0]
def rec(pos, used, path, nsent_slots):
    nodes[0] += 1
    if nodes[0] > 3_000_000 or len(sols) > 3: return
    if len(path) > len(best[0]): best[0] = list(path)
    if pos == base + end_rel: sols.append(list(path)); return
    if pos > base + end_rel: return
    t = BitReader(data_ud); t.pos = pos
    if not t.bit(): return
    d = E.read_ubitvar(t)
    if d > 10: return
    hp = t.pos
    tried = set()
    for j in cand:
        if j in used: continue
        p = flat[j]
        key = (p['type'], p['flags'], p.get('bits'), p.get('elements'))
        if key in tried: continue
        tried.add(key)
        ws = range(0, 80) if p['type'] == 7 else [None]
        for w in ws:
            t2 = BitReader(data_ud); t2.pos = hp
            try:
                if w is None: v = E.dec(t2, p)
                else: t2.pos += w; v = f'w={w}'
            except Exception: continue
            nb = BitReader(data_ud); nb.pos = t2.pos
            if nb.left() <= 0: continue
            if t2.pos != base + end_rel and not nb.bit(): continue
            used.add(j); path.append((d, p['name'], t2.pos - base, v if not isinstance(v, list) else ('arr', len(v))))
            rec(t2.pos, used, path, nsent_slots)
            path.pop(); used.discard(j)
rec(base, set(), [], 0)
print('nodes', nodes[0], 'solutions', len(sols))
show = sols[0] if sols else best[0]
for r in show: print('  ', r)
