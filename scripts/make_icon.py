"""Іконка програми (src/gui/app.ico) — той самий логотип, що малює ui::draw_logo у вікні:
заокруглений квадрат із діагональним градієнтом акценту, кадр плівки зі стрілкою і
перфораціями. Без сторонніх бібліотек: кожен розмір малюється окремо зі згладжуванням
(4×4 вибірки на піксель) і кладеться в .ico як PNG.

  python scripts/make_icon.py [файл.ico]
"""
import os
import struct
import sys
import zlib

ACCENT = (123, 97, 255)          # фіолетовий акцент (ui::accent_presets, "violet")
SIZES = [16, 24, 32, 48, 64, 128, 256]
SS = 4                            # вибірок на піксель по кожній осі


def mix(a, b, t):
    return tuple(a[i] + (b[i] - a[i]) * t for i in range(3))


C0 = mix(ACCENT, (255, 255, 255), 0.12)
C1 = mix(ACCENT, (24, 180, 230), 0.55)


def in_round_rect(x, y, x0, y0, x1, y1, r):
    if x < x0 or x > x1 or y < y0 or y > y1:
        return False
    dx = max(x0 + r - x, 0.0, x - (x1 - r))
    dy = max(y0 + r - y, 0.0, y - (y1 - r))
    return dx * dx + dy * dy <= r * r


def in_triangle(x, y, a, b, c):
    def edge(p, q):
        return (q[0] - p[0]) * (y - p[1]) - (q[1] - p[1]) * (x - p[0])
    e1, e2, e3 = edge(a, b), edge(b, c), edge(c, a)
    return (e1 >= 0 and e2 >= 0 and e3 >= 0) or (e1 <= 0 and e2 <= 0 and e3 <= 0)


def sample(x, y, s):
    """RGBA однієї точки логотипа розміром s (як у draw_logo)."""
    if not in_round_rect(x, y, 0, 0, s, s, s * 0.26):
        return (0.0, 0.0, 0.0, 0.0)
    t = max(0.0, min(1.0, (x + y) / (2 * s)))   # градієнт уздовж діагоналі
    col = list(mix(C0, C1, t))
    a = 1.0
    cx = cy = s * 0.5
    fw, fh = s * 0.6, s * 0.46
    th = max(1.0, s * 0.07)
    rr = s * 0.08

    def over(c, alpha):
        for i in range(3):
            col[i] = col[i] * (1 - alpha) + c[i] * alpha

    # Кадр — обведення заокругленого прямокутника (лінія по центру контуру)
    hw, hh = fw * 0.5, fh * 0.5
    outer = in_round_rect(x, y, cx - hw - th / 2, cy - hh - th / 2, cx + hw + th / 2, cy + hh + th / 2, rr + th / 2)
    inner = in_round_rect(x, y, cx - hw + th / 2, cy - hh + th / 2, cx + hw - th / 2, cy + hh - th / 2, max(0.0, rr - th / 2))
    if outer and not inner:
        over((255, 255, 255), 235 / 255)
    tr = fh * 0.3
    if in_triangle(x, y, (cx - tr * 0.7, cy - tr), (cx + tr * 1.05, cy), (cx - tr * 0.7, cy + tr)):
        over((255, 255, 255), 1.0)
    rd = s * 0.035
    for dx in (-0.18, 0.18):
        for py in (cy - hh - s * 0.09, cy + hh + s * 0.09):
            ex, ey = x - (cx + dx * s), y - py
            if ex * ex + ey * ey <= rd * rd:
                over((255, 255, 255), 200 / 255)
    return (col[0], col[1], col[2], a)


def render(s):
    rows = []
    for py in range(s):
        row = bytearray([0])   # PNG: фільтр «None»
        for px in range(s):
            acc = [0.0, 0.0, 0.0, 0.0]
            for sy in range(SS):
                for sx in range(SS):
                    r, g, b, a = sample(px + (sx + 0.5) / SS, py + (sy + 0.5) / SS, s)
                    acc[0] += r * a
                    acc[1] += g * a
                    acc[2] += b * a
                    acc[3] += a
            n = SS * SS
            alpha = acc[3] / n
            if alpha > 0:
                row += bytes(int(round(min(255, acc[i] / acc[3]))) for i in range(3))
            else:
                row += b"\0\0\0"
            row.append(int(round(alpha * 255)))
        rows.append(bytes(row))
    return png(s, s, b"".join(rows))


def png(w, h, raw):
    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def ico(images):
    head = struct.pack("<HHH", 0, 1, len(images))
    offset = 6 + 16 * len(images)
    entries, data = b"", b""
    for s, blob in images:
        entries += struct.pack("<BBBBHHII", s % 256, s % 256, 0, 0, 1, 32, len(blob), offset + len(data))
        data += blob
    return head + entries + data


def main():
    repo = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(repo, "src", "gui", "app.ico")
    images = [(s, render(s)) for s in SIZES]
    open(out, "wb").write(ico(images))
    print(out, ", ".join(f"{s}px" for s, _ in images))


if __name__ == "__main__":
    main()
