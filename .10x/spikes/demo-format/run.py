import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
exec(open('flatvar.py').read().split('def score')[0])
want = sys.argv[2]
for s, ud in snap['instancebaseline']:
    if ud is None: continue
    cid = int(s)
    if classes[cid][1] != want: continue
    flat = flatten(classes[cid][2], child_first=True, sort='tf2')
    b = BitReader(ud); idx = -1
    while b.bit():
        d = E.read_ubitvar(b); idx += 1 + d; E.dec(b, flat[idx])
        if idx == 12: break
    # now: read '1' + ubitvar + 1-bit value repeatedly while pattern holds
    n = 0; deltas = []
    while True:
        save = b.pos
        if not b.bit(): print('END marker after', n); break
        d = E.read_ubitvar(b); deltas.append(d)
        v = b.ubit(1)
        if d != 0 and n > 0: b.pos = save; break
        n += 1
        if n > 200: break
    print('run of 1-bit props:', n, 'first deltas', deltas[:3], 'next deltas', deltas[-2:])
    print('next 120 bits:', ''.join(str(b.bit()) for _ in range(120)))
