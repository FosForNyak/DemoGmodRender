import struct, sys, collections
fn = sys.argv[1]
f = open(fn,'rb'); data = f.read()
pos = 1072
counts = collections.Counter(); sizes = collections.Counter()
first = []
ticks=[]
while pos < len(data):
    cmd = data[pos]; tick = struct.unpack_from('<i', data, pos+1)[0]; pos += 5
    if len(first) < 30: first.append((cmd, tick, pos))
    counts[cmd]+=1
    if cmd in (1,2):
        pos += 76 + 8
        ln = struct.unpack_from('<i', data, pos)[0]; pos += 4 + ln; sizes[cmd]+=ln
        ticks.append(tick)
    elif cmd == 3: pass
    elif cmd == 4 or cmd == 6 or cmd == 8:
        ln = struct.unpack_from('<i', data, pos)[0]; pos += 4 + ln; sizes[cmd]+=ln
    elif cmd == 5:
        pos += 4; ln = struct.unpack_from('<i', data, pos)[0]; pos += 4 + ln; sizes[cmd]+=ln
    elif cmd == 7:
        print('stop at', pos, 'remaining', len(data)-pos); break
    else:
        print('UNKNOWN cmd', cmd, 'tick', tick, 'at', pos-5); break
print(counts); print(sizes)
print(first[:12])
print('ticks min/max', min(ticks), max(ticks))
