import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
exec(open('tail.py').read().split("print('after'")[0])
base = b.pos
for s in range(-40, 60):
    t = BitReader(data_ud); t.pos = base; idx = 240; n = 0; ok = True; last = None
    try:
        while t.bit():
            idx += 1 + E.read_ubitvar(t)
            j = idx + s
            if j < 0 or j >= len(flat): ok = False; break
            E.dec(t, flat[j]); n += 1; last = flat[j]['name']
    except Exception as e: ok = False
    if ok and t.left() < 8: print('SHIFT', s, 'decoded', n, 'left', t.left(), 'last', last)
    elif n > 20: print('shift', s, 'partial', n, 'left', t.left(), 'last', last)
