import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
exec(open('nw2probe.py').read().split("dd = ref.Demo(sys.argv[1]); dd.run(int(sys.argv[2]))")[0])
choice[0] = sys.argv[3]
orig_pe = ref.Demo.packet_entities
def pe(self, b):
    mx = b.ubit(13); delta = b.bit()
    if delta: b.long()
    which = b.bit(); upd = b.ubit(13); L = b.ubit(24); upd_base = b.bit()
    print('PE delta', delta, 'upd', upd, 'L', L)
    eb = BitReader(self.data); eb.pos = b.pos; eb.end = b.pos + L; b.skip(L)
    base = -1
    for n in range(upd):
        base = base + 1 + ref.read_ubitvar(eb); ix = base
        f1 = eb.bit()
        if not f1:
            if eb.bit():
                cid = eb.ubit(self.class_bits); serial = eb.ubit(10)
                st = dict(self.instance_baseline(cid)); p0 = eb.pos
                try:
                    ref.read_props(eb, self.flat(cid), st)
                except Exception as e:
                    print(f'  #{n} enter ix={ix} {self.classes[cid][1]} ERR {e}'); return
                fl = self.flat(cid)
                nw = [(fl[i]['name'], v) for i, v in st.items() if fl[i]['type'] == 7 and v]
                org = next((v for i, v in st.items() if fl[i]['name'] == 'm_vecOrigin'), None)
                print(f'  #{n} enter ix={ix} {self.classes[cid][1]} serial={serial} bits={eb.pos-p0} origin={org} nw2={nw}')
                self.ents[ix] = dict(cid=cid, serial=serial, props=st)
            else:
                print(f'  #{n} DELTA ix={ix} (unexpected in full update)'); return
        else:
            print(f'  #{n} leave ix={ix} del={eb.bit()}')
        if n > 12: return
ref.Demo.packet_entities = pe
dd = ref.Demo(sys.argv[1]); dd.run(8)
