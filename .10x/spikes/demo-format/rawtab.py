import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
exec(open('baseline.py').read().split('def enc_tf2')[0])
for nm in sys.argv[2:]:
    print('==', nm, len(tables[nm]))
    for i, p in enumerate(tables[nm]):
        print(f'  {i:3d} {p["name"]:36s} t{p["type"]} {hex(p["flags"]):7s} bits={p.get("bits")} el={p.get("elements")} dt={p.get("dt")} ex={p.get("exclude")}')
