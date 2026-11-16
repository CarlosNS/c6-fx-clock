"""Paints the repository's social preview (the picture shown when a link to it is shared) and the README
header: a night by the sea in pixel art, the Marea Solsticial style of the web installer, with three
watches showing real screens (the face picker, the main menu, an alarm ringing), and the name.

    python tools/social_preview.py [out.png]      # default docs/img/social.png

1280 x 640 (what GitHub asks for), with what matters inside its 80 px safe border. The scene is drawn
on a 320 x 160 grid, 4 px per pixel; the watches on their own grid (4 px the middle one, 3 px the side
ones). The screens are full screenshots of the watch in English (docs/img/social/, taken with the
console's sershot). Upload it in GitHub: Settings > General > Social preview.
"""
import math
import os
import random
import sys

from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
W, H, U = 1280, 640, 4
GW, GH = W // U, H // U
SCREENS = ('picker', 'menu', 'alarm')  # left, middle, right: docs/img/social/<name>.png
TITLE, TAGLINE = 'Marea', 'Open-source pixel-art smartwatch · ESP32-S3 AMOLED'

NOCHE = (16, 16, 41); LINEA = (66, 57, 123); GRIS = (173, 158, 206)
CREMA = (247, 239, 231); ORO = (255, 178, 90); ORO2 = (255, 210, 99); HALO = (90, 56, 24); CORAL = (232, 106, 96)
SKY_TOP, SKY_LOW = (10, 10, 30), (46, 30, 84)
SEA_TOP, SEA_LOW = (22, 26, 66), (8, 12, 34)
HOR = 120  # horizon, in grid pixels


def mix(a, b, k):
    k = max(0.0, min(1.0, k))
    return tuple(int(round(a[i] + (b[i] - a[i]) * k)) for i in range(3))


def rrect_in(x, y, x0, y0, x1, y1, r):
    if not (x0 <= x < x1 and y0 <= y < y1): return False
    cx = min(max(x + .5, x0 + r), x1 - r); cy = min(max(y + .5, y0 + r), y1 - r)
    return (x + .5 - cx) ** 2 + (y + .5 - cy) ** 2 <= r * r


# ------------------------------------------------------------------ the scene

def scene():
    im = Image.new('RGB', (GW, GH))
    px = im.load()
    rnd = random.Random(7)
    for y in range(GH):
        for x in range(GW):
            if y < HOR:
                b = y / HOR * 7; lo = int(b); f = b - lo
                c = mix(SKY_TOP, SKY_LOW, (lo + (1 if f > .5 and (x + y) % 2 else 0)) / 7)
            else:
                k = (y - HOR) / (GH - HOR)
                c = mix(SEA_TOP, SEA_LOW, k * 1.2)
                if (y - HOR) % 3 == 0 and (x * 7 + y * 3) % 13 < 4: c = mix(c, (60, 70, 130), .45)
            px[x, y] = c
    # a warm glow on the horizon, behind the watches
    for y in range(HOR - 26, HOR + 4):
        for x in range(GW):
            d = math.hypot((x - GW / 2) / 120, (y - HOR) / 26)
            if d < 1 and ((x + y) % 2 or d < .7): px[x, y] = mix(px[x, y], HALO, (1 - d) * .6)
    # stars, a few big ones with a cross
    for _ in range(GW * HOR // 330):
        x, y = rnd.randrange(GW), rnd.randrange(HOR - 16)
        k = rnd.random()
        px[x, y] = mix(px[x, y], CREMA, .25 + .6 * k * k * (1 - y / HOR))
    for (x, y) in [(22, 18), (61, 46), (232, 16), (304, 84), (98, 70)]:
        px[x, y] = CREMA
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)): px[x + dx, y + dy] = mix(CREMA, SKY_TOP, .5)
    # the moon, high on the right, and its golden path on the sea
    mx, my, mr = 276, 34, 11
    for y in range(my - mr - 6, my + mr + 7):
        for x in range(mx - mr - 6, mx + mr + 7):
            d = math.hypot(x + .5 - mx, y + .5 - my)
            if d < mr:
                c = ORO2 if d < mr - 2 or x < mx else ORO
                if (x - mx + 4) ** 2 + (y - my + 3) ** 2 < 6 or (x - mx - 3) ** 2 + (y - my - 4) ** 2 < 4: c = mix(ORO2, ORO, .6)
                px[x, y] = c
            elif d < mr + 2: px[x, y] = mix(px[x, y], (214, 110, 120), .45)
            elif d < mr + 6 and (x + y) % 2: px[x, y] = mix(px[x, y], (170, 80, 130), .25)
    for y in range(HOR + 1, GH, 2):  # broken strokes, scattered more the nearer they are
        k = (y - HOR) / (GH - HOR)
        for _ in range(1 + int(k * 3)):
            w = 2 + k * 14
            x0 = int(mx + rnd.uniform(-w, w))
            ln = rnd.randint(2, 3 + int(k * 6))
            for x in range(x0, min(GW, x0 + ln)):
                px[x, y] = mix(px[x, y], ORO2 if k < .2 else ORO, .9 - k * .35)
    # the lighthouse on its islet, far on the left, with its beam
    for y in range(HOR - 3, HOR + 3):
        for x in range(12, 52):
            if abs(x - 31) < 19 - abs(y - HOR) * 3 + (2 if y < HOR else 0): px[x, y] = mix((30, 24, 52), (14, 12, 30), (y - HOR + 3) / 6)
    for y in range(HOR - 26, HOR - 2):
        hw = 2 + (y - (HOR - 26)) * 2 // 24
        for x in range(31 - hw, 32 + hw):
            band = ((y - (HOR - 26)) // 4) % 2
            c = CORAL if band else CREMA
            px[x, y] = mix(c, (40, 30, 70), .45) if x > 31 else c
    for y in range(HOR - 31, HOR - 26):
        for x in range(29, 35): px[x, y] = ORO2 if HOR - 30 <= y <= HOR - 28 and 30 <= x <= 33 else (40, 30, 70)
    for y in range(HOR - 33, HOR - 31):
        for x in range(30 - (y == HOR - 32), 34 + (y == HOR - 32)): px[x, y] = CORAL
    for x in range(28, -1, -1):  # the beam, to the left, fading out of the picture
        t = (28 - x) / 40
        for y in range(int(HOR - 29 - t * 9), int(HOR - 28 + t * 9) + 1):
            if (x + y) % 2 == 0 or t < .3: px[x, y] = mix(px[x, y], ORO2, .55 * (1 - t) ** 1.3)
    # waves near the shore: dashed crests
    for (y, n) in [(GH - 26, 7), (GH - 15, 9), (GH - 5, 11)]:
        for _ in range(n):
            x0 = rnd.randrange(-4, GW)
            for i in range(9):
                x = x0 + i
                yy = y - int(2 * math.sin(i / 8 * math.pi))
                if 0 <= x < GW:
                    px[x, yy] = CREMA if i < 7 else mix(CREMA, SEA_LOW, .5)
                    if yy + 1 < GH: px[x, yy + 1] = mix(px[x, yy + 1], (70, 140, 180), .5)
    return im.resize((W, H), Image.NEAREST)


# ------------------------------------------------------------------ a watch

def watch(screen, u, dim=0.0, sw=69, sh=84, top=26):
    """The watch from the front on a grid of u px: metal case, the two buttons on the right, the bracelet
    above and below (long enough to leave the picture), the face on its screen. Returns (RGBA, screen box)."""
    b = 4                                  # case border
    bw, bh, br = sw + 2 * b, sh + 2 * b, 16  # body
    strap = 46
    bot = 40                                # bracelet below, in grid pixels (top: above)
    gw, gh = bw + 4, top + bh + bot
    im = Image.new('RGBA', (gw, gh), (0, 0, 0, 0))
    px = im.load()
    BX0, BY0 = 0, top
    BX1, BY1 = BX0 + bw, BY0 + bh
    def put(x, y, c, a=255):
        if 0 <= x < gw and 0 <= y < gh: px[x, y] = c + (a,)
    ST_HI, ST, ST_MID, ST_LO, ST_GAP = (236, 238, 246), (196, 200, 218), (150, 154, 180), (98, 100, 132), (40, 40, 64)
    cx = bw // 2
    for y in list(range(0, BY0 + 4)) + list(range(BY1 - 4, gh)):
        near = min(abs(y - BY0), abs(y - BY1))
        fl = 3 if near < 4 else 1 if near < 7 else 0
        x0, x1 = cx - strap // 2 - fl, cx + strap // 2 + fl
        row = (y - (BY1 if y > BY0 else BY0)) % 6
        # the far ends of the bracelet fade into the night (top) or leave the picture (bottom)
        a = 255 if y >= 18 else int(255 * (y / 18) ** 1.5)
        for x in range(x0, x1):
            mid = x0 + 14 <= x < x1 - 14
            if x in (x0 + 13, x1 - 14): c = ST_GAP
            elif row == 5: c = ST_GAP if not mid else ST_LO
            elif mid: c = ST_HI if row == 0 else ST if row < 3 else ST_MID
            else:
                c = ST if row == 0 else ST_MID if row < 3 else ST_LO
                if x == x0: c = ST_HI if row < 3 else ST
                if x == x1 - 1: c = ST_LO
            put(x, y, c, a)
    for y in range(BY0 + 2, BY1 + 3):     # shadow
        for x in range(BX0 + 2, BX1 + 3):
            if rrect_in(x, y, BX0 + 2, BY0 + 2, BX1 + 2, BY1 + 2, br) and not rrect_in(x, y, BX0, BY0, BX1, BY1, br):
                put(x, y, (6, 6, 18))
    for b0 in (BY0 + bh * 30 // 100, BY0 + bh * 62 // 100):  # the buttons, the USB port between them
        for y in range(b0, b0 + 9):
            for x in range(BX1 - 1, BX1 + 3): put(x, y, CREMA if y < b0 + 2 else GRIS if y < b0 + 7 else LINEA)
    for y in range(BY0, BY1):
        for x in range(BX0, BX1):
            if not rrect_in(x, y, BX0, BY0, BX1, BY1, br): continue
            k = ((x - BX0) / bw + (y - BY0) / bh) / 2
            c = mix(CREMA, GRIS, k * 1.5) if k < .78 else LINEA
            if rrect_in(x, y, BX0 + 1, BY0 + 1, BX1 - 1, BY1 - 1, br - 1) and not rrect_in(x, y, BX0 + 3, BY0 + 3, BX1 - 3, BY1 - 3, br - 3):
                c = mix(c, (255, 255, 255), .25) if x + y < BX0 + BY0 + bw * .6 else c
            put(x, y, c)
    for y in range(BY0 + bh * 46 // 100, BY0 + bh * 54 // 100): put(BX1 - 1, y, (20, 18, 40))
    big = im.resize((gw * u, gh * u), Image.NEAREST)
    # the face on the screen, its corners as round as the real panel's (120 px on 410)
    sx, sy, sww, shh = (BX0 + b) * u, (BY0 + b) * u, sw * u, sh * u
    f = Image.open(os.path.join(ROOT, 'docs', 'img', 'social', f'{screen}.png')).convert('RGB').resize((sww, shh), Image.LANCZOS)
    if dim: f = Image.blend(f, Image.new('RGB', f.size, NOCHE), dim)
    m = Image.new('L', (sww * 4, shh * 4), 0)
    ImageDraw.Draw(m).rounded_rectangle((0, 0, sww * 4 - 1, shh * 4 - 1), radius=int(sww * 4 * 120 / 410), fill=255)
    m = m.resize((sww, shh), Image.LANCZOS)
    bezel = Image.new('L', (sww + 2 * u, shh + 2 * u), 0)
    ImageDraw.Draw(bezel).rounded_rectangle((0, 0, sww + 2 * u - 1, shh + 2 * u - 1), radius=int(sww * 120 / 410) + u, fill=255)
    big.paste(Image.new('RGBA', bezel.size, (8, 8, 20, 255)), (sx - u, sy - u), bezel)
    big.paste(f, (sx, sy), m)
    # a glass glint in the top-left corner
    g = ImageDraw.Draw(big)
    r0 = int(sww * 120 / 410)
    for i in range(3):
        g.rectangle((sx + r0 // 2 + i * u, sy + u, sx + r0 // 2 + i * u + u - 1, sy + 2 * u - 1), fill=mix(CREMA, NOCHE, .4) + (150,))
    if dim:
        shade = Image.new('RGBA', big.size, NOCHE + (0,))
        a = big.getchannel('A').point(lambda v: int(v * dim))
        shade.putalpha(a)
        body = big.copy()
        body.alpha_composite(shade)
        # keep the face's own dimming (done above) and darken the metal the same
        big = body
    return big, (sx, sy, sww, shh)


# ------------------------------------------------------------------ text

def pixel_text(text, font, size, scale, colour, shadow):
    """Text drawn without smoothing at size, then enlarged by scale (chunky pixels), with a hard shadow."""
    f = ImageFont.truetype(os.path.join(ROOT, 'tools', 'fonts', font), size)
    x0, y0, x1, y1 = f.getbbox(text)
    w, h = x1 - x0 + 2, y1 - y0 + 2
    m = Image.new('L', (w, h), 0)
    d = ImageDraw.Draw(m)
    d.fontmode = '1'
    d.text((-x0, -y0), text, font=f, fill=255)
    m = m.resize((w * scale, h * scale), Image.NEAREST)
    out = Image.new('RGBA', (m.width + scale, m.height + scale), (0, 0, 0, 0))
    out.paste(Image.new('RGBA', m.size, shadow + (255,)), (scale, scale), m)
    out.paste(Image.new('RGBA', m.size, colour + (255,)), (0, 0), m)
    return out


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, 'docs', 'img', 'social.png')
    img = scene().convert('RGBA')
    title = pixel_text(TITLE, 'Jersey10-Regular.ttf', 28, 4, ORO2, (60, 28, 20))
    tag = pixel_text(TAGLINE, 'VT323-Regular.ttf', 16, 2, CREMA, (20, 14, 40))
    ty = 86  # inside the safe border (GitHub's template: 80 px)
    cy = 382  # the watches' centre
    mid, (sx, sy, sww, shh) = watch(SCREENS[1], 4, sw=64, sh=78, top=5)  # short: clear of the tagline
    for name, cx in ((SCREENS[0], W // 2 - 340), (SCREENS[2], W // 2 + 340)):
        side, (_, ssy, _, ssh) = watch(name, 3, dim=.12, sw=64, sh=78, top=30)
        img.alpha_composite(side, (cx - side.width // 2 + 6, cy - (ssy + ssh // 2) + 12))
    img.alpha_composite(mid, (W // 2 - mid.width // 2 + 8, cy - (sy + shh // 2)))
    img.alpha_composite(title, (W // 2 - title.width // 2, ty))
    img.alpha_composite(tag, (W // 2 - tag.width // 2, ty + title.height + 2))
    img.convert('RGB').save(out, optimize=True)
    print(out, os.path.getsize(out) // 1024, 'KB')


if __name__ == '__main__':
    main()
