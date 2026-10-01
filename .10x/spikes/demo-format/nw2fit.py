import sys, os, collections
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ref
CANDS = {
  'i32': lambda b: b.sbit(32), 'u32': lambda b: b.ubit(32), 'varint': lambda b: b.varint32(),
  'angle96': lambda b: (b.float(), b.float(), b.float()), 'ent13': lambda b: b.ubit(13), 'ent16': lambda b: b.ubit(16),
  'ehandle23': lambda b: b.ubit(23), 'ehandle32': lambda b: b.ubit(32), 'u8': lambda b: b.ubit(8), 'u16': lambda b: b.ubit(16),
  'f64': lambda b: (b.ubit(32), b.ubit(32)), 'i64': lambda b: (b.ubit(32), b.ubit(32)),
}
fixed = dict(x.split('=') for x in sys.argv[3:]) if len(sys.argv) > 3 else {}
target = int(sys.argv[2])
def make(choice):
    def dec_nw2(b):
        n = b.ubit(13); out = []
        for _ in range(n):
            key = b.ubit(12); typ = b.ubit(3); kind = ref.NW2_WIDTH.get(typ)
            if kind == 'float': v = b.float()
            elif kind == 'bool': v = b.bit()
            elif kind == 'vector': v = (b.float(), b.float(), b.float())
            elif kind == 'nil': v = None
            elif kind == 'string': v = bytes(b.ubit(8) for _ in range(b.ubit(9)))
            elif str(typ) in choice: v = CANDS[choice[str(typ)]](b)
            else: raise ValueError('unknown NW2 type %d' % typ)
            out.append((key, typ, v))
        return out
    return dec_nw2
res = []
for c in CANDS:
    ch = dict(fixed); ch[str(target)] = c
    ref.dec_nw2 = make(ch)
    d = ref.Demo(sys.argv[1]); d.run(int(os.environ.get('N', '3000')))
    errs = sum(d.errors.values())
    res.append((errs, c, dict(d.stats).get('enter'), [k for k in d.errors][:2]))
for r in sorted(res)[:6]: print(r)
