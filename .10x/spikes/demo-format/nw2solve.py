import sys, os, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
exec(open('dbg7.py').read().split("ref.Demo.packet_entities = pe")[0])
captured = {}
def dec_nw2_cap(b):
    start = b.pos; n = b.ubit(13)
    if n > 5 and 'player' not in captured:
        captured['player'] = (start, n, b.d)
        raise StopIteration
    out = []
    for _ in range(n):
        key = b.ubit(12); typ = b.ubit(3)
        kind = ref.NW2_WIDTH.get(typ)
        if kind == 'float': v = b.float()
        elif kind == 'bool': v = b.bit()
        elif kind == 'vector': v = (b.float(), b.float(), b.float())
        else: raise LookupError(typ)
        out.append((key, typ, v))
    return out
ref.dec_nw2 = dec_nw2_cap
dd = ref.Demo(sys.argv[1]); dd.run(8)
start, n, data = captured['player']
print('nw2 entries', n)
def rd(pos, nbits):
    v = 0
    for i in range(nbits): v |= ((data[(pos + i) >> 3] >> ((pos + i) & 7)) & 1) << i
    return v
NAMES = ['m_bProp','m_bDoor','m_bButton','m_eDoorState','m_bLocked','alium.firstconnect','m_fNoclipSpeed','m_vPlayerColor','m_flSpeedModifier','m_fUseDistance','ash.noclip','m_vAim','m_iPlayerKeys','m_fUseStartTime','m_eUseEntity','m_sFlashlightColor','m_bFlashlight','m_bInPrisonerPod']
W = {1: [32], 3: [1], 4: [96]}
def strlen_opts(pos):
    # candidates: null-terminated; 16-bit length prefix bytes; 9-bit; 32-bit
    out = []
    p = pos
    for _ in range(300):
        if rd(p, 8) == 0: out.append(('cstr', p + 8 - pos)); break
        p += 8
    for lb in (8, 9, 16, 32):
        L = rd(pos, lb)
        if L < 300: out.append((f'len{lb}', lb + 8 * L))
    return out
sols = []
def rec(pos, i, path, tw):
    if len(sols) > 20: return
    if i == n: sols.append((list(path), dict(tw), pos)); return
    key = rd(pos, 12); typ = rd(pos + 12, 3)
    if key >= 64: return
    p = pos + 15
    if typ in W: opts = [(None, w) for w in W[typ]]
    elif typ in tw: opts = [tw[typ]] if not str(tw[typ][0]).startswith(('cstr','len')) else [o for o in strlen_opts(p) if o[0] == tw[typ][0]]
    else: opts = [(f'w{w}', w) for w in range(0, 65)] + strlen_opts(p)
    for kind, w in opts:
        tw2 = dict(tw)
        if typ not in W: tw2[typ] = (kind, w) if kind and not kind.startswith('w') else (kind, w)
        path.append((key, typ, kind, w)); rec(p + w, i + 1, path, tw2); path.pop()
rec(start + 13, 0, [], {})
print('solutions', len(sols))
for path, tw, end in sols[:6]:
    print('types', tw)
    for key, typ, kind, w in path:
        print(f'   {NAMES[key] if key < len(NAMES) else key:20s} t{typ} {kind} w={w}')
    print('--')
