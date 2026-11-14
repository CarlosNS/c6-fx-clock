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
    h = np.clip((px * bax + py * bay) / max(bax * bax + bay * bay, 1e-6), 0, 1)
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

    bg_key = (0, 0, 0)  # what is left where nothing was drawn (KEY: transparent)
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
                    c = self.bg_key
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
    # sloping shoulders and straight sides: the arms hang outside the body, nothing bulges past them
    tor = smin(capsule(36, 115, 84, 115, 10), poly([(31, 118), (89, 118), (87, 172), (33, 172)]), 6)
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
    for sx, ex_, hx in ((31, 36, 63), (89, 86, 84)):  # shoulder, elbow and hand x; hands at y 126
        o = -1 if sx < 60 else 1
        S.fill(skin, capsule(sx, 122, ex_, 166, 6.2, 6), depth=6, roundness=6, amb=0.2)                      # upper arm
        S.fill(shirt, smin(capsule(sx - o, 116, sx + o * 0.5, 131, 7, 6.6), ellipse(sx - o * 2, 117, 6, 5), 3),
               depth=4, roundness=6, amb=0.18)                                                                # short sleeve
        S.px(shirt, [(sx + k - 6, 131 + (k * o) // 6) for k in range(13)], 3)                                # its hem
        fa = smin(capsule(ex_, 166, hx, 126, 6.6, 4.8), ellipse((ex_ * 2 + hx) / 3, 152, 7, 5.2, 1.1 * o), 2.5)
        S.fill(skin, fa, depth=6, roundness=6, amb=0.2)                                                       # forearm
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


# ---------------------------------------------------------------- scene 3: tuning (11-13)

GTR_J = (70.0, 112.0)                 # where the neck meets the body
GTR_D = (0.912, 0.410)                # along the guitar, from the headstock towards the body
GTR_N = (-0.410, 0.912)               # across it


def G(a, p):
    """Guitar coordinates (a along the neck, + towards the body; p across) to the canvas."""
    return GTR_J[0] + a * GTR_D[0] + p * GTR_N[0], GTR_J[1] + a * GTR_D[1] + p * GTR_N[1]


def scene_tuning(k=0):
    S = Scene(light=(0.55, -0.6, 0.6))  # a warm lamp up on the right of the practice room
    drop = 2 * k                         # frame 1: the sweat drop has slid a little

    foam = S.new('foam', [0x0c0e18, 0x141826, 0x1e2436, 0x2a3248, 0x3a445e])
    cab = S.new('cab', [0x0a0a0e, 0x15151c, 0x22222c, 0x34343e, 0x4c4c58], 0x050508)
    grille = S.new('grille', [0x1a1712, 0x2a251c, 0x3c3428, 0x524636])
    metal = S.new('metal', [0x3a3a44, 0x6a6a76, 0xa4a4b0, 0xdcdce4], 0x101014)
    floor = S.new('floor', [0x1a1210, 0x2a1c16, 0x3e2a1e, 0x543a28, 0x6c4c32])
    cable = S.new('cable', [0x08080a, 0x16161a, 0x2a2a30], 0x040406)
    jeans = S.new('jeans', [0x0e1220, 0x182036, 0x24304e, 0x34446a, 0x4a5c86], 0x080a16)
    hair = S.new('hair', [0x07050c, 0x110b16, 0x1c1322, 0x2a1e30, 0x3c2c40, 0x564052], 0x0c0a22)
    shirt = S.new('shirt', [0x0a0a12, 0x141420, 0x20202e, 0x2e2e42, 0x40405a], 0x0a0a22)
    skin = S.new('skin', [0x4a2a24, 0x763e30, 0xa45e48, 0xcc8460, 0xeaa87a, 0xffcc9c], 0x2c140e)
    ink = S.new('tattoo', [0x18203a, 0x26324e, 0x384664])
    gtr = S.new('guitar', [0x3a0810, 0x6a0e18, 0x9e1a22, 0xd03030, 0xf06a50, 0xffb090], 0x1c0408)
    bind = S.new('binding', [0x8a8a8a, 0xdcdcdc, 0xffffff])
    neck = S.new('neck', [0x1c120c, 0x2e1e14, 0x46301e, 0x604228], 0x0e0806)
    hw = S.new('hardware', [0x0c0c10, 0x1e1e26, 0x3a3a46, 0x7a7a86, 0xd0d0da], 0x060608)
    strings = S.new('strings', [0x8a8a92, 0xc8c8d0, 0xf4f4fa])
    tuner = S.new('tuner', [0x0e0e14, 0x1e1e2a, 0x34344a, 0x50506a], 0x06060a)
    screen = S.new('screen', [0x0a1418, 0x12242a, 0x1c3a40])
    sweat = S.new('sweat', [0x4a8ac8, 0x8cc4f0, 0xd8f0ff, 0xffffff], 0x1a2a50)
    mouth = S.new('mouth', [0x2a0e10, 0xe8e0d0, 0xfff8ec])

    # -- the practice room: acoustic foam wedges on the wall, alternating direction tile by tile
    tx, ty = XX % 12, YY % 12
    odd = ((XX // 12 + YY // 12) % 2) == 1
    wedge = np.where(odd, np.abs((tx % 4) - 2), np.abs((ty % 4) - 2)) * 0.7
    S.put(foam, np.ones((H, W), bool), S.shade(np.full((H, W), -9.0, np.float32), 1, 1, bump=wedge, amb=0.25, kd=0.5)
          + 0.12 * np.clip((XX - 20) / 120, 0, 1) - np.clip((YY - 100) / 70, 0, 1) * 0.12)
    S.put(foam, (tx < 0.6) | (ty < 0.6), 0.05)
    # the floor: old boards
    fl = poly([(-4, 150), (140, 146), (140, 172), (-4, 172)])
    S.fill(floor, fl, depth=1, roundness=2, tex=0.12 * np.sin(YY * 2.4) + 0.08 * (noise(51, 6) - 0.5))
    # a cable curling on the floor
    for i in range(14):
        t0, t1 = i / 14 * 6.2, (i + 1) / 14 * 6.2
        S.fill(cable, capsule(18 + 10 * math.cos(t0), 160 + 3 * math.sin(t0), 18 + 10 * math.cos(t1), 160 + 3 * math.sin(t1), 1.2),
               depth=1, roundness=1.2, amb=0.4)
    # a speaker cabinet behind him on the right
    cb = poly([(110, 66), (142, 64), (142, 172), (110, 172)])
    S.fill(cab, cb, depth=3, roundness=3, tex=0.06 * (grain(52) - 0.5))
    gp = poly([(114, 72), (142, 70), (142, 150), (114, 150)])
    weave = 0.1 * ((XX.astype(int) + YY.astype(int)) % 2)
    S.put(grille, gp < 0, 0.45 + weave - 0.25 * np.clip((XX - 114) / 30, 0, 1) * 0)
    for cx, cy in ((112, 68), (112, 168)):
        S.fill(metal, ellipse(cx, cy, 3, 3), depth=2, roundness=2)

    # -- the combo amp he sits on: tolex, piping, a woven grille and corner caps
    amp = poly([(40, 134), (114, 134), (116, 172), (38, 172)])
    S.fill(cab, amp, depth=3, roundness=4, tex=0.08 * (grain(53) - 0.5))
    ag = poly([(46, 144), (108, 144), (109, 172), (45, 172)])
    S.put(grille, ag < 0, 0.4 + weave)
    S.put(metal, np.abs(ag) < 0.7, 0.55)                                # piping round the grille
    S.fill(metal, ellipse(41, 135, 3, 3), depth=2, roundness=2)
    S.fill(metal, ellipse(113, 135, 3, 3), depth=2, roundness=2)

    # -- legs: thighs forward over the amp, jeans, a torn knee
    S.fill(jeans, smin(capsule(56, 134, 48, 150, 10), capsule(48, 150, 46, 172, 9, 8), 4), depth=6, roundness=9,
           bump=0.8 * ridge(52, 140, 44, 158, 1.5))
    S.fill(jeans, smin(capsule(84, 136, 96, 152, 10), capsule(96, 152, 100, 172, 9, 8), 4), depth=6, roundness=9,
           bump=0.8 * ridge(90, 142, 100, 160, 1.5))
    S.put(skin, (ellipse(97, 153, 4, 2, 0.3) < 0), 0.55)                # skin through the torn knee
    S.px(bind, [(94, 152), (96, 151), (99, 154), (100, 155)], 1)       # loose threads

    # -- the hair behind: long, falling forward as he leans in
    hcx, hcy = 64, 74
    back = smin(ellipse(64, 70, 19, 18, 0.1), capsule(48, 78, 42, 120, 9, 6), 6)
    back = smin(back, capsule(80, 78, 88, 118, 9, 6), 6)
    back = back + 1.5 * np.sin(XX * 0.9 + 2 * noise(54, 5)) * np.clip((YY - 104) / 12, 0, 1)
    S.fill(hair, back, depth=4, roundness=7, bump=0.5 * np.sin(XX * 1.25 + 2.5 * noise(55, 6)), kd=0.7, amb=0.12)

    # -- a strong body: thick neck, broad sloping traps, a sleeveless black shirt
    S.fill(skin, smin(capsule(64, 86, 64, 100, 8), capsule(44, 100, 84, 100, 6), 5), depth=4, roundness=6, amb=0.22)  # neck, traps
    tor = smin(poly([(40, 100), (90, 100), (92, 138), (40, 138)]), capsule(42, 102, 88, 102, 7), 6)
    S.fill(shirt, tor, depth=6, roundness=12, bump=0.6 * ridge(64, 104, 64, 136, 2) + 0.5 * ridge(50, 120, 80, 122, 2), amb=0.18)
    S.put(shirt, (np.abs(ellipse(64, 99, 11, 6)) < 1) & (YY > 99), 0.55)                       # neck line
    S.put(shirt, (np.abs(capsule(44, 100, 46, 118, 4.5)) < 0.8), 0.6)                           # arm holes
    S.put(shirt, (np.abs(capsule(84, 100, 82, 118, 4.5)) < 0.8), 0.6)

    # -- his right arm (on the left), behind the neck: huge shoulder, tattooed, reaching up to the pegs
    up = smin(capsule(44, 104, 28, 118, 8, 7), ellipse(37, 110, 9, 7, -0.7), 3)
    S.fill(skin, up, depth=6, roundness=7, amb=0.2, bump=0.9 * ridge(42, 106, 32, 114, 2))
    fa = smin(capsule(28, 118, 21, 88, 7, 5), ellipse(25, 107, 6.2, 7.5, 0.2), 2.5)
    S.fill(skin, fa, depth=6, roundness=6, amb=0.22)
    # two tribal bands round the upper arm
    ax, ay, bx, by = 44, 104, 28, 118
    L2 = (bx - ax) ** 2 + (by - ay) ** 2
    t = ((XX - ax) * (bx - ax) + (YY - ay) * (by - ay)) / L2
    pp = ((XX - ax) * (by - ay) - (YY - ay) * (bx - ax)) / math.sqrt(L2)
    zig = 0.035 * np.sign(np.sin(pp * 1.6))
    bandm = ((np.abs(t - 0.42 - zig) < 0.05) | (np.abs(t - 0.62 + zig) < 0.03)) & (up < -0.8)
    S.put(ink, bandm, 0.5)
    # -- the guitar: a red V across his thigh, white binding, pickups, the neck at a real length
    body = poly([G(*q) for q in [(0, -8), (44, -25), (50, -13), (29, 0), (50, 13), (44, 25), (0, 8)]])
    S.fill(gtr, body + 1.2, depth=3, roundness=6, amb=0.3)
    S.put(bind, (body > -1.2) & (body < -0.2) & (S.obj == gtr), 0.6)
    for a0 in (11, 23):
        pu = capsule(*G(a0, -4.5), *G(a0, 4.5), 2)
        S.fill(hw, pu, depth=1.5, roundness=2, amb=0.25)
        S.px(hw, [G(a0, p) for p in (-3, -1.5, 0, 1.5, 3)], 3)
    S.fill(hw, capsule(*G(33, -3.5), *G(33, 3.5), 1.4), depth=1, roundness=1.5)              # bridge
    for p in (14, 18):
        S.fill(hw, ellipse(*G(40, p), 1.8, 1.8), depth=1.5, roundness=1.5)                     # knobs
    nk = capsule(*G(-48, 0), *G(2, 0), 2.8, 3.2)
    S.fill(neck, nk, depth=2, roundness=2.5, amb=0.3)
    for a0 in range(-46, 0, 5):
        S.px(hw, [G(a0 - (a0 + 46) * 0.15, p) for p in (-1.5, 0, 1.5)], 3)                     # frets, closer up the neck
    for a0 in (-36, -26, -16):
        S.px(bind, [G(a0 + 2, 0)], 2)                                                          # dot inlays
    hs = poly([G(*q) for q in [(-47, -3.2), (-58, -6.5), (-64, -2), (-62, 0), (-64, 2), (-58, 6.5), (-47, 3.2)]])
    S.fill(gtr, hs + 0.8, depth=2, roundness=3, amb=0.32)
    for a0 in (-51, -55, -59):
        for p in (-1, 1):
            S.fill(metal, ellipse(*G(a0, p * (6.2 + (a0 + 51) * -0.18)), 1.5, 1.5), depth=1, roundness=1.5)  # pegs
    for p in (-1.2, 1.2):
        S.fill(strings, capsule(*G(-47, p * 0.8), *G(33, p * 1.3), 0.3), depth=0.3, roundness=0.5, amb=0.1)

    # -- the clip-on tuner at the tip of the headstock: the clamp and a little screen
    tip = G(-63, 0)
    S.fill(tuner, capsule(tip[0] + 1, tip[1] - 1, tip[0] + 1, tip[1] - 7, 1.6), depth=1.5, roundness=1.5, amb=0.3)  # clamp
    tb = smin(poly([(tip[0] - 7, tip[1] - 17), (tip[0] + 9, tip[1] - 17), (tip[0] + 9, tip[1] - 7), (tip[0] - 7, tip[1] - 7)]),
              ellipse(tip[0] + 1, tip[1] - 12, 8, 5), 2)
    S.fill(tuner, tb, depth=3, roundness=3, amb=0.3)
    sc = poly([(tip[0] - 4, tip[1] - 15), (tip[0] + 6, tip[1] - 15), (tip[0] + 6, tip[1] - 9), (tip[0] - 4, tip[1] - 9)])
    S.put(screen, sc < 0, 0.3)
    TUNER_SCREEN = (int(tip[0] - 4), int(tip[1] - 15), 11, 7)

    # the hand pinching the top peg
    peg = G(-55, -6.9)
    S.fill(skin, smin(ellipse(peg[0] + 1.5, peg[1] + 4.5, 5, 4.5, 0.3), capsule(peg[0] + 2, peg[1] + 2, peg[0] - 1, peg[1] - 1, 1.8), 2),
           depth=3, roundness=3, amb=0.26)
    S.fill(skin, capsule(peg[0] - 2, peg[1] + 2 + k * 0.6, peg[0], peg[1] - 1 + k * 0.6, 1.5), depth=1.5, roundness=1.5, amb=0.3)  # thumb
    S.px(skin, [(peg[0] + 2, peg[1] + 3), (peg[0] + 3, peg[1] + 4)], 1)

    # -- his left arm (on the right): down to the strings, plucking near the bridge
    S.fill(skin, smin(capsule(88, 104, 102, 124, 8, 7), ellipse(92, 110, 8.5, 7, 0.9), 3), depth=6, roundness=7, amb=0.2,
           bump=0.9 * ridge(90, 106, 98, 118, 2))
    S.fill(skin, smin(capsule(102, 124, 88, 128, 6.5, 5), ellipse(97, 126, 6, 4.5, -0.3), 2), depth=5, roundness=6, amb=0.22)
    S.fill(skin, smin(ellipse(84, 127, 5, 4.5, -0.4), capsule(82, 124, 79, 122, 1.6), 1.5), depth=3, roundness=3, amb=0.24)
    S.px(skin, [(84, 125), (86, 126)], 1)
    # -- the head, leaning in, glaring at the tuner: frown, gritted teeth, a drop of sweat
    face = smin(ellipse(hcx, hcy, 13, 15.5, 0.12), ellipse(hcx - 1, hcy + 9, 10, 7.5, 0.12), 4)
    S.fill(skin, face, depth=5, roundness=9, amb=0.3)
    inner = face < -0.5
    S.put(skin, inner & (ellipse(hcx - 1, hcy + 10.5, 11, 6.5) < 0) & (ellipse(hcx, hcy + 5, 9, 6.5) > 0) & CHECK, 0.4)
    ey = hcy + 1
    # the eyes drawn by hand: thick brows in a hard V down onto narrowed lids, a sideways glare at the tuner
    EYE = ['BB......',   # brow, high at the outside...
           'bBBB....',
           '..bBBB..',
           '....bBBB',   # ...down to the nose
           '.LLLLLL.',   # the lid, pressed down
           '.WPPWW..',   # pupils to the left: looking at the tuner
           '..SSSS..']
    tone = {'B': (skin, 0), 'b': (skin, 1), 'L': (skin, 0), 'S': (skin, 2), 'W': (mouth, 1), 'P': (mouth, 0)}
    for ex, mirror in ((57, False), (70, True)):
        for r, row in enumerate(EYE):
            for c, ch in enumerate(row):
                if ch == '.':
                    continue
                cc = c
                if mirror and ch in 'BbLS':
                    cc = 7 - c
                if mirror and ch in 'PW':
                    cc = c + 1
                o_, tn = tone[ch]
                S.px(o_, [(ex - 4 + cc, ey - 5 + r)], tn)
    S.px(skin, [(63, ey - 4), (63, ey - 3), (64, ey - 4), (64, ey - 3)], 1)                    # the furrow
    S.px(skin, [(63, ey - 2), (64, ey - 2)], 2)
    S.px(skin, [(63, ey + 3), (63, ey + 4), (63, ey + 5), (62, ey + 6)], 2)                    # nose
    S.px(skin, [(65, ey + 4), (65, ey + 5)], 5)
    S.px(skin, [(62, ey + 7), (66, ey + 7)], 1)
    teeth = [(x, ey + 11) for x in range(59, 68)] + [(x, ey + 12) for x in range(59, 68)]
    S.px(mouth, teeth, 2)
    S.px(mouth, [(x, ey + 11) for x in (61, 64, 66)] + [(x, ey + 12) for x in (61, 64, 66)], 1)
    S.px(skin, [(x, ey + 10) for x in range(59, 68)] + [(58, ey + 11), (68, ey + 11)], 0)       # lips pulled tight
    S.px(skin, [(x, ey + 13) for x in range(59, 68)], 0)
    # fringe, damp, sticking to the forehead
    fr = ellipse(64, 60, 17, 7.5, 0.1)
    fr = fr + 1.0 * np.sin(np.arctan2(YY - 66, XX - 64) * 10 + 2 * noise(56, 4)) * (YY < 62)
    fr = smin(fr, capsule(51, 60, 50, 80, 4.5, 2), 3)
    fr = smin(fr, capsule(77, 62, 79, 82, 4.5, 2), 3)
    fr = smin(fr, capsule(58, 62, 56, 71, 3.5, 1.2), 2)
    S.fill(hair, fr, depth=3, roundness=4, bump=0.5 * np.sin(XX * 1.4 + noise(57, 4) * 2), amb=0.14, kd=0.7)
    S.fill(hair, capsule(50, 92, 44, 118, 4.5, 2.5), depth=3, roundness=4, bump=0.5 * np.sin(XX * 1.5), amb=0.14, kd=0.7)
    S.fill(hair, capsule(79, 92, 84, 116, 4.5, 2.5), depth=3, roundness=4, bump=0.5 * np.sin(XX * 1.5), amb=0.2, kd=0.7)
    # the comic sweat drop on his temple
    dx, dy = 73, 64 + drop
    S.fill(sweat, smin(ellipse(dx, dy + 2, 2.6, 3), capsule(dx, dy - 3.5, dx, dy + 1, 0.4, 2.2), 1.5), depth=2, roundness=2.5, amb=0.45)
    S.px(sweat, [(dx - 1, dy + 1)], 3)

    S.outlines(skip={foam, grille, floor, ink, bind, strings, screen, mouth})
    return S, TUNER_SCREEN


# ---------------------------------------------------------------- scene 4: garage rehearsal (13-16)

CONES = [(19, 100), (49, 100), (19, 134), (49, 134)]   # speaker centres (art px), radius 12
CAB = (4, 80, 64, 160)                                 # the 4x12 cabinet: x0, y0, x1, y1


def guitar(S, body_obj, J, D, body_pts, neck_len, head_pts, colours, amb=0.28, scale=1.0):
    """An electric guitar laid along D from its neck joint J: body polygon and headstock in guitar
    coordinates (a along, + towards the body; p across). Returns the coordinate function."""
    D = np.array(D, float) / np.linalg.norm(D)
    N = np.array([-D[1], D[0]])
    def g(a, p):
        return J[0] + a * D[0] + p * N[0], J[1] + a * D[1] + p * N[1]
    neck_o, hw_o, chrome_o, str_o, bind_o = colours
    body = poly([g(a * scale, p * scale) for a, p in body_pts])
    S.fill(body_obj, body + 0.8, depth=3, roundness=6, amb=amb, kd=0.9)
    S.put(bind_o, (body > -0.8) & (body < 0) & (S.obj == body_obj), 0.7)
    # chrome humbuckers with pole pieces, the bridge and the knobs
    for a0 in (10, 20):
        S.fill(chrome_o, capsule(*g(a0, -4.2), *g(a0, 4.2), 2.1), depth=2, roundness=2.2, amb=0.3, kd=1.1)
        S.px(chrome_o, [g(a0, p) for p in (-3, -1.5, 0, 1.5, 3)], 4)
    S.fill(chrome_o, capsule(*g(28, -3.6), *g(28, 3.6), 1.3), depth=1, roundness=1.4, amb=0.35)
    S.fill(chrome_o, capsule(*g(32, -3), *g(32, 3), 0.9), depth=1, roundness=1, amb=0.35)
    # the neck: rosewood, frets, inlays
    S.fill(neck_o, capsule(*g(-neck_len, 0), *g(2, 0), 2.6, 3.0), depth=2, roundness=2.5, amb=0.3)
    for k in range(10):
        a0 = -neck_len + 2 + k * (neck_len - 4) / 10 * (1 - k * 0.035)
        S.px(hw_o, [g(a0, p) for p in (-1, 1)], 2)
    hs = poly([g(-neck_len - a, p) for a, p in head_pts])
    S.fill(body_obj, hs + 0.6, depth=2, roundness=2.5, amb=amb)
    for i in range(3):
        S.fill(chrome_o, ellipse(*g(-neck_len - 3 - i * 3.6, -5.5 - i * 0.7), 1.3, 1.3), depth=1, roundness=1.3)
    # thin strings from the nut to the bridge
    for p in (-1.4, 0, 1.4):
        S.fill(str_o, capsule(*g(-neck_len, p * 0.7), *g(28, p * 1.2), 0.26), depth=0.3, roundness=0.4, amb=-0.1)
    return g


def scene_garage(k=0):
    S = Scene(light=(-0.35, -0.7, 0.6))  # the bare bulb up on the left

    carton = S.new('carton', [0x1a1418, 0x2a2022, 0x3c2e2c, 0x52403a, 0x6a5448, 0x846a58])
    floor = S.new('floor', [0x16141a, 0x222028, 0x302e36, 0x403e48, 0x54525c])
    tolex = S.new('tolex', [0x08080a, 0x111114, 0x1c1c22, 0x2a2a32, 0x3c3c46], 0x040406)
    piping = S.new('piping', [0x8a8270, 0xd0c6aa, 0xf4ecd4])
    gold = S.new('gold', [0x5a4418, 0x8e6c26, 0xc89c3c, 0xf0cc6a, 0xfff0b0], 0x2a1e08)
    knob = S.new('knob', [0x0c0c0e, 0x24242a, 0x4a4a54, 0x8a8a96], 0x060608)
    surround = S.new('surround', [0x060608, 0x101014, 0x1c1c24, 0x2c2c38])
    cone = S.new('cone', [0x121216, 0x1e1e24, 0x2c2c34, 0x3e3e48, 0x54545e])
    cap = S.new('dustcap', [0x2a2a30, 0x4a4a54, 0x74747e, 0xa8a8b4])
    metal = S.new('metal', [0x3a3a44, 0x6a6a76, 0xa4a4b0, 0xdcdce4, 0xffffff], 0x101014)
    cable = S.new('cable', [0x08080a, 0x16161a, 0x2a2a30, 0x404048], 0x040406)
    pedal = S.new('pedal', [0x3a0a40, 0x5c1466, 0x8a2494, 0xb444c0], 0x1a0420)
    gblack = S.new('guitar_black', [0x060608, 0x101016, 0x1e1e2a, 0x30304a, 0x4a4a70, 0x7070a0], 0x020204)
    gwhite = S.new('guitar_white', [0x6a6a78, 0x9898a6, 0xc4c4d0, 0xe4e4ec, 0xffffff], 0x1a1a24)
    neck = S.new('neck', [0x1c120c, 0x2e1e14, 0x46301e, 0x604228], 0x0e0806)
    hw = S.new('hardware', [0x0c0c10, 0x1e1e26, 0x3a3a46, 0x9a9aa6], 0x060608)
    chrome = S.new('chrome', [0x2a2a34, 0x5a5a68, 0x9a9aa8, 0xd8d8e4, 0xffffff], 0x0a0a10)
    strings = S.new('strings', [0x9a9aa2, 0xd0d0d8, 0xf4f4fa])
    bindw = S.new('binding', [0x8a8a8a, 0xdcdcdc, 0xffffff])
    bindr = S.new('binding_red', [0x6a1018, 0xb02028, 0xe04848])
    strap = S.new('strap', [0x2a140a, 0x46220e, 0x683416, 0x8c4c22], 0x140804)
    stand = S.new('stand', [0x0e0e12, 0x1c1c22, 0x30303a, 0x50505c], 0x060608)
    mic = S.new('mic', [0x2a2a30, 0x5a5a64, 0x9a9aa4, 0xd4d4dc], 0x0a0a0e)

    # -- the wall: egg cartons, rows of cups and bumps, lit from above
    cups = np.cos(XX * math.pi / 5) * np.cos(YY * math.pi / 5)
    S.put(carton, np.ones((H, W), bool),
          S.shade(np.full((H, W), -9.0, np.float32), 1, 1, bump=2.4 * cups, amb=0.28, kd=0.7)
          + 0.1 * (noise(61, 8) - 0.5) - np.clip((YY - 40) / 120, 0, 1) * 0.15 + 0.1 * np.clip(1 - XX / 90, 0, 1))
    S.put(carton, ((np.abs((XX % 40) - 0.5) < 0.6) | (np.abs((YY % 40) - 0.5) < 0.6)) & CHECK, 0.15)   # where the cartons meet
    # -- the floor: concrete
    S.fill(floor, poly([(-4, 156), (140, 152), (140, 172), (-4, 172)]), depth=1, roundness=2,
           tex=0.1 * (noise(62, 5) - 0.5) + 0.06 * (grain(63) - 0.5))

    # -- the half stack: head on top of a 4x12 cab
    x0, y0, x1, y1 = CAB
    S.fill(tolex, poly([(x0, 55), (x1, 55), (x1, 77), (x0, 77)]), depth=3, roundness=3, tex=0.08 * (grain(64) - 0.5))
    S.fill(gold, poly([(x0 + 3, 63), (x1 - 3, 63), (x1 - 3, 72), (x0 + 3, 72)]), depth=1, roundness=1.5, amb=0.4)
    for i in range(7):
        S.fill(knob, ellipse(x0 + 8 + i * 6.5, 67.5, 2, 2), depth=1.5, roundness=2, amb=0.35)
        S.px(gold, [(x0 + 8 + i * 6.5, 66)], 4)
    S.fill(piping, capsule(x0 + 18, 59, x1 - 18, 59, 1.6), depth=1, roundness=1.5, amb=0.6)   # a plain plate, no brand
    S.px(tolex, [(x0 + 22 + i * 3, 59 - (i % 2)) for i in range(7)], 2)                     # a scribble on it
    S.fill(tolex, capsule(x0 + 22, 54, x1 - 22, 54, 1.4), depth=1, roundness=1.4)          # handle
    S.fill(tolex, poly([(x0, y0), (x1, y0), (x1, y1), (x0, y1)]), depth=3, roundness=3, tex=0.08 * (grain(65) - 0.5))
    baffle = poly([(x0 + 3, y0 + 3), (x1 - 3, y0 + 3), (x1 - 3, y1 - 3), (x0 + 3, y1 - 3)])
    S.put(tolex, baffle < 0, 0.15)
    S.put(piping, np.abs(baffle) < 0.7, 0.6)
    for cx, cy in CONES:
        rr = np.hypot(XX - cx, YY - cy)
        S.put(surround, rr < 12, 0.35 + 0.25 * np.cos(np.arctan2(YY - cy, XX - cx) + 2.3))
        ridges = 0.25 * np.sin(rr * 2.2)
        S.put(cone, rr < 10.2, 0.45 + 0.35 * ((cy - YY) * 0.35 + (cx - XX) * 0.2) / 10 + ridges * 0.4)
        S.fill(cap, ellipse(cx, cy, 3.6, 3.6), depth=2.5, roundness=3.6, amb=0.3)
        for a in range(0, 360, 90):  # mounting screws
            S.px(metal, [(cx + 11.2 * math.cos(math.radians(a + 45)), cy + 11.2 * math.sin(math.radians(a + 45)))], 2)
    for cx, cy in ((x0 + 1, y0 + 1), (x1 - 1, y0 + 1), (x0 + 1, y1 - 1), (x1 - 1, y1 - 1), (x0 + 1, 56), (x1 - 1, 56)):
        S.fill(metal, ellipse(cx, cy, 2.6, 2.6), depth=2, roundness=2)

    # -- cables coiled on the floor, one running to a pedal
    def coil(cx, cy, rx, ry, turns, w, seed):
        n = int(18 * turns)
        pts = []
        for i in range(n + 1):
            t = i / 18 * 2 * math.pi
            r = 1 + 0.12 * math.sin(t * 1.7 + seed)
            pts.append((cx + rx * r * math.cos(t) + i * 0.08, cy + ry * r * math.sin(t)))
        for a_, b_ in zip(pts, pts[1:]):
            S.fill(cable, capsule(*a_, *b_, w), depth=1, roundness=w, amb=0.3)
    coil(70, 163, 10, 2.6, 2.3, 1.1, 1)
    S.fill(cable, capsule(48, 162, 76, 160, 1.0), depth=1, roundness=1, amb=0.3)
    S.fill(pedal, smin(poly([(36, 155), (48, 155), (49, 167), (35, 167)]), ellipse(42, 155, 6, 1.5), 1.5), depth=2, roundness=2.5, amb=0.35)
    S.fill(metal, ellipse(42, 163, 2.2, 1.6), depth=1.5, roundness=1.5)          # footswitch
    for x in (38, 46):
        S.fill(knob, ellipse(x, 157.5, 1.3, 1.1), depth=1, roundness=1.2)
    PEDAL_LED = (42, 159)

    # -- the mic stand between the amp and the guitars, the mic tilted on top
    S.fill(stand, capsule(70, 70, 70, 146, 1.1), depth=1, roundness=1.2, amb=0.3)
    for dx in (-8, 0, 8):
        S.fill(stand, capsule(70, 144, 70 + dx, 152, 1.0), depth=1, roundness=1, amb=0.3)
    S.fill(stand, ellipse(70, 104, 1.8, 1.8), depth=1.5, roundness=1.5)          # clutch
    S.fill(mic, smin(capsule(70, 70, 67, 63, 2.2, 1.8), ellipse(66, 60.5, 3.2, 3.2), 1.5), depth=2, roundness=2.5, amb=0.35)
    S.px(mic, [(65, 59), (67, 60), (65, 61), (67, 62)], 0)                        # its grille
    S.fill(cable, capsule(71, 74, 73, 146, 0.7), depth=0.5, roundness=0.7)

    # -- the guitars on a double stand, side by side, leaning apart: a black arrow-pointed one and a
    #    white spiky one, both bigger than life on this little screen
    S.fill(stand, capsule(86, 150, 124, 150, 1.4), depth=1, roundness=1.4)
    for x in (86, 124):
        S.fill(stand, capsule(x, 150, x + (-4 if x < 100 else 4), 162, 1.2), depth=1, roundness=1.2)
    bodyA = [(0, 6), (14, 9), (30, 22), (40, 21), (34, 8), (44, -4), (36, -10), (24, -12), (12, -7), (0, -6)]
    gA = guitar(S, gblack, (84, 96), (0.06, 1.0), bodyA, 40,
                [(0, -3), (6, -5), (16, -9), (18, -6), (8, 1), (4, 3), (0, 3)], (neck, hw, chrome, strings, bindr),
                amb=0.12, scale=1.25)
    bodyB = [(0, -6), (10, -10), (20, -20), (24, -12), (36, -16), (32, -4), (44, 2), (34, 8), (40, 18),
             (26, 12), (12, 9), (0, 6)]
    gB = guitar(S, gwhite, (118, 98), (-0.10, 1.0), bodyB, 40,
                [(0, -3), (5, -6), (16, -8), (14, -4), (19, 1), (6, 3), (0, 3)], (neck, hw, chrome, strings, bindw),
                scale=1.2)
    # leather straps hanging from the bottom strap buttons down to the floor, studded
    for g_, a_end, side in ((gA, 44 * 1.25, -1), (gB, 44 * 1.2, 1)):
        sx, sy = g_(a_end - 1, 0)
        ex_, ey_ = sx + side * 10, 165
        mx, my = sx + side * 2, ey_ + 2
        prev = (sx, sy)
        for i in range(1, 9):
            t = i / 8
            q = ((1 - t) ** 2 * sx + 2 * t * (1 - t) * mx + t * t * ex_, (1 - t) ** 2 * sy + 2 * t * (1 - t) * my + t * t * ey_)
            S.fill(strap, capsule(*prev, *q, 1.7), depth=1.5, roundness=1.7, amb=0.3)
            if i % 2 == 0:
                S.px(metal, [q], 3)
            prev = q

    S.outlines(skip={carton, floor, piping, surround, cone, strings, bindw, bindr, chrome})
    return S, (CONES, PEDAL_LED)


# ---------------------------------------------------------------- scene 5: on the road (16-19)

BUS_WHEELS = [(56, 137), (116, 137)]   # wheel centres (art px), for the spinning hubcaps
KEY = (255, 0, 255)                     # transparent colour in a sprite frame


def scene_bus(k=0):
    """Frame 0: the background (sky, sun, mountains, the road and the headlight beam on it).
    Frame 1: the van alone, the rest transparent; the watch puts the moving things between the two."""
    S = Scene(light=(0.7, -0.45, 0.55))  # the setting sun, behind on the right

    sky = S.new('sky', [0x2a1a4a, 0x4a2260, 0x7a2c6a, 0xb03e64, 0xe0604e, 0xf6904a, 0xffc060, 0xffe08a])
    sun = S.new('sun', [0xffb050, 0xffd070, 0xffe8a0, 0xfff8dc])
    mfar = S.new('mountains_far', [0x5a2a5a, 0x6e3462, 0x84406a, 0x9a4e72])
    mnear = S.new('mountains_near', [0x2a1638, 0x381c46, 0x482452, 0x5a2e5e])
    road = S.new('road', [0x16141e, 0x201e2a, 0x2c2a38, 0x3a3848, 0x4a485a])
    lit = S.new('road_lit', [0x3a3020, 0x5a4a2a, 0x806a36, 0xa88c44, 0xd0b058])
    line = S.new('line', [0x8a8478, 0xd8d0c0, 0xf4eee0])
    body = S.new('van', [0x040406, 0x09090d, 0x111118, 0x1c1c26, 0x2c2c3a, 0x46465a], 0x020203)
    glass = S.new('glass', [0x140c1e, 0x241632, 0x3a2448, 0x6a3a5a, 0xb0605a, 0xf0a070], 0x08040c)
    chrome = S.new('chrome', [0x3a3440, 0x6a6070, 0xa49aa8, 0xdcd4dc, 0xffffff], 0x100c14)
    lamp = S.new('lamp', [0xffd060, 0xffec9c, 0xfffbe8], 0x3a2a10)
    tyre = S.new('tyre', [0x08080a, 0x141418, 0x222228, 0x34343c], 0x040406)
    fl_r = S.new('flame_red', [0x7a0e10, 0xb01e18, 0xd83a1e])
    fl_o = S.new('flame_orange', [0xd85a10, 0xf08a1e, 0xffaa30])
    fl_y = S.new('flame_yellow', [0xffc83a, 0xffe070, 0xfff4b0])
    gear = S.new('gear', [0x0a0a0e, 0x16161c, 0x262630, 0x3a3a46, 0x54545e], 0x050508)
    case = S.new('case', [0x2a1810, 0x44281a, 0x643c24, 0x8a5634], 0x140a06)
    strap = S.new('strap', [0x8a3a10, 0xc85a1a, 0xf08a30])
    rack = S.new('rack', [0x1a1a20, 0x34343e, 0x5a5a66, 0x8a8a96], 0x08080c)

    if k == 0:
        # -- the sky: dusk above, gold at the horizon
        S.put(sky, np.ones((H, W), bool), np.clip((YY - 4) / 96, 0, 1) * 0.98 + 0.03 * (grain(71) - 0.5))
        S.fill(sun, ellipse(24, 88, 19, 19), depth=2, roundness=20, amb=0.6, kd=0.3)
        for y0 in (80, 86, 91):  # bands of cloud across the sun
            S.put(sky, (np.abs(YY - y0) < 0.8) & (ellipse(24, 88, 21, 21) < 0) & (np.abs(XX - 24 - (y0 - 86) * 2) < 18), 0.86)
        # -- mountains: a far hazy range and a nearer darker one
        far = 88 - 7 * np.sin(XX * 0.05 + 1) + 6 * np.clip(1 - XX / 50, 0, 1) - 5 * np.sin(XX * 0.13 + 2) - 3 * noise(72, 4)
        S.put(mfar, YY > far, 0.35 + 0.4 * np.clip((XX - 40) / 100, 0, 1) - 0.25 * np.clip((YY - far) / 20, 0, 1))
        near = 95 - 5 * np.sin(XX * 0.07 + 4) + 4 * np.clip(1 - XX / 50, 0, 1) - 4 * np.sin(XX * 0.17) - 2 * noise(73, 3)
        S.put(mnear, YY > near, 0.3 + 0.3 * np.clip((XX - 60) / 80, 0, 1) - 0.2 * np.clip((YY - near) / 10, 0, 1))
        # -- the road: asphalt, the edge line, and the headlight beam lying on it
        S.put(road, YY > 104, 0.35 + 0.25 * np.clip((YY - 104) / 64, 0, 1) + 0.1 * (grain(74) - 0.5) - 0.1 * (grain(75) > 0.92))
        S.put(line, (YY > 106) & (YY < 108), 0.7)
        beam = poly([(34, 112), (34, 118), (-6, 166), (-6, 128)])
        tv = 0.35 + 0.25 * np.clip((YY - 104) / 64, 0, 1) + 0.1 * (grain(74) - 0.5) + 0.35 * np.clip(1 - (34 - XX) / 50, 0, 1)
        S.put(lit, (beam < -1.5) & (YY > 108), tv)
        S.put(lit, (beam >= -1.5) & (beam < 0) & (YY > 108) & CHECK, tv - 0.15)
        S.outlines(skip={sky, sun, mfar, mnear, road, lit, line})
        return S, BUS_WHEELS

    S.bg_key = KEY
    # ---------------- the van, facing left, the front a little towards us
    # roof rack and the gear strapped on it
    S.fill(rack, capsule(52, 78, 126, 77, 1.2), depth=1, roundness=1.2, amb=0.3)
    for x in (56, 76, 96, 120):
        S.fill(rack, capsule(x, 77, x, 81, 0.9), depth=1, roundness=1, amb=0.3)
    S.fill(gear, poly([(58, 62), (78, 62), (78, 76), (58, 76)]), depth=3, roundness=3)              # an amp
    S.put(gear, (np.abs(poly([(60, 65), (76, 65), (76, 74), (60, 74)])) < 0.7), 0.7)
    S.fill(case, smin(capsule(82, 70, 98, 70, 6.2), ellipse(98, 70, 2.5, 6.2), 1), depth=4, roundness=6)  # drum case
    S.fill(rack, ellipse(82, 70, 2.4, 6), depth=2, roundness=2, amb=0.4)
    S.fill(gear, poly([(102, 68), (110, 66), (126, 68), (126, 76), (110, 76), (102, 75)]), depth=3, roundness=3)  # guitar case
    S.fill(rack, capsule(104, 71, 124, 71, 0.6), depth=0.5, roundness=0.6, amb=0.5)
    for x in (68, 90, 114):
        S.put(strap, (np.abs(XX - x) < 1.2) & (YY > 61) & (YY < 78) & (S.obj >= 0), 0.6)
    # the body
    van = poly([(48, 80), (128, 78), (133, 82), (134, 132), (42, 134), (38, 128), (38, 104), (44, 90)])
    van = cut(van, ellipse(56, 137, 13, 12))
    van = cut(van, ellipse(116, 137, 13, 12))
    S.fill(body, van, depth=5, roundness=8, amb=0.22, bump=0.8 * ridge(42, 106, 134, 104, 1.2))
    # painted flames licking back along the side
    inside = (van < -1.5) & (XX > 40)
    for core, (o_, scale_) in enumerate(((fl_r, 1.0), (fl_o, 0.72), (fl_y, 0.42))):
        m = np.zeros((H, W), bool)
        for yc, L, w in ((110, 62, 3.6), (117, 80, 4.2), (124, 56, 4.0), (130, 74, 3.0)):
            u = (XX - 41) / (L * (0.55 + 0.45 * scale_))          # 0 at the nose, 1 at the tip
            curve = -3.5 * u * u                                   # tips curl up as they trail back
            m |= (u >= 0) & (u < 1) & (np.abs(YY - yc - curve) < w * scale_ * np.clip(1 - u, 0, 1) ** 0.8 + 0.4 * (core == 0))
        S.put(o_, inside & m, 0.6 + 0.25 * np.clip(1 - (XX - 41) / 40, 0, 1))
    # windows: the windscreen and three side windows, the sunset in them
    for wpts in ([(48, 85), (60, 84), (60, 101), (43, 102)], [(64, 86), (82, 86), (82, 100), (64, 100)],
                 [(86, 86), (104, 86), (104, 100), (86, 100)], [(108, 85), (126, 85), (126, 100), (108, 100)]):
        wp = poly(wpts)
        S.fill(glass, wp + 0.3, depth=1, roundness=2, amb=0.2,
               tex=0.5 * np.clip((YY - 86) / 14, 0, 1) + 0.25 * (np.abs((XX - YY * 0.6) % 14 - 3) < 1.5))
    # the front, three quarters: chrome grille, the headlight on, the bumper
    face = poly([(31, 104), (39, 101), (39, 134), (31, 132)])
    S.fill(body, face, depth=2, roundness=3, amb=0.3)
    for y in range(117, 129, 2):
        S.fill(chrome, capsule(32, y, 38, y - 0.4, 0.6), depth=0.6, roundness=0.7, amb=0.4)
    S.fill(lamp, ellipse(35, 110, 3.2, 3.6), depth=2, roundness=3, amb=0.8)
    S.fill(chrome, smin(capsule(28, 134, 134, 134, 2.2), ellipse(31, 133, 3.5, 2.6), 2), depth=2, roundness=2.2, amb=0.35, kd=1.1)
    S.fill(chrome, capsule(44, 108, 47, 108, 0.8), depth=0.6, roundness=0.8, amb=0.4)  # door handle... and a mirror
    S.fill(chrome, smin(ellipse(43, 92, 2, 3), capsule(43, 94, 46, 96, 0.6), 1), depth=1, roundness=1.5, amb=0.4)
    # wheels: fat tyres, chrome hubcaps
    for cx, cy in BUS_WHEELS:
        S.fill(tyre, ellipse(cx, cy, 10, 10), depth=4, roundness=5, amb=0.2)
        S.fill(chrome, ellipse(cx, cy, 5, 5), depth=3, roundness=4, amb=0.35, kd=1.1)
        S.px(chrome, [(cx, cy)], 0)
    S.outlines(skip={fl_r, fl_o, fl_y, strap})
    return S, BUS_WHEELS


# ---------------------------------------------------------------- scene 6: the show (19-23)

SHOW_LAMPS = [(18, 24), (42, 22), (68, 21), (94, 22), (119, 24)]   # spotlights on the truss (beams start here)


def scene_show(k=0):
    """Frame 0: the stage (truss, backdrop, the band backlit, amps). Frames 1 and 2: the crowd in front,
    horns up, two poses; the rest transparent. The watch adds the beams and the fire in between."""
    S = Scene(light=(0.0, -0.8, 0.5))  # backlight from the stage

    back = S.new('backdrop', [0x050308, 0x0a0610, 0x120a1c, 0x1c1028, 0x281636])
    haze = S.new('haze', [0x1a0e26, 0x2a1438, 0x3e1c48])
    truss = S.new('truss', [0x0e0e14, 0x1e1e28, 0x343442, 0x5a5a6a], 0x050508)
    can = S.new('can', [0x0a0a0e, 0x1a1a22, 0x2e2e3a], 0x040406)
    lens = S.new('lens', [0xffb060, 0xffe0a0, 0xffffff])
    stage = S.new('stage', [0x0a080c, 0x16121a, 0x241c28, 0x3a2c3a], 0x040306)
    sil = S.new('silhouette', [0x050307, 0x0c0810, 0x160e1c], 0x020103)
    rim = S.new('rim', [0x7a3a5a, 0xc0604a, 0xff9a50])
    crowd = S.new('crowd', [0x020103, 0x07040a, 0x0e0814, 0x1a1020], 0x010002)
    crim = S.new('crowd_rim', [0x3a1830, 0x6a2a40, 0xa04a40])
    glow = S.new('glow', [0x1a0618, 0x3a0c28, 0x6a1834, 0xa02c38, 0xd8503c, 0xff8a4a, 0xffc070])

    if k == 0:
        S.put(back, np.ones((H, W), bool), 0.15 + 0.6 * np.exp(-((XX - 68) / 60) ** 2 - ((YY - 100) / 40) ** 2)
              + 0.08 * (noise(81, 10) - 0.5))
        S.put(haze, (noise(82, 12) > 0.7) & CHECK & (YY > 40) & (YY < 118), 0.2)
        # the backdrop: a faint lightning skull of no band in particular
        S.put(back, (poly([(72, 40), (62, 60), (70, 60), (63, 80), (80, 54), (71, 54), (78, 40)]) < 0), 0.75)
        # amp walls at the sides of the stage
        for x0, x1 in ((2, 30), (107, 135)):
            for y0 in (88, 104):
                S.fill(sil, poly([(x0, y0), (x1, y0), (x1, y0 + 15), (x0, y0 + 15)]), depth=2, roundness=2, amb=0.25)
                S.put(sil, (((XX - x0) % 7 < 1) | ((YY - y0) % 7 < 1)) & (XX > x0 + 1) & (XX < x1 - 1) & (YY > y0 + 1) & (YY < y0 + 14), 0.9)
        # drum riser with the kit, the drummer's head and raised sticks
        S.fill(stage, poly([(46, 108), (90, 108), (92, 120), (44, 120)]), depth=2, roundness=2)
        S.fill(sil, smin(ellipse(68, 110, 9, 6), ellipse(56, 104, 5, 3), 2), depth=3, roundness=4)
        S.fill(sil, ellipse(80, 104, 5, 3), depth=2, roundness=3)
        S.fill(sil, smin(ellipse(68, 96, 4, 4.5), ellipse(68, 103, 6, 5), 2), depth=3, roundness=4)
        S.fill(sil, capsule(64, 98, 58, 90, 0.8), depth=1, roundness=1)
        S.fill(sil, capsule(72, 98, 79, 91, 0.8), depth=1, roundness=1)
        for cx in (52, 86):
            S.fill(sil, capsule(cx - 6, 95, cx + 6, 94, 0.9), depth=1, roundness=1)   # cymbals
            S.fill(sil, capsule(cx, 95, cx, 108, 0.5), depth=1, roundness=1)
        # the guitarist (left) headbanging, hair flying, and the bassist (right), legs apart
        def player(x, flip, lean):
            f = -1 if flip else 1
            S.fill(sil, capsule(x - 3, 118, x - 1, 104, 2.4), depth=2, roundness=2)
            S.fill(sil, capsule(x + 4, 118, x + 1, 104, 2.4), depth=2, roundness=2)
            S.fill(sil, smin(capsule(x, 104, x + f * lean, 90, 4.5, 4), ellipse(x + f * lean, 91, 5.5, 3.5), 2), depth=3, roundness=4)
            S.fill(sil, ellipse(x + f * (lean + 3), 86, 3.6, 4), depth=3, roundness=3)
            S.fill(sil, smin(capsule(x + f * (lean + 3), 84, x + f * (lean + 12), 92, 2.5, 0.8),
                             capsule(x + f * (lean + 2), 85, x + f * (lean + 10), 96, 2, 0.6), 2), depth=2, roundness=2)   # hair
            S.fill(sil, poly([(x - f * 2, 96), (x - f * 12, 103), (x - f * 14, 99), (x - f * 4, 92)]), depth=1, roundness=1.5)  # neck
            S.fill(sil, smin(ellipse(x + f * 2, 99, 6, 4.5, 0.4 * f), poly([(x + f * 6, 98), (x + f * 12, 104), (x + f * 8, 105)]), 1.5),
                   depth=2, roundness=2)                                                                                     # body
        player(36, False, 4)
        player(101, True, 3)
        # backlight catching the top edges of everything on stage
        on = (S.obj == sil)
        up = np.zeros((H, W), bool)
        up[1:, :] = on[1:, :] & ~on[:-1, :]
        S.put(rim, up & (YY > 80), 0.5 + 0.4 * (np.abs(XX - 68) < 40))
        S.fill(stage, poly([(-4, 118), (140, 118), (140, 126), (-4, 126)]), depth=2, roundness=2,
               bump=0.8 * ridge(-4, 119, 140, 119, 1))
        # the glow spilling off the front of the stage, behind the crowd
        S.put(glow, YY >= 126, 0.95 - np.clip((YY - 126) / 46, 0, 1) * 0.75 - 0.25 * np.clip(np.abs(XX - 68) / 80, 0, 1)
              + 0.04 * (grain(85) - 0.5))
        # the truss with its spotlights
        S.fill(truss, capsule(2, 16, 135, 16, 1.2), depth=1, roundness=1.2)
        S.fill(truss, capsule(2, 20, 135, 20, 1.2), depth=1, roundness=1.2)
        S.put(truss, ((np.abs(((XX + YY) % 8) - 4) < 0.8) | (np.abs(((XX - YY) % 8) - 4) < 0.8)) & (YY > 16) & (YY < 20), 0.6)
        for lx, ly in SHOW_LAMPS:
            S.fill(can, smin(capsule(lx, ly - 4, lx, ly, 3), capsule(lx, 18, lx, ly - 3, 0.8), 1), depth=2, roundness=3)
            S.fill(lens, ellipse(lx, ly + 0.5, 2.4, 1.3), depth=1, roundness=1.5, amb=0.7)
        S.outlines(skip={back, haze, rim, lens, glow})
        return S, SHOW_LAMPS

    # ---------------- the crowd: heads, shoulders, arms up with the horns, against the glow
    S.bg_key = KEY
    rng = np.random.default_rng(84)
    heads = [(x, 146 + int(rng.integers(-3, 4))) for x in range(-2, 142, 9)]
    for i, (x, y) in enumerate(heads):
        S.fill(crowd, smin(ellipse(x, y, 4.5, 5), ellipse(x, y + 12, 8.5, 8), 2.5), depth=3, roundness=4, amb=0.05, kd=0.3)
        if (i * 7) % 3 == 0:  # long hair
            S.fill(crowd, capsule(x - 3, y, x - 5, y + 9, 2.5, 1.5), depth=2, roundness=2)
    for x in range(3, 140, 11):  # a second row, in front
        y = 160 + int(rng.integers(-2, 3))
        S.fill(crowd, smin(ellipse(x, y, 5, 5.5), ellipse(x, y + 12, 9.5, 8), 2.5), depth=3, roundness=4, amb=0.0, kd=0.2)
    # arms up: the horns (index and little finger up), half of them pumping between the two frames
    arms = [(8, 1), (24, -1), (40, 1), (57, 1), (80, -1), (96, 1), (113, -1), (129, 1)]
    for i, (x, lean) in enumerate(arms):
        up_ = (i % 2 == 0) == (k == 1)
        top = 120 if up_ else 128
        sx, sy = x - lean * 4, 152
        hx, hy = x + lean * 3, top
        S.fill(crowd, capsule(sx, sy, hx, hy + 4, 2.2, 1.8), depth=2, roundness=2)
        S.fill(crowd, ellipse(hx, hy + 3, 2.6, 2.4), depth=2, roundness=2)                  # fist
        S.px(crowd, [(hx - 2, hy + 1), (hx - 2, hy), (hx - 2, hy - 1), (hx - 2, hy - 2)], 2)  # index
        S.px(crowd, [(hx + 2, hy + 1), (hx + 2, hy), (hx + 2, hy - 1)], 2)                  # little finger
    on = S.obj == crowd
    edge = np.zeros((H, W), bool)
    edge[1:, :] = on[1:, :] & ~on[:-1, :]
    S.put(crim, edge, 0.5 + 0.4 * np.exp(-((XX - 68) / 40) ** 2))
    S.outlines(skip={crim})
    return S, SHOW_LAMPS


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
SCENES = [('sleep', scene_sleep, 2), ('coffee', scene_coffee, 2), ('tuning', scene_tuning, 2), ('garage', scene_garage, 1), ('bus', scene_bus, 2), ('show', scene_show, 3)]


def main():
    outdir = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, 'build_art')
    os.makedirs(outdir, exist_ok=True)
    only = sys.argv[2] if len(sys.argv) > 2 else None
    lines = ['// Generated by tools/metal_art.py: the pixel art of "Trueno y peluches" (fx_metal.c). Do not edit.',
             '#pragma once', '#include <stdint.h>', '']
    for name, paint, nf in SCENES:
        painted = [paint(k) for k in range(nf)]
        frames = [S_.render() for S_, _ in painted]
        extra = painted[0][1]
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
        if KEY in pal:
            lines.append(f'#define METAL_{name.upper()}_KEY {pal.index(KEY)}')
        if name == 'tuning':  # where the tuner's little screen is, for the needle and the LED
            lines += ['#define METAL_TUNER_%s %d' % kv for kv in zip('XYWH', extra)]
        print(name, 'colours', len(pal), 'bytes', [len(rle(i)) for i in imgs])
    open(os.path.join(ROOT, 'src', 'fx_metal_art.h'), 'w', newline=NL).write(NL.join(lines) + NL)


if __name__ == '__main__':
    main()
