import sys, os, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
exec(open('tail.py').read().split("print('after'")[0])
start = b.pos
allbits = ''.join(str((data_ud[i >> 3] >> (i & 7)) & 1) for i in range(len(data_ud) * 8))
def lsb(v, n): return ''.join(str((v >> i) & 1) for i in range(n))
pats = {'f1.0': lsb(0x3F800000, 32), 'ffffffff': '1' * 32, 'f-1.0': lsb(0xBF800000, 32)}
for name, pt in pats.items():
    i = allbits.find(pt, start); hits = []
    while i != -1 and len(hits) < 12: hits.append(i - start); i = allbits.find(pt, i + 1)
    print(name, hits)
# markers: positions of "1000000" (has-more + delta 0) after start
