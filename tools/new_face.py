"""Creates a new clock face from a working template and registers it in the firmware.

    python tools/new_face.py burbujas "Burbujas"                       # wide canvas, group Efectos
    python tools/new_face.py faro "Faro" --native --group nature --desc "Un faro en la costa..."
    python tools/new_face.py burbujas "Burbujas" --dry-run             # show what would change

It writes src/fx_<id>.c (a face that already draws a background, something that moves and the
time, ready to be changed) and touches the four places that list the faces:
  - src/fx.h          FX_COUNT + 1 and the two prototypes
  - src/fx_common.c   a new entry at the END of fx_list (face numbers are saved on the clocks:
                      never insert in the middle or reorder)
  - src/CMakeLists.txt the new source file
Then: pio run, flash, and pick the face in the app. Guide: wiki "Crear una esfera".
"""
import argparse
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "src")
GROUPS = {"effects": "FX_G_EFFECTS", "games": "FX_G_GAMES", "art": "FX_G_ART",
          "nature": "FX_G_NATURE", "clocks": "FX_G_CLOCKS"}

TEMPLATE = r'''// "{label}" (full screen, {orient}): {desc_comment}
// Made from tools/new_face.py: replace the drawing below with your own idea.
#include <stdio.h>
#include "app.h"
#include "control.h"
#include "fx.h"
#include "gfx.h"

#define W {w_macro}  // {orient}: {w_px} x {h_px} px, the whole screen{scale_note}
#define H {h_macro}
// The screen corners are rounded (radius {corner} px here): keep text and anything important away
// from them. The outer 2 px of each edge may not show either.

static int s_ox, s_oy;  // state kept between frames (here: how far the finger moved the ball)

// Called every time the face is chosen: reset the state, build lookup tables
void {id}_enter(void)
{{
    s_ox = s_oy = 0;
}}

// Draws one whole frame into fb (W x H pixels, RGB565 made with rgb565()). Runs 30 times per
// second and must take well under 33 ms: use integers, tables and the gfx_* helpers.
//   t  = animation time in ms (already scaled by the speed setting), dt = ms since the last frame
void {id}_render(uint16_t *fb, uint32_t t, uint32_t dt)
{{
    // 1. Background: a vertical gradient whose colour drifts slowly with time
    uint8_t hue = t / 160;
    for (int y = 0; y < H; y++) {{
        uint16_t c = hsv565(hue + y * 40 / H, 210, 25 + y * 70 / H);
        uint16_t *row = fb + y * W;
        for (int x = 0; x < W; x++) row[x] = c;
    }}

    // 2. Something that moves: a glowing ball on a Lissajous path (sin_q14: 1024 steps per turn,
    //    values -16384..16384), nudged by the finger on the app's touch pad
    int dx, dy;
    app_take_drag(&dx, &dy);
    s_ox += dx / 2;
    s_oy += dy / 2;
    int bx = W / 2 + ((sin_q14[(t / 7) & 1023] * (W / 3)) >> 14) + s_ox;
    int by = H / 3 + ((sin_q14[(t / 11 + 256) & 1023] * (H / 6)) >> 14) + s_oy;
    gfx_glow(fb, bx, by, 34, rgb565(255, 230, 160), 12);
    gfx_circle(fb, bx, by, 9, rgb565(255, 250, 230));

    // 3. The time (every face draws its own): date, HH:MM and the seconds bar if enabled
    char date[24];
    clock_date_str(date);
    uint16_t white = rgb565(255, 255, 255), ink = rgb565(12, 10, 30);
    gfx_text_ol(fb, (W - gfx_text_width(date, 2)) / 2, H - {date_up}, date, white, ink, 2, 1);
    clock_draw_row(fb, &font_round_m, H - {row_up}, -4, white, ink, FONT_FILL | FONT_OUTLINE);
    clock_draw_seconds_bar(fb, {bar_x}, H - {bar_up}, W - {bar_w}, 3, white, rgb565(60, 60, 80));
}}
'''


def c_string(s):
    return s.replace("\\", "\\\\").replace('"', '\\"')


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("id", help="C identifier and file name: lowercase letters, digits, _ (e.g. burbujas)")
    ap.add_argument("label", help='name shown in the app (e.g. "Burbujas")')
    ap.add_argument("--native", action="store_true",
                    help="410 x 502 at full resolution instead of the wide canvas (274 x 336, enlarged x1.5): "
                         "sharper, but twice the pixels to draw (fine for faces that barely move)")
    ap.add_argument("--group", choices=GROUPS, default="effects", help="section of the app (default effects)")
    ap.add_argument("--desc", default="", help="one sentence for the app: what the face shows")
    ap.add_argument("--hint", default="", help="what dragging on the touch pad does (shows the pad)")
    ap.add_argument("--dry-run", action="store_true")
    a = ap.parse_args()

    if not re.fullmatch(r"[a-z][a-z0-9_]{1,20}", a.id):
        sys.exit("id: letras minúsculas, números y _, empezando por letra (máx. 21)")
    path = os.path.join(SRC, f"fx_{a.id}.c")
    if os.path.exists(path):
        sys.exit(f"ya existe {path}")
    fx_h = open(os.path.join(SRC, "fx.h"), encoding="utf-8").read()
    if re.search(rf"\b{a.id}_render\b", fx_h):
        sys.exit(f"ya hay una esfera llamada {a.id}")
    common = open(os.path.join(SRC, "fx_common.c"), encoding="utf-8").read()
    cmake = open(os.path.join(SRC, "CMakeLists.txt"), encoding="utf-8").read()

    count = int(re.search(r"#define FX_COUNT (\d+)", fx_h).group(1))
    desc = a.desc or f"Esfera {a.label}: describe aquí en una frase lo que se ve."
    orient = "native" if a.native else "wide canvas"
    if a.native:
        geo = dict(w_macro="FW", h_macro="FH", w_px=410, h_px=502, scale_note="", corner=120,
                   date_up=180, row_up=150, bar_up=44, bar_x=110, bar_w=220)
    else:
        geo = dict(w_macro="WW", h_macro="WH", w_px=274, h_px=336, scale_note=", enlarged x1.5 when sent",
                   corner=80, date_up=122, row_up=100, bar_up=30, bar_x=72, bar_w=144)

    face_c = TEMPLATE.format(id=a.id, label=a.label, orient=orient, desc_comment=a.desc or "describe what it shows", **geo)

    fx_h2 = fx_h.replace(f"#define FX_COUNT {count}", f"#define FX_COUNT {count + 1}")
    anchor = "\n// Scratch memory shared by faces"
    assert anchor in fx_h2, "fx.h: no encuentro dónde añadir los prototipos"
    fx_h2 = fx_h2.replace(anchor, f"void {a.id}_enter(void);\nvoid {a.id}_render(uint16_t *fb, uint32_t t, uint32_t dt);\n" + anchor, 1)

    entry = (f'    {{.name = "{c_string(a.label.upper())}", .res = {"FX_NATIVE" if a.native else "FX_WIDE"}, '
             f'.enter = {a.id}_enter, .render = {a.id}_render,\n'
             f'     .label = "{c_string(a.label)}", .group = {GROUPS[a.group]},'
             + (f' .hint = "{c_string(a.hint)}",' if a.hint else "")
             + f'\n     .desc = "{c_string(desc)}"}},\n')
    end = common.index("};", common.index("const fx_t fx_list[FX_COUNT]"))
    common2 = common[:end] + entry + common[end:]

    last_fx = re.findall(r'"fx_\w+\.c"', cmake)[-1]
    cmake2 = cmake.replace(last_fx, f'{last_fx} "fx_{a.id}.c"', 1)

    print(f"Esfera nueva número {count}: {a.label} ({orient}, grupo {a.group})")
    print(f"  crea      src/fx_{a.id}.c")
    print(f"  modifica  src/fx.h (FX_COUNT {count} -> {count + 1}), src/fx_common.c, src/CMakeLists.txt")
    if a.dry_run:
        print("--dry-run: no se ha cambiado nada.")
        return
    for name, text in ((path, face_c), ("fx.h", fx_h2), ("fx_common.c", common2), ("CMakeLists.txt", cmake2)):
        with open(os.path.join(SRC, name), "w", encoding="utf-8", newline="\n") as f:
            f.write(text)
    print("\nAhora: pio run -t upload, y elígela en el reloj, en la app o con `fx %d`." % count)
    print("Antes de enviarla: añádela a la tabla del README y su captura: python tools/ble_shot.py --fx %d --out fx%02d.png" % (count, count) + ".")


if __name__ == "__main__":
    main()
