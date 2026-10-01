import struct
class BitReader:
    def __init__(s, data, start=0, end=None):
        s.d = data; s.pos = start*8; s.end = (len(data) if end is None else end)*8
    def left(s): return s.end - s.pos
    def ubit(s, n):
        if n == 0: return 0
        if s.pos + n > s.end: raise EOFError('overrun %d+%d>%d' % (s.pos, n, s.end))
        v = 0; i = 0
        while i < n:
            byte = s.d[s.pos >> 3]; off = s.pos & 7
            take = min(8 - off, n - i)
            v |= ((byte >> off) & ((1 << take) - 1)) << i
            i += take; s.pos += take
        return v
    def sbit(s, n):
        v = s.ubit(n)
        if v & (1 << (n-1)): v -= 1 << n
        return v
    def bit(s): return s.ubit(1)
    def byte(s): return s.ubit(8)
    def word(s): return s.ubit(16)
    def long(s): return s.sbit(32)
    def float(s): return struct.unpack('<f', struct.pack('<I', s.ubit(32)))[0]
    def string(s, maxlen=4096):
        out = bytearray()
        while True:
            c = s.ubit(8)
            if c == 0: break
            out.append(c)
            if len(out) > maxlen: raise ValueError('string too long')
        return out.decode('utf-8', 'replace')
    def bytes_(s, n): return bytes(s.ubit(8) for _ in range(n))
    def varint32(s):
        r = 0
        for i in range(5):
            b = s.ubit(8); r |= (b & 0x7f) << (7*i)
            if not (b & 0x80): break
        return r
    def skip(s, n):
        if s.pos + n > s.end: raise EOFError('skip overrun')
        s.pos += n
def log2(v):
    r = 0
    while v > 1: v >>= 1; r += 1
    return r
