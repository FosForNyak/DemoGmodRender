#!/usr/bin/env python3
"""
compare_av.py — чи однакові за часом два відео з імітатора гри (FAKE_DEMO_CLOCK=1): кількість кадрів,
кадри з білими спалахами і початки "біпів" 1 кГц у звуці. Так перевіряється, що паралельний
рендер (частини кількох копій гри, склеєні без перекодування) не має швів і зсуву звуку.

Використання: compare_av.py a.mp4 b.mp4 [--ffmpeg ПАПКА] [--frame-tolerance N]
  --frame-tolerance 1 — спалах може бути на сусідньому кадрі (стик дописування після збою починається
  з ключового кадру, що не завжди припадає на тік: зсув до пів кадру)
Без numpy — лише стандартна бібліотека.
"""
import array
import math
import os
import subprocess
import sys

sys.stdout.reconfigure(encoding='utf-8', errors='replace')   # консоль Windows у CI — cp1252
args = [a for a in sys.argv[1:] if not a.startswith('--')]
ffdir = sys.argv[sys.argv.index('--ffmpeg') + 1] if '--ffmpeg' in sys.argv else ''
if '--ffmpeg' in sys.argv:
    args.remove(ffdir)
tol = int(sys.argv[sys.argv.index('--frame-tolerance') + 1]) if '--frame-tolerance' in sys.argv else 0
if '--frame-tolerance' in sys.argv:
    args.remove(str(tol))
ffmpeg = os.path.join(ffdir, 'ffmpeg') if ffdir else 'ffmpeg'
SR = 8000


def run(cmd):
    return subprocess.run(cmd, capture_output=True, check=True).stdout


def analyse(path):
    raw = run([ffmpeg, '-v', 'error', '-i', path, '-vf', 'scale=16:9,format=gray', '-f', 'rawvideo', '-'])
    n = len(raw) // 144
    means = [sum(raw[i * 144:(i + 1) * 144]) / 144 for i in range(n)]
    base = sorted(means)[len(means) // 2]
    flashes = [i for i, m in enumerate(means) if m > base + 60]
    pcm = array.array('h')
    pcm.frombytes(run([ffmpeg, '-v', 'error', '-i', path, '-map', '0:a:0', '-ac', '1', '-ar', str(SR), '-f', 's16le', '-']))
    # Потужність 1 кГц (Гьорцель) у вікнах 20 мс з кроком 5 мс
    win, hop = 160, 40
    k = 2 * math.cos(2 * math.pi * 1000 / SR)
    power = []
    for start in range(0, len(pcm) - win, hop):
        s1 = s2 = 0.0
        for x in pcm[start:start + win]:
            s1, s2 = x + k * s1 - s2, s1
        power.append(s1 * s1 + s2 * s2 - k * s1 * s2)
    thr = max(power) * 0.2 if power else 0
    beeps, i = [], 0
    while i < len(power):
        if power[i] > thr:
            beeps.append((i * hop + win / 2) / SR)
            i += int(0.5 * SR / hop)
        else:
            i += 1
    return n, flashes, beeps, len(pcm) / SR


ok = True
res = []
for path in args[:2]:
    n, flashes, beeps, secs = analyse(path)
    print(f'{os.path.basename(path)}: кадрів {n}, звук {secs:.2f} с, спалахи на кадрах {flashes}, '
          f'біпи {[round(b, 3) for b in beeps]}')
    res.append((n, flashes, beeps, secs))
(na, fa, ba, sa), (nb, fb, bb, sb) = res
# Звичайний рендер може мати зайвий кадр у самому кінці (гра дописує кадр на останньому тіку),
# тож порівнюємо спільну частину
if abs(na - nb) > 1:
    print(f'  ! різна кількість кадрів: {na} і {nb}')
    ok = False
n = min(na, nb)
fa, fb = [f for f in fa if f < n], [f for f in fb if f < n]
end = min(sa, sb) - 0.1
ba, bb = [b for b in ba if b < end], [b for b in bb if b < end]
if len(fa) != len(fb) or any(abs(x - y) > tol for x, y in zip(fa, fb)):
    print('  ! спалахи на різних кадрах')
    ok = False
if len(ba) != len(bb) or any(abs(x - y) > 0.015 for x, y in zip(ba, bb)):
    print('  ! біпи не збігаються (допуск 15 мс)')
    ok = False
if abs(sa - sb) > 0.1:
    print(f'  ! різна тривалість звуку: {sa:.3f} і {sb:.3f} с')
    ok = False
if len(fa) < 3 or len(ba) < 3:
    print('  ! замало спалахів чи біпів для перевірки')
    ok = False
print('РЕЗУЛЬТАТ:', 'OK' if ok else 'ПРОВАЛ')
sys.exit(0 if ok else 1)
