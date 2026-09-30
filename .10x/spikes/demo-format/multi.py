import sys, os, subprocess, re
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
exec(open('sig.py').read().split('SIG = {')[0])
name2dt = {c[1]: c[2] for c in classes}
import ents as E
from bits import BitReader
# quick greedy marker walk: find index of first coord-vector (origin) and first run of >=20 one-bit props
def summary(ud):
    out = subprocess.run([sys.executable, 'infer.py', sys.argv[1], CN, '120'], capture_output=True, text=True, encoding='utf-8', errors='replace').stdout
    rows = [l for l in out.splitlines() if re.match(r'\s+\d+ ', l)]
    return rows
for CN in sys.argv[2:]:
    rows = summary(None)
    idx_desc = [(int(r.split()[0]), r) for r in rows]
    first = idx_desc[0][0] if idx_desc else None
    origin = next((i for i, r in idx_desc if "fl=0x2 " in r and ' t2 ' in r), None)
    bools = next((i for i, r in idx_desc if ' t0 fl=0x1 bits=1 ' in r), None)
    flat = sort(unsorted(name2dt[CN], ('DT_PredictableId',)), 'stable')
    exp_origin = next(i for i, p in enumerate(flat) if p['name'] == 'm_vecOrigin' and p['table'] == 'DT_BaseEntity')
    exp_bool = next(i for i, p in enumerate(flat) if p['table'] == 'm_GMOD_bool')
    co = [p['name'] for p in flat if p['flags'] & F_CO]
    print(f'{CN:22s} first={first} origin={origin} (exp {exp_origin}) bools={bools} (exp {exp_bool}) CO={co}')
