"""Generates src/fonts.c: large anti-aliased clock digits drawn procedurally (no external font files).

Styles: round and 7-segment (three sizes), and a broad-nib calligraphic style (two sizes). Every pixel is one byte: high nibble = glyph coverage,
low nibble = outline coverage (glyph dilated by `pad` px) so faces can draw a dark halo
behind the digits and keep them legible over any background.

Usage: python tools/gen_fonts.py
"""
import math
import os
from PIL import Image, ImageDraw, ImageFilter

S = 8                    # supersampling factor
CW, CH = 100, 160        # design canvas (units)
GLYPHS = "0123456789:-"
SIZES = {"xl": (104, 3), "m": (60, 2), "s": (34, 2)}   # height px, outline px
# Full-screen faces on the 410x502 AMOLED
BIG = {"xxl": (176, 4)}

L, R, T, B, M = 22, 78, 20, 140, 80
W = 24                   # stroke width of the round style


def ell(cx, cy, rx, ry, a0, a1, n=48):
    pts = []
    for i in range(n + 1):
        a = math.radians(a0 + (a1 - a0) * i / n)
        pts.append((cx + rx * math.cos(a), cy + ry * math.sin(a)))
    return pts


def stroke(d, pts, closed=False):
    # A round pen stamped densely along the path: PIL's wide lines leave thin spikes at the joints
    # between segments, which show on the big sizes
    sp = [(x * S, y * S) for x, y in pts]
    if closed:
        sp.append(sp[0])
    r = W * S / 2
    for (x0, y0), (x1, y1) in zip(sp, sp[1:] or sp):
        n = max(1, int(math.hypot(x1 - x0, y1 - y0) / (S / 2)))
        for i in range(n + 1):
            x, y = x0 + (x1 - x0) * i / n, y0 + (y1 - y0) * i / n
            d.ellipse([x - r, y - r, x + r, y + r], fill=255)


def dot(d, cx, cy, r):
    d.ellipse([(cx - r) * S, (cy - r) * S, (cx + r) * S, (cy + r) * S], fill=255)


def glyph_paths(ch, pen, dotfn, serifs=False):
    """Draws a digit as centerline strokes with the given pen (shared by the round and nib styles)."""
    img = Image.new("L", (CW * S, CH * S), 0)
    d = ImageDraw.Draw(img)
    if ch == "0":
        pen(d, ell(50, 80, 28, 60, 0, 360, 96), True)
    elif ch == "1":
        pen(d, [(32, 38), (54, T), (54, B)], False)
        if serifs:
            pen(d, [(34, B), (74, B)], False)
    elif ch == "2":
        pen(d, ell(50, T + 32, 28, 32, 195, 380) + [(L, B), (R + 2, B)], False)
    elif ch == "3":
        pen(d, ell(50, 50, 26, 30, 205, 450) + ell(50, 110, 28, 30, 270, 515)[1:], False)
    elif ch == "4":
        pen(d, [(64, B), (64, T), (L - 2, 104), (R + 6, 104)], False)
        if serifs:
            pen(d, [(50, B), (78, B)], False)
    elif ch == "5":
        pen(d, [(R, T), (L + 2, T), (L, 76)] + ell(52, 106, 28, 34, 215, 500), False)
        if serifs:
            pen(d, [(R, T), (R + 1, T + 12)], False)
    elif ch in "69":
        pen(d, ell(50, 108, 28, 32, 0, 360, 64), True)
        pen(d, [(68, T), (25, 98)], False)
        if ch == "9":
            img = img.rotate(180)
    elif ch == "7":
        pen(d, [(L - 2, T), (R + 2, T), (42, B)], False)
        if serifs:
            pen(d, [(L - 2, T), (L - 3, T + 14)], False)
    elif ch == "8":
        pen(d, ell(50, 48, 24, 28, 0, 360, 64), True)
        pen(d, ell(50, 110, 28, 30, 0, 360, 64), True)
    elif ch == ":":
        dotfn(d, 50, 56, 13)
        dotfn(d, 50, 112, 13)
    elif ch == "-":
        pen(d, [(L + 4, M), (R - 4, M)], False)
    return img


def round_glyph(ch):
    return glyph_paths(ch, lambda d, pts, closed: stroke(d, pts, closed), dot)


# Broad-nib pen: a flat ellipse held at a fixed angle gives thick downstrokes and thin
# horizontals, the calligraphic contrast of classic JRPG menu numerals.
NIB_RX, NIB_RY, NIB_ANGLE = 13.5, 3.0, math.radians(22)


def nib_stamp(d, x, y):
    ca, sa = math.cos(NIB_ANGLE), math.sin(NIB_ANGLE)
    pts = []
    for i in range(20):
        a = i * math.pi * 2 / 20
        ex, ey = NIB_RX * math.cos(a), NIB_RY * math.sin(a)
        pts.append(((x + ex * ca - ey * sa) * S, (y + ex * sa + ey * ca) * S))
    d.polygon(pts, fill=255)


def nib(d, pts, closed):
    pts = list(pts) + ([pts[0]] if closed else [])
    for (x0, y0), (x1, y1) in zip(pts, pts[1:]):
        n = max(1, int(math.hypot(x1 - x0, y1 - y0) * 2))
        for i in range(n + 1):
            nib_stamp(d, x0 + (x1 - x0) * i / n, y0 + (y1 - y0) * i / n)


def nib_dot(d, cx, cy, r):
    d.polygon([(cx * S, (cy - r) * S), ((cx + r) * S, cy * S), (cx * S, (cy + r) * S), ((cx - r) * S, cy * S)], fill=255)


def nib_glyph(ch):
    return glyph_paths(ch, nib, nib_dot, serifs=True)


SEGS = {"0": "abcdef", "1": "bc", "2": "abged", "3": "abgcd", "4": "fgbc",
        "5": "afgcd", "6": "afgedc", "7": "abc", "8": "abcdefg", "9": "abcdfg"}
X0, X1, Y0, YM, Y1, ST, GAP = 22, 78, 22, 80, 138, 20, 3


def hseg(x0, x1, y):
    h = ST / 2
    return [(x0 + GAP, y), (x0 + GAP + h, y - h), (x1 - GAP - h, y - h), (x1 - GAP, y),
            (x1 - GAP - h, y + h), (x0 + GAP + h, y + h)]


def vseg(x, y0, y1):
    h = ST / 2
    return [(x, y0 + GAP), (x + h, y0 + GAP + h), (x + h, y1 - GAP - h), (x, y1 - GAP),
            (x - h, y1 - GAP - h), (x - h, y0 + GAP + h)]


SEG_POLY = {"a": hseg(X0, X1, Y0), "g": hseg(X0, X1, YM), "d": hseg(X0, X1, Y1),
            "f": vseg(X0, Y0, YM), "b": vseg(X1, Y0, YM), "e": vseg(X0, YM, Y1), "c": vseg(X1, YM, Y1)}


def seg_glyph(ch):
    img = Image.new("L", (CW * S, CH * S), 0)
    d = ImageDraw.Draw(img)
    if ch == ":":
        for cy in (56, 108):
            d.rectangle([42 * S, (cy - 8) * S, 58 * S, (cy + 8) * S], fill=255)
    elif ch == "-":
        d.polygon([(x * S, y * S) for x, y in SEG_POLY["g"]], fill=255)
    else:
        for s in SEGS[ch]:
            d.polygon([(x * S, y * S) for x, y in SEG_POLY[s]], fill=255)
    return img


def build(style, fn, h, pad):
    w = round(h * CW / CH)
    out = []
    for ch in GLYPHS:
        img = fn(ch)
        if ch == ":":  # colon is narrow: keep the middle half of the canvas
            img = img.crop((CW * S // 4, 0, CW * S * 3 // 4, CH * S))
        gw = round(img.width / (CW * S) * w)
        small = img.resize((gw, h), Image.LANCZOS)
        canvas = Image.new("L", (gw + 2 * pad, h + 2 * pad), 0)
        canvas.paste(small, (pad, pad))
        outline = canvas.filter(ImageFilter.MaxFilter(2 * pad + 1)).filter(ImageFilter.BoxBlur(0.6))
        data = bytes(((f >> 4) << 4) | (o >> 4) for f, o in zip(canvas.tobytes(), outline.tobytes()))
        out.append((gw, canvas.width, canvas.height, data))
    return w, out


def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    lines = ["// Generated by tools/gen_fonts.py — do not edit", '#include "fonts.h"', ""]
    styles = (("round", round_glyph, {**SIZES, **BIG}), ("seg", seg_glyph, {**SIZES, **BIG}),
              ("nib", nib_glyph, {"m": (60, 2), "s": (34, 2)}))
    for style, fn, sizes in styles:
        for size, (h, pad) in sizes.items():
            name = f"font_{style}_{size}"
            adv, glyphs = build(style, fn, h, pad)
            for i, (_, gw, gh, data) in enumerate(glyphs):
                hexs = ",".join(str(b) for b in data)
                lines.append(f"static const uint8_t {name}_{i}[{len(data)}] = {{{hexs}}};")
            lines.append(f"const font_t {name} = {{.h = {h}, .pad = {pad}, .glyphs = {{")
            for i, (adv_w, gw, gh, _) in enumerate(glyphs):
                lines.append(f"    {{.adv = {adv_w}, .w = {gw}, .h = {gh}, .px = {name}_{i}}},")
            lines.append("}};")
            lines.append("")
    with open(os.path.join(root, "src", "fonts.c"), "w", encoding="utf-8") as f:
        f.write("\n".join(lines))
    print("fonts.c written")


if __name__ == "__main__":
    main()
