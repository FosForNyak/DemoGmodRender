import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
exec(open('tail.py').read().split("print('after'")[0])
base = b.pos
allbits = ''.join(str((data_ud[i >> 3] >> (i & 7)) & 1) for i in range(len(data_ud) * 8))
seg = allbits[base: base + 1881]
# print segment with 7-bit header candidates '1000000' marked
for i in range(0, len(seg), 96): print(f'{i:5d}', seg[i:i+96])
