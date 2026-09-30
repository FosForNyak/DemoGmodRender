import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
exec(open('baseline.py').read().split('def enc_tf2')[0])
print('class ids == list index:', all(c[0] == i for i, c in enumerate(classes)))
print([(s, classes[int(s)][1], len(u) if u else None) for s, u in snap['instancebaseline']])
