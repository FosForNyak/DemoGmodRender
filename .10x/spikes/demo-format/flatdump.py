import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
exec(open('baseline.py').read().split('def enc_tf2')[0])
import ents as E
for dtn in sys.argv[2:]:
    fl = E.flatten(tables, dtn)
    print(dtn, len(fl))
    for i,p in enumerate(fl[:int(os.environ.get("N","16"))]): print(' ', i, p['name'], p['type'], hex(p['flags']), p.get('bits'), p.get('low'), p.get('high'), p['table'])
