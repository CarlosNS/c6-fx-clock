"""Exports the watch's clock fonts for the custom face editor of the app (Mis esferas), so its preview
draws the time exactly as the watch does: the same glyph bitmaps, read from the firmware's own sources.

    python tools/font_atlas.py      # writes src/webapp/fuentes.png and src/webapp/fuentes.json

fuentes.png is one grayscale atlas (alpha 0..15 times 17); fuentes.json says where each glyph is and
how it is placed. Fonts (ids as in src/fx_custom.c):
    round_s .. round_xxl, seg_s .. seg_xxl, nib_s, nib_m   anti-aliased digits (fill alpha only)
    pix                                                    the 7x10 pixel digits of gfx.c (scaled)
    jersey, vt                                             the system's bitmap fonts (scaled)
src/webapp/ only goes into the Android app (android/app/build.gradle.kts copies it), not the firmware.
"""
import json
import os
import re

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "src")
OUT = os.path.join(SRC, "webapp")
TIME_CHARS = "0123456789:"
# The date line (VT323): capitals, digits and the accented capitals the month and day names use
DATE_CHARS = " 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZÁÉÍÓÚÑÜ"


def arrays(text):
    """name -> list of ints, for every 'static const uint8_t name[...] = {...};'"""
    out = {}
    for m in re.finditer(r"static const uint8_t (\w+)\[\d*\]\s*=\s*\{([^}]*)\}", text):
        out[m.group(1)] = [int(v, 0) for v in m.group(2).replace("\n", " ").split(",") if v.strip()]
    return out


def aa_fonts():
    text = open(os.path.join(SRC, "fonts.c"), encoding="utf-8").read()
    data = arrays(text)
    fonts = {}
    for m in re.finditer(r"const font_t (\w+) = \{\.h = (\d+), \.pad = (\d+), \.glyphs = \{(.*?)\n\}\};", text, re.S):
        name, h, pad, body = m.group(1), int(m.group(2)), int(m.group(3)), m.group(4)
        glyphs = re.findall(r"\{\.adv = (\d+), \.w = (\d+), \.h = (\d+), \.px = (\w+)\}", body)
        g = {}
        for ch, (adv, w, gh, px) in zip("0123456789:-", glyphs):
            if ch not in TIME_CHARS:
                continue
            w, gh, raw = int(w), int(gh), data[px]
            g[ch] = {"adv": int(adv), "w": w, "h": gh, "a": [b >> 4 for b in raw[: w * gh]]}
        fonts[name.replace("font_", "")] = {"type": "aa", "h": h, "pad": pad, "g": g}
    return fonts


def bit_font(name, chars):
    text = open(os.path.join(SRC, "ui_fonts.c"), encoding="utf-8").read()
    gm = re.search(r"ui_%s_glyphs\[\d+\] = \{(.*?)\};" % name, text, re.S)
    bm = re.search(r"ui_%s_bits\[\d*\] = \{(.*?)\};" % name, text, re.S)
    fm = re.search(r"const ui_font_t ui_%s = \{[^,]+, \d+, [^,]+, (\d+), (\d+)\}" % name, text)
    bits = [int(v, 0) for v in bm.group(1).replace("\n", " ").split(",") if v.strip()]
    g = {}
    for e in re.findall(r"\{(0x[0-9a-fA-F]+), (\d+), (\d+), (-?\d+), (-?\d+), (\d+), (\d+)\}", gm.group(1)):
        cp, w, h, xo, yo, adv, off = int(e[0], 16), *map(int, e[1:])
        ch = chr(cp)
        if ch not in chars:
            continue
        bpr = (w + 7) // 8
        a = [15 if bits[off + j * bpr + (i >> 3)] >> (i & 7) & 1 else 0 for j in range(h) for i in range(w)]
        g[ch] = {"adv": adv, "w": w, "h": h, "xo": xo, "yo": yo, "a": a}
    return {"type": "bits", "h": int(fm.group(1)) + int(fm.group(2)), "g": g}


def pix_font():
    text = open(os.path.join(SRC, "gfx.c"), encoding="utf-8").read()
    body = re.search(r"pixglyph\[12\]\[10\] = \{(.*?)\n\};", text, re.S).group(1)
    rows = re.findall(r"\{([^{}]*)\}", body)
    g = {}
    for ch, row in zip("0123456789:-", rows):
        lines = re.findall(r'"([.#]*)"', row)
        if ch not in TIME_CHARS:
            continue
        w = len(lines[0])
        g[ch] = {"adv": w + 1, "w": w, "h": 10, "a": [15 if c == "#" else 0 for l in lines for c in l]}
    return {"type": "pix", "h": 10, "g": g}


def main():
    fonts = aa_fonts()
    fonts["pix"] = pix_font()
    fonts["jersey"] = bit_font("jersey", TIME_CHARS + " ")
    fonts["vt"] = bit_font("vt", DATE_CHARS)
    # Pack every glyph in rows of an atlas 1024 px wide
    W, x, y, row_h, place = 1024, 0, 0, 0, []
    for fname, f in fonts.items():
        for ch, g in f["g"].items():
            if x + g["w"] > W:
                x, y, row_h = 0, y + row_h + 1, 0
            place.append((fname, ch, x, y))
            g["x"], g["y"] = x, y
            x += g["w"] + 1
            row_h = max(row_h, g["h"])
    img = Image.new("L", (W, y + row_h + 1), 0)
    px = img.load()
    for fname, ch, gx, gy in place:
        g = fonts[fname]["g"][ch]
        for j in range(g["h"]):
            for i in range(g["w"]):
                px[gx + i, gy + j] = g["a"][j * g["w"] + i] * 17
        del g["a"]
    os.makedirs(OUT, exist_ok=True)
    img.save(os.path.join(OUT, "fuentes.png"), optimize=True)
    with open(os.path.join(OUT, "fuentes.json"), "w", encoding="utf-8") as fp:
        json.dump(fonts, fp, ensure_ascii=False, separators=(",", ":"))
    print(f"font_atlas: {len(place)} glyphs, atlas {img.size[0]}x{img.size[1]} -> src/webapp/")


if __name__ == "__main__":
    main()
