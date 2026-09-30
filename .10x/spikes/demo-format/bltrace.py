import sys
import os
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
exec(open(os.path.join(HERE, 'baseline.py')).read().split('def enc_tf2')[0])
import ents as E
from bits import BitReader
shown = 0
for s, ud in snap['instancebaseline']:
    if ud is None: continue
    cid = int(s); dt = classes[cid][2]; flat = E.flatten(tables, dt)
    b = BitReader(ud)
    idx = -1; log = []; err = None
    try:
        while b.bit():
            idx += 1 + E.read_ubitvar(b)
            if idx >= len(flat): raise ValueError('idx %d >= %d' % (idx, len(flat)))
            p = flat[idx]; start = b.pos
            v = E.dec(b, p)
            log.append((idx, p['name'], p['type'], hex(p['flags']), p.get('bits'), b.pos - start, v if not isinstance(v, list) else ('arr', len(v))))
    except Exception as e:
        err = str(e)
    if err or b.left() >= 8:
        shown += 1
        print('==', classes[cid][1], dt, 'flat', len(flat), 'err', err, 'left', b.left())
        for row in log[-40:]: print('   ', row)
        if shown >= 1: break
