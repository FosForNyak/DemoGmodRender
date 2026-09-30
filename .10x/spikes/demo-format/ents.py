"""Spike: decode PacketEntities for the first N packets and print player positions."""
import struct, sys, math, collections
sys.path.insert(0, sys.argv[0].rsplit('\\', 1)[0])
from bits import *
import msgs

DPT_Int, DPT_Float, DPT_Vector, DPT_VectorXY, DPT_String, DPT_Array, DPT_DataTable, DPT_Int64 = range(8)
F_UNSIGNED, F_COORD, F_NOSCALE, F_ROUNDDOWN, F_ROUNDUP, F_NORMAL, F_EXCLUDE, F_XYZE, F_INSIDEARRAY = [1 << i for i in range(9)]
F_PROXY_ALWAYS, F_CHANGES_OFTEN, F_IS_VEC_ELEM, F_COLLAPSIBLE, F_COORD_MP, F_COORD_MP_LOW, F_COORD_MP_INT = [1 << i for i in range(9, 16)]

def read_tables(b):
    tables = {}
    while b.bit():
        b.bit()
        name = b.string(); n = b.ubit(10); props = []
        for _ in range(n):
            t = b.ubit(5); pname = b.string(); fl = b.ubit(16)
            p = dict(type=t, name=pname, flags=fl, table=name)
            if t == DPT_DataTable: p['dt'] = b.string()
            elif fl & F_EXCLUDE: p['exclude'] = b.string()
            elif t == DPT_Array: p['elements'] = b.ubit(10)
            else: p['low'] = b.float(); p['high'] = b.float(); p['bits'] = b.ubit(7)
            props.append(p)
        # array element = previous prop with INSIDEARRAY
        for i, p in enumerate(props):
            if p['type'] == DPT_Array: p['elem'] = props[i - 1]
        tables[name] = props
    nclasses = b.word(); classes = []
    for _ in range(nclasses):
        cid = b.word(); cname = b.string(); dt = b.string(); classes.append((cid, cname, dt))
    return tables, classes

def gather_excludes(tables, name, out):
    for p in tables[name]:
        if p['flags'] & F_EXCLUDE: out.add((p['exclude'], p['name']))
        elif p['type'] == DPT_DataTable: gather_excludes(tables, p['dt'], out)

def flatten(tables, name):
    excl = set(); gather_excludes(tables, name, excl)
    flat = []
    def build(tname):
        nondt = []
        iterate(tname, nondt)
        flat.extend(nondt)
    def iterate(tname, nondt):
        for p in tables[tname]:
            if p['flags'] & (F_EXCLUDE | F_INSIDEARRAY): continue
            if (tname, p['name']) in excl: continue
            if p['type'] == DPT_DataTable:
                if p['flags'] & F_COLLAPSIBLE: iterate(p['dt'], nondt)
                else: build(p['dt'])
            else: nondt.append(p)
    build(name)
    # SDK 2013: move CHANGES_OFTEN to the front (single pass swap)
    start = 0
    for i in range(len(flat)):
        if flat[i]['flags'] & F_CHANGES_OFTEN:
            flat[i], flat[start] = flat[start], flat[i]; start += 1
    return flat

def read_ubitvar(b):
    ret = b.ubit(6)
    s = ret & 48
    if s == 16: ret = (ret & 15) | (b.ubit(4) << 4)
    elif s == 32: ret = (ret & 15) | (b.ubit(8) << 4)
    elif s == 48: ret = (ret & 15) | (b.ubit(28) << 4)
    return ret

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
        iv = b.bit()
        if iv:
            sign = b.bit(); v = b.ubit(11 if inb else 14) + 1
            return -v if sign else float(v)
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

def dec(b, p):
    t = p['type']
    if t == DPT_Int:
        if p['flags'] & F_NORMAL:  # SPROP_VARINT reuses the NORMAL bit (as in CS:GO)
            v = b.varint32()
            return v if p['flags'] & F_UNSIGNED else ((v >> 1) ^ -(v & 1))
        return b.ubit(p['bits']) if p['flags'] & F_UNSIGNED else b.sbit(p['bits'])
    if t == DPT_Int64:
        if p['flags'] & F_NORMAL:
            v = 0
            for i in range(10):
                c = b.ubit(8); v |= (c & 0x7f) << (7 * i)
                if not c & 0x80: break
            return v if p['flags'] & F_UNSIGNED else ((v >> 1) ^ -(v & 1))
        if p['flags'] & F_UNSIGNED:
            lo = b.ubit(32); hi = b.ubit(p['bits'] - 32); return lo | (hi << 32)
        neg = b.bit(); lo = b.ubit(32); hi = b.ubit(p['bits'] - 32 - 1); v = lo | (hi << 32)
        return -v if neg else v
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
        mx = p['elements']; bits = log2(mx) + 1; n = b.ubit(bits)
        return [dec(b, p['elem']) for _ in range(n)]
    raise ValueError('type %d' % t)

def read_prop_delta(b):
    # SDK 2013 CDeltaBitsWriter::WritePropIndex: 1 has-more bit (read by caller), 2-bit width selector, then diff-1 in 4/8/12 bits
    s = b.ubit(2)
    return b.ubit(4 + 4 * s)

def read_props(b, flat, state):
    idx = -1
    while b.bit():
        idx += 1 + read_prop_delta(b)
        if idx >= len(flat): raise ValueError('prop index %d >= %d' % (idx, len(flat)))
        p = flat[idx]
        state[idx] = dec(b, p)

def main():
    fn = sys.argv[1]; limit = int(sys.argv[2])
    data = open(fn, 'rb').read()
    pos = 1072
    tables = classes = None
    flats = {}
    string_tables = {}  # name -> list of (string, userdata)
    ents = {}  # index -> dict(cls, serial, props)
    npk = 0
    baselines = {}
    server_class_bits = None
    report_ticks = set()
    while True:
        cmd = data[pos]; tick = struct.unpack_from('<i', data, pos + 1)[0]; pos += 5
        if cmd in (1, 2):
            pos += 84; ln = struct.unpack_from('<i', data, pos)[0]; pos += 4
            b = BitReader(data, pos, pos + ln); pos += ln; npk += 1
            try:
                while b.left() >= 6:
                    t = b.ubit(6)
                    if t == 26 and flats:
                        b.ubit(13); delta = b.bit()
                        if delta: b.long()
                        which = b.bit(); upd = b.ubit(13); L = b.ubit(24); ub = b.bit()
                        eb = BitReader(data); eb.pos = b.pos; eb.end = b.pos + L; b.skip(L)
                        base = -1
                        for _ in range(upd):
                            base = base + 1 + read_ubitvar(eb); ix = base
                            if not eb.bit():
                                if eb.bit():  # enter PVS
                                    cid = eb.ubit(server_class_bits); serial = eb.ubit(10)
                                    cname, dt = classes[cid][1], classes[cid][2]
                                    if dt not in flats: flats[dt] = flatten(tables, dt)
                                    st = {}
                                    if cid in baselines: st.update(baselines[cid])
                                    ents[ix] = dict(cls=cname, dt=dt, serial=serial, props=st)
                                    read_props(eb, flats[dt], st)
                                else:
                                    e = ents[ix]; read_props(eb, flats[e['dt']], e['props'])
                            else:
                                if eb.bit(): ents.pop(ix, None)
                        if delta:
                            while eb.bit(): ents.pop(eb.ubit(13), None)
                        continue
                    if t == 12:
                        name = b.string(); ebits = b.ubit(5); ne = b.ubit(ebits + 1); L = b.varint32()
                        fixed = b.bit(); ud = (b.ubit(12), b.ubit(4)) if fixed else None; comp = b.bit()
                        if name == 'instancebaseline' and not comp:
                            tb = BitReader(data); tb.pos = b.pos; tb.end = b.pos + L
                            string_tables[name] = read_table_entries(tb, ne, ebits, fixed, ud)
                        b.skip(L); continue
                    msgs.parse_msg(b, t, {})
            except Exception as ex:
                print('packet', npk, 'tick', tick, 'ERR', type(ex).__name__, ex)
                if flats: return report(ents, tick, flats)
            if npk >= limit: break
        elif cmd == 6:
            L = struct.unpack_from('<i', data, pos)[0]; pos += 4
            tables, classes = read_tables(BitReader(data, pos, pos + L)); pos += L
            server_class_bits = log2(len(classes)) + 1
            # decode instance baselines now that we have tables
            for s, udata in string_tables.get('instancebaseline', []):
                if s is None or udata is None: continue
                cid = int(s); dt = classes[cid][2]
                if dt not in flats: flats[dt] = flatten(tables, dt)
                st = {}; read_props(BitReader(udata), flats[dt], st); baselines[cid] = st
            flats.setdefault('__ready__', [])
        elif cmd == 3: pass
        elif cmd == 8:
            L = struct.unpack_from('<i', data, pos)[0]; pos += 4
            sb = BitReader(data, pos, pos + L); pos += L
            ntab = sb.byte(); snap = {}
            try:
                for _ in range(ntab):
                    tname = sb.string(); ns = sb.word(); items = []
                    snap[tname] = items
                    for _ in range(ns):
                        s = sb.string(); ud = None
                        if sb.bit(): n = sb.word(); ud = bytes(sb.ubit(8) for _ in range(n))
                        items.append((s, ud))
                    if sb.bit():
                        for _ in range(sb.word()):
                            sb.string()
                            if sb.bit(): n = sb.word(); sb.skip(n * 8)
            except EOFError:
                print('dem_stringtables TRUNCATED in table', tname, 'after', len(items), 'items of', ns)
            print('dem_stringtables:', {k: len(v) for k, v in snap.items()})
            baselines.clear()
            for s, udata in snap.get('instancebaseline', []):
                if udata is None: continue
                cid = int(s); dt = classes[cid][2]
                if dt not in flats: flats[dt] = flatten(tables, dt)
                st = {}; read_props(BitReader(udata), flats[dt], st); baselines[cid] = st
            print('baselines decoded', len(baselines))
        elif cmd == 4: L = struct.unpack_from('<i', data, pos)[0]; pos += 4 + L
        elif cmd == 5: pos += 4; L = struct.unpack_from('<i', data, pos)[0]; pos += 4 + L
        else: break
    report(ents, tick, flats)

def read_table_entries(b, n, ebits, fixed, ud):
    out = []; last = -1; hist = []
    for _ in range(n):
        idx = last + 1
        if not b.bit(): idx = b.ubit(ebits)
        last = idx
        s = None
        if b.bit():
            if b.bit():
                h = b.ubit(5); cnt = b.ubit(5)
                s = hist[h][:cnt] + b.string()
            else: s = b.string()
        udata = None
        if b.bit():
            if fixed: nbits = ud[1]; udata = bytes([b.ubit(nbits)])
            else:
                nbytes = b.ubit(14); udata = bytes(b.ubit(8) for _ in range(nbytes))
        hist.append(s or ''); hist = hist[-32:]
        while len(out) <= idx: out.append((None, None))
        out[idx] = (s, udata)
    return out

def report(ents, tick, flats):
    print('tick', tick, 'entities', len(ents))
    by = collections.Counter(e['cls'] for e in ents.values()); print(by.most_common(12))
    for ix, e in sorted(ents.items()):
        if e['cls'] == 'CGMOD_Player':
            fl = flats[e['dt']]
            vals = {fl[i]['name']: v for i, v in e['props'].items()}
            org = vals.get('m_vecOrigin'); z = vals.get('m_vecOrigin[2]')
            print(' player', ix, 'origin', org, z, 'health', vals.get('m_iHealth'), 'eye', vals.get('m_angEyeAngles[0]'), vals.get('m_angEyeAngles[1]'))

if __name__ == '__main__':
    main()
