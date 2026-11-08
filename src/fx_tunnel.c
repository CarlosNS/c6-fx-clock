// Endless textured tunnel using precomputed distance/angle lookup tables (rendered at half resolution),
// over the whole screen (wide canvas).
// The tables only cover one quadrant: the other three are mirror images (distance is symmetric,
// the angle just changes sign), which keeps them at 10 KB instead of 41 KB.
#include <math.h>
#include "app.h"
#include "control.h"
#include "fx.h"
#include "gfx.h"

#define TW 138          // half of the 274 px canvas, rounded up to an even number
#define TH (WH / 2)
#define QW (TW / 2)
#define QH (TH / 2)
#define SHADES 16

static uint8_t dist_t[QH][QW], ang_t[QH][QW], shade_t[QH][QW];
static uint8_t qx_of[TW], flip_x[TW];
static uint16_t pal[SHADES][256];
static bool s_ready;
static int s_rot, s_fwd;

void tunnel_enter(void)
{
    if (s_ready) return;
    for (int y = 0; y < QH; y++) {
        for (int x = 0; x < QW; x++) {
            float dx = x + 0.5f, dy = y + 0.5f;
            float r = sqrtf(dx * dx + dy * dy);
            dist_t[y][x] = (uint8_t)((int)(1600.0f / r) & 255);
            ang_t[y][x] = (uint8_t)((int)(256.0f * atan2f(dy, dx) / 3.14159265f) & 255);  // 2 texture turns per circle
            int s = (int)(r * SHADES / 76.0f);
            shade_t[y][x] = s >= SHADES ? SHADES - 1 : s;
        }
    }
    for (int x = 0; x < TW; x++) {
        flip_x[x] = x < QW;
        qx_of[x] = x < QW ? QW - 1 - x : x - QW;
    }
    s_ready = true;
}

void tunnel_render(uint16_t *fb, uint32_t t, uint32_t dt)
{
    int dx, dy;
    app_take_drag(&dx, &dy);
    s_rot += dx;   // horizontal drag twists the tunnel
    s_fwd -= dy;   // vertical drag pushes you forward / back

    uint8_t hue = t / 50;
    for (int s = 0; s < SHADES; s++) {
        int lvl = s * 255 / (SHADES - 1);
        for (int i = 0; i < 256; i++) {
            int v = lvl * (90 + (i * 165 >> 8)) >> 8;
            pal[s][i] = hsv565(hue + (i >> 2), 220, (uint8_t)v);
        }
    }

    uint8_t uoff = t / 30 + s_rot;   // twist
    uint8_t voff = t / 6 + s_fwd;    // forward motion
    for (int y = 0; y < TH; y++) {
        uint16_t *r0 = fb + (y * 2) * WW, *r1 = r0 + WW;
        bool fy = y < QH;
        int qy = fy ? QH - 1 - y : y - QH;
        const uint8_t *dr = dist_t[qy], *ar = ang_t[qy], *sr = shade_t[qy];
        for (int x = 0; x < TW; x++) {
            int qx = qx_of[x];
            uint8_t a = ar[qx];
            if (flip_x[x] != fy) a = -a;  // mirrored in exactly one axis: angle changes sign
            uint8_t u = a + uoff, v = dr[qx] + voff;
            uint16_t c = pal[sr[qx]][(uint8_t)(u ^ v)];
            r0[x * 2] = r1[x * 2] = c;
            if (x * 2 + 1 < WW) r0[x * 2 + 1] = r1[x * 2 + 1] = c;
        }
    }

    // Clock: 7-segment digits tinted with the complementary hue, over a dark panel
    char date[24];
    clock_date_str(date);
    uint16_t ink = rgb565(0, 0, 0), digit = hsv565(hue + 128, 90, 255);
    gfx_darken_rect(fb, 46, 46, WW - 92, 250, 14, 2);
    gfx_text_ol(fb, (WW - gfx_text_width(date, 2)) / 2, 18, date, rgb565(255, 255, 255), ink, 2, 1);
    clock_draw_stacked(fb, &font_seg_xl, 60, 12, digit, ink, FONT_FILL | FONT_OUTLINE);
    clock_draw_seconds_bar(fb, 70, 308, WW - 140, 3, digit, rgb565(40, 40, 50));
}
