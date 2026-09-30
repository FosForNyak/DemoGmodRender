import struct, sys
sys.path.insert(0, sys.argv[0].rsplit('/',1)[0])
from bits import *
fn = sys.argv[1]; data = open(fn,'rb').read()
pos = 1072
packets = []
while True:
    cmd = data[pos]; tick = struct.unpack_from('<i', data, pos+1)[0]; pos += 5
    if cmd in (1,2):
        pos += 84; ln = struct.unpack_from('<i', data, pos)[0]; pos += 4
        packets.append((cmd, tick, pos, ln)); pos += ln
        if len(packets) >= int(sys.argv[2]): break
    elif cmd == 3: pass
    elif cmd in (4,6,8): ln = struct.unpack_from('<i', data, pos)[0]; pos += 4+ln
    elif cmd == 5: pos += 4; ln = struct.unpack_from('<i', data, pos)[0]; pos += 4+ln
    else: break
for (cmd, tick, p, ln) in packets[:int(sys.argv[3])]:
    b = BitReader(data, p, p+ln)
    print('== packet cmd', cmd, 'tick', tick, 'len', ln)
    n = 0
    while b.left() >= 6 and n < 40:
        t = b.ubit(6); n += 1
        print('  msg', t, 'at bit', b.pos - p*8 - 6)
        if t == 0: continue
        if t == 3: print('   net_Tick', b.long(), b.ubit(16), b.ubit(16)); continue
        if t == 4: print('   StringCmd', repr(b.string())); continue
        if t == 5:
            c = b.byte(); kv = [(b.string(), b.string()) for _ in range(c)]; print('   SetConVar', c, kv[:8]); continue
        if t == 6: print('   SignonState', b.byte(), b.long()); continue
        if t == 7: print('   Print', repr(b.string()[:200])); continue
        if t == 8:
            print('   ServerInfo proto', b.word(), 'count', b.long(), 'hltv', b.bit(), 'ded', b.bit(), 'crc', hex(b.ubit(32)&0xffffffff), 'maxclasses', b.word())
            print('    md5', b.bytes_(16).hex(), 'slot', b.byte(), 'maxcl', b.byte(), 'interval', b.float(), 'os', chr(b.byte()))
            print('    gamedir', b.string(), 'map', b.string(), 'sky', b.string(), 'host', b.string())
            print('    next bits', [b.bit() for _ in range(16)], 'left', b.left()); break
        print('   (unhandled, stop)'); break
