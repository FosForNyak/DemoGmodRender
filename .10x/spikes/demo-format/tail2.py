import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
exec(open('flatvar2.py').read().split('def decode_ok')[0])
want = sys.argv[2]; shift = int(sys.argv[3]); stop = int(sys.argv[4])
for s, ud in snap['instancebaseline']:
    if ud and classes[int(s)][1] == want: data_ud = ud; cid = int(s)
flat = sort(unsorted(classes[cid][2], ('DT_PredictableId',)), 'stable')
# data index = model index + shift for idx >= first CO...: decode data using flat[idx - shift] when idx>=shift
b = BitReader(data_ud); idx = -1
while b.bit():
    d = E.read_ubitvar(b); idx += 1 + d
    j = (idx - 3 if 4 <= idx <= 10 else (0 if idx == 11 else idx - 4)) if shift == 99 else idx - shift
    if j < 0: raise SystemExit('neg')
    v = E.dec(b, flat[j])
    if idx == stop: break
print('after data idx', idx, 'model', j, flat[j]['name'], 'bitpos', b.pos)
base = b.pos
allbits = ''.join(str((data_ud[i >> 3] >> (i & 7)) & 1) for i in range(len(data_ud) * 8))
def lsb(v, n): return ''.join(str((v >> i) & 1) for i in range(n))
for name, pt in {'ffffffff': '1'*32, 'ehandle3': ('1'*23 + '1000000') * 2 + '1'*23}.items():
    i = allbits.find(pt, base); print(name, i - base if i >= 0 else None)
