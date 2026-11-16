// "Bajamar", the power-off scene (bye.h). Drawn in 2x2 "art pixels" (205 x 251) like the Marea
// Solsticial background: a night sky in bands, the sea with two waves, the moon with its pink halo and
// golden way on the water, and above it the power symbol in a ring that fills while PWR is held. The
// edge by the PWR button (the lower one on the right side) breathes in gold.
#include "bye.h"
#include <math.h>
#include "ui.h"

#define AP 2
#define W (UI_W / AP)  // 205
#define H (UI_H / AP)  // 251
#define CX 102
#define HY 176         // the horizon
#define ICY 70         // the power symbol's centre
#define RING 27        // its ring's radius
#define GLOW_Y 189    // the PWR button's height on the right edge (the USB port is at 125)
#define MOON_R 15
#define MOON_TOP 118   // the moon's centre at the start of the hold
#define MOON_SET (HY - 16)

static const float TAU = 6.2831853f;

static float clampf(float x) { return x < 0 ? 0 : x > 1 ? 1 : x; }
static float ease_out(float x) { x = clampf(x); return 1 - (1 - x) * (1 - x) * (1 - x); }
static float ease_in(float x) { x = clampf(x); return x * x * x; }
static float ease_out_back(float x)
{
    const float c = 1.9f;
    x = clampf(x) - 1;
    return 1 + (c + 1) * x * x * x + c * x * x;
}

// One art pixel: c laid over what is there by a (0..256)
static void put(int x, int y, uint16_t c, int a)
{
    if (x < 0 || y < 0 || x >= W || y >= H || a <= 0) return;
    uint16_t *p = ui_fb() + (y * AP) * UI_W + x * AP;
    if (a < 256) c = ui_mix(c, p[0], a);
    p[0] = p[1] = p[UI_W] = p[UI_W + 1] = c;
}

static void rect(int x, int y, int w, int h, uint16_t c, int a)
{
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++) put(x + i, y + j, c, a);
}

static int a256(float k) { return (int)lroundf(clampf(k) * 256); }

// The stars, the same every time (a little seeded generator)
#define NSTARS 52
static struct { uint8_t x, y, big; float ph, sp, d; } s_star[NSTARS];
static void stars_init(void)
{
    static bool done;
    if (done) return;
    done = true;
    uint32_t seed = 7;
#define RND() ((seed = (uint32_t)(((uint64_t)seed * 16807) % 2147483647)) / 2147483647.f)
    for (int n = 0; n < NSTARS; n++) {
        float x = 8 + RND() * (W - 16), y = 8 + RND() * (HY - 22);
        float d = hypotf((x - CX) / CX, (y - HY / 2.f) / (HY / 2.f)) / 1.41f;
        s_star[n].x = (uint8_t)x;
        s_star[n].y = (uint8_t)y;
        s_star[n].big = RND() < 0.18f;
        s_star[n].ph = RND() * TAU;
        s_star[n].sp = 0.6f + RND() * 1.4f;
        s_star[n].d = clampf(d);
    }
#undef RND
}

static uint16_t mixrgb(int r0, int g0, int b0, int r1, int g1, int b1, float k)
{
    return ui_rgb((uint8_t)(r0 + (r1 - r0) * k), (uint8_t)(g0 + (g1 - g0) * k), (uint8_t)(b0 + (b1 - b0) * k));
}

// The power symbol: a dark disc, the ring (filled clockwise from the top up to p) and the glyph.
// fl: the white flash when it completes; burst: ms into the switch-off (the ring's wave), or -1
static void power_symbol(float p, int alpha, int dy, float fl, int burst)
{
    int cy = ICY + dy;
    bool full = p >= 1;
    float lim = p * TAU;
    uint16_t flash = ui_rgb(255, 246, 214), track = MS_BARRA_OFF, glyph_c = full ? ui_mix(MS_CREMA, MS_ORO2, 128) : MS_CREMA;
    for (int y = cy - RING - 3; y <= cy + RING + 3; y++)
        for (int x = CX - RING - 3; x <= CX + RING + 3; x++) {
            int dx = x - CX, ddy = y - cy;
            float d = sqrtf((float)(dx * dx + ddy * ddy));
            if (d > RING + 1.6f) continue;
            float ang = atan2f((float)dx, (float)-ddy);
            if (ang < 0) ang += TAU;
            uint16_t c;
            int a = alpha;
            bool glyph = (fabsf(d - 11.5f) <= 1.5f && ang > 0.68f && ang < TAU - 0.68f) || (dx >= -1 && dx <= 1 && ddy >= -16 && ddy <= -3);
            if (glyph) c = glyph_c;
            else if (fabsf(d - RING) <= 1.6f) {
                c = ang <= lim ? MS_ORO : track;
                if (!full && ang <= lim && lim - ang < 0.22f) c = MS_ORO2;
            } else if (d <= RING - 3) {
                c = MS_PANEL;
                a = alpha * 225 / 256;
                put(x, y, c, a);
                continue;
            } else continue;
            if (fl > 0) c = ui_mix(flash, c, a256(fl));
            put(x, y, c, a);
        }
    if (burst >= 0 && burst < 300) {
        float rr = RING + 3 + burst * 0.03f;
        int a = 256 - burst * 256 / 300;
        for (int y = cy - 40; y <= cy + 40; y++)
            for (int x = CX - 40; x <= CX + 40; x++) {
                float d = hypotf((float)(x - CX), (float)(y - cy));
                if (fabsf(d - rr) <= 0.7f) put(x, y, MS_ORO2, a);
            }
    }
}

// The whole scene. p: the ring (0..1); mp: the moon's way down (0..1, a little under 0 in the bounce);
// t2: ms into the switch-off, or -1 while held
static void scene(uint32_t t, float p, float mp, int t2)
{
    bool off = t2 >= 0;
    float T = (float)t;
    stars_init();
    // sky in bands of 6, purple near the horizon
    for (int y = 0; y < HY; y += 6) {
        float k = (float)y / HY;
        uint16_t c = k < 0.6f ? mixrgb(10, 9, 30, 24, 18, 56, k / 0.6f) : mixrgb(24, 18, 56, 56, 30, 80, (k - 0.6f) / 0.4f);
        int h = y + 6 > HY ? HY - y : 6;
        ui_rect(0, y * AP, UI_W, h * AP, c);
    }
    // the sea, with faint dashed rows
    for (int y = HY; y < H; y++) {
        ui_rect(0, y * AP, UI_W, AP, mixrgb(18, 18, 50, 9, 9, 26, (float)(y - HY) / (H - HY)));
        if ((y - HY) % 7 == 3) {
            uint16_t c = ui_rgb(26, 26, 66);
            for (int x = 0; x < W; x++)
                if ((x + y * 3) % 11 < 6) put(x, y, c, 256);
        }
    }
    // the moon's way on the water: the higher the moon, the longer
    float len = off ? (1 - clampf(t2 / 400.f)) * 14 : 12 + (1 - mp) * 46;
    for (int k = 0; 3 + k * 3 < 3 + len; k++) {
        int y = HY + 3 + k * 3;
        int w = (int)lroundf(7 - k * 0.35f);
        if (w < 1) w = 1;
        int sh = (int)lroundf(sinf(T * 0.004f + k * 1.3f) * 1.5f);
        rect(CX - w + sh, y, w * 2 + 1, 1, MS_ORO2, a256((1 - k * 3 / (len + 3)) * 0.85f));
    }
    // two waves, calmer as the moon sets and flat by the end of the switch-off
    float amp = off ? 2.2f * (1 - clampf(t2 / 900.f)) : 3.2f - mp * 1.0f;
    uint16_t crest = ui_rgb(74, 84, 156), body = ui_rgb(44, 48, 104), deep = ui_rgb(30, 32, 76);
    for (int wv = 0; wv < 2; wv++) {
        int wy = HY + (wv ? 50 : 28);
        float ph = wv ? 2.1f : 0;
        for (int x = 0; x < W; x++) {
            float v = sinf(x * 0.105f + T * 0.0016f + ph) + 0.35f * sinf(x * 0.23f - T * 0.0011f);
            int y = wy + (int)lroundf(amp * v);
            put(x, y, crest, 256);
            put(x, y + 1, body, 256);
            put(x, y + 2, deep, 256);
            if (amp > 1.2f && v > 1.15f) put(x, y - 1, MS_CREMA, 192);
        }
    }
    // the switch-off: sky and sea go dark from 400 to 900 ms
    if (off && t2 > 400) {
        int keep = 256 - a256((t2 - 400) / 500.f);
        uint16_t *fb = ui_fb();
        if (keep <= 0) ui_fill(MS_NEGRO);
        else for (int i = 0; i < UI_W * UI_H; i++) fb[i] = ui_mix(fb[i], MS_NEGRO, keep);
    }
    // the stars twinkle; in the switch-off they go out from the edges in
    for (int n = 0; n < NSTARS; n++) {
        if (off && t2 > 900 - s_star[n].d * 500) continue;
        int a = a256(0.55f + 0.45f * sinf(T * 0.003f * s_star[n].sp + s_star[n].ph));
        int x = s_star[n].x, y = s_star[n].y;
        put(x, y, MS_CREMA, a);
        if (s_star[n].big) {
            put(x - 1, y, MS_CREMA, a * 45 / 100);
            put(x + 1, y, MS_CREMA, a * 45 / 100);
            put(x, y - 1, MS_CREMA, a * 45 / 100);
            put(x, y + 1, MS_CREMA, a * 45 / 100);
        }
    }
    // the moon and its halo, hidden by the sea
    float my = off ? MOON_SET + ease_in(t2 / 450.f) * 34 : MOON_TOP + (MOON_SET - MOON_TOP) * mp;
    float ma = off ? 1 - clampf((t2 - 250) / 250.f) : 1;
    if (ma > 0) {
        uint16_t crater = ui_rgb(222, 208, 200), limb = ui_rgb(214, 198, 196), rosa = ui_rgb(196, 92, 146);
        int y0 = (int)floorf(my - 30), y1 = (int)(my + 31);
        if (y1 > HY) y1 = HY;
        for (int y = y0; y < y1; y++)
            for (int x = CX - 30; x <= CX + 30; x++) {
                float dx = x - CX, dy = y - my, d = sqrtf(dx * dx + dy * dy);
                if (d <= MOON_R) {
                    uint16_t c = MS_CREMA;
                    if (hypotf(dx + 5, dy + 3) < 3.2f || hypotf(dx - 4, dy - 5) < 2.4f || hypotf(dx - 6, dy + 6) < 1.6f) c = crater;
                    if (d > MOON_R - 1.3f && dx + dy > 4) c = limb;
                    put(x, y, c, a256(ma));
                } else if (d < 30) {
                    float k = 1 - (d - MOON_R) / 15;
                    put(x, y, rosa, a256(0.32f * k * k * ma));
                }
            }
    }
    // the glint as the moon goes under
    if (off && t2 > 220 && t2 < 760) {
        float g = sinf(3.14159265f * (t2 - 220) / 540.f);
        int gw = (int)lroundf(6 + 34 * g);
        rect(CX - gw, HY - 1, gw * 2 + 1, 2, MS_ORO2, a256(g));
        rect(CX - (gw >> 1), HY - 2, gw + 1, 1, MS_CREMA, a256(g * 0.8f));
    }
    // the horizon: in the switch-off the last line of light, which closes into a point and goes out
    if (!off || t2 < 900) {
        float k = off ? clampf((t2 - 600) / 300.f) : 0;
        rect(0, HY, W, 1, ui_mix(MS_CREMA, ui_rgb(78, 64, 128), a256(k)), 256);
        if (k > 0) {
            rect(0, HY - 1, W, 1, MS_CREMA, a256(k * 0.25f));
            rect(0, HY + 1, W, 1, MS_CREMA, a256(k * 0.25f));
        }
    } else if (t2 < 1300) {
        int hw = (int)lroundf(CX * (1 - ease_out((t2 - 900) / 400.f)));
        rect(CX - hw, HY, hw * 2 + 1, 1, MS_CREMA, 256);
        rect(CX - hw, HY - 1, hw * 2 + 1, 1, MS_CREMA, 64);
        rect(CX - hw, HY + 1, hw * 2 + 1, 1, MS_CREMA, 64);
    } else {
        int u = t2 - 1300;
        float a = u < 80 ? 1 : u < 150 ? 0 : u < 220 ? 1 : 1 - clampf((u - 220) / 120.f);
        rect(CX - 1, HY - 1, 3, 3, MS_CREMA, a256(a * 0.35f));
        put(CX, HY, MS_CREMA, a256(a));
    }
    // the power symbol
    float ia = 1, fl = 0;
    int idy = 0;
    if (off) {
        fl = t2 < 260 ? 1 - t2 / 260.f : 0;
        ia = 1 - clampf((t2 - 220) / 380.f);
        idy = (int)lroundf(ease_in((t2 - 220) / 380.f) * 10);
    }
    if (ia > 0) power_symbol(p, a256(ia), idy, fl, off ? t2 : -1);
    // the edge by the PWR button breathes in gold while it is held
    float ga = off ? 1 - clampf(t2 / 300.f) : 0.55f + 0.45f * sinf(T * 0.008f);
    if (ga > 0)
        for (int r = 4; r >= 1; r--) rect(W - r * 3, GLOW_Y - r * 4, r * 3, r * 8 + 1, MS_ORO, a256(ga * 0.9f / (r + 1)));
}

void bye_hold(uint32_t t, int permille)
{
    float p = clampf(permille / 1000.f);
    scene(t, p, ease_out(p) * 0.15f + p * 0.85f, -1);
}

void bye_cancel(uint32_t t, int from, uint32_t since)
{
    float p0 = clampf(from / 1000.f), k = clampf(since / (float)BYE_CANCEL_MS);
    float mp = p0 * (1 - ease_out_back(k));
    scene(t, p0 * (1 - ease_out(k)), mp < -0.08f ? -0.08f : mp, -1);
}

void bye_off(uint32_t t, uint32_t t2)
{
    if (t2 >= BYE_OFF_MS) {
        ui_fill(MS_NEGRO);
        return;
    }
    scene(t, 1, 1, (int)t2);
}
