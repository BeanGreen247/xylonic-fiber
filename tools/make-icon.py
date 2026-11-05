#!/usr/bin/env python3
"""Renders the Xylonic Fiber app icon (PIL + numpy, no other dependencies) and writes every size the platforms need.

Design: hybrid of the original logo (teal, white note) and a dark tile with a glowing ring around a dark centre, one teal hue family. Run from the repo root: python3 tools/make-icon.py
"""
import math, os
import numpy as np
from PIL import Image, ImageDraw, ImageFilter

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SS = 2        # supersampling
N = 1024 * SS

def hexc(h): h = h.lstrip('#'); return np.array([int(h[i:i+2], 16) for i in (0, 2, 4)], dtype=np.float32)

STOPS = [(0.0, '#00a9c2'), (0.30, '#00e5ff'), (0.62, '#3d8bff'), (0.84, '#7c5cff'), (1.0, '#d35cff')]
def ramp(t):
    t = np.clip(t, 0, 1)
    out = np.zeros(t.shape + (3,), np.float32)
    for (a, ca), (b, cb) in zip(STOPS, STOPS[1:]):
        m = (t >= a) & (t <= b)
        u = ((t - a) / (b - a))[..., None]
        out = np.where(m[..., None], hexc(ca) * (1 - u) + hexc(cb) * u, out)
    return out

def layer(): return Image.new('RGBA', (N, N), (0, 0, 0, 0))

def art(scale=1.0, with_bg=False):
    """Ring + note (+ optional tile background) as an RGBA image N x N. scale shrinks the artwork around the centre.

    Hybrid of the original logo (teal, white note) and a dark tile with a glowing ring around a dark centre: one hue family only.
    """
    c = N / 2
    ys, xs = np.mgrid[0:N, 0:N].astype(np.float32)
    dx, dy = xs - c, ys - c
    r = np.hypot(dx, dy)
    R = 318 * SS * scale          # ring centre radius
    W = 112 * SS * scale          # ring thickness
    # ring: lighter teal at the top left, the original deep teal at the bottom right
    t = np.clip(((dx + dy) / (2 * (R + W / 2)) + 1) / 2, 0, 1)
    top, mid, bot = hexc('#2fd3e6'), hexc('#00a9bf'), hexc('#006a7e')
    col = np.where((t < 0.5)[..., None], top * (1 - t[..., None] * 2) + mid * (t[..., None] * 2), mid * (1 - (t[..., None] - 0.5) * 2) + bot * ((t[..., None] - 0.5) * 2))
    aa = np.clip((W / 2 - np.abs(r - R)) + 0.5, 0, 1) * 255
    rgba = np.zeros((N, N, 4), np.float32)
    rgba[..., :3] = col; rgba[..., 3] = aa
    ring = Image.fromarray(rgba.astype(np.uint8), 'RGBA')
    out = tile() if with_bg else layer()
    glow = ring.filter(ImageFilter.GaussianBlur(30 * SS * scale))
    ga = np.array(glow).astype(np.float32); ga[..., 3] *= 0.45
    out = Image.alpha_composite(out, Image.fromarray(ga.astype(np.uint8), 'RGBA'))
    out = Image.alpha_composite(out, ring)
    # dark centre
    disc = layer(); ri = R - W / 2
    ImageDraw.Draw(disc).ellipse([c - ri, c - ri, c + ri, c + ri], fill=(6, 12, 16, 255))
    out = Image.alpha_composite(out, disc)
    # the Xylonic note (logo.svg geometry), white as in the original logo
    k = 1.15 * SS * scale
    def P(x, y): return (c + (x - 256) * k, c + (y - 252) * k)
    note = layer(); nd = ImageDraw.Draw(note)
    white = (255, 255, 255, 255)
    head = Image.new('L', (N, N), 0); hdw = ImageDraw.Draw(head)
    hx, hy = P(213, 352); rx, ry = 61 * k, 47 * k
    hdw.ellipse([hx - rx, hy - ry, hx + rx, hy + ry], fill=255)
    head = head.rotate(15, center=(hx, hy), resample=Image.BICUBIC)
    note.paste(Image.new('RGBA', (N, N), white), (0, 0), head)
    x0, y0 = P(250, 128); x1, y1 = P(276, 358)
    nd.rounded_rectangle([x0, y0, x1, y1], radius=13 * k, fill=white)
    p0, p1, p2, p3 = P(276, 128), P(366, 146), P(362, 224), P(296, 258)
    wd = 25 * k
    for i in range(0, 241):
        u = i / 240
        bx = (1-u)**3*p0[0] + 3*(1-u)**2*u*p1[0] + 3*(1-u)*u**2*p2[0] + u**3*p3[0]
        by = (1-u)**3*p0[1] + 3*(1-u)**2*u*p1[1] + 3*(1-u)*u**2*p2[1] + u**3*p3[1]
        nd.ellipse([bx - wd/2, by - wd/2, bx + wd/2, by + wd/2], fill=white)
    out = Image.alpha_composite(out, note)
    return out

def tile():
    """Dark rounded-square background with a faint teal glow, as RGBA N x N (full square, not yet rounded)."""
    ys, xs = np.mgrid[0:N, 0:N].astype(np.float32)
    v = ys / N
    top, bot = hexc('#0e1b22'), hexc('#060c10')
    img = (top[None, None, :] * (1 - v[..., None]) + bot[None, None, :] * v[..., None])
    g = np.clip(1 - np.hypot(xs - N * 0.28, ys - N * 0.22) / (N * 0.75), 0, 1) ** 2
    img = img + g[..., None] * hexc('#00bcd4')[None, None, :] * 0.10
    rgba = np.concatenate([np.clip(img, 0, 255), np.full((N, N, 1), 255, np.float32)], axis=2)
    return Image.fromarray(rgba.astype(np.uint8), 'RGBA')

def rounded(img, frac=0.2237):
    m = Image.new('L', (N, N), 0)
    ImageDraw.Draw(m).rounded_rectangle([0, 0, N - 1, N - 1], radius=int(N * frac), fill=255)
    out = img.copy(); out.putalpha(m)
    return out

def circle(img):
    m = Image.new('L', (N, N), 0)
    ImageDraw.Draw(m).ellipse([0, 0, N - 1, N - 1], fill=255)
    out = img.copy(); out.putalpha(m)
    return out

def save(img, path, size):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    img.resize((size, size), Image.LANCZOS).save(path, optimize=True)

def main():
    full = art(1.0, with_bg=True)
    icon = rounded(full)
    base = os.path.join(ROOT, 'assets', 'icons')
    for s in (1024, 512, 256, 128, 64, 32, 16):
        save(icon, os.path.join(base, f'app-icon-{s}.png'), s)
    res = os.path.join(ROOT, 'android', 'app', 'src', 'main', 'res')
    for d, s in (('mdpi', 48), ('hdpi', 72), ('xhdpi', 96), ('xxhdpi', 144), ('xxxhdpi', 192)):
        save(icon, os.path.join(res, f'mipmap-{d}', 'ic_launcher.png'), s)
        save(circle(full), os.path.join(res, f'mipmap-{d}', 'ic_launcher_round.png'), s)
    # adaptive icon (API 26+): 108 dp layers; the visible mask is the central 72 dp, the safe zone 66 dp -> artwork at 0.80
    save(tile(), os.path.join(res, 'drawable-nodpi', 'ic_launcher_bg.png'), 432)
    save(art(0.80, with_bg=False), os.path.join(res, 'drawable-nodpi', 'ic_launcher_fg.png'), 432)
    print('icons written')

if __name__ == '__main__':
    main()
