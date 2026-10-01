import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
exec(open('flatvar.py').read().split('def score')[0])
fl = flatten(sys.argv[2], child_first=True, sort='none')
for i, p in enumerate(fl[:int(sys.argv[3])]):
    print(i, p['name'], hex(p['flags']), 'CO' if p['flags'] & F_CO else '', p['table'])
