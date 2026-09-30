import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ref
from bits import BitReader
def pe(self, b):
    mx = b.ubit(13); delta = b.bit()
    if delta: b.long()
    which = b.bit(); upd = b.ubit(13); L = b.ubit(24); upd_base = b.bit()
    eb = BitReader(self.data); eb.pos = b.pos; eb.end = b.pos + L; b.skip(L)
    base = -1
    for n in range(upd):
        base = base + 1 + ref.read_ubitvar(eb); ix = base
        if not eb.bit():
            if eb.bit():
                cid = eb.ubit(self.class_bits); serial = eb.ubit(10)
                st = dict(self.instance_baseline(cid)); p0 = eb.pos
                fl = self.flat(cid); idx = -1; lastp = []
                try:
                    while eb.bit():
                        idx += 1 + ref.read_prop_delta(eb)
                        st[idx] = ref.dec(eb, fl[idx]); lastp.append((idx, fl[idx]['name'], fl[idx]['type'], str(st[idx])[:40]))
                except Exception as e:
                    print(f'  #{n} enter ix={ix} {self.classes[cid][1]} ERR {e}; last props {lastp[-6:]}'); raise SystemExit
                print(f'  #{n} enter ix={ix} {self.classes[cid][1]} bits={eb.pos-p0} nprops={len(lastp)} last={lastp[-3:]}')
                if self.classes[cid][1] in ('CPlayerResource',): [print('       ', x, fl[x[0]]['table'], hex(fl[x[0]]['flags']), fl[x[0]].get('bits')) for x in lastp]
                self.ents[ix] = dict(cid=cid, serial=serial, props=st)
            else:
                print(f'  #{n} DELTA ix={ix} (unexpected)'); raise SystemExit
        else:
            print(f'  #{n} leave ix={ix} del={eb.bit()}')
ref.Demo.packet_entities = pe
d = ref.Demo(sys.argv[1]); d.run(7)
