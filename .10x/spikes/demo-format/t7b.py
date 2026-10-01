import sys, os, pickle
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
exec(open('baseline.py').read().split('def enc_tf2')[0])
import ents as E
from bits import BitReader
found = pickle.load(open(sys.argv[2], 'rb'))
seen = set()
for k, ud in found.items():
    if k[0] not in ('72', '130'): continue
    if (k[0], len(ud)) in seen: continue
    seen.add((k[0], len(ud)))
    cid = int(k[0]); flat = E.flatten(tables, classes[cid][2])
    b = BitReader(ud); idx = -1
    try:
        while b.bit():
            idx += 1 + E.read_prop_delta(b)
            p = flat[idx]
            if p['type'] == 7:
                pos = b.pos
                bits = ''.join(str(b.bit()) for _ in range(min(80, b.left())))
                # candidate widths: after w bits we should see the next header '1'+sel+delta and the rest should decode
                oks = []
                L = BitReader(ud); L.pos = pos; n13 = L.ubit(13)
                print('   13-bit value', n13)
                for w in list(range(0, 64)) + [13 + n13, 13 + 8 * n13, 13 + 16 * n13, 13 + 32 * n13, 13 + 64 * n13] + list(range(64, 1200)):
                    t = BitReader(ud); t.pos = pos + w; j = idx; good = True
                    try:
                        while t.bit():
                            j += 1 + E.read_prop_delta(t); E.dec(t, flat[j])
                        good = t.left() < 8
                    except Exception: good = False
                    if good: oks.append(w)
                    if len(oks) > 3: break
                print(f'{classes[cid][1]:22s} t7 at idx {idx} widths_ok={oks} bits={bits[:40]}')
                break
            E.dec(b, p)
    except Exception as e:
        print(classes[cid][1], 'ERR before t7', e)
