import struct, sys
data = open(sys.argv[1], 'rb').read()
lo, hi = int(sys.argv[2]), int(sys.argv[3])
pos = 1072; n = 0
while True:
    cmd = data[pos]; pos += 5
    pos += 84; ln = struct.unpack_from('<i', data, pos)[0]; pos += 4
    n += 1
    if n == 2: break
    pos += ln
pkt = data[pos:pos + ln]
nbits = ln * 8
bits = bytearray(nbits)
for i in range(nbits):
    bits[i] = (pkt[i >> 3] >> (i & 7)) & 1
def byte_at(i):
    v = 0
    for k in range(8): v |= bits[i + k] << k
    return v
i = lo
shown = 0
while i < hi and shown < 12:
    run = bytearray(); j = i
    while j + 8 <= hi:
        c = byte_at(j)
        if 32 <= c < 127: run.append(c); j += 8
        else: break
    if len(run) >= 5:
        print('bit', i, 'rel', i - lo, repr(run.decode()), 'end-byte', byte_at(j) if j + 8 <= hi else None)
        shown += 1
        i = j
    else:
        i += 1
print('raw first 64 bits:', ''.join(str(bits[k]) for k in range(lo, lo + 64)))
