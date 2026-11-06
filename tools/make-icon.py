#!/usr/bin/env python3
"""Renders the Xylonic Fiber app icon (PIL + numpy, no other dependencies) and writes every size the platforms need.

Design: Vice City night purple, a striped sunset disc with a neon rim, cyan speed streaks and a dark, forward-leaning note. Run from the repo root: python3 tools/make-icon.py
"""
import math, os, sys
import numpy as np
from PIL import Image, ImageChops, ImageDraw, ImageFilter

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


# ---------------------------------------------------------------- shared helpers for the extra styles (1024-unit design space)
YS, XS = np.mgrid[0:N, 0:N].astype(np.float32)

def Pt(x, y, s=1.0): return (N / 2 + (x - 512) * SS * s, N / 2 + (y - 512) * SS * s)
def Lg(v, s=1.0): return v * SS * s
def gmask(): return Image.new('L', (N, N), 0)

def rr(box, r, s, m=None, outset=0):
    m = gmask() if m is None else m
    x0, y0 = Pt(box[0], box[1], s); x1, y1 = Pt(box[2], box[3], s); o = Lg(outset, s)
    ImageDraw.Draw(m).rounded_rectangle([x0 - o, y0 - o, x1 + o, y1 + o], radius=Lg(r + outset, s), fill=255)
    return m

def circ(c, r, s, m=None):
    m = gmask() if m is None else m
    x, y = Pt(c[0], c[1], s); rad = Lg(r, s)
    ImageDraw.Draw(m).ellipse([x - rad, y - rad, x + rad, y + rad], fill=255)
    return m

def poly(pts, s, m=None):
    m = gmask() if m is None else m
    ImageDraw.Draw(m).polygon([Pt(x, y, s) for x, y in pts], fill=255)
    return m

def line(a, b, w, s, m=None):
    m = gmask() if m is None else m
    ImageDraw.Draw(m).line([Pt(*a, s), Pt(*b, s)], fill=255, width=max(1, int(Lg(w, s))))
    for q in (a, b): circ(q, w / 2, s, m)
    return m

def sub(a, b): return ImageChops.subtract(a, b)

def colorize(mask, rgb):
    a = np.asarray(mask, np.float32)
    return Image.fromarray(np.dstack([np.clip(rgb, 0, 255), a]).astype(np.uint8), 'RGBA')

def flat(mask, col):
    rgb = np.empty((N, N, 3), np.float32); rgb[:] = hexc(col)
    return colorize(mask, rgb)

def interp(t, stops):
    xs = [a for a, _ in stops]
    return np.dstack([np.interp(t, xs, [hexc(c)[k] for _, c in stops]) for k in range(3)]).astype(np.float32)

def vfill(mask, stops, y0, y1, s=1.0):
    return colorize(mask, interp(np.clip((YS - Pt(0, y0, s)[1]) / Lg(y1 - y0, s), 0, 1), stops))

def hfill(mask, stops, x0, x1, s=1.0):
    return colorize(mask, interp(np.clip((XS - Pt(x0, 0, s)[0]) / Lg(x1 - x0, s), 0, 1), stops))

def glow(mask, col, blur, s=1.0, a=0.85):
    m = mask.filter(ImageFilter.GaussianBlur(Lg(blur, s))).point(lambda v: int(v * a))
    return flat(m, col)

def stack(*layers):
    out = layer()
    for l in layers: out = Image.alpha_composite(out, l)
    return out

def night(top, bot, glow_col=None, gxy=(0.28, 0.22), gamt=0.16):
    img = interp(YS / N, [(0, top), (1, bot)])
    if glow_col:
        g = np.clip(1 - np.hypot(XS - N * gxy[0], YS - N * gxy[1]) / (N * 0.75), 0, 1) ** 2
        img = img + g[..., None] * hexc(glow_col)[None, None, :] * gamt
    return Image.fromarray(np.dstack([np.clip(img, 0, 255), np.full((N, N), 255, np.float32)]).astype(np.uint8), 'RGBA')

CREAM, CYAN, PINK, NIGHT = '#fff1d6', '#19e6ff', '#ff2d95', '#14052e'

# ---------------------------------------------------------------- cassette
def cassette_fg(s):
    body = rr((110, 250, 914, 790), 72, s)
    rim = rr((110, 250, 914, 790), 72, s, outset=10)
    label = rr((184, 306, 840, 566), 40, s)
    win = rr((296, 372, 728, 508), 68, s)
    reels = gmask(); rings = gmask()
    for cx in (404, 620):
        circ((cx, 440), 60, s, rings)
    hubs = gmask()
    for cx in (404, 620):
        circ((cx, 440), 40, s, hubs)
    spokes = gmask()
    for cx in (404, 620):
        for ang in (90, 210, 330):
            a = math.radians(ang)
            line((cx, 440), (cx + 40 * math.cos(a), 440 + 40 * math.sin(a)), 14, s, spokes)
    base = poly([(300, 790), (352, 640), (672, 640), (724, 790)], s)
    holes = circ((404, 728), 20, s); circ((620, 728), 20, s, holes)
    screws = gmask()
    for q in ((158, 300), (866, 300), (158, 742), (866, 742)): circ(q, 17, s, screws)
    stripe = rr((184, 480, 840, 512), 0, s)
    return stack(
        glow(rim, PINK, 46, s, 0.9), flat(rim, CYAN),
        vfill(body, [(0, '#ff4fa8'), (1, '#c4157f')], 250, 790, s),
        flat(label, CREAM), flat(ImageChops.multiply(stripe, label), PINK),
        flat(win, '#1a0838'), flat(rings, CREAM), flat(hubs, '#1a0838'), flat(spokes, CREAM),
        flat(base, '#3b0f6b'), flat(holes, CYAN), flat(screws, '#7a0b4c'))

# ---------------------------------------------------------------- vinyl
def vinyl_fg(s):
    cx, cy = Pt(512, 512, s); R = Lg(392, s)
    r = np.hypot(XS - cx, YS - cy)
    disc = circ((512, 512), 392, s)
    rim = circ((512, 512), 392, s, None); rim = sub(circ((512, 512), 404, s), circ((512, 512), 392, s))
    shade = interp(np.clip(r / R, 0, 1), [(0, '#241338'), (0.6, '#120824'), (1, '#0a0418')])
    grooves = np.zeros((N, N), np.float32)
    for rad in range(176, 388, 13):
        grooves = np.maximum(grooves, np.clip(Lg(1.6, s) - np.abs(r - Lg(rad, s)) + 0.5, 0, 1))
    gl = Image.fromarray((grooves * 46 * (r < R)).astype(np.uint8), 'L')
    ang = np.arctan2(YS - cy, XS - cx)
    wedge = np.zeros((N, N), np.float32)
    for a0 in (-0.9, math.pi - 0.9):
        d = np.abs(((ang - a0 + math.pi) % (2 * math.pi)) - math.pi)
        wedge = np.maximum(wedge, np.clip(1 - d / 0.30, 0, 1) ** 1.5)
    wm = Image.fromarray((wedge * 70 * ((r < R) & (r > Lg(165, s)))).astype(np.uint8), 'L').filter(ImageFilter.GaussianBlur(Lg(5, s)))
    label = circ((512, 512), 160, s)
    lab = vfill(label, [(0, '#ffe45e'), (0.4, '#ff9a1f'), (0.7, '#ff2d95'), (1, '#a100c8')], 352, 672, s)
    ring = sub(circ((512, 512), 168, s), circ((512, 512), 160, s))
    return stack(glow(rim, CYAN, 22, s, 0.9), flat(rim, CYAN), colorize(disc, shade), flat(gl, '#ffffff'), flat(wm, '#ffffff'),
                 flat(ring, '#0a0418'), lab, flat(circ((512, 512), 34, s), '#1a0838'), flat(circ((512, 512), 12, s), CREAM))

# ---------------------------------------------------------------- equalizer
def equalizer_fg(s):
    tops = (430, 250, 150, 330, 500)
    bars = gmask(); caps = gmask()
    for i, t in enumerate(tops):
        x0 = 150 + i * 150
        rr((x0, t, x0 + 108, 800), 54, s, bars)
        rr((x0, t - 70, x0 + 108, t - 28), 21, s, caps)
    ramp_ = [(0, CYAN), (0.5, '#7c5cff'), (1, PINK)]
    both = ImageChops.lighter(bars, caps)
    rgb = interp(np.clip((XS - Pt(150, 0, s)[0]) / Lg(758, s), 0, 1), ramp_)
    capc = colorize(caps, rgb * 0.45 + 255 * 0.55)
    return stack(glow(both, '#7c5cff', 40, s, 0.8), colorize(bars, rgb), capc)

# ---------------------------------------------------------------- minimal note
def note_mask(s, k, shear=0.0):
    K = k * SS * s
    def Q(x, y): return (N / 2 + (x - 266) * K, N / 2 + (y - 262) * K)
    hx, hy = Q(213, 352)
    head = gmask(); ImageDraw.Draw(head).ellipse([hx - 61 * K, hy - 47 * K, hx + 61 * K, hy + 47 * K], fill=255)
    head = head.rotate(15, center=(hx, hy), resample=Image.BICUBIC)
    nd = ImageDraw.Draw(head)
    x0, y0 = Q(250, 128); x1, y1 = Q(276, 358)
    nd.rounded_rectangle([x0, y0, x1, y1], radius=13 * K, fill=255)
    p0, p1, p2, p3 = Q(276, 128), Q(366, 146), Q(362, 224), Q(296, 258)
    for i in range(241):
        u = i / 240
        bx = (1-u)**3*p0[0] + 3*(1-u)**2*u*p1[0] + 3*(1-u)*u**2*p2[0] + u**3*p3[0]
        by = (1-u)**3*p0[1] + 3*(1-u)**2*u*p1[1] + 3*(1-u)*u**2*p2[1] + u**3*p3[1]
        nd.ellipse([bx - 13 * K, by - 13 * K, bx + 13 * K, by + 13 * K], fill=255)
    return head

def note_bg(s): return night('#2ee0f7', '#0ab0d4')
def note_fg(s):
    m = note_mask(s, 2.0)
    shadow = ImageChops.offset(m, 0, int(Lg(14, s))).filter(ImageFilter.GaussianBlur(Lg(14, s))).point(lambda v: int(v * 0.30))
    return stack(flat(shadow, '#00566b'), flat(m, '#ffffff'))

# ---------------------------------------------------------------- synthwave grid
HORIZON = 620
def grid_bg(s):
    out = night('#12002e', '#12002e')
    yh = Pt(0, HORIZON, s)[1]
    sky = interp(np.clip(YS / yh, 0, 1) ** 1.6, [(0, '#12002e'), (0.6, '#3a0a5c'), (1, '#9a1a86')])
    ground = interp(np.clip((YS - yh) / (N - yh), 0, 1), [(0, '#1c0238'), (1, '#07010f')])
    img = np.where((YS < yh)[..., None], sky, ground)
    out = Image.fromarray(np.dstack([img, np.full((N, N), 255, np.float32)]).astype(np.uint8), 'RGBA')
    lines = gmask(); d = ImageDraw.Draw(lines)
    vx, vy = Pt(512, HORIZON, s)
    for i in range(-14, 15):
        xb, yb = Pt(512 + i * 170, HORIZON + 1000, s)
        d.line([(vx, vy), (xb, yb)], fill=255, width=max(2, int(Lg(5, s))))
    for j in range(1, 10):
        y = HORIZON + 420 * (j / 9) ** 2.1
        _, yy = Pt(0, y, s)
        d.line([(0, yy), (N, yy)], fill=255, width=max(2, int(Lg(3 + j * 0.9, s))))
    lines = ImageChops.multiply(lines, Image.fromarray((np.asarray(YS) > yh).astype(np.uint8) * 255, 'L'))
    hz = rr((-200, HORIZON - 7, 1224, HORIZON + 7), 0, s)
    return stack(out, glow(lines, PINK, 8, s, 0.9), flat(lines, '#ff4fb0'), glow(hz, CYAN, 22, s, 0.9), flat(hz, CYAN))

def grid_fg(s):
    cx, cy = Pt(512, HORIZON, s); R = Lg(318, s)
    r = np.hypot(XS - cx, YS - cy)
    m = (r < R) & (YS < cy)
    slit = np.zeros((N, N), bool)
    for dist, half in ((46, 24), (112, 19), (172, 14), (230, 10), (284, 6)):
        slit |= np.abs((cy - YS) - Lg(dist, s)) < Lg(half, s)
    alpha = np.clip(R - r + 0.5, 0, 1) * (YS < cy) * (~slit)
    sun = vfill(Image.fromarray((alpha * 255).astype(np.uint8), 'L'), [(0, '#ffe45e'), (0.4, '#ff9a1f'), (0.75, '#ff2d95'), (1, '#c1119f')], HORIZON - 318, HORIZON, s)
    halo = Image.fromarray((np.clip(1 - (r - R) / Lg(130, s), 0, 1) ** 2.2 * 140 * (YS < cy)).astype(np.uint8), 'L')
    return stack(flat(halo, PINK), sun)

# ---------------------------------------------------------------- pixel
PIX_NOTE = None
def _pix_cells():
    cells = set()
    for r_ in range(1, 13):
        cells |= {(r_, 8), (r_, 9)}
    for r_, cs in ((0, range(8, 13)), (1, range(8, 14)), (2, range(10, 15)), (3, range(12, 15)), (4, range(12, 15)), (5, range(11, 14)), (6, range(10, 13))):
        cells |= {(r_, c_) for c_ in cs}
    for r_ in range(15):
        for c_ in range(10):
            if ((c_ + 0.5 - 5.0) / 3.9) ** 2 + ((r_ + 0.5 - 11.8) / 2.6) ** 2 <= 1: cells.add((r_, c_))
    return cells

def pixel_bg(s):
    img = night('#2a0f5e', '#12042a')
    ov = layer(); d = ImageDraw.Draw(ov)
    cell = Lg(64, s); ox = N / 2 - 8 * cell; oy = N / 2 - 8 * cell
    for gx in range(17):
        for gy in range(17):
            if (gx + gy) % 2 == 0:
                d.rectangle([ox + gx * cell, oy + gy * cell, ox + (gx + 1) * cell, oy + (gy + 1) * cell], fill=(255, 255, 255, 7))
    return Image.alpha_composite(img, ov)

def pixel_fg(s):
    cells = _pix_cells()
    cell = round(Lg(46, s)); ox = int(N / 2 - 7.5 * cell); oy = int(N / 2 - 7.5 * cell)
    def draw(col, dx, dy, pick=None):
        m = gmask(); d = ImageDraw.Draw(m)
        for (r_, c_) in cells:
            if pick and not pick(r_, c_): continue
            x, y = ox + (c_ + dx) * cell, oy + (r_ + dy) * cell
            d.rectangle([x, y, x + cell - 1, y + cell - 1], fill=255)
        return flat(m, col)
    return stack(draw('#14052e', 1.3, 1.3), draw(PINK, 0.7, 0.7), draw(CYAN, 0, 0), draw('#d6fbff', 0, 0, lambda r_, c_: (r_, c_ - 1) not in cells or (r_ - 1, c_) not in cells))

# ---------------------------------------------------------------- colour variants (hue-rotated renders of the neon styles)
def hue(img, deg):
    a = img.getchannel('A')
    h, sa, v = img.convert('RGB').convert('HSV').split()
    k = int(round(deg / 360 * 256))
    out = Image.merge('HSV', (h.point(lambda x: (x + k) % 256), sa, v)).convert('RGB')
    out.putalpha(a)
    return out

# ---------------------------------------------------------------- Material variants (flat, tonal, no glow)
PALS = {  # bg tile, blob (tonal container), primary
    'violet': ('#EADDFF', '#D0BCFF', '#4F378B'),
    'violet_dark': ('#1D1B20', '#4A4458', '#D0BCFF'),
    'teal': ('#CDE5E1', '#9FD0CB', '#00504F'),
    'rose': ('#FFD9E3', '#FFB0C8', '#8C2A4F'),
    'green': ('#CFF1D6', '#8DD4A0', '#1B5E34'),
}

def mat_bg(pal): return lambda s: flat(rr((-10, -10, 1034, 1034), 0, 1.0), PALS[pal][0])

def mat_note_fg(pal):
    bg, blob, pri = PALS[pal]
    return lambda s: stack(flat(circ((512, 512), 372, s), blob), flat(note_mask(s, 1.7), pri))

def mat_vinyl_fg(pal):
    bg, blob, pri = PALS[pal]
    def fg(s):
        cx, cy = Pt(512, 512, s)
        ang = np.arctan2(YS - cy, XS - cx)
        wedge = np.zeros((N, N), np.float32)
        for a0 in (-0.9, math.pi - 0.9):
            wedge = np.maximum(wedge, (np.abs(((ang - a0 + math.pi) % (2 * math.pi)) - math.pi) < 0.22).astype(np.float32))
        r = np.hypot(XS - cx, YS - cy)
        sh = Image.fromarray((wedge * 38 * ((r < Lg(380, s)) & (r > Lg(176, s)))).astype(np.uint8), 'L')
        rings = gmask()
        for rad in (230, 290, 340): rings = ImageChops.lighter(rings, sub(circ((512, 512), rad + 4, s), circ((512, 512), rad - 4, s)))
        rgb = np.empty((N, N, 3), np.float32); rgb[:] = hexc(blob)
        return stack(flat(circ((512, 512), 380, s), pri), flat(sh, '#ffffff'), colorize(rings.point(lambda v: int(v * 0.35)), rgb),
                     flat(circ((512, 512), 160, s), blob), flat(circ((512, 512), 38, s), bg))
    return fg

def mat_bars_fg(pal):
    bg, blob, pri = PALS[pal]
    def fg(s):
        out = []
        for i, (t, col) in enumerate(((470, pri), (300, blob), (190, pri), (360, blob), (520, pri))):
            x0 = 160 + i * 146
            out.append(flat(rr((x0, t, x0 + 104, 790), 52, s), col))
        return stack(*out)
    return fg

def mat_cassette_fg(pal):
    bg, blob, pri = PALS[pal]
    def fg(s):
        reels = circ((404, 450), 56, s); circ((620, 450), 56, s, reels)
        hubs = circ((404, 450), 26, s); circ((620, 450), 26, s, hubs)
        return stack(flat(rr((110, 250, 914, 790), 78, s), pri), flat(rr((184, 316, 840, 580), 44, s), bg), flat(rr((296, 372, 728, 528), 78, s), pri),
                     flat(reels, blob), flat(hubs, pri), flat(poly([(300, 790), (352, 650), (672, 650), (724, 790)], s), blob))
    return fg

# ---------------------------------------------------------------- registry and output
def camel(name): return ''.join(p.capitalize() for p in name.split('_'))

def shifted(base, deg):
    return (lambda s: hue(base[2](s), deg), lambda s: hue(base[3](s), deg), base[4])

BASE = [  # name, label, background(scale), foreground(scale), adaptive-layer scale
    ('vice', 'Vice City', lambda s: tile(), lambda s: art(s, False), 0.80),
    ('cassette', 'Cassette', lambda s: night('#2a0f5e', '#0f0630', PINK, gamt=0.22), cassette_fg, 0.74),
    ('vinyl', 'Vinyl', lambda s: night('#3a0d63', '#14032d', PINK, (0.5, 0.5), 0.2), vinyl_fg, 0.76),
    ('equalizer', 'Equalizer', lambda s: night('#1d0a40', '#08011a', '#7c5cff', (0.5, 0.45), 0.25), equalizer_fg, 0.76),
    ('note', 'Minimal Note', note_bg, note_fg, 0.80),
    ('grid', 'Synthwave Grid', grid_bg, grid_fg, 0.80),
    ('pixel', 'Pixel', pixel_bg, pixel_fg, 0.80),
]
B = {b[0]: b for b in BASE}
ICONS = list(BASE)
for name, label, base, deg in (('vice_ocean', 'Vice City Jungle', 'vice', 150), ('cassette_mint', 'Cassette Acid', 'cassette', 100),
                               ('vinyl_ember', 'Vinyl Plum', 'vinyl', 40), ('equalizer_sunset', 'Equalizer Lime', 'equalizer', 200),
                               ('grid_ocean', 'Grid Toxic', 'grid', 130), ('note_violet', 'Note Violet', 'note', 70), ('pixel_lime', 'Pixel Maroon', 'pixel', 90)):
    ICONS.append((name, label) + shifted(B[base], deg))
for name, label, fgf, pal in (('material_note', 'Material Note', mat_note_fg, 'violet'), ('material_note_dark', 'Material Note Dark', mat_note_fg, 'violet_dark'),
                              ('material_vinyl', 'Material Vinyl', mat_vinyl_fg, 'teal'), ('material_bars', 'Material Bars', mat_bars_fg, 'rose'),
                              ('material_cassette', 'Material Cassette', mat_cassette_fg, 'green')):
    ICONS.append((name, label, mat_bg(pal), fgf(pal), 0.80))

def write_generated():
    """Keeps the C++ list and the manifest aliases in step with ICONS."""
    hdr = ['// Generated by tools/make-icon.py: selectable app icons (assets/icons/styles/<name>-<size>.png; Android: one activity-alias each).',
           '#pragma once', 'struct AppIconDef { const char* name; const char* label; };', 'constexpr AppIconDef kAppIcons[] = {']
    hdr += [f'    {{"{n}", "{l}"}},' for n, l, *_ in ICONS]
    hdr += ['};', 'constexpr int kAppIconCount = sizeof(kAppIcons) / sizeof(kAppIcons[0]);', '']
    open(os.path.join(ROOT, 'src', 'app', 'app_icons.h'), 'w').write('\n'.join(hdr))
    mp = os.path.join(ROOT, 'android', 'app', 'src', 'main', 'AndroidManifest.xml')
    m = open(mp).read()
    al = ''
    for i, (n, *_) in enumerate(ICONS):
        icon = '@mipmap/ic_launcher' if i == 0 else f'@mipmap/ic_launcher_{n}'
        rnd = ' android:roundIcon="@mipmap/ic_launcher_round"' if i == 0 else ''
        al += (f'        <activity-alias android:name="org.xylonic.fiber.Icon{camel(n)}" android:targetActivity="org.xylonic.fiber.XylonicActivity" android:exported="true" '
               f'android:enabled="{"true" if i == 0 else "false"}" android:icon="{icon}"{rnd} android:label="@string/app_name">\n'
               '            <intent-filter>\n                <action android:name="android.intent.action.MAIN" />\n                <category android:name="android.intent.category.LAUNCHER" />\n'
               '            </intent-filter>\n        </activity-alias>\n')
    a, b = m.index('<!-- app-icons:begin -->'), m.index('<!-- app-icons:end -->')
    open(mp, 'w').write(m[:a] + '<!-- app-icons:begin -->\n' + al + '        ' + m[b:])

def main():
    only = set(sys.argv[1:])
    base = os.path.join(ROOT, 'assets', 'icons')
    res = os.path.join(ROOT, 'android', 'app', 'src', 'main', 'res')
    write_generated()
    for name, label, bgf, fgf, asc in ICONS:
        if only and name not in only: continue
        full = Image.alpha_composite(bgf(1.0), fgf(1.0))
        icon = rounded(full)
        for sz in (512, 256, 128):
            save(icon, os.path.join(base, 'styles', f'{name}-{sz}.png'), sz)
        if name == 'vice':
            for sz in (1024, 512, 256, 128, 64, 32, 16):
                save(icon, os.path.join(base, f'app-icon-{sz}.png'), sz)
            for d, sz in (('mdpi', 48), ('hdpi', 72), ('xhdpi', 96), ('xxhdpi', 144), ('xxxhdpi', 192)):
                save(icon, os.path.join(res, f'mipmap-{d}', 'ic_launcher.png'), sz)
                save(circle(full), os.path.join(res, f'mipmap-{d}', 'ic_launcher_round.png'), sz)
            save(bgf(asc), os.path.join(res, 'drawable-nodpi', 'ic_launcher_bg.png'), 432)
            save(fgf(asc), os.path.join(res, 'drawable-nodpi', 'ic_launcher_fg.png'), 432)
        else:
            save(icon, os.path.join(res, 'mipmap-xxhdpi', f'ic_launcher_{name}.png'), 144)   # API 24-25 fallback
            save(bgf(asc), os.path.join(res, 'drawable-nodpi', f'ic_launcher_{name}_bg.png'), 432)
            save(fgf(asc), os.path.join(res, 'drawable-nodpi', f'ic_launcher_{name}_fg.png'), 432)
            open(os.path.join(res, 'mipmap-anydpi-v26', f'ic_launcher_{name}.xml'), 'w').write(
                '<?xml version="1.0" encoding="utf-8"?>\n<adaptive-icon xmlns:android="http://schemas.android.com/apk/res/android">\n'
                f'    <background android:drawable="@drawable/ic_launcher_{name}_bg" />\n    <foreground android:drawable="@drawable/ic_launcher_{name}_fg" />\n'
                '    <monochrome android:drawable="@drawable/ic_stat_note" />\n</adaptive-icon>\n')
        print('wrote', name, flush=True)

if __name__ == '__main__':
    main()
