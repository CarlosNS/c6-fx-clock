// "Clasico" (full screen, wide canvas): a rectangular Art Deco dress watch. The screen is the case:
// dark gunmetal with polished edges that follow the rounded corners of the display, deep green sunburst dial whose reflections turn slowly, railroad minute
// track, applied gold batons, faceted dauphine hands, a date window at 3 and a small seconds
// subdial at 6 that ticks 8 times per second like an automatic movement. Original design.
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "app.h"
#include "fx.h"
#include "gfx.h"

#define W WW
#define H WH
// The screen is the case: the case follows the display's own rounded corners (2 px in, as the outer
// pixels may not show), and the dial sits 16 px in, with corners wide enough to stay clear of them.
// Case, bezel and dial rectangles: x0, y0, x1, y1 (exclusive)
#define CASE_X0 2
#define CASE_X1 (WW - 2)
#define CASE_Y0 2
#define CASE_Y1 (WH - 2)
#define CASE_R (WIDE_CORNER_R - 2)
#define DIAL_X0 16
#define DIAL_X1 (WW - 16)
#define DIAL_Y0 16
#define DIAL_Y1 (WH - 16)
#define DIAL_R 48
#define CX 137
#define CY 150
#define SUB_Y 258
#define SUB_R 34

// Angle of every pixel of one dial quadrant (|dx|, |dy|), 0..255 for 0..90 degrees, kept in the
// shared scratch arena
#define QW (DIAL_X1 - CX)
#define QH (DIAL_Y1 - CY)
#define QANG ((uint8_t (*)[QW])fx_scratch)
#define SCRATCH_ID 22
static uint16_t s_dial[64];       // dial shades, dark to light, in the chosen hue
static int s_hue = -1;

static uint16_t c_gold, c_gold_l, c_gold_d, c_champ;

void classic_enter(void)
{
    _Static_assert(QW * QH <= FX_SCRATCH_SIZE, "angle table too big");
    if (!fx_scratch_claim(SCRATCH_ID)) {
        for (int dy = 0; dy < QH; dy++)
            for (int dx = 0; dx < QW; dx++) {
                float a = atan2f((float)dy, (float)dx) / 1.5707963f * 256;  // runs once
                QANG[dy][dx] = (uint8_t)(a > 255 ? 255 : a);
            }
    }
    c_gold = rgb565(206, 176, 112);
    c_gold_l = rgb565(246, 226, 168);
    c_gold_d = rgb565(128, 100, 56);
    c_champ = rgb565(214, 196, 150);
}

// Dial shades for a hue (degrees): constant saturation, value from near black to a deep mid tone.
// Hue 146 gives the original deep green.
static void build_palette(int hue)
{
    int sector = hue / 60, f = (hue % 60) * 256 / 60;
    for (int i = 0; i < 64; i++) {
        int v = 15 + i * 107 / 63;      // 0..255 scale: about 0.06 .. 0.48
        int lo = v * 97 / 256;          // saturation ~0.62
        int up = lo + (v - lo) * f / 256, dn = v - (v - lo) * f / 256;
        int r, g, b;
        switch (sector) {
        case 0: r = v; g = up; b = lo; break;
        case 1: r = dn; g = v; b = lo; break;
        case 2: r = lo; g = v; b = up; break;
        case 3: r = lo; g = dn; b = v; break;
        case 4: r = up; g = lo; b = v; break;
        default: r = v; g = lo; b = dn; break;
        }
        s_dial[i] = rgb565(r, g, b);
    }
    s_hue = hue;
}

// Horizontal span of a rounded rectangle on row y, in 1/16 px; false if the row is outside
static bool rr_span(int y, int x0, int y0, int x1, int y1, int r, int *xl, int *xr)
{
    if (y < y0 || y >= y1) return false;
    int dy = y < y0 + r ? y0 + r - y : y >= y1 - r ? y - (y1 - r - 1) : 0, ins = 0;
    if (dy > 0) {
        int yy = dy * 16 - 8, k = r * 16;
        ins = yy >= k ? k : k - gfx_isqrt32(k * k - yy * yy);
    }
    *xl = x0 * 16 + ins;
    *xr = x1 * 16 - ins;
    return *xr > *xl;
}

// Antialiased rounded rectangle fill. The next layer drawn on top (hole, may be NULL) is skipped,
// apart from the pixels its own antialiased edge blends over, so stacked layers don't overdraw.
typedef struct { int x0, y0, x1, y1, r; } rr_t;

static void rrect(uint16_t *fb, rr_t o, const rr_t *hole, uint16_t c)
{
    for (int y = o.y0; y < o.y1; y++) {
        int xl, xr, hl, hr;
        if (!rr_span(y, o.x0, o.y0, o.x1, o.y1, o.r, &xl, &xr)) continue;
        uint16_t *row = fb + y * W;
        int a = (xl + 15) >> 4, b = xr >> 4;
        if (hole && rr_span(y, hole->x0, hole->y0, hole->x1, hole->y1, hole->r, &hl, &hr)) {
            int ha = (hl + 15) >> 4, hb = hr >> 4;
            for (int x = a; x < ha; x++) row[x] = c;
            for (int x = hb; x < b; x++) row[x] = c;
        } else {
            for (int x = a; x < b; x++) row[x] = c;
        }
        if ((xl & 15) && (xl >> 4) < W) row[xl >> 4] = gfx_blend(row[xl >> 4], c, 16 - (xl & 15));
        if ((xr & 15) && b < W) row[b] = gfx_blend(row[b], c, xr & 15);
    }
}

// Angle of (dx, dy) in 1/1024 turns, 0 = +x, clockwise on screen (y down)
static inline int angle(int dx, int dy)
{
    int a = QANG[dy < 0 ? -dy : dy][dx < 0 ? -dx : dx];
    if (dx < 0) a = 512 - a;
    if (dy < 0) a = -a;
    return a & 1023;
}

// Sunburst dial: radial grooves catch the light in two opposite sectors that follow `light`
static void dial(uint16_t *fb, int light)
{
    for (int y = DIAL_Y0; y < DIAL_Y1; y++) {
        int xl, xr;
        if (!rr_span(y, DIAL_X0, DIAL_Y0, DIAL_X1, DIAL_Y1, DIAL_R, &xl, &xr)) continue;
        uint16_t *row = fb + y * W;
        int dy = y - CY, dy2 = dy * dy;
        int x0 = (xl + 15) >> 4, x1 = xr >> 4;
        // The light and the shading are smooth: worked out every other pixel, the grooves' grain
        // alternates between the pair so it stays fine
        int base = 22 - (dy2 >> 12);
        for (int x = x0; x < x1; x += 2) {
            int dx = x - CX, a = angle(dx, dy);
            int shine = sin_q14[(2 * (a - light) + 256) & 1023];  // cos(2 (a - light))
            int lvl = base + (shine * 13 >> 14) - ((dx * dx) >> 12);
            int g = (a * 37 >> 2) & 3, l0 = lvl + g, l1 = lvl + (3 - g);
            row[x] = s_dial[l0 < 0 ? 0 : l0 > 63 ? 63 : l0];
            if (x + 1 < x1) row[x + 1] = s_dial[l1 < 0 ? 0 : l1 > 63 ? 63 : l1];
        }
        // Soft edges against the bezel
        if (xl & 15) row[xl >> 4] = gfx_blend(row[xl >> 4], s_dial[10], 16 - (xl & 15));
        if (xr & 15) row[x1] = gfx_blend(row[x1], s_dial[10], xr & 15);
    }
}

// Where a ray from the centre at angle a (0 = 12 o'clock, clockwise) meets the dial's rounded
// rectangle inset by `ins` px; result in 1/16 px
static void on_rect(int a, int ins, int *px, int *py)
{
    float fs = sin_q14[a & 1023] / 16384.0f, fc = -sin_q14[(a + 256) & 1023] / 16384.0f;
    float hx = fs >= 0 ? DIAL_X1 - 1 - CX - ins : CX - DIAL_X0 - ins;
    float hy = fc >= 0 ? DIAL_Y1 - 1 - CY - ins : CY - DIAL_Y0 - ins;
    float rc = DIAL_R - ins, as = fabsf(fs), ac = fabsf(fc);
    float t = (as > 1e-4f && hx / as < hy / (ac > 1e-4f ? ac : 1e-4f)) ? hx / as : hy / (ac > 1e-4f ? ac : 1e-4f);
    float ex = as * t, ey = ac * t;
    if (rc > 0 && ex > hx - rc && ey > hy - rc) {
        // In a corner: meet the arc around (hx - rc, hy - rc) instead
        float kx = hx - rc, ky = hy - rc, b = as * kx + ac * ky;
        t = b + sqrtf(b * b - kx * kx - ky * ky + rc * rc);
    }
    *px = CX * 16 + (int)(fs * t * 16);
    *py = CY * 16 + (int)(fc * t * 16);
}

// Thin outline of the dial's rounded rectangle inset by `ins` px, blended over the dial
static void rr_outline(uint16_t *fb, int ins, uint16_t c, int alpha)
{
    int l = DIAL_X0 + ins, r = DIAL_X1 - 1 - ins, tp = DIAL_Y0 + ins, bt = DIAL_Y1 - 1 - ins, rc = DIAL_R - ins;
    for (int x = l + rc; x <= r - rc; x++) {
        fb[tp * W + x] = gfx_blend(fb[tp * W + x], c, alpha);
        fb[bt * W + x] = gfx_blend(fb[bt * W + x], c, alpha);
    }
    for (int y = tp + rc; y <= bt - rc; y++) {
        fb[y * W + l] = gfx_blend(fb[y * W + l], c, alpha);
        fb[y * W + r] = gfx_blend(fb[y * W + r], c, alpha);
    }
    // Corner arcs, sampled finely enough to leave no gaps
    int n = rc * 2;
    for (int i = 0; i <= n; i++) {
        int a = i * 256 / n;
        int ox = rc * sin_q14[a] >> 14, oy = rc * sin_q14[(a + 256) & 1023] >> 14;
        int pts[4][2] = {{l + rc - ox, tp + rc - oy}, {r - rc + ox, tp + rc - oy}, {l + rc - ox, bt - rc + oy},
                         {r - rc + ox, bt - rc + oy}};
        for (int k = 0; k < 4; k++) {
            uint16_t *px = &fb[pts[k][1] * W + pts[k][0]];
            *px = gfx_blend(*px, c, alpha);
        }
    }
}

// Applied gold baton along the ray: shadow, body, polished ridge
static void baton(uint16_t *fb, int ax, int ay, int bx, int by, int r)
{
    gfx_aa_capsule(fb, ax + 20, ay + 28, bx + 20, by + 28, r + 6, 0, 7);
    gfx_aa_capsule(fb, ax, ay, bx, by, r, c_gold_d, 16);
    gfx_aa_capsule(fb, ax - 4, ay - 4, bx - 4, by - 4, r - 10, c_gold, 16);
    gfx_aa_capsule(fb, ax - 6, ay - 6, bx - 6, by - 6, 5, c_gold_l, 12);
}

// Dauphine hand: two triangular facets, one lit and one in shade, with a soft shadow
static void dauphine(uint16_t *fb, int a, int len, int width)
{
    int s = sin_q14[a & 1023], c = -sin_q14[(a + 256) & 1023];
    int px = -c, py = s;  // perpendicular
    // Points in 1/16 px
    int tipx = CX * 16 + (s * len >> 10), tipy = CY * 16 + (c * len >> 10);
    int tlx = CX * 16 - (s * 8 >> 10), tly = CY * 16 - (c * 8 >> 10);
    int mx = CX * 16 + (s * len / 4 >> 10), my = CY * 16 + (c * len / 4 >> 10);
    int lx = mx + (px * width >> 10), ly = my + (py * width >> 10);
    int rx = mx - (px * width >> 10), ry = my - (py * width >> 10);
    gfx_aa_capsule(fb, tlx + 28, tly + 44, (mx + tipx) / 2 + 28, (my + tipy) / 2 + 44, width * 12, 0, 6);
    int16_t left[6] = {tlx >> 4, tly >> 4, lx >> 4, ly >> 4, tipx >> 4, tipy >> 4};
    int16_t right[6] = {tlx >> 4, tly >> 4, rx >> 4, ry >> 4, tipx >> 4, tipy >> 4};
    gfx_poly(fb, left, 3, c_gold_l);
    gfx_poly(fb, right, 3, c_gold_d);
    // Antialiased edges and the central ridge
    gfx_aa_capsule(fb, tlx, tly, lx, ly, 7, c_gold, 14);
    gfx_aa_capsule(fb, lx, ly, tipx, tipy, 7, c_gold, 14);
    gfx_aa_capsule(fb, tlx, tly, rx, ry, 7, c_gold_d, 14);
    gfx_aa_capsule(fb, rx, ry, tipx, tipy, 7, c_gold_d, 14);
    gfx_aa_capsule(fb, tlx, tly, tipx, tipy, 5, c_gold, 10);
}

static void subdial(uint16_t *fb, bool secs)
{
    gfx_aa_capsule(fb, CX * 16, SUB_Y * 16, CX * 16, SUB_Y * 16, (SUB_R + 1) * 16, c_gold_d, 16);
    // Azurage: fine concentric grooves
    for (int y = SUB_Y - SUB_R; y <= SUB_Y + SUB_R; y++) {
        int dy = y - SUB_Y;
        uint16_t *row = fb + y * W;
        for (int x = CX - SUB_R; x <= CX + SUB_R; x++) {
            int dx = x - CX, d2 = dx * dx + dy * dy;
            if (d2 >= SUB_R * SUB_R - SUB_R) continue;
            int d = gfx_isqrt32(d2 * 4);  // half-pixel rings
            int lvl = 12 + (d & 1) * 3 - (dx + dy) / 12;
            row[x] = s_dial[lvl < 0 ? 0 : lvl];
        }
    }
    for (int i = 0; i < 60; i++) {
        int a = i * 1024 / 60, r0 = i % 5 ? 28 : 25;
        int s = sin_q14[a], c = -sin_q14[(a + 256) & 1023];
        gfx_aa_capsule(fb, CX * 16 + (s * r0 >> 10), SUB_Y * 16 + (c * r0 >> 10), CX * 16 + (s * 31 >> 10),
                       SUB_Y * 16 + (c * 31 >> 10), i % 5 ? 4 : 7, c_champ, i % 5 ? 9 : 14);
    }
    if (!secs) return;
    // Eight beats per second
    uint32_t ms = g_now.minute_ms / 125 * 125;
    int a = (int)(ms * 1024 / 60000);
    int s = sin_q14[a & 1023], c = -sin_q14[(a + 256) & 1023];
    gfx_aa_capsule(fb, CX * 16 - (s * 7 >> 10) + 16, SUB_Y * 16 - (c * 7 >> 10) + 24, CX * 16 + (s * 29 >> 10) + 16,
                   SUB_Y * 16 + (c * 29 >> 10) + 24, 9, 0, 6);
    gfx_aa_capsule(fb, CX * 16 - (s * 7 >> 10), SUB_Y * 16 - (c * 7 >> 10), CX * 16 + (s * 29 >> 10),
                   SUB_Y * 16 + (c * 29 >> 10), 6, c_gold_l, 16);
    gfx_aa_capsule(fb, CX * 16, SUB_Y * 16, CX * 16, SUB_Y * 16, 30, c_gold, 16);
}

void classic_render(uint16_t *fb, uint32_t t, uint32_t dt)
{
    (void)dt;
    if (g_app.classic_hue != s_hue) build_palette(g_app.classic_hue);
    // Background only around the case
    uint16_t bg = rgb565(6, 8, 7);
    for (int y = 0; y < H; y++) {
        uint16_t *row = fb + y * W;
        if (y < CASE_Y0 || y >= CASE_Y1) {
            for (int x = 0; x < W; x++) row[x] = bg;
        } else {
            for (int x = 0; x <= CASE_X0; x++) row[x] = bg;
            for (int x = CASE_X1 - 1; x < W; x++) row[x] = bg;
        }
    }
    // Outside the case's rounded corners: only the thin sliver up to the case edge (the rest is
    // beyond the display's own corners)
    for (int y = CASE_Y0; y < CASE_Y0 + CASE_R; y++)
        for (int k = 0; k < 2; k++) {
            int yy = k ? CASE_Y1 - 1 - (y - CASE_Y0) : y, xl, xr;
            if (!rr_span(yy, CASE_X0, CASE_Y0, CASE_X1, CASE_Y1, CASE_R, &xl, &xr)) continue;
            int a = (xl + 15) >> 4, b = xr >> 4;
            for (int x = a - 3 < 0 ? 0 : a - 3; x < a; x++) fb[yy * W + x] = bg;
            for (int x = b; x < b + 3 && x < W; x++) fb[yy * W + x] = bg;
        }

    // The case: dark outer edge, polished bevel, brushed gunmetal body
    static const rr_t L0 = {CASE_X0, CASE_Y0, CASE_X1, CASE_Y1, CASE_R},
                      L1 = {CASE_X0 + 1, CASE_Y0 + 1, CASE_X1 - 1, CASE_Y1 - 1, CASE_R - 1},
                      L2 = {CASE_X0 + 3, CASE_Y0 + 3, CASE_X1 - 3, CASE_Y1 - 3, CASE_R - 3},
                      B0 = {DIAL_X0 - 3, DIAL_Y0 - 3, DIAL_X1 + 3, DIAL_Y1 + 3, DIAL_R + 3},
                      B1 = {DIAL_X0 - 2, DIAL_Y0 - 2, DIAL_X1 + 2, DIAL_Y1 + 2, DIAL_R + 2},
                      DL = {DIAL_X0, DIAL_Y0, DIAL_X1, DIAL_Y1, DIAL_R};
    rrect(fb, L0, &L1, rgb565(20, 22, 22));
    rrect(fb, L1, &L2, rgb565(128, 134, 130));
    rrect(fb, L2, &B0, rgb565(46, 50, 50));
    // A long highlight down the left flank, like light on polished steel
    for (int y = CASE_Y0 + CASE_R; y < CASE_Y1 - CASE_R; y++) {
        int f = sin_u8[(uint8_t)((y - CASE_Y0) * 128 / (CASE_Y1 - CASE_Y0))] / 32;
        fb[y * W + CASE_X0 + 5] = gfx_blend(fb[y * W + CASE_X0 + 5], rgb565(220, 226, 222), f);
        fb[y * W + CASE_X0 + 6] = gfx_blend(fb[y * W + CASE_X0 + 6], rgb565(220, 226, 222), f / 2);
    }
    // Gold inner bezel
    rrect(fb, B0, &B1, c_gold_d);
    rrect(fb, B1, &DL, c_gold);

    // The light source drifts slowly, as if the wrist moved
    int light = 160 + (sin_q14[(t / 24) & 1023] * 90 >> 14);
    dial(fb, light);

    // Railroad minute track
    for (int i = 0; i < 60; i++) {
        int a = i * 1024 / 60, x0, y0, x1, y1;
        on_rect(a, 6, &x0, &y0);
        on_rect(a, i % 5 ? 10 : 11, &x1, &y1);
        gfx_aa_capsule(fb, x0, y0, x1, y1, i % 5 ? 5 : 8, c_champ, i % 5 ? 10 : 15);
    }
    rr_outline(fb, 6, c_champ, 9);
    rr_outline(fb, 10, c_champ, 9);

    // Applied hour batons (double at 12)
    for (int h = 0; h < 12; h++) {
        int a = h * 1024 / 12, x0, y0, x1, y1;
        on_rect(a, 15, &x0, &y0);
        on_rect(a, h % 3 ? 29 : 34, &x1, &y1);
        if (h == 0) {
            baton(fb, x0 - 64, y0, x1 - 64, y1, 26);
            baton(fb, x0 + 64, y0, x1 + 64, y1, 26);
        } else if (h == 6 || h == 3) {
            continue;  // the subdial and the date window sit there
        } else {
            baton(fb, x0, y0, x1, y1, h % 3 ? 22 : 28);
        }
    }

    // Signature under 12
    const char *mark = "MAREA", *sub = "AUTOMATIC";
    ui_text(fb, &ui_m, CX - ui_width(&ui_m, mark) / 2, 72, mark, c_champ, c_champ, FONT_FILL);
    ui_text(fb, &ui_s, CX - ui_width(&ui_s, sub) / 2, 94, sub, gfx_blend(s_dial[44], c_champ, 9), 0, FONT_FILL);

    // Date window at 3
    if (g_now.valid) {
        char d[4];
        snprintf(d, sizeof(d), "%d", g_now.day);
        int wx = DIAL_X1 - 1 - 16 - 22, wy = CY - 9;
        gfx_fill_rect(fb, wx - 1, wy - 1, 24, 20, c_gold_d);
        gfx_fill_rect(fb, wx, wy, 22, 18, c_gold);
        gfx_fill_rect(fb, wx + 1, wy + 1, 20, 16, rgb565(232, 226, 210));
        gfx_fill_rect(fb, wx + 1, wy + 1, 20, 2, rgb565(196, 190, 176));  // inner shadow
        ui_text(fb, &ui_s, wx + 11 - ui_width(&ui_s, d) / 2, wy + 1, d, rgb565(30, 34, 32), 0, FONT_FILL);
    }

    bool secs = g_app.show_secs && g_now.valid;
    subdial(fb, secs);

    // Hands (10:10 until the time is known)
    int h = g_now.valid ? g_now.h : 10, m = g_now.valid ? g_now.m : 10;
    uint32_t mms = g_now.valid ? g_now.minute_ms : 0;
    int a_h = (int)(((h % 12) * 60 + m) * 1024 / 720 + mms * 1024 / 43200000);
    int a_m = (int)((m * 60000 + mms) * 1024 / 3600000);
    dauphine(fb, a_h, 62, 6);
    dauphine(fb, a_m, 96, 5);
    gfx_aa_capsule(fb, CX * 16, CY * 16, CX * 16, CY * 16, 56, c_gold_d, 16);
    gfx_aa_capsule(fb, CX * 16 - 4, CY * 16 - 4, CX * 16 - 4, CY * 16 - 4, 40, c_gold, 16);
    gfx_aa_capsule(fb, CX * 16 - 12, CY * 16 - 12, CX * 16 - 12, CY * 16 - 12, 12, c_gold_l, 14);
}
