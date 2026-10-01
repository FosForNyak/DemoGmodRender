import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ref
from bits import BitReader
target = int(sys.argv[2])
orig = ref.Demo.packet_entities
state = {'n': 0}
def pe(self, b):
    state['n'] += 1
    if self._npk != target: return orig(self, b)
    mx = b.ubit(13); delta = b.bit()
    if delta: b.long()
    which = b.bit(); upd = b.ubit(13); L = b.ubit(24); upd_base = b.bit()
    eb = BitReader(self.data); eb.pos = b.pos; eb.end = b.pos + L; b.skip(L)
    base = -1
    for n in range(upd):
        base = base + 1 + ref.read_ubitvar(eb); ix = base
        if not eb.bit():
            enter = eb.bit()
            if enter:
                cid = eb.ubit(self.class_bits); serial = eb.ubit(10); st = dict(self.instance_baseline(cid))
            else:
                cid = self.ents[ix]['cid']; st = self.ents[ix]['props']
            fl = self.flat(cid); idx = -1; log = []
            try:
                while eb.bit():
                    idx += 1 + ref.read_prop_delta(eb)
                    p = fl[idx]; st[idx] = ref.dec(eb, p); log.append((idx, p['name'], p['type'], hex(p['flags']), str(st[idx])[:50]))
            except Exception as e:
                print(f'#{n} ix={ix} {"enter" if enter else "delta"} {self.classes[cid][1]} ERR {e}')
                for r in log[-10:]: print('    ', r)
                raise
            print(f'#{n} ix={ix} {"enter" if enter else "delta"} {self.classes[cid][1]} props={[r[1] for r in log][:8]}')
        else:
            print(f'#{n} ix={ix} leave del={eb.bit()}')
ref.Demo.packet_entities = pe
# track packet number
orig_run = ref.Demo.run
d = ref.Demo(sys.argv[1])
import struct, msgs
d._npk = 0
_orig_create = ref.Demo.string_table_create
def run(self, limit):
    pos = 1072; npk = 0
    while pos < len(self.data):
        cmd = self.data[pos]; tick = struct.unpack_from('<i', self.data, pos + 1)[0]; pos += 5
        if cmd in (1, 2):
            pos += 84; ln = struct.unpack_from('<i', self.data, pos)[0]; pos += 4
            b = BitReader(self.data, pos, pos + ln); pos += ln; npk += 1; self._npk = npk
            try:
                while b.left() >= 6:
                    t = b.ubit(6)
                    if t == 26 and self.tables: self.packet_entities(b); continue
                    if t == 12: self.string_table_create(b); continue
                    if t == 13: self.string_table_update(b); continue
                    msgs.parse_msg(b, t, {})
            except Exception as ex:
                if npk == target: raise
            if npk >= limit: break
        elif cmd == 6:
            L = struct.unpack_from('<i', self.data, pos)[0]; pos += 4
            self.tables, self.classes = ref.read_tables(BitReader(self.data, pos, pos + L)); pos += L
            self.class_bits = ref.log2(len(self.classes)) + 1
        elif cmd == 3: pass
        elif cmd in (4, 8): L = struct.unpack_from('<i', self.data, pos)[0]; pos += 4 + L
        elif cmd == 5: pos += 4; L = struct.unpack_from('<i', self.data, pos)[0]; pos += 4 + L
        elif cmd == 7: break
run(d, target)
