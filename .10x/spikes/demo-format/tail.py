import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
exec(open('flatvar2.py').read().split('def decode_ok')[0])
want = sys.argv[2]; stop = int(sys.argv[3])
for s, ud in snap['instancebaseline']:
    if ud and classes[int(s)][1] == want: data_ud = ud; cid = int(s)
flat = sort(unsorted(classes[cid][2], ('DT_PredictableId',)), 'stable')
b = BitReader(data_ud); idx = -1
while b.bit():
    d = E.read_ubitvar(b); idx += 1 + d; E.dec(b, flat[idx])
    if idx == stop: break
print('after', idx, 'bitpos', b.pos, 'of', len(data_ud) * 8)
bits = ''.join(str(b.bit()) for _ in range(min(400, b.left())))
for i in range(0, len(bits), 80): print(bits[i:i+80])
