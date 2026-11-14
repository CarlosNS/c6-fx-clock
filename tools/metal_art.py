"""Paints the scenes of "Trueno y peluches" (src/fx_metal.c) as 16-bit style pixel art and writes them to
src/fx_metal_art.h as palette images (RLE), plus previews.

    python tools/metal_art.py [out_dir_for_previews]

Each scene is built from soft shapes (signed distance fields joined smoothly, so nothing looks like a
plain rectangle or triangle) lit from the scene's light: every shape gets a height profile, a normal and
3 to 5 tones of its own colour ramp, with a checker dither where two tones meet. Dark blue or dark brown
outlines separate a shape from what is behind it. Small things (eyes, seams, the tattoo) are placed by
hand. The watch only draws the picture and animates on top (dust in the light, the Zzz, breathing).

The canvas is art.c's: 137 x 168 art pixels, each 3 x 3 on the panel.
"""
import math
import os
import sys

import numpy as np
from PIL import Image, ImageDraw

W, H = 137, 168
NL = chr(10)
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
YY, XX = np.mgrid[0:H, 0:W].astype(np.float32)
XX += 0.5
YY += 0.5
CHECK = ((np.arange(W)[None, :] + np.arange(H)[:, None]) & 1).astype(bool)


def hexrgb(h):
    return ((h >> 16) & 255, (h >> 8) & 255, h & 255)


def noise(seed, scale=1.0):
    """Smooth value noise in 0..1 (bilinear over a coarse random grid)."""
    rng = np.random.default_rng(seed)
    gw, gh = int(W / scale) + 3, int(H / scale) + 3
    g = rng.random((gh, gw)).astype(np.float32)
    fx, fy = XX / scale, YY / scale
    x0, y0 = fx.astype(int), fy.astype(int)
    tx, ty = fx - x0, fy - y0
    tx, ty = tx * tx * (3 - 2 * tx), ty * ty * (3 - 2 * ty)
    a, b = g[y0, x0], g[y0, x0 + 1]
    c, d = g[y0 + 1, x0], g[y0 + 1, x0 + 1]
    return (a * (1 - tx) + b * tx) * (1 - ty) + (c * (1 - tx) + d * tx) * ty


def grain(seed):
    return np.random.default_rng(seed).random((H, W)).astype(np.float32)


# ---- signed distance fields (negative inside), in art pixels

def ellipse(cx, cy, rx, ry, ang=0.0):
    c, s = math.cos(ang), math.sin(ang)
    dx, dy = XX - cx, YY - cy
    u, v = dx * c + dy * s, -dx * s + dy * c
    k = np.sqrt((u / rx) ** 2 + (v / ry) ** 2)
    return (k - 1) * min(rx, ry)


def capsule(ax, ay, bx, by, r0, r1=None):
    r1 = r0 if r1 is None else r1
    px, py, bax, bay = XX - ax, YY - ay, bx - ax, by - ay
    h = np.clip((px * bax + py * bay) / (bax * bax + bay * bay), 0, 1)
    return np.hypot(px - bax * h, py - bay * h) - (r0 + (r1 - r0) * h)


def poly(pts):
    pts = np.asarray(pts, np.float32)
    n = len(pts)
    d = np.full((H, W), 1e9, np.float32)
    inside = np.zeros((H, W), bool)
    for i in range(n):
        ax, ay = pts[i]
        bx, by = pts[(i + 1) % n]
        px, py, ex, ey = XX - ax, YY - ay, bx - ax, by - ay
        h = np.clip((px * ex + py * ey) / (ex * ex + ey * ey), 0, 1)
        d = np.minimum(d, np.hypot(px - ex * h, py - ey * h))
        cross = ((ay <= YY) & (by > YY)) | ((by <= YY) & (ay > YY))
        xint = ax + (YY - ay) * (bx - ax) / np.where(by - ay == 0, 1e-6, by - ay)
        inside ^= cross & (XX < xint)
    return np.where(inside, -d, d)


def smin(a, b, k=4.0):
    h = np.clip(0.5 + 0.5 * (b - a) / k, 0, 1)
    return b * (1 - h) + a * h - k * h * (1 - h)


def cut(a, b):
    return np.maximum(a, -b)


def ridge(ax, ay, bx, by, w):
    """A soft crease along a segment, 1 on it and fading over w pixels."""
    d = capsule(ax, ay, bx, by, 0)
    return np.exp(-(d / w) ** 2)


# ---- the canvas: per pixel an object and a tone of that object's ramp

class Scene:
    def __init__(self, light=(-0.55, -0.6, 0.6)):
        self.obj = np.full((H, W), -1, np.int16)
        self.tone = np.zeros((H, W), np.int16)
        self.ramps = []      # per object: list of rgb, dark to light
        self.outline = []    # per object: rgb or None
        self.names = []
        self.fixed = np.zeros((H, W), bool)
        L = np.array(light, np.float32)
        self.L = L / np.linalg.norm(L)

    def new(self, name, ramp, outline=None):
        self.ramps.append([hexrgb(c) for c in ramp])
        self.outline.append(hexrgb(outline) if outline is not None else None)
        self.names.append(name)
        return len(self.ramps) - 1

    def shade(self, sdf, depth, roundness, bump=None, amb=0.28, kd=0.85, tex=None, bias=0.0):
        """Lit value 0..1 of a shape: a rounded height profile over `roundness` pixels plus bumps."""
        t = np.clip(-sdf / roundness, 0, 1)
        hgt = np.sqrt(1 - (1 - t) ** 2) * depth
        if bump is not None:
            hgt = hgt + bump
        gy, gx = np.gradient(hgt)
        n = np.stack([-gx, -gy, np.ones_like(gx)], -1)
        n /= np.linalg.norm(n, axis=-1, keepdims=True)
        v = amb + kd * np.clip(n @ self.L, 0, 1) + bias
        if tex is not None:
            v = v + tex
        return v

    def put(self, o, mask, v):
        n = len(self.ramps[o])
        t = np.clip(v, 0, 0.999) * (n - 1)
        base = np.floor(t)
        f = t - base
        idx = base + (f > 0.66) + ((f > 0.34) & (f <= 0.66) & CHECK)
        idx = np.clip(idx, 0, n - 1).astype(np.int16)
        self.obj[mask] = o
        self.tone[mask] = idx[mask]

    def fill(self, o, sdf, depth=4, roundness=6, **kw):
        mask = sdf < 0
        self.put(o, mask, self.shade(sdf, depth, roundness, **kw))
        return mask

    zoom = None  # (z, px, py): the camera, for pixels placed by hand

    def px(self, o, pts, tone):
        for x, y in pts:
            x, y = int(round(x)), int(round(y))
            xs, ys = [x], [y]
            if self.zoom:
                z, cx, cy = self.zoom
                xs = range(math.floor((x - cx) * z + cx), max(math.floor((x + 1 - cx) * z + cx), math.floor((x - cx) * z + cx) + 1))
                ys = range(math.floor((y - cy) * z + cy), max(math.floor((y + 1 - cy) * z + cy), math.floor((y - cy) * z + cy) + 1))
            for yy in ys:
                for xx in xs:
                    if 0 <= xx < W and 0 <= yy < H:
                        self.obj[yy, xx] = o
                        self.tone[yy, xx] = tone
                        self.fixed[yy, xx] = True

    def lighten(self, mask, steps=1, keep=()):
        """Moves pixels up their ramp (the light beam); a checker on the edge band."""
        for o in range(len(self.ramps)):
            if o in keep:
                continue
            m = mask & (self.obj == o) & ~self.fixed
            self.tone[m] = np.minimum(self.tone[m] + steps, len(self.ramps[o]) - 1)

    def outlines(self, skip=()):
        """1 px outline on each object's edge against whatever is behind it (lower object number)."""
        out = np.zeros((H, W), bool)
        o = self.obj
        for dy, dx in ((0, 1), (0, -1), (1, 0), (-1, 0)):
            nb = np.full((H, W), -1, np.int16)
            ys = slice(max(dy, 0), H + min(dy, 0))
            yd = slice(max(-dy, 0), H + min(-dy, 0))
            xs = slice(max(dx, 0), W + min(dx, 0))
            xd = slice(max(-dx, 0), W + min(-dx, 0))
            nb[yd, xd] = o[ys, xs]
            out |= (nb < o)
        self.edge = out
        self.skip = set(skip)

    def render(self):
        """Palette (list of rgb) and index image."""
        pal, lut, img = [], {}, np.zeros((H, W), np.int32)
        def ix(c):
            if c not in lut:
                lut[c] = len(pal)
                pal.append(c)
            return lut[c]
        for y in range(H):
            for x in range(W):
                o = self.obj[y, x]
                if o < 0:
                    c = (0, 0, 0)
                elif self.edge[y, x] and self.outline[o] is not None and o not in self.skip:
                    c = self.outline[o]
                else:
                    c = self.ramps[o][self.tone[y, x]]
                img[y, x] = ix(c)
        return pal, img


# ---------------------------------------------------------------- scene 1: asleep with Rosita (00-08)

def scene_sleep(breath=0):
    global XX, YY
    S = Scene(light=(-0.6, -0.55, 0.58))  # moonlight from the window, upper left
    b = -breath  # breathing in lifts the chest, the arm and the plush one pixel

    wall = S.new('wall', [0x090c1f, 0x0f1530, 0x161f44, 0x1f2b5a, 0x2c3c74])
    sky = S.new('sky', [0x14225a, 0x1d3274, 0x2a4690, 0x3d5fae])
    moon = S.new('moon', [0xaab8e0, 0xd8e0f8, 0xf4f6ff])
    frame = S.new('frame', [0x1c1220, 0x2e1e2c, 0x463040, 0x5e4458], 0x0a0610)
    curtain = S.new('curtain', [0x1a1030, 0x2a1a48, 0x3c2862, 0x50387c], 0x0a0618)
    poster = S.new('poster', [0x1a1622, 0x2a2232, 0x3c3044, 0x524258], 0x0a0810)
    bolt = S.new('bolt', [0x6a1a22, 0x9a2a2a, 0xc8483a, 0xe87a4a])
    sheet = S.new('sheet', [0x1e2450, 0x2c346a, 0x404a88, 0x5a64a6, 0x7a84c2], 0x0c0f26)
    pillow = S.new('pillow', [0x2e3466, 0x454d88, 0x6670ac, 0x8e98cc, 0xb8c0e6, 0xdde2fa], 0x12152e)
    hair = S.new('hair', [0x0a0812, 0x16101e, 0x241a2e, 0x362842, 0x4c3a58], 0x0a0a26)
    shirt = S.new('shirt', [0x0a0c16, 0x121626, 0x1c2238, 0x283050, 0x384268], 0x0a0c2a)
    skin = S.new('skin', [0x4a2a22, 0x744232, 0xa0624a, 0xc88a66, 0xe8b08a, 0xf8d0aa], 0x2c140e)
    ink = S.new('tattoo', [0x1c2440, 0x2c3a5e, 0x40507a])
    teddy = S.new('teddy', [0x5a1e44, 0x8e3268, 0xc24c8e, 0xe872ae, 0xff9ccb, 0xffc6e2], 0x3a1226)
    snout = S.new('snout', [0xb0507c, 0xe08cb4, 0xffbcd8, 0xffe2f0], 0x3a1226)
    blanket = S.new('blanket', [0x2a0a18, 0x4a1226, 0x6e1c34, 0x942a42, 0xb84452], 0x16040c)
    face = skin

    # -- the wall: dark paint with a faint brush texture
    S.put(wall, np.ones((H, W), bool), 0.32 + 0.14 * noise(11, 9) + 0.06 * (grain(12) - 0.5) - YY / H * 0.12)

    # -- the window, upper left, with the moon; a curtain pulled aside
    win = poly([(7, 33), (31, 31), (32, 72), (7, 74)])
    S.fill(frame, smin(win - 2.5, win - 2.5, 1), depth=2, roundness=2)
    pane = win + 0.5
    S.put(sky, pane < 0, 0.25 + 0.5 * (1 - (YY - 27) / 45) + 0.05 * (grain(3) - 0.5))
    S.fill(moon, ellipse(24, 42, 5, 5), depth=3, roundness=4, amb=0.45)
    S.put(sky, (ellipse(26.5, 40.5, 4.2, 4.2) < 0) & (pane < 0), 0.6)  # a crescent
    for i in range(9):
        x, y = 9 + (i * 37) % 22, 35 + (i * 23) % 34
        if (x - 24) ** 2 + (y - 42) ** 2 > 50:
            S.px(moon, [(x, y)], 1 + (i % 2))
    mull = (capsule(19, 31, 19, 74, 0.9) < 0) | (capsule(6, 53, 32, 52, 0.9) < 0)
    S.put(frame, mull & (win < 0), 0.55)
    cur = smin(capsule(5, 26, 3, 86, 3.5, 6), ellipse(3, 62, 5, 14, -0.1), 3)
    folds = 0.8 * np.sin((XX - 3) * 1.6 + YY * 0.05)
    S.fill(curtain, cur, depth=3, roundness=4, bump=folds)

    # -- a band poster on the right wall (a generic lightning skull, no real band)
    pst = poly([(104, 44), (131, 46), (130, 80), (103, 78)])
    S.fill(poster, pst, depth=1, roundness=2, amb=0.35)
    S.fill(bolt, poly([(118, 49), (110, 62), (116, 62), (111, 75), (124, 58), (117, 58), (122, 49)]), depth=1, roundness=1, amb=0.5)
    S.px(poster, [(106 + i, 47) for i in range(0, 9, 2)], 3)

    # -- the bed and the sleepers, closer to the camera: everything from here is drawn 15 % bigger,
    #    growing up from the bottom of the screen
    X0, Y0 = XX, YY
    S.zoom = (1.15, 68, 168)
    XX, YY = (X0 - 68) / 1.15 + 68, (Y0 - 168) / 1.15 + 168

    # -- the bed: mattress and sheet across the bottom
    mat = smin(poly([(-4, 112), (140, 104), (140, 170), (-4, 170)]), ellipse(30, 128, 40, 14), 6)
    S.fill(sheet, mat, depth=3, roundness=10,
           bump=1.6 * ridge(2, 156, 44, 134, 4.5) - 1.0 * ridge(20, 146, 60, 130, 3.5))

    # -- the pillow, crumpled, with a dent where the head is
    pil = smin(ellipse(30, 104, 34, 17, -0.08), ellipse(8, 113, 14, 12), 6)
    dent = 2.4 * np.exp(-(((XX - 38) / 16) ** 2 + ((YY - 100) / 9) ** 2))
    wr = (1.3 * ridge(4, 98, 18, 110, 1.8) + 1.1 * ridge(50, 92, 62, 106, 1.8) + 1.0 * ridge(8, 118, 30, 114, 1.8)
          + 0.8 * ridge(2, 106, 8, 94, 1.5))
    S.fill(pillow, pil, depth=6, roundness=12, bump=wr - dent)

    # -- the long hair spread over the pillow: clumps of strands flowing out from the head, ragged tips
    hcx, hcy = 37, 88
    ang = np.arctan2(YY - hcy, XX - hcx)
    rad = np.hypot(XX - hcx, YY - hcy)
    hr = smin(ellipse(30, 86, 23, 15, -0.3), ellipse(16, 102, 13, 9, 0.5), 7)
    hr = smin(hr, capsule(20, 96, 6, 116, 6, 2.5), 5)
    hr = smin(hr, capsule(46, 76, 58, 70, 4, 1.5), 3)
    hr = hr + 1.8 * np.sin(ang * 13 + 2.5 * noise(7, 6)) * np.clip((rad - 12) / 10, 0, 1)  # ragged tips
    warp = 3.0 * noise(5, 5)
    strands = 0.45 * np.sin(ang * 22 + warp) + 0.2 * np.sin(ang * 53 + 2 * warp)
    sheen = 0.35 * np.exp(-((rad - 19 - 2 * noise(8, 7)) / 2.5) ** 2) * (ang < 0.6)
    S.fill(hair, hr, depth=3, roundness=6, bump=strands, kd=0.8, amb=0.12, tex=sheen)

    # -- body: shoulder and chest in a black shirt, the rest under the blanket
    tor = smin(ellipse(66, 116 + b, 16, 13, 0.3), capsule(66, 120 + b, 132, 126 + b, 17, 19), 8)
    tfold = 1.0 * ridge(82, 110 + b, 98, 134 + b, 2.2) + 0.8 * ridge(106, 110 + b, 116, 132 + b, 2.2)
    S.fill(shirt, tor, depth=5, roundness=12, bump=tfold, amb=0.2)
    S.fill(skin, capsule(48, 100, 58, 108 + b, 5.5), depth=3, roundness=5)  # neck

    # -- the head, tilted on the pillow, face to us; F(u, v): face coordinates (u right, v down)
    a = math.radians(-24)
    ca, sa = math.cos(a), math.sin(a)
    def F(u, v):
        return hcx + u * ca - v * sa, hcy + u * sa + v * ca
    hd = smin(ellipse(hcx, hcy, 12, 14.5, a), ellipse(*F(0, 8.5), 9, 7, a), 4)
    S.fill(skin, hd, depth=5, roundness=9, amb=0.34)
    inner = hd < -0.5
    # stubble along the jaw, a soft shadow where the cheek sinks into the pillow
    jaw = inner & (ellipse(*F(0, 9), 10, 6.5, a) < 0) & (ellipse(*F(0, 4), 8, 6, a) > 0)
    S.put(skin, jaw & CHECK, 0.42)
    S.put(skin, inner & (ellipse(*F(-11, 3), 4, 8, a) < 0), 0.34)
    def arc(u, v, rx, ry, a0, a1, w, o, tone):
        cx, cy = F(u, v)
        dx, dy = XX - cx, YY - cy
        lu, lv = dx * ca + dy * sa, -dx * sa + dy * ca
        t = np.degrees(np.arctan2(lv / ry, lu / rx)) % 360
        m = (np.abs(ellipse(cx, cy, rx, ry, a)) < w) & (t >= a0) & (t <= a1)
        S.obj[m], S.tone[m], S.fixed[m] = o, tone, True
    for s_ in (-1, 1):
        arc(4.6 * s_, 0.6, 2.9, 1.7, 15, 165, 0.62, skin, 0)          # closed eyes
        arc(4.8 * s_, -2.6, 3.4, 1.6, 200, 340, 0.7, skin, 0)         # relaxed brows
        S.px(skin, [F(4.6 * s_ + 3.0 * s_, 0.4)], 0)                  # lash tip
    S.px(skin, [F(1.4, 2.0), F(1.8, 3.2), F(1.9, 4.2)], 2)           # nose, shadow side
    S.px(skin, [F(0.9, 5.0), F(-0.9, 5.1)], 1)                        # nostrils
    S.px(skin, [F(-1.2, 4.6), F(-0.2, 4.0)], 5)                       # light on the tip
    arc(0, 6.6, 2.8, 1.3, 25, 155, 0.55, skin, 1)                     # a little smile
    gt = inner & (ellipse(*F(0, 11), 2.6, 2.2, a) < 0)
    S.put(skin, gt & CHECK, 0.05)
    S.put(skin, gt & ~CHECK, 0.25)
    # hair over the top of the head and two locks framing the face, parted in the middle
    cap = cut(ellipse(*F(0, -8), 13.8, 9.5, a), ellipse(*F(0, 0), 9.8, 6.5, a))
    cap = smin(cap, capsule(*F(-10.5, -6), *F(-11.5, 9), 3.2, 1.6), 3)
    cap = smin(cap, capsule(*F(10.5, -6), *F(11.5, 8), 3.2, 1.4), 3)
    cap = cut(cap, capsule(*F(0, -14), *F(0.6, -8), 0.8))
    S.fill(hair, cap, depth=3, roundness=4, bump=0.5 * np.sin(ang * 30 + warp), amb=0.14, tex=sheen * 0.8)

    # -- Rosita: a round pink plush with floppy ears, seams and a stitched snout
    fur = 0.10 * (grain(21) - 0.5) + 0.05 * (noise(22, 2) - 0.5)
    tb = ellipse(86, 122 + b, 18, 16.5, 0.15)                 # body
    th = ellipse(79, 96 + b, 13.5, 12.5, -0.1)                # head, resting on the body
    S.fill(teddy, tb, depth=6, roundness=12, tex=fur)
    S.fill(teddy, capsule(72, 138 + b, 67, 148 + b, 6, 5.5), depth=4, roundness=6, tex=fur)    # legs on the blanket
    S.fill(teddy, capsule(99, 136 + b, 104, 147 + b, 6, 5.5), depth=4, roundness=6, tex=fur)
    S.fill(snout, ellipse(67, 147.5 + b, 3.5, 3, 0.4), depth=2, roundness=3)                   # paw pads
    S.fill(snout, ellipse(104.5, 146.5 + b, 3.5, 3, -0.4), depth=2, roundness=3)
    # floppy ears hanging at the sides of the head
    S.fill(teddy, ellipse(65.5, 97 + b, 4.6, 6.5, 0.45), depth=3, roundness=4, tex=fur)
    S.fill(teddy, ellipse(93, 95 + b, 4.6, 6.5, -0.5), depth=3, roundness=4, tex=fur)
    S.put(teddy, ellipse(66, 98 + b, 2.2, 4, 0.45) < 0, 0.95)                                # inner ears
    S.put(teddy, ellipse(92.5, 96 + b, 2.2, 4, -0.5) < 0, 0.95)
    S.fill(teddy, th, depth=6, roundness=10, tex=fur)
    S.fill(snout, ellipse(78.5, 102 + b, 5, 3.8), depth=3, roundness=4)
    S.px(teddy, [(77, 99 + b), (78, 99 + b), (79, 99 + b), (80, 99 + b), (78, 100 + b), (79, 100 + b)], 0)  # nose
    S.px(teddy, [(78, 99 + b)], 4)
    S.px(teddy, [(78, 101 + b), (79, 101 + b), (77, 102 + b), (80, 102 + b), (76, 102 + b), (81, 102 + b)], 0)  # stitched smile
    S.px(teddy, [(78, 102 + b), (79, 102 + b)], 0)
    S.px(snout, [(78, 102 + b), (79, 102 + b)], 1)
    for ex in (71, 84):  # big button eyes with a glint
        S.px(teddy, [(ex + i, 92 + b + j) for i in range(3) for j in range(3)], 0)
        S.px(teddy, [(ex, 92 + b), (ex + 2, 92 + b), (ex, 94 + b), (ex + 2, 94 + b)], 2)
        S.px(teddy, [(ex + 1, 92 + b)], 5)
    S.px(teddy, [(80 + (k > 4), 85 + b + k) for k in range(0, 8, 2)], 1)     # seam over the head
    S.px(teddy, [(90 + k // 4, 110 + b + k) for k in range(0, 26, 2)], 1)    # seam down the tummy
    S.px(teddy, [(97, 112 + b), (98, 113 + b), (96, 114 + b)], 1)            # a darned patch
    # her little arm over his
    S.fill(teddy, capsule(72, 112 + b, 66, 124 + b, 4.5, 4), depth=3, roundness=4, tex=fur)

    # -- his arm hugging her tight: a short sleeve over the shoulder, biceps, a sharp elbow, the forearm
    #    across her tummy and a fist sunk in her fur
    up = smin(capsule(61, 114 + b, 67, 133 + b, 5.6, 5.0), ellipse(62.5, 124 + b, 6.4, 4.3, 1.2), 2.5)
    fa = smin(capsule(67, 134 + b, 92, 126 + b, 5.0, 3.6), ellipse(74.5, 132.5 + b, 6.8, 4.6, -0.3), 2.5)
    S.fill(skin, smin(up, fa, 1.5), depth=6, roundness=5, amb=0.16, kd=0.95,
           bump=0.8 * ridge(70, 131 + b, 84, 128 + b, 1.6))
    S.fill(skin, smin(ellipse(96.5, 124 + b, 5, 4.4, -0.3), capsule(91, 127 + b, 94, 126 + b, 3.4), 1.5), depth=4, roundness=4, amb=0.2)
    S.px(skin, [(97, 122 + b), (97, 123 + b), (99, 122 + b), (99, 123 + b), (95, 123 + b), (95, 124 + b)], 1)   # between the fingers
    S.px(skin, [(96, 121 + b), (98, 120 + b), (100, 121 + b)], 5)                                            # knuckles
    S.fill(skin, capsule(91.5, 121.5 + b, 95, 120 + b, 1.6), depth=2, roundness=2, amb=0.3)                  # thumb
    S.fill(shirt, smin(capsule(58, 107 + b, 62, 113 + b, 8.2), ellipse(60, 111 + b, 9.5, 7, 0.8), 3), depth=4, roundness=6, amb=0.2)  # sleeve
    S.px(shirt, [(55 + k, 120 + b - k // 3) for k in range(0, 14)], 3)                                       # its hem
    # her little paw resting on his arm
    S.fill(teddy, capsule(71, 115 + b, 67, 123 + b, 4, 3.6), depth=3, roundness=4, tex=fur)
    # the tattoo on the forearm: a skull with crossed bones, faded blue ink
    tx, ty = 76, 130 + b
    sk = ['.XXXX.', 'XXXXXX', 'X..X.X', 'XXXXXX', '.X.X..']
    pts = [(tx + x, ty + y - 2) for y, r in enumerate(sk) for x, ch in enumerate(r) if ch == 'X']
    S.px(ink, pts, 1)
    S.px(ink, [(tx - 2, ty + 2), (tx - 1, ty + 1), (tx + 6, ty + 1), (tx + 7, ty + 2), (tx - 2, ty - 3), (tx + 7, ty - 3)], 2)

    # -- the blanket over the hips and the legs, a turned-down hem
    bl = poly([(98, 138 + b), (112, 126), (140, 118), (140, 170), (40, 170), (46, 156), (70, 150)])
    bl = smin(bl, ellipse(112, 150, 30, 16, -0.3), 6)
    bfold = (1.6 * ridge(96, 168, 120, 132, 3) + 1.3 * ridge(60, 168, 86, 150, 3) + 1.1 * ridge(126, 166, 137, 140, 3)
             - 1.2 * ridge(106, 140, 136, 126, 2))
    S.fill(blanket, bl, depth=6, roundness=12, bump=bfold, amb=0.24)
    hem = (capsule(46, 156, 72, 150, 2.2) < 0) | (capsule(72, 150, 100, 138 + b, 2.2) < 0) | (capsule(100, 138 + b, 140, 118, 2.2) < 0)
    S.put(blanket, hem & (bl < 1.5), 0.95)

    XX, YY = X0, Y0
    S.zoom = None

    # -- the moonbeam from the window across the bed: one step brighter, dithered edges
    d = np.array([0.78, 0.62])
    nrm = np.array([-d[1], d[0]])
    s = XX * nrm[0] + YY * nrm[1]
    along = XX * d[0] + YY * d[1]
    beam = (s > 14) & (s < 50) & (along > 36)
    soft = ((s > 11) & (s <= 14) | (s >= 50) & (s < 53)) & CHECK & (along > 36)
    S.lighten(beam | soft, 1)
    S.lighten(beam & (s > 22) & (s < 42) & (S.obj == wall), 1)

    S.outlines(skip={wall, sky, moon, ink, poster, snout})
    return S, (d, nrm)


# ---------------------------------------------------------------- scene 2: morning coffee (08-11)

def scene_coffee(blink=0):
    S = Scene(light=(0.65, -0.35, 0.65))  # warm dawn light from the window on the right

    tile = S.new('tile', [0x2e1c2a, 0x4a2c36, 0x6a4040, 0x8c5a4c, 0xb07a5e, 0xd09e74, 0xeac290])
    cab = S.new('cabinet', [0x1e1218, 0x32202a, 0x4a3036, 0x664440, 0x86604e], 0x120a0e)
    jar = S.new('jar', [0x3a3a52, 0x5a6080, 0x8a94b0, 0xc4ccdc], 0x16121e)
    plant = S.new('plant', [0x14261e, 0x1e3e2a, 0x2e5a34, 0x4a7a3e], 0x0a140e)
    sky = S.new('sky', [0x6a5a9a, 0x9a6a98, 0xd0809a, 0xf2a07c, 0xffc888, 0xffe2a8])
    sun = S.new('sun', [0xffe0a0, 0xfff0c8, 0xfffae8])
    roofs = S.new('roofs', [0x3a2a44, 0x4e3652, 0x664460])
    frame = S.new('frame', [0x5a4040, 0x8a6458, 0xb8907a, 0xdcbaa0], 0x2a1a1a)
    slat = S.new('blind', [0x7a5a50, 0xa88070, 0xd0aa8c, 0xecd0b0, 0xfff0d8], 0x3a2626)
    counter = S.new('counter', [0x2a1a22, 0x4a2e30, 0x6e4a40, 0x946a54], 0x160c10)
    moka = S.new('moka', [0x2a2a36, 0x4a4c5a, 0x7a7e8c, 0xb4b8c4, 0xe8ecf4], 0x12121a)
    hair = S.new('hair', [0x07050c, 0x110b16, 0x1c1322, 0x2a1e30, 0x3c2c40, 0x564052], 0x0c0a22)
    shirt = S.new('shirt', [0x0c0c16, 0x161624, 0x222234, 0x30304a, 0x42425e], 0x0a0a22)
    skull = S.new('skull', [0x6a6a78, 0x9a98a0, 0xc8c4c4])
    skin = S.new('skin', [0x4a2a24, 0x763e30, 0xa45e48, 0xcc8460, 0xeaa87a, 0xffcc9c], 0x2c140e)
    bags = S.new('bags', [0x5a3448, 0x7a4658])
    eye = S.new('eye', [0x1a1014, 0xd8c8c0, 0xfff4ec])
    mug = S.new('mug', [0x0e0a0e, 0x1c1418, 0x2e2228, 0x4a3a3c, 0x7a5e54], 0x060406)
    coffee = S.new('coffee', [0x1e0e08, 0x3a1e10, 0x6a3a1e])

    # -- the wall: square tiles with a little bevel each, warmer and brighter towards the window
    tx, ty = XX % 11, YY % 11
    grout = (tx < 1) | (ty < 1)
    tid = (XX // 11) * 7 + (YY // 11) * 13
    warm = np.clip((XX - 10) / 140, 0, 1) * 0.25 - np.clip((YY - 120) / 60, 0, 1) * 0.15
    bevel = np.where(tx < 2.2, 0.06, 0) + np.where(ty < 2.2, 0.05, 0) - np.where(tx > 9.2, 0.05, 0) - np.where(ty > 9.2, 0.05, 0)
    v = 0.36 + warm + bevel + 0.03 * np.sin(tid) + 0.03 * (grain(31) - 0.5)
    S.put(tile, np.ones((H, W), bool), np.where(grout, v - 0.12, v))

    # -- left: a cabinet corner and a shelf with jars and a little plant
    S.fill(cab, poly([(-4, -4), (30, -4), (30, 40), (-4, 40)]), depth=2, roundness=3, bump=0.6 * ridge(4, 0, 4, 38, 1.2))
    S.put(cab, (np.abs(poly([(3, 4), (22, 4), (22, 34), (3, 34)])) < 0.8), 0.15)           # door panel
    S.px(cab, [(25, y) for y in range(18, 26)], 4)                                     # handle
    S.fill(cab, capsule(-2, 70, 28, 70, 1.8), depth=1.5, roundness=2)                  # shelf
    S.fill(jar, smin(capsule(8, 58, 8, 66, 4), ellipse(8, 56, 3, 1.5), 1.5), depth=3, roundness=4)
    S.fill(jar, capsule(18, 61, 18, 66, 3.2), depth=3, roundness=3)
    S.fill(plant, smin(ellipse(24, 58, 4, 5), ellipse(21, 61, 3, 3), 2), depth=3, roundness=3, tex=0.15 * (grain(32) - 0.5))

    # -- the window on the right: dawn sky, the sun on the rooftops, blinds half down
    win = poly([(98, 44), (136, 43), (136, 104), (98, 104)])
    S.fill(frame, win - 2.5, depth=2, roundness=2.5)
    pane = win + 0.5
    S.put(sky, pane < 0, 0.05 + 0.95 * np.clip((YY - 50) / 50, 0, 1) + 0.03 * (grain(33) - 0.5))
    S.fill(sun, ellipse(116, 96, 7, 7), depth=2, roundness=4, amb=0.6)
    rf = poly([(98, 98), (106, 94), (112, 97), (118, 92), (126, 96), (136, 93), (136, 104), (98, 104)])
    S.put(roofs, (rf < 0) & (pane < 0), 0.3 + 0.2 * (grain(34) > 0.8))
    blinds_bottom = 74
    for y0 in range(45, blinds_bottom, 4):
        sl = capsule(99, y0 + 1.2, 135, y0 + 1.2, 1.4)
        S.fill(slat, sl, depth=1.6, roundness=1.6, amb=0.4)
    S.fill(slat, capsule(99, blinds_bottom + 1, 135, blinds_bottom + 1, 2), depth=2, roundness=2, amb=0.45)   # bottom bar
    S.fill(slat, capsule(130, 44, 130, 90, 0.6), depth=0.5, roundness=1, amb=0.5)                            # the cord
    S.fill(slat, ellipse(130, 91, 1.4, 2), depth=1, roundness=1.5)
    S.put(frame, (capsule(117, 76, 117, 104, 0.8) < 0) & (pane < 0), 0.55)                                   # mullion

    # -- the counter behind him, and a moka pot steaming quietly on it
    S.fill(counter, poly([(-4, 146), (140, 140), (140, 172), (-4, 172)]), depth=2, roundness=3, bump=0.8 * ridge(-4, 147, 140, 141, 1))
    mk = smin(poly([(116, 124), (128, 124), (126, 134), (128, 146), (114, 146), (116, 134)]), ellipse(122, 123, 5, 1.5), 1)
    S.fill(moka, mk, depth=4, roundness=5, bump=0.8 * ridge(115, 134, 129, 134, 0.8))
    S.fill(moka, smin(capsule(122, 118, 122, 121, 2), ellipse(122, 117.5, 1.4, 1.4), 1), depth=1, roundness=1)  # knob
    S.fill(cab, capsule(129, 128, 133, 138, 1.5), depth=1, roundness=1.5)                                     # handle

    # -- the hair behind: a long heavy mane falling over the shoulders
    hcx, hcy = 60, 82
    ang = np.arctan2(YY - hcy + 30, XX - hcx)
    back = smin(ellipse(60, 76, 20, 19), capsule(43, 84, 34, 136, 10, 7), 6)
    back = smin(back, capsule(77, 84, 88, 134, 10, 7), 6)
    back = back + 1.6 * np.sin(XX * 0.9 + 2 * noise(41, 5)) * np.clip((YY - 110) / 14, 0, 1)   # ragged ends
    strands = 0.5 * np.sin(XX * 1.25 + 2.5 * noise(42, 6) + YY * 0.05) + 0.2 * np.sin(XX * 2.9 + noise(43, 3) * 3)
    S.fill(hair, back, depth=4, roundness=7, bump=strands, kd=0.85, amb=0.12)

    # -- the worn black t-shirt, a crew neck, and a faded pixel skull
    tor = smin(capsule(28, 120, 94, 120, 15), ellipse(61, 156, 42, 30), 10)
    wear = 0.6 * ridge(44, 134, 52, 166, 2.2) + 0.5 * ridge(80, 132, 74, 166, 2.2) - 0.4 * ridge(60, 106, 60, 116, 2)
    S.fill(shirt, tor, depth=6, roundness=14, bump=wear, amb=0.18, tex=0.1 * (grain(44) > 0.93))
    S.fill(skin, capsule(60, 94, 60, 106, 6.5), depth=3, roundness=6, amb=0.2)    # neck
    S.put(shirt, (np.abs(ellipse(60, 106, 8.5, 4)) < 1.0) & (YY > 105), 0.55)       # collar
    sk = ['..XXXXXXX..', '.XXXXXXXXX.', 'XXXXXXXXXXX', 'XX..XXX..XX', 'XX..XXX..XX', 'XXXXX.XXXXX',
          '.XXXX.XXXX.', '..XXXXXXX..', '..X.X.X.X..', '..XXXXXXX..']
    gr = grain(45)
    pts = [(68 + x, 141 + y) for y, r in enumerate(sk) for x, ch in enumerate(r) if ch == 'X' and gr[141 + y, 68 + x] > 0.18]
    S.px(skull, pts, 1)
    S.px(skull, [p for p in pts if (p[0] + p[1]) % 3 == 0], 2)
    S.px(shirt, [(70, 144), (71, 144), (76, 144), (77, 144), (70, 145), (71, 145), (76, 145), (77, 145)], 0)  # eye holes

    # -- the head, front on, a long night behind it
    hcy = 84
    face = smin(ellipse(hcx, hcy, 13.5, 16), ellipse(hcx, hcy + 9, 10, 8), 4)
    S.fill(skin, face, depth=5, roundness=9, amb=0.3)
    inner = face < -0.5
    S.put(skin, inner & (ellipse(hcx, hcy + 11, 11, 6.5) < 0) & (ellipse(hcx, hcy + 5.5, 9, 6.5) > 0) & CHECK, 0.4)  # stubble
    ey = hcy + 1
    for ex in (53, 67):
        o = 1 if ex > hcx else -1  # towards the outside of the face
        if blink:
            S.px(skin, [(ex - 2, ey), (ex - 1, ey + 1), (ex, ey + 1), (ex + 1, ey + 1), (ex + 2, ey)], 0)
        else:
            # tired eyes: the upper lid down over half the eye, the dark iris peeking out below it
            S.px(eye, [(ex - 2, ey + 1), (ex + 2, ey + 1)], 1)
            S.px(eye, [(ex - 1, ey + 1), (ex, ey + 1), (ex + 1, ey + 1)], 0)
            S.px(eye, [(ex - o, ey + 1)], 2)
            S.px(skin, [(ex - 3, ey), (ex - 2, ey), (ex - 1, ey), (ex, ey), (ex + 1, ey), (ex + 2, ey), (ex + 3, ey + 1)], 0)
            S.px(skin, [(ex - 2, ey + 2), (ex - 1, ey + 2), (ex, ey + 2), (ex + 1, ey + 2), (ex + 2, ey + 2)], 3)
        S.px(bags, [(ex - 2, ey + 3), (ex - 1, ey + 3), (ex, ey + 3), (ex + 1, ey + 3), (ex + 2, ey + 3)], 1)   # bags under them
        S.px(bags, [(ex + 2 * o, ey + 3), (ex + 3 * o, ey + 2)], 0)
        S.px(skin, [(ex - 3, ey - 4), (ex - 2, ey - 4), (ex - 1, ey - 5), (ex, ey - 5), (ex + 1, ey - 5), (ex + 2, ey - 4), (ex + 3, ey - 4)], 0)  # brows
        S.px(skin, [(ex - 2, ey - 3), (ex + 2, ey - 3)], 1)
    S.px(skin, [(59, ey + 3), (59, ey + 4), (59, ey + 5), (59, ey + 6), (58, ey + 7)], 2)   # nose, shadow side
    S.px(skin, [(61, ey + 5), (61, ey + 6)], 5)
    S.px(skin, [(58, ey + 8), (62, ey + 8)], 1)
    S.px(skin, [(56, ey + 13), (57, ey + 12), (58, ey + 12), (59, ey + 12), (60, ey + 12), (61, ey + 12), (62, ey + 12), (63, ey + 13)], 1)  # flat, tired mouth
    S.px(skin, [(58, ey + 14), (59, ey + 14), (60, ey + 14), (61, ey + 14)], 3)
    # messy fringe: an uneven, ragged crown and a few locks falling over the forehead
    fr = ellipse(60, 66, 17, 8)
    fr = fr + 1.3 * np.sin(np.arctan2(YY - 72, XX - 60) * 11 + 2.5 * noise(47, 4)) * (YY < 68)
    fr = smin(fr, capsule(47, 66, 47, 82, 4.5, 2), 3)
    fr = smin(fr, capsule(55, 66, 52, 78, 4, 1.5), 3)
    fr = smin(fr, capsule(66, 66, 70, 79, 4, 1.5), 3)
    fr = smin(fr, capsule(73, 68, 74.5, 86, 4, 1.2), 2)
    S.fill(hair, fr, depth=3, roundness=4, bump=0.5 * np.sin(XX * 1.4 + noise(46, 4) * 2), amb=0.14, kd=0.7)
    S.fill(hair, capsule(61, 67, 64, 80, 1.6, 0.7), depth=1.5, roundness=2, amb=0.18)   # a stray lock down the middle
    # locks falling in front of the shoulders
    S.fill(hair, capsule(46, 96, 40, 126, 4.5, 2.5), depth=3, roundness=4, bump=0.5 * np.sin(XX * 1.5), amb=0.14, kd=0.7)
    S.fill(hair, capsule(75, 96, 81, 124, 4.5, 2.5), depth=3, roundness=4, bump=0.5 * np.sin(XX * 1.5), amb=0.2, kd=0.7)

    # -- the arms: short sleeves, elbows at his sides, forearms rising to the mug held under his chin
    for sx, ex_, hx in ((28, 38, 63), (94, 90, 84)):  # hands at y 126
        S.fill(shirt, ellipse(sx + (2 if sx < 60 else -2), 124, 8.5, 10.5, 0.2 if sx < 60 else -0.2), depth=4, roundness=7, amb=0.18)    # sleeve
        up = capsule(sx + (3 if sx < 60 else -3), 130, ex_, 168, 7, 6.5)
        fa = smin(capsule(ex_, 168, hx, 126, 7, 5), ellipse((ex_ * 2 + hx) / 3, 152, 7.5, 5.5, -1.1 if sx < 60 else 1.1), 2.5)
        S.fill(skin, smin(up, fa, 2), depth=6, roundness=6, amb=0.2)
    # the mug: black, with the warm light on its rim and on the side towards the window; coffee inside
    S.fill(mug, cut(ellipse(87, 119, 5, 7), ellipse(87, 119, 2.2, 4)), depth=2, roundness=2, amb=0.3)   # handle
    mg = smin(poly([(63, 106), (84, 106), (83, 133), (64, 133)]), ellipse(73.5, 133, 9.5, 1.8), 2)
    S.fill(mug, mg, depth=6, roundness=7, amb=0.16, kd=1.05)
    S.fill(mug, np.abs(ellipse(73.5, 106, 10.5, 2.6)) - 0.8, depth=1, roundness=1, amb=0.7)
    S.fill(coffee, ellipse(73.5, 106, 9, 1.8), depth=1, roundness=1.5, amb=0.4)
    # both hands round it, fingers over the front, thumbs on the rim
    for hx, d in ((63, 1), (84, -1)):
        S.fill(skin, ellipse(hx, 122, 5.5, 7), depth=4, roundness=4, amb=0.22)
        for k in range(4):
            y = 113 + k * 4
            S.fill(skin, capsule(hx, y, hx + d * 6, y + 0.5, 1.8, 1.6), depth=2, roundness=2, amb=0.25)
        S.fill(skin, capsule(hx - d, 110, hx + d * 4, 108.5, 1.7), depth=1.5, roundness=2, amb=0.3)

    # -- dawn through the blinds: thin stripes from the slats, a broad warm band below them
    ty0 = YY - (98 - XX) * 0.62
    left = XX < 98
    gaps = (ty0 > 45) & (ty0 < blinds_bottom) & (((ty0 - 45) % 4) > 2.6)
    band = (ty0 > blinds_bottom + 3) & (ty0 < 103)
    edge = ((ty0 > blinds_bottom + 1) & (ty0 <= blinds_bottom + 3) | (ty0 >= 103) & (ty0 < 105)) & CHECK
    S.lighten(left & (gaps | band | edge), 1, keep={mug, coffee})
    S.lighten(left & band & (S.obj == tile), 1)

    S.outlines(skip={tile, sky, sun, roofs, skull, bags, eye, coffee})
    return S, None


# ---------------------------------------------------------------- output

def rle(img):
    out, flat = [], img.flatten()
    i = 0
    while i < len(flat):
        j = i
        while j < len(flat) and flat[j] == flat[i] and j - i < 255:
            j += 1
        out += [j - i, int(flat[i])]
        i = j
    return out


def rgb565(c):
    """In the panel's byte order, as lcd.h's rgb565()."""
    r, g, b = c
    v = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
    return ((v >> 8) | (v << 8)) & 0xFFFF


def preview(img, pal, path):
    rgb = np.array(pal, np.uint8)[img]
    im = Image.fromarray(rgb, 'RGB').resize((W * 3, H * 3), Image.NEAREST).crop((0, 0, 410, 502))
    m = Image.new('L', im.size, 0)
    ImageDraw.Draw(m).rounded_rectangle((0, 0, 409, 501), 120, fill=255)
    bg = Image.new('RGB', im.size, (0, 0, 0))
    bg.paste(im, (0, 0), m)
    bg.save(path)
    return bg


# name, painter, frames (its argument: 0, 1, ...)
SCENES = [('sleep', scene_sleep, 2), ('coffee', scene_coffee, 2)]


def main():
    outdir = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, 'build_art')
    os.makedirs(outdir, exist_ok=True)
    only = sys.argv[2] if len(sys.argv) > 2 else None
    lines = ['// Generated by tools/metal_art.py: the pixel art of "Trueno y peluches" (fx_metal.c). Do not edit.',
             '#pragma once', '#include <stdint.h>', '']
    for name, paint, nf in SCENES:
        frames = [paint(k)[0].render() for k in range(nf)]
        pal = []  # one palette for all the frames of a scene
        for p, _ in frames:
            for c in p:
                if c not in pal:
                    pal.append(c)
        assert len(pal) <= 256, (name, len(pal))
        imgs = [np.array(p and [pal.index(c) for c in p], np.int32)[img] for p, img in frames]
        if not only or only == name:
            for k, img in enumerate(imgs):
                preview(img, pal, os.path.join(outdir, f'metal_{name}{"_" + str(k) if k else ""}.png'))
        lines.append(f'static const uint16_t metal_{name}_pal[{len(pal)}] = {{' +
                     ', '.join(f'0x{rgb565(c):04x}' for c in pal) + '};')
        for k, img in enumerate(imgs):
            data = rle(img)
            lines.append(f'static const uint8_t metal_{name}_{k}[{len(data)}] = {{')
            for i in range(0, len(data), 32):
                lines.append('    ' + ', '.join(str(v) for v in data[i:i + 32]) + ',')
            lines.append('};')
        print(name, 'colours', len(pal), 'bytes', [len(rle(i)) for i in imgs])
    open(os.path.join(ROOT, 'src', 'fx_metal_art.h'), 'w', newline=NL).write(NL.join(lines) + NL)


if __name__ == '__main__':
    main()
