import sys, os, collections
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ref
from bits import BitReader
# Monkeypatch dec_nw2: on unknown type, try a set of candidate readers and keep the first that lets the whole packet decode
CANDS = {
  'i32': lambda b: b.sbit(32), 'u32': lambda b: b.ubit(32), 'varint': lambda b: b.varint32(),
  'angle96': lambda b: (b.float(), b.float(), b.float()), 'ent13': lambda b: b.ubit(13), 'ent16': lambda b: b.ubit(16),
  'ehandle23': lambda b: b.ubit(23), 'ehandle32': lambda b: b.ubit(32), 'str': lambda b: b.string(),
  'str_len16': lambda b: bytes(b.ubit(8) for _ in range(b.ubit(16))), 'str_len9': lambda b: bytes(b.ubit(8) for _ in range(b.ubit(9))),
  'u8': lambda b: b.ubit(8), 'u16': lambda b: b.ubit(16), 'f32': lambda b: b.float(),
}
choice = {}
orig = ref.dec_nw2
def dec_nw2(b):
    n = b.ubit(13); out = []
    for _ in range(n):
        key = b.ubit(12); typ = b.ubit(3)
        kind = ref.NW2_WIDTH.get(typ)
        if kind == 'float': v = b.float()
        elif kind == 'bool': v = b.bit()
        elif kind == 'vector': v = (b.float(), b.float(), b.float())
        elif typ in choice: v = CANDS[choice[typ]](b)
        else: raise LookupError((typ, key, b.pos))
        out.append((key, typ, v))
    return out
ref.dec_nw2 = dec_nw2
d = ref.Demo(sys.argv[1])
import struct
# run until first LookupError, then retry that packet with each candidate
class Stop(Exception): pass
target = None
def run_packet_with(choice_kind, typ):
    choice[typ] = choice_kind
    dd = ref.Demo(sys.argv[1])
    dd.run(int(sys.argv[2]))
    del choice[typ]
    return dd
dd = ref.Demo(sys.argv[1]); dd.run(int(sys.argv[2]))
errs = [k for k in dd.errors if 'LookupError' in k]
print('first errors', list(dd.errors.items())[:3])
for kind in CANDS:
    dd = run_packet_with(kind, 0)
    bad = sum(dd.errors.values())
    print(f'{kind:10s} errors={bad} first={list(dd.errors)[:1]} stats={dict(dd.stats)}')
