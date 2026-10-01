"""Reference decoder (spike): full entity state for GMod protocol-24 demos.

Confirmed format facts are encoded here; the C++ implementation mirrors this file.
"""
import struct, sys, os, math, collections
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from bits import BitReader, log2

# ---- props -----------------------------------------------------------------
DPT_Int, DPT_Float, DPT_Vector, DPT_VectorXY, DPT_String, DPT_Array, DPT_DataTable, DPT_GModTable = range(8)
F_UNSIGNED, F_COORD, F_NOSCALE, F_ROUNDDOWN, F_ROUNDUP, F_NORMAL, F_EXCLUDE, F_XYZE, F_INSIDEARRAY = [1 << i for i in range(9)]
F_PROXY_ALWAYS, F_CHANGES_OFTEN, F_IS_VEC_ELEM, F_COLLAPSIBLE, F_COORD_MP, F_COORD_MP_LOW, F_COORD_MP_INT = [1 << i for i in range(9, 16)]
F_VARINT = F_NORMAL  # GMod reuses the NORMAL bit for varint ints (like CS:GO)

NW2_WIDTH = {0: 'nil', 1: 'float', 2: 'int', 3: 'bool', 4: 'vector', 5: 'angle', 6: 'entity', 7: 'string'}  # 5 unconfirmed
unknown_nw2 = collections.Counter()


def read_tables(b):
    tables = {}
    while b.bit():
        b.bit()  # needs decoder
        name = b.string(); n = b.ubit(10); props = []
        for _ in range(n):
            t = b.ubit(5); pname = b.string(); fl = b.ubit(16)
            p = dict(type=t, name=pname, flags=fl, table=name)
            if t == DPT_DataTable: p['dt'] = b.string()
            elif fl & F_EXCLUDE: p['exclude'] = b.string()
            elif t == DPT_Array: p['elements'] = b.ubit(10)
            else: p['low'] = b.float(); p['high'] = b.float(); p['bits'] = b.ubit(7)
            props.append(p)
        for i, p in enumerate(props):
            if p['type'] == DPT_Array: p['elem'] = props[i - 1]
        tables[name] = props
    nclasses = b.word(); classes = []
    for _ in range(nclasses):
        cid = b.word(); cname = b.string(); dt = b.string(); classes.append((cid, cname, dt))
    return tables, classes


def flatten(tables, name):
    excl = set()
    def gather(t):
        for p in tables[t]:
            if p['flags'] & F_EXCLUDE: excl.add((p['exclude'], p['name']))
            elif p['type'] == DPT_DataTable: gather(p['dt'])
    gather(name)
    flat = []
    def build(t):
        own = []; iterate(t, own); flat.extend(own)
    def iterate(t, own):
        for p in tables[t]:
            if p['flags'] & (F_EXCLUDE | F_INSIDEARRAY) or (t, p['name']) in excl: continue
            if p['type'] == DPT_DataTable:
                if p['flags'] & F_COLLAPSIBLE: iterate(p['dt'], own)
                else: build(p['dt'])
            else: own.append(p)
    build(name)
    start = 0  # SDK 2013 single-pass swap of CHANGES_OFTEN props to the front
    for i in range(len(flat)):
        if flat[i]['flags'] & F_CHANGES_OFTEN:
            flat[i], flat[start] = flat[start], flat[i]; start += 1
    return flat


def read_ubitvar(b):
    # SDK 2013 bf_read::ReadUBitVar: low 2 bits = encoding, value in 4 / 8 / 12 / 32 bits
    six = b.ubit(6); enc = six & 3
    if enc == 0: return six >> 2
    b.pos -= 4
    return b.ubit((8, 12, 32)[enc - 1])


def read_prop_delta(b):
    return read_ubitvar(b)


def bit_coord(b):
    iv = b.bit(); fv = b.bit(); v = 0.0
    if iv or fv:
        sign = b.bit()
        if iv: iv = b.ubit(14) + 1
        if fv: fv = b.ubit(5)
        v = iv + fv / 32.0
        if sign: v = -v
    return v


def bit_coord_mp(b, integral, low):
    inb = b.bit()
    if integral:
        if b.bit():
            sign = b.bit(); v = b.ubit(11 if inb else 14) + 1
            return float(-v if sign else v)
        return 0.0
    iv = b.bit(); sign = b.bit()
    if iv: iv = b.ubit(11 if inb else 14) + 1
    fv = b.ubit(3 if low else 5)
    v = iv + fv * (1 / 8.0 if low else 1 / 32.0)
    return -v if sign else v


def bit_normal(b):
    sign = b.bit(); fv = b.ubit(11); v = fv * (1.0 / ((1 << 11) - 1))
    return -v if sign else v


def dec_float(b, p):
    fl = p['flags']
    if fl & F_COORD: return bit_coord(b)
    if fl & (F_COORD_MP | F_COORD_MP_LOW | F_COORD_MP_INT):
        return bit_coord_mp(b, bool(fl & F_COORD_MP_INT), bool(fl & F_COORD_MP_LOW))
    if fl & F_NOSCALE: return b.float()
    if fl & F_NORMAL: return bit_normal(b)
    n = p['bits']; v = b.ubit(n)
    return p['low'] + (p['high'] - p['low']) * (v / ((1 << n) - 1)) if n else p['low']


nw2_flags = collections.Counter()

def dec_nw2(b):
    v = b.ubit(13); n = v & 4095; out = []
    if v >> 12: nw2_flags[n] += 1
    for _ in range(n):
        key = b.ubit(12); typ = b.ubit(3)
        kind = NW2_WIDTH.get(typ)
        if kind == 'float': v = b.float()
        elif kind == 'bool': v = b.bit()
        elif kind == 'vector': v = (b.float(), b.float(), b.float())
        elif kind == 'nil': v = None
        elif kind == 'int': v = b.sbit(32)
        elif kind == 'angle': v = (b.float(), b.float(), b.float())
        elif kind == 'entity': v = b.ubit(23)
        elif kind == 'string': v = bytes(b.ubit(8) for _ in range(b.ubit(9))).decode('utf-8', 'replace')
        else:
            unknown_nw2[typ] += 1
            raise ValueError('unknown NW2 type %d' % typ)
        out.append((key, typ, v))
    return out


def dec(b, p):
    t = p['type']
    if t == DPT_Int:
        if p['flags'] & F_VARINT:
            v = b.varint32()
            return v if p['flags'] & F_UNSIGNED else ((v >> 1) ^ -(v & 1))
        return b.ubit(p['bits']) if p['flags'] & F_UNSIGNED else b.sbit(p['bits'])
    if t == DPT_Float: return dec_float(b, p)
    if t == DPT_Vector:
        x = dec_float(b, p); y = dec_float(b, p)
        if p['flags'] & F_NORMAL:
            sign = b.bit(); s = x * x + y * y; z = math.sqrt(1 - s) if s < 1 else 0.0
            return (x, y, -z if sign else z)
        return (x, y, dec_float(b, p))
    if t == DPT_VectorXY: return (dec_float(b, p), dec_float(b, p))
    if t == DPT_String:
        n = b.ubit(9); return bytes(b.ubit(8) for _ in range(n)).decode('utf-8', 'replace')
    if t == DPT_Array:
        n = b.ubit(log2(p['elements']) + 1)
        return [dec(b, p['elem']) for _ in range(n)]
    if t == DPT_GModTable: return dec_nw2(b)
    raise ValueError('prop type %d' % t)


def read_props(b, flat, state):
    idx = -1
    while b.bit():
        idx += 1 + read_prop_delta(b)
        if idx >= len(flat): raise ValueError('prop index %d >= %d' % (idx, len(flat)))
        state[idx] = dec(b, flat[idx])


# ---- string tables -----------------------------------------------------------
class StringTable:
    def __init__(self, name, max_entries_bits, fixed, ud):
        self.name = name; self.eb = max_entries_bits; self.fixed = fixed; self.ud = ud
        self.entries = {}  # index -> [string, userdata]

    def parse(self, b, n):
        last = -1; hist = []; changed = []
        for _ in range(n):
            idx = last + 1
            if not b.bit(): idx = b.ubit(self.eb)
            last = idx; s = None
            if b.bit():
                if b.bit(): h = b.ubit(5); c = b.ubit(5); s = hist[h][:c] + b.string()
                else: s = b.string()
            u = None
            if b.bit():
                if self.fixed: u = bytes([b.ubit(self.ud[1])])
                else: nb = b.ubit(19); u = bytes(b.ubit(8) for _ in range(nb))
            e = self.entries.setdefault(idx, [None, None])
            if s is not None: e[0] = s
            if u is not None: e[1] = u
            hist.append(e[0] or ''); del hist[:-32]
            changed.append(idx)
        return changed


def lzss(data):
    if data[:4] != b'LZSS': raise ValueError('not LZSS')
    size = struct.unpack_from('<I', data, 4)[0]; src = 8; out = bytearray(); cmd = 0; get = 0
    while True:
        if not get: cmd = data[src]; src += 1
        get = (get + 1) & 7
        if cmd & 1:
            pos = data[src] << 4 | data[src + 1] >> 4; cnt = (data[src + 1] & 15) + 1; src += 2
            if cnt == 1: break
            start = len(out) - pos - 1
            for i in range(cnt): out.append(out[start + i])
        else:
            out.append(data[src]); src += 1
        cmd >>= 1
    if len(out) != size: raise ValueError('lzss size %d != %d' % (len(out), size))
    return bytes(out)


# ---- demo -------------------------------------------------------------------
class Demo:
    def __init__(self, path):
        self.data = open(path, 'rb').read()
        self.tables = self.classes = None; self.flats = {}; self.class_bits = None
        self.st = []  # string tables by id
        self.ents = {}  # index -> dict(cid, serial, props)
        self.inst_baselines = {}  # (cid, blob) -> decoded
        self.ent_baselines = [{}, {}]  # baseline set -> index -> (cid, props)
        self.errors = collections.Counter(); self.stats = collections.Counter()

    def flat(self, cid):
        dt = self.classes[cid][2]
        if dt not in self.flats: self.flats[dt] = flatten(self.tables, dt)
        return self.flats[dt]

    def table(self, name):
        return next((t for t in self.st if t.name == name), None)

    def instance_baseline(self, cid):
        t = self.table('instancebaseline')
        blob = None
        for idx, (s, u) in t.entries.items():
            if s == str(cid): blob = u
        if blob is None: return {}
        key = (cid, blob)
        if key not in self.inst_baselines:
            st = {}; read_props(BitReader(blob), self.flat(cid), st); self.inst_baselines[key] = st
        return self.inst_baselines[key]

    def packet_entities(self, b):
        mx = b.ubit(13); delta = b.bit()
        if delta: b.long()
        which = b.bit(); upd = b.ubit(13); L = b.ubit(24); upd_base = b.bit()
        eb = BitReader(self.data); eb.pos = b.pos; eb.end = b.pos + L; b.skip(L)
        base = -1
        for _ in range(upd):
            base = base + 1 + read_ubitvar(eb); ix = base
            if not eb.bit():
                if eb.bit():  # enter PVS
                    cid = eb.ubit(self.class_bits); serial = eb.ubit(10)
                    old = self.ent_baselines[which].get(ix)
                    st = dict(old[1]) if old and old[0] == cid else dict(self.instance_baseline(cid))
                    read_props(eb, self.flat(cid), st)
                    self.ents[ix] = dict(cid=cid, serial=serial, props=st)
                    if upd_base: self.ent_baselines[which ^ 1][ix] = (cid, dict(st))
                    self.stats['enter'] += 1
                else:  # delta
                    e = self.ents[ix]; read_props(eb, self.flat(e['cid']), e['props']); self.stats['delta'] += 1
            else:
                if eb.bit(): self.ents.pop(ix, None); self.stats['delete'] += 1
                self.stats['leave'] += 1
        if delta:
            while eb.bit(): self.ents.pop(eb.ubit(13), None); self.stats['explicit_delete'] += 1
        if eb.left() > 7: raise ValueError('packet entities leftover %d bits' % eb.left())

    def string_table_create(self, b):
        name = b.string(); eb = b.ubit(5); ne = b.ubit(eb + 1); L = b.varint32()
        fixed = b.bit(); ud = (b.ubit(12), b.ubit(4)) if fixed else None; comp = b.bit()
        t = StringTable(name, eb, fixed, ud); self.st.append(t)
        start = b.pos; b.skip(L)
        if comp:
            tb = BitReader(self.data); tb.pos = start
            dsize = tb.ubit(32); csize = tb.ubit(32)
            blob = bytes(tb.ubit(8) for _ in range(csize))
            tb = BitReader(lzss(blob))
        else:
            tb = BitReader(self.data); tb.pos = start; tb.end = start + L
        t.parse(tb, ne)

    def string_table_update(self, b):
        tid = b.ubit(5); ch = b.word() if b.bit() else 1; L = b.ubit(20)
        tb = BitReader(self.data); tb.pos = b.pos; tb.end = b.pos + L; b.skip(L)
        self.st[tid].parse(tb, ch)

    def run(self, limit, on_tick=None):
        import msgs
        pos = 1072; npk = 0
        while pos < len(self.data):
            cmd = self.data[pos]; tick = struct.unpack_from('<i', self.data, pos + 1)[0]; pos += 5
            if cmd in (1, 2):
                pos += 84; ln = struct.unpack_from('<i', self.data, pos)[0]; pos += 4
                b = BitReader(self.data, pos, pos + ln); pos += ln; npk += 1
                try:
                    while b.left() >= 6:
                        t = b.ubit(6)
                        if t == 26 and self.tables: self.packet_entities(b); continue
                        if t == 12: self.string_table_create(b); continue
                        if t == 13: self.string_table_update(b); continue
                        msgs.parse_msg(b, t, {})
                except Exception as ex:
                    self.errors[type(ex).__name__ + ': ' + str(ex)[:60]] += 1
                    if sum(self.errors.values()) <= 5: print('ERR packet', npk, 'tick', tick, ex)
                if on_tick and cmd == 2: on_tick(self, tick)
                if npk >= limit: break
            elif cmd == 6:
                L = struct.unpack_from('<i', self.data, pos)[0]; pos += 4
                self.tables, self.classes = read_tables(BitReader(self.data, pos, pos + L)); pos += L
                self.class_bits = log2(len(self.classes)) + 1
            elif cmd == 3: pass
            elif cmd in (4, 8): L = struct.unpack_from('<i', self.data, pos)[0]; pos += 4 + L
            elif cmd == 5: pos += 4; L = struct.unpack_from('<i', self.data, pos)[0]; pos += 4 + L
            elif cmd == 7: break
        return npk


if __name__ == '__main__':
    d = Demo(sys.argv[1]); limit = int(sys.argv[2])
    jumps = collections.Counter(); last = {}
    def on_tick(d, tick):
        for ix, e in d.ents.items():
            if d.classes[e['cid']][1] != 'CGMOD_Player': continue
            fl = d.flat(e['cid'])
            org = next((v for i, v in e['props'].items() if fl[i]['name'] == 'm_vecOrigin'), None)
            if org is None: continue
            if ix in last:
                dx = math.dist(org[:2] if len(org) == 2 else org, last[ix][:2] if len(org) == 2 else last[ix])
                if dx > 1000: jumps[ix] += 1
            last[ix] = org
    import time; t0 = time.time()
    n = d.run(limit, on_tick)
    print('packets', n, 'time %.1fs' % (time.time() - t0))
    print('errors', dict(d.errors)); print('stats', dict(d.stats)); print('unknown NW2 types', dict(unknown_nw2), 'nw2 top-bit flag with counts', dict(nw2_flags))
    print('entities alive', len(d.ents), collections.Counter(d.classes[e['cid']][1] for e in d.ents.values()).most_common(8))
    print('player origin jumps >1000', dict(jumps))
    # show a few players
    for ix, e in sorted(d.ents.items()):
        if d.classes[e['cid']][1] == 'CGMOD_Player':
            fl = d.flat(e['cid']); pr = {fl[i]['name']: v for i, v in e['props'].items()}
            print(' player', ix, {k: pr.get(k) for k in ('m_vecOrigin', 'm_vecOrigin[2]', 'm_iHealth', 'm_angEyeAngles[0]', 'm_angEyeAngles[1]')})
