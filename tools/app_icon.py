"""Paints the Marea app icon in pixel art and writes it as Android vector drawables, plus the favicon of
the control page and the web installer.

    python tools/app_icon.py [preview.png]

The watch seen from the front (its metal bracelet, the two buttons on the right side with the USB port
between them) showing a night by the sea: the moon on the horizon, its golden path and two breaking
waves, in the Marea Solsticial style. A 72 x 72 pixel grid on the adaptive icon's 108 dp (1.5 dp per
pixel); launchers show the middle 48 x 48. Every pixel is a vector square, so it stays sharp at any
size.

Writes, under android/app/src/main/res/drawable/: ic_launcher_background.xml (the sky and the sea),
ic_launcher_foreground.xml (the watch), ic_launcher_monochrome.xml (themed icons, Android 13+) and
ic_stat_marea.xml (the notification icon: one colour, 24 dp). The favicon is a 48 x 48 PNG put inline
in src/index.html and webflasher/index.html.
"""
import base64
import io
import math
import os
import re
import sys

from PIL import Image, ImageDraw

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RES = os.path.join(ROOT, 'android', 'app', 'src', 'main', 'res', 'drawable')
G = 72
NOCHE = (16, 16, 41); PANEL = (33, 24, 66); LINEA = (66, 57, 123); GRIS = (173, 158, 206)
CREMA = (247, 239, 231); ORO = (255, 178, 90); ORO2 = (255, 210, 99); HALO = (90, 56, 24)

def mix(a, b, k):
    return tuple(int(round(a[i] + (b[i] - a[i]) * k)) for i in range(3))

def rrect_in(x, y, x0, y0, x1, y1, r):
    # inside a rounded rectangle [x0,x1) x [y0,y1) with corner radius r
    if not (x0 <= x < x1 and y0 <= y < y1): return False
    cx = min(max(x + .5, x0 + r), x1 - r); cy = min(max(y + .5, y0 + r), y1 - r)
    return (x + .5 - cx) ** 2 + (y + .5 - cy) ** 2 <= r * r

def background():
    im = Image.new('RGB', (G, G))
    px = im.load()
    sky_top, sky_low = (10, 10, 30), (46, 30, 84)
    hor = 50
    for y in range(G):
        for x in range(G):
            if y < hor:
                k = y / hor
                # banded gradient with a checker dither between bands, pixel-art style
                b = k * 6; lo = int(b); f = b - lo
                kk = (lo + (1 if (f > .5 and (x + y) % 2) else 0)) / 6
                c = mix(sky_top, sky_low, kk)
            else:
                k = (y - hor) / (G - hor)
                c = mix((22, 26, 66), (8, 12, 34), min(1, k * 1.3))
                if (y - hor) % 3 == 0 and (x * 7 + y * 3) % 11 < 4: c = mix(c, (60, 70, 130), .5)
            px[x, y] = c
    # a warm glow on the horizon, behind the watch
    for y in range(hor - 8, hor + 3):
        for x in range(G):
            d = abs(x - 36) / 30 + abs(y - hor) / 8
            if d < 1: px[x, y] = mix(px[x, y], HALO, (1 - d) * .55)
    for (x, y, big) in [(17, 17, 1), (55, 14, 0), (14, 33, 0), (58, 31, 1), (24, 10, 0), (49, 22, 0)]:
        px[x, y] = CREMA
        if big:
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)): px[x + dx, y + dy] = mix(CREMA, sky_top, .55)
    return im

# the watch body, in grid pixels
BX0, BY0, BX1, BY1, BR = 22, 19, 50, 53, 8
SX0, SY0, SX1, SY1, SR = BX0 + 3, BY0 + 3, BX1 - 3, BY1 - 3, 6

def wave_y(x):
    u = (x - SX0) / (SX1 - SX0)
    h = 0
    for c in (.36, .80):  # two breaking crests (an M): a long back to the right, a steep face to the left
        t = u - c
        if 0 <= t < .30: h = max(h, 5 * (1 - t / .30) ** 1.6)
        elif -.07 < t < 0: h = max(h, 5 * (1 + t / .07) ** .6)
    return SY1 - 3 - h + .8 * math.sin(u * 19)

def foreground(img):
    im = img.copy() if img else Image.new('RGBA', (G, G), (0, 0, 0, 0))
    px = im.load()
    def put(x, y, c): px[x, y] = c + ((255,) if im.mode == 'RGBA' else ())
    # straps: a metal bracelet of three-piece links (polished middle, brushed sides), flaring at the lugs
    ST_HI, ST, ST_MID, ST_LO, ST_GAP = (236, 238, 246), (196, 200, 218), (150, 154, 180), (98, 100, 132), (40, 40, 64)
    for y in list(range(4, BY0 + 3)) + list(range(BY1 - 3, 68)):
        near = min(abs(y - BY0), abs(y - BY1))
        fl = 2 if near < 3 else 1 if near < 5 else 0
        x0, x1 = 28 - fl, 44 + fl
        row = (y - (BY1 if y > 36 else BY0)) % 4   # 4-pixel link rows
        for x in range(x0, x1):
            mid = x0 + 5 <= x < x1 - 5          # the polished middle link
            if x in (x0 + 4, x1 - 5): c = ST_GAP  # the seams between the pieces
            elif row == 3: c = ST_GAP if not mid else ST_LO
            elif mid: c = ST_HI if row == 0 else ST if row == 1 else ST_MID
            else:
                c = ST if row == 0 else ST_MID if row == 1 else ST_LO
                if x == x0: c = ST_HI if row < 2 else ST
                if x == x1 - 1: c = ST_LO
            put(x, y, c)
    # shadow under the body
    for y in range(BY0 + 1, BY1 + 2):
        for x in range(BX0 + 1, BX1 + 2):
            if rrect_in(x, y, BX0 + 1, BY0 + 1, BX1 + 1, BY1 + 1, BR) and not rrect_in(x, y, BX0, BY0, BX1, BY1, BR):
                put(x, y, (6, 6, 18))
    # the two buttons on the right side, like the real watch (the USB port sits between them)
    for b0 in (28, 41):
        for y in range(b0, b0 + 4):
            for x in range(BX1 - 1, BX1 + 2): put(x, y, CREMA if y == b0 else GRIS if y < b0 + 3 else LINEA)
    hor = SY0 + 17
    for y in range(BY0, BY1):
        for x in range(BX0, BX1):
            if not rrect_in(x, y, BX0, BY0, BX1, BY1, BR): continue
            if not rrect_in(x, y, SX0, SY0, SX1, SY1, SR):
                # the case: light top-left, shaded bottom-right
                k = ((x - BX0) / (BX1 - BX0) + (y - BY0) / (BY1 - BY0)) / 2
                put(x, y, mix(CREMA, GRIS, min(1, k * 1.5)) if k < .75 else LINEA)
                continue
            # the screen: night sky, the moon on the horizon, its golden path, the wave
            if y < hor:
                c = mix((8, 8, 26), (40, 26, 74), (y - SY0) / (hor - SY0))
            else:
                c = mix((18, 30, 76), (10, 16, 46), (y - hor) / (SY1 - hor))
                # the moon's path: broken golden strokes, narrowing towards the horizon
                w = 1 + (y - hor) * .55
                if abs(x + .5 - 36) < w and (y - hor) % 2 == 0 and (x * 5 + y) % 7 != 0:
                    c = ORO if (y - hor) > 2 else ORO2
            mx, my, mr = 36, hor, 7.5
            d = math.hypot(x + .5 - mx, y + .5 - my)
            if y < hor:
                if d < mr: c = ORO2 if d < mr - 1.6 or (x < mx) else ORO
                elif d < mr + 1.5: c = mix(c, (214, 110, 120), .45)
                elif d < mr + 3.5 and (x + y) % 2: c = mix(c, (170, 80, 130), .3)
            wy = wave_y(x + .5)
            if y >= wy:
                c = CREMA if y < wy + 1.2 else mix((70, 140, 180), (16, 36, 84), min(1, (y - wy) / 7))
                if wy + 1.2 <= y < wy + 2.5 and (x + y) % 2: c = mix(CREMA, (70, 140, 180), .5)
            # the lip curling over each crest's face, and the shade under it
            for cc in (.36, .80):
                lx = SX0 + (cc - .02) * (SX1 - SX0)
                ly = SY1 - 3 - 5
                if (x + .5 - lx) ** 2 / 6 + (y + .5 - ly - 1) ** 2 / 1.8 < 1 and x + .5 <= lx + 1: c = CREMA
                if lx - 3 <= x + .5 <= lx - .5 and ly + 2 <= y + .5 <= ly + 3.5: c = (12, 26, 62)
            put(x, y, c)
    # the USB port, a dark slot in the middle of the right side
    for y in range(34, 38): put(BX1 - 1, y, (20, 18, 40))
    # a glass glint
    for i in range(3): put(SX0 + 2 + i, SY0 + 1, mix(CREMA, (8, 8, 26), .35))
    put(SX0 + 1, SY0 + 2, mix(CREMA, (8, 8, 26), .35))
    return im

def silhouette():
    # one-colour version: the watch with the moon and the wave cut out (themed icon and notifications)
    im = Image.new('L', (G, G), 0)
    px = im.load()
    for y in range(G):
        for x in range(G):
            on = rrect_in(x, y, BX0, BY0, BX1, BY1, BR) or (27 <= x < 45 and (4 <= y < BY0 + 2 or BY1 - 2 <= y < 68)) \
                or (BX1 <= x < BX1 + 2 and (28 <= y < 32 or 41 <= y < 45))
            if rrect_in(x, y, SX0, SY0, SX1, SY1, SR):
                wy = wave_y(x + .5)
                on = not (wy <= y < wy + 3) and not (y < SY0 + 17 and math.hypot(x + .5 - 36, y + .5 - SY0 - 17) < 7.5)
            if (27 <= x < 45) and (BY0 <= y < BY0 + 2 or BY1 - 2 <= y < BY1) and not rrect_in(x, y, BX0, BY0, BX1, BY1, BR):
                on = True
            # the straps: a gap between strap and case
            if 27 <= x < 45 and y in (BY0 - 2, BY1 + 1, BY0 - 6): on = False
            px[x, y] = 255 if on else 0
    return im

def squircle(size, r):
    m = Image.new('L', (size * 4, size * 4), 0)
    ImageDraw.Draw(m).rounded_rectangle((0, 0, size * 4 - 1, size * 4 - 1), radius=size * 4 * r, fill=255)
    return m.resize((size, size), Image.LANCZOS)


def vector(im, dp, path, x0=0, y0=0, n=G, colour=None):
    """Writes the opaque pixels of im[x0:x0+n, y0:y0+n] as a vector drawable: one path per colour, made of
    horizontal runs. colour: one fill for everything (alpha images)."""
    px = im.load()
    runs = {}
    for y in range(n):
        x = 0
        while x < n:
            p = px[x0 + x, y0 + y]
            on = p > 0 if colour else (len(p) < 4 or p[3] > 0)
            if not on:
                x += 1
                continue
            c = colour or '#%02X%02X%02X' % p[:3]
            e = x + 1
            while e < n:
                q = px[x0 + e, y0 + y]
                if (q > 0 if colour else (len(q) < 4 or q[3] > 0)) and (colour or '#%02X%02X%02X' % q[:3]) == c: e += 1
                else: break
            runs.setdefault(c, []).append('M%d,%dh%dv1h-%dz' % (x, y, e - x, e - x))
            x = e
    with open(path, 'w', encoding='utf-8', newline='\n') as f:
        f.write('<?xml version="1.0" encoding="utf-8"?>\n<!-- Generated by tools/app_icon.py: do not edit by hand -->\n')
        f.write('<vector xmlns:android="http://schemas.android.com/apk/res/android"\n'
                '    android:width="%ddp" android:height="%ddp" android:viewportWidth="%d" android:viewportHeight="%d">\n'
                % (dp, dp, n, n))
        for c, r in runs.items():
            f.write('    <path android:fillColor="%s" android:pathData="%s" />\n' % (c, ''.join(r)))
        f.write('</vector>\n')
    return os.path.getsize(path)


def favicon(full):
    im = full.crop((12, 12, 60, 60)).convert('RGBA')
    im.putalpha(squircle(48, .22).point(lambda a: 255 if a >= 128 else 0))
    b = io.BytesIO()
    im.save(b, 'PNG', optimize=True)
    return 'data:image/png;base64,' + base64.b64encode(b.getvalue()).decode()


def put_favicon(path, uri):
    s = open(path, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    link = '<link rel="icon" type="image/png" href="%s">' % uri
    if '<link rel="icon"' in s: s = re.sub(r'<link rel="icon"[^>]*>', lambda m: link, s, count=1)
    else: s = re.sub(r'(<title>[^<]*</title>)', lambda m: m.group(1) + nl + link, s, count=1)
    open(path, 'w', encoding='utf-8', newline='').write(s)


def preview(full, sil, out):
    S = 432
    big = full.crop((12, 12, 60, 60)).resize((S, S), Image.NEAREST)
    sheet = Image.new('RGB', (S * 2 + 72, S + 48), (232, 228, 236))
    sheet.paste(big, (24, 24), squircle(S, .32))
    mono = Image.new('RGB', (G, G), (52, 66, 92))
    mono.paste((196, 222, 255), (0, 0), sil)
    sheet.paste(mono.crop((12, 12, 60, 60)).resize((S, S), Image.NEAREST), (S + 48, 24), squircle(S, .5))
    sheet.save(out)


def main():
    full = foreground(background())
    sil = silhouette()
    print('background', vector(background(), 108, os.path.join(RES, 'ic_launcher_background.xml')))
    print('foreground', vector(foreground(None), 108, os.path.join(RES, 'ic_launcher_foreground.xml')))
    print('monochrome', vector(sil, 108, os.path.join(RES, 'ic_launcher_monochrome.xml'), colour='#FFFFFFFF'))
    print('notification', vector(sil, 24, os.path.join(RES, 'ic_stat_marea.xml'), 12, 12, 48, colour='#FFFFFFFF'))
    uri = favicon(full)
    for p in ('src/index.html', 'webflasher/index.html'): put_favicon(os.path.join(ROOT, p), uri)
    print('favicon', len(uri))
    if len(sys.argv) > 1: preview(full, sil, sys.argv[1])


if __name__ == '__main__':
    main()
