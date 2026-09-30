import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
exec(open('baseline.py').read().split('def enc_tf2')[0])
from bits import BitReader
for s, ud in snap['instancebaseline']:
    if ud is None: continue
    cid = int(s)
    if classes[cid][1] != sys.argv[2]: continue
    b = BitReader(ud)
    bits = ''.join(str(b.bit()) for _ in range(int(sys.argv[3])))
    print(classes[cid][1], len(ud), 'bytes')
    for i in range(0, len(bits), 64): print(f'{i:5d}', bits[i:i+64])
