import sys, os, itertools
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
exec(open('baseline.py').read().split('def enc_tf2')[0])
import ents as E
from bits import BitReader
F_EXCLUDE, F_INSIDEARRAY, F_COLLAPSIBLE, F_CO = 1<<6, 1<<8, 1<<12, 1<<10

def flatten(name, child_first=True, sort='tf2', excl_mode='global', collapse=True):
    excl = set(); E.gather_excludes(tables, name, excl)
    flat = []
    def build(tname):
        nondt = []
        iterate(tname, nondt)
        if child_first: flat.extend(nondt)
        else: pass
        return nondt
    def iterate(tname, nondt):
        for p in tables[tname]:
            if p['flags'] & (F_EXCLUDE | F_INSIDEARRAY): continue
            if excl_mode == 'global' and (tname, p['name']) in excl: continue
            if p['type'] == 6:
                if collapse and p['flags'] & F_COLLAPSIBLE: iterate(p['dt'], nondt)
                else:
                    if child_first: build(p['dt'])
                    else: nondt.append(('CHILD', p['dt']))
            else: nondt.append(p)
    if child_first:
        build(name)
    else:
        def emit(tname):
            nondt = []; iterate(tname, nondt)
            own = [x for x in nondt if not isinstance(x, tuple)]
            flat.extend(own)
            for x in nondt:
                if isinstance(x, tuple): emit(x[1])
        emit(name)
    if sort == 'tf2':
        start = 0
        for i in range(len(flat)):
            if flat[i]['flags'] & F_CO:
                flat[i], flat[start] = flat[start], flat[i]; start += 1
    elif sort == 'stable':
        flat = [p for p in flat if p['flags'] & F_CO] + [p for p in flat if not p['flags'] & F_CO]
    return flat

def score(**kw):
    ok = 0; fails = []
    for s, ud in snap['instancebaseline']:
        if ud is None: continue
        cid = int(s); dt = classes[cid][2]
        flat = flatten(dt, **kw)
        b = BitReader(ud); idx = -1; good = True
        try:
            while b.bit():
                idx += 1 + E.read_ubitvar(b)
                if idx >= len(flat): good = False; break
                E.dec(b, flat[idx])
        except Exception: good = False
        if good and b.left() < 8: ok += 1
        else: fails.append(classes[cid][1])
    return ok, fails

for cf, so in itertools.product([True, False], ['tf2', 'stable', 'none']):
    ok, fails = score(child_first=cf, sort=so)
    print(f'child_first={cf!s:5} sort={so:6} ok={ok}/34  fails={fails[:5]}')
