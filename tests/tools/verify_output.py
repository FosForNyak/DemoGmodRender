#!/usr/bin/env python3
"""
verify_output.py — перевірка відео, створеного з імітатора гри (fake_game):
  * синхронізація: білі спалахи у відео і "біпи" 1 кГц у звуці кожну секунду
  * голоси з синтетичного демо на правильних місцях (440/660/880 Гц)
Використання: verify_output.py out.mp4 truth.json first_tick [--no-voice]
"""
import json, subprocess, sys
import numpy as np

path, truth_path, first_tick = sys.argv[1], sys.argv[2], int(sys.argv[3])
check_voice = '--no-voice' not in sys.argv
truth = json.load(open(truth_path, encoding='utf-8'))
ok = True

def run(cmd):
    return subprocess.run(cmd, capture_output=True, check=True).stdout

info = json.loads(run(['ffprobe', '-v', 'error', '-show_streams', '-show_format', '-of', 'json', path]))
v = [s for s in info['streams'] if s['codec_type'] == 'video'][0]
a = [s for s in info['streams'] if s['codec_type'] == 'audio']
fr = v['avg_frame_rate'].split('/')
fps = float(fr[0]) / float(fr[1])
print(f"Відео: {v['codec_name']} {v['width']}x{v['height']} {v.get('pix_fmt')} {fps:.3f} fps; аудіо-доріжок: {len(a)}")
for s in a:
    print(f"  аудіо: {s['codec_name']} {s['sample_rate']} Гц, {s.get('channels')} кан., тег: {s.get('tags', {}).get('title')}")

# --- яскравість кадрів ---
W, H = 32, 18
raw = run(['ffmpeg', '-v', 'error', '-i', path, '-vf', f'scale={W}:{H}', '-pix_fmt', 'gray', '-f', 'rawvideo', '-'])
frames = np.frombuffer(raw, np.uint8).reshape(-1, H, W).mean(axis=(1, 2))
base = np.median(frames)
flash_idx = [i for i in range(1, len(frames) - 1) if frames[i] > base + 25 and frames[i] >= frames[i - 1] and frames[i] >= frames[i + 1]]
flash_t = [i / fps for i in flash_idx]
print(f"Кадрів: {len(frames)} ({len(frames) / fps:.2f} с), спалахи: {[round(t, 3) for t in flash_t]}")

# --- звук ---
sr = 48000
pcm = np.frombuffer(run(['ffmpeg', '-v', 'error', '-i', path, '-map', '0:a:0', '-ac', '1', '-ar', str(sr), '-f', 'f32le', '-']), np.float32)
print(f"Звук: {len(pcm) / sr:.2f} с")

def band_energy(x, f, win=1024, hop=256):
    n = (len(x) - win) // hop
    t = np.arange(win) / sr
    c, s = np.cos(2 * np.pi * f * t), np.sin(2 * np.pi * f * t)
    e = np.empty(n)
    for i in range(n):
        seg = x[i * hop:i * hop + win]
        e[i] = (seg @ c) ** 2 + (seg @ s) ** 2
    return e, hop

e1k, hop = band_energy(pcm, 1000.0)
thr = e1k.max() * 0.2
beeps = []
i = 0
while i < len(e1k):
    if e1k[i] > thr:
        beeps.append((i * hop + 512) / sr)
        i += int(0.5 * sr / hop)
    else:
        i += 1
print(f"Біпи: {[round(b, 3) for b in beeps]}")

# --- синхронізація ---
pairs = 0
for ft in flash_t:
    near = [b for b in beeps if abs(b - ft) < 0.3]
    if not near:
        continue
    pairs += 1
    d = near[0] - ft
    if abs(d) > 1.5 / fps + 0.02:
        print(f"  ! розсинхрон на {ft:.3f} с: {d * 1000:.1f} мс")
        ok = False
print(f"Пар спалах/біп: {pairs}")
if pairs < 5:
    print("  ! замало пар для перевірки синхронізації")
    ok = False

# --- голоси ---
if check_voice:
    ti = truth['tick_interval']
    origin = first_tick * ti
    for sp in truth['speech']:
        f = sp['freq']
        start = sp['start'] - origin
        mid = start + sp['duration'] / 2
        if mid - 0.2 < 0 or mid + 0.2 > len(pcm) / sr:
            print(f"  голос {f:.0f} Гц на {start:.2f}-{start + sp['duration']:.2f} с: поза відео (фрагмент) — пропущено")
            continue
        seg = pcm[int((mid - 0.2) * sr):int((mid + 0.2) * sr)]
        other = pcm[int((start - 0.6) * sr):int((start - 0.2) * sr)] if start > 0.7 else None
        t = np.arange(len(seg)) / sr
        p = abs(seg @ np.exp(-2j * np.pi * f * t)) ** 2
        # енергія тієї ж частоти ДО початку фрази (має бути малою)
        if other is not None and len(other) > 0:
            to = np.arange(len(other)) / sr
            po = abs(other @ np.exp(-2j * np.pi * f * to)) ** 2
        else:
            po = 0
        good = p > 20 * (po + 1e-6)
        print(f"  голос {f:.0f} Гц на {start:.2f}-{start + sp['duration']:.2f} с: сила {p:.3g} (до фрази {po:.3g}) {'OK' if good else 'ПОГАНО'}")
        ok &= bool(good)

print("РЕЗУЛЬТАТ:", "OK" if ok else "ПРОВАЛ")
sys.exit(0 if ok else 1)
