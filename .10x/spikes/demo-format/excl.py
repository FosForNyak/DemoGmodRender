import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
exec(open('baseline.py').read().split('def enc_tf2')[0])
import ents as E
for dt in sys.argv[2:]:
    ex = set(); E.gather_excludes(tables, dt, ex); print(dt, sorted(ex))
# any table containing props with the CO flag other than the known ones
for tn, props in tables.items():
    for p in props:
        if p['flags'] & (1<<10) and p['name'] not in ('m_flAnimTime','m_flSimulationTime','m_vecOrigin','m_angRotation','m_iHealth') and not p['name'].startswith('m_vecVelocity'):
            print('CO', tn, p['name'], hex(p['flags']), p['type'])
