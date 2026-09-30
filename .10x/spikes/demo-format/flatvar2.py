import sys, os, itertools
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
exec(open('baseline.py').read().split('def enc_tf2')[0])
import ents as E
from bits import BitReader
F_EXCLUDE, F_INSIDEARRAY, F_COLLAPSIBLE, F_CO = 1<<6, 1<<8, 1<<12, 1<<10

def unsorted(name, drop_tables=()):
    excl = set(); E.gather_excludes(tables, name, excl)
    flat = []
    def build(tname):
        nondt = []; iterate(tname, nondt); flat.extend(nondt)
    def iterate(tname, nondt):
        for p in tables[tname]:
            if p['flags'] & (F_EXCLUDE | F_INSIDEARRAY): continue
            if (tname, p['name']) in excl: continue
            if p['type'] == 6:
                if p['dt'] in drop_tables: continue
                if p['flags'] & F_COLLAPSIBLE: iterate(p['dt'], nondt)
                else: build(p['dt'])
            else: nondt.append(p)
    build(name)
    return flat

def sort(flat, mode):
    flat = list(flat)
    if mode == 'tf2':
        s = 0
        for i in range(len(flat)):
            if flat[i]['flags'] & F_CO: flat[i], flat[s] = flat[s], flat[i]; s += 1
    elif mode == 'stable':
        flat = [p for p in flat if p['flags'] & F_CO] + [p for p in flat if not p['flags'] & F_CO]
    elif mode == 'tfX':  # displaced items inserted after the CO region instead of swapped far away
        co = [p for p in flat if p['flags'] & F_CO]
        n = len(co); head = [p for p in flat[:n] if not p['flags'] & F_CO]
        rest = [p for p in flat[n:] if not p['flags'] & F_CO]
        flat = co + rest[:0] + [p for p in flat if not p['flags'] & F_CO and p not in head][:0]
        # tfX: CO first, then non-CO from original positions >= n, with displaced head items inserted after first 4 remaining
        body = [p for p in flat[n:]]
        flat = co + body
    return flat

def decode_ok(ud, flat):
    b = BitReader(ud); idx = -1
    try:
        while b.bit():
            idx += 1 + E.read_ubitvar(b)
            if idx >= len(flat): return False
            E.dec(b, flat[idx])
    except Exception: return False
    return b.left() < 8

for drop, mode in itertools.product([(), ('DT_PredictableId',)], ['tf2', 'stable']):
    ok = []; bad = []
    for s, ud in snap['instancebaseline']:
        if ud is None: continue
        cid = int(s); flat = sort(unsorted(classes[cid][2], drop), mode)
        (ok if decode_ok(ud, flat) else bad).append(classes[cid][1])
    print(f'drop={drop} mode={mode}: ok={len(ok)} bad={bad[:6]}')
