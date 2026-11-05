#!/usr/bin/env python3
"""Renders the Xylonic Fiber app icon (PIL + numpy, no other dependencies) and writes every size the platforms need.

Design: Vice City night purple, a striped sunset disc with a neon rim, cyan speed streaks and a dark, forward-leaning note. Run from the repo root: python3 tools/make-icon.py
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
    """Vice City take on the Xylonic note: a striped sunset disc over a night-purple tile, speed streaks, a dark note with a neon rim.

    scale shrinks the artwork around the centre (adaptive icon layers keep it inside the safe zone).
    """
    c = N / 2
    ys, xs = np.mgrid[0:N, 0:N].astype(np.float32)
    cy = c + 14 * SS * scale
    dx, dy = xs - c, ys - cy
    r = np.hypot(dx, dy)
    R = 372 * SS * scale
    out = tile() if with_bg else layer()
    # speed streaks behind the sun: cyan to transparent, thin, staggered
    streaks = layer(); sd = ImageDraw.Draw(streaks)
    for (yy, x0, x1, th, a0) in ((-250, -520, -60, 12, 230), (-150, -520, -150, 8, 190), (-60, -520, -200, 16, 255), (40, -520, -120, 9, 200), (140, -520, -250, 12, 230), (240, -520, -90, 8, 170)):
        for i in range(60):
            t0 = i / 60.0
            xa = c + (x0 + (x1 - x0) * t0) * SS * scale; xb = c + (x0 + (x1 - x0) * (t0 + 1 / 60.0)) * SS * scale
            al = int(a0 * (t0 ** 0.9))
            sd.rectangle([xa, cy + (yy - th * 0.7) * SS * scale, xb + 1, cy + (yy + th * 0.7) * SS * scale], fill=(0, 229, 255, al))
    out = Image.alpha_composite(out, streaks.filter(ImageFilter.GaussianBlur(1.2 * SS)))
    # outer glow of the sun
    glow = np.zeros((N, N, 4), np.float32)
    glow[..., :3] = hexc('#ff2d95'); glow[..., 3] = np.clip(1 - (r - R) / (150 * SS * scale), 0, 1) ** 2.2 * 150 * (r > R * 0.6)
    out = Image.alpha_composite(out, Image.fromarray(glow.astype(np.uint8), 'RGBA'))
    # the sun: yellow -> orange -> hot pink -> magenta, slits cut into the lower half
    v = np.clip((dy + R) / (2 * R), 0, 1)
    stops = [(0.0, '#ffe45e'), (0.38, '#ff9a1f'), (0.68, '#ff2d95'), (1.0, '#a100c8')]
    col = np.zeros((N, N, 3), np.float32)
    for (t0, c0), (t1, c1) in zip(stops, stops[1:]):
        m = (v >= t0) & (v <= t1)
        u = ((v - t0) / (t1 - t0))[..., None]
        col = np.where(m[..., None], hexc(c0) * (1 - u) + hexc(c1) * u, col)
    alpha = np.clip(R - r + 0.5, 0, 1)
    # slits: bands whose height grows towards the bottom of the sun
    lower = dy > 0
    slit = np.zeros((N, N), np.float32)
    y_off = dy / R   # 0 (centre) .. 1 (bottom)
    for k in range(7):
        centre = 0.20 + k * 0.118
        half = 0.012 + k * 0.0075
        slit = np.maximum(slit, (np.abs(y_off - centre) < half).astype(np.float32))
    alpha = alpha * (1 - slit * lower)
    sun = np.zeros((N, N, 4), np.float32)
    sun[..., :3] = col; sun[..., 3] = alpha * 255
    out = Image.alpha_composite(out, Image.fromarray(sun.astype(np.uint8), 'RGBA'))
    # neon rim on the sun (thin cyan ring just outside)
    ring = np.zeros((N, N, 4), np.float32)
    ring[..., :3] = hexc('#19e6ff'); ring[..., 3] = np.clip(4 * SS * scale - np.abs(r - (R + 18 * SS * scale)) + 0.5, 0, 1) * 235
    ringimg = Image.fromarray(ring.astype(np.uint8), 'RGBA')
    out = Image.alpha_composite(out, ringimg.filter(ImageFilter.GaussianBlur(10 * SS * scale)))
    out = Image.alpha_composite(out, ringimg)
    # the note: dark silhouette (like the glowing-disc-and-silhouette icon), skewed forward for speed, with a cyan neon edge
    k = 1.02 * SS * scale
    def P(x, y): return (c + (x - 256) * k, cy + (y - 262) * k)
    note = Image.new('L', (N, N), 0); nd = ImageDraw.Draw(note)
    hx, hy = P(213, 352); rx, ry = 61 * k, 47 * k
    head = Image.new('L', (N, N), 0); ImageDraw.Draw(head).ellipse([hx - rx, hy - ry, hx + rx, hy + ry], fill=255)
    head = head.rotate(15, center=(hx, hy), resample=Image.BICUBIC)
    note = Image.composite(Image.new('L', (N, N), 255), note, head)
    nd = ImageDraw.Draw(note)
    x0, y0 = P(250, 128); x1, y1 = P(276, 358)
    nd.rounded_rectangle([x0, y0, x1, y1], radius=13 * k, fill=255)
    p0, p1, p2, p3 = P(276, 128), P(366, 146), P(362, 224), P(296, 258)
    wd = 25 * k
    for i in range(0, 241):
        u = i / 240
        bx = (1-u)**3*p0[0] + 3*(1-u)**2*u*p1[0] + 3*(1-u)*u**2*p2[0] + u**3*p3[0]
        by = (1-u)**3*p0[1] + 3*(1-u)**2*u*p1[1] + 3*(1-u)*u**2*p2[1] + u**3*p3[1]
        nd.ellipse([bx - wd/2, by - wd/2, bx + wd/2, by + wd/2], fill=255)
    shear = -0.16   # italic lean: the note leans into the motion
    note = note.transform(note.size, Image.AFFINE, (1, shear, -shear * cy, 0, 1, 0), resample=Image.BICUBIC)
    edge = note.filter(ImageFilter.MaxFilter(2 * int(3 * SS * scale) + 1))
    edge_rgba = Image.new('RGBA', (N, N), (25, 230, 255, 255)); edge_rgba.putalpha(edge)
    out = Image.alpha_composite(out, edge_rgba.filter(ImageFilter.GaussianBlur(5 * SS * scale)))
    out = Image.alpha_composite(out, edge_rgba)
    body = Image.new('RGBA', (N, N), (18, 4, 40, 255)); body.putalpha(note)
    out = Image.alpha_composite(out, body)
    return out

def tile():
    """Dark rounded-square background with a faint teal glow, as RGBA N x N (full square, not yet rounded)."""
    ys, xs = np.mgrid[0:N, 0:N].astype(np.float32)
    v = ys / N
    top, bot = hexc('#2b0a4d'), hexc('#0c0120')
    img = (top[None, None, :] * (1 - v[..., None]) + bot[None, None, :] * v[..., None])
    g = np.clip(1 - np.hypot(xs - N * 0.28, ys - N * 0.22) / (N * 0.75), 0, 1) ** 2
    img = img + g[..., None] * hexc('#ff2d95')[None, None, :] * 0.16
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
