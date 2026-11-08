// "Tiempo" (portrait): a quiet analogue dial with the live weather of the configured city below.
// Soft paper tones that follow the sky (sunny, grey, rainy, night), antialiased hands with a faint
// shadow and a sweeping seconds hand, and a small animated weather icon next to the temperature.
#include <stdio.h>
#include <string.h>
#include "app.h"
#include "fx.h"
#include "gfx.h"
#include "weather.h"

#define W WW
#define H WH
#define CX 137
#define CY 112
#define ICX 92
#define ICY 248

typedef struct { uint8_t top[3], bot[3], ink[3], soft[3], accent[3], cloud[3]; } palette_t;
static const palette_t PAL_SUN = {{248, 243, 233}, {238, 231, 216}, {60, 56, 50}, {170, 160, 144}, {206, 98, 62}, {206, 208, 212}};
static const palette_t PAL_GREY = {{238, 239, 238}, {222, 225, 226}, {60, 64, 68}, {158, 164, 168}, {200, 104, 70}, {188, 194, 200}};
static const palette_t PAL_WET = {{228, 234, 238}, {206, 216, 224}, {50, 60, 72}, {146, 160, 172}, {196, 96, 70}, {160, 172, 186}};
static const palette_t PAL_NIGHT = {{32, 38, 48}, {20, 24, 30}, {232, 226, 212}, {100, 108, 120}, {232, 134, 94}, {96, 106, 120}};

static const palette_t *s_pal;
static uint16_t s_ink, s_soft, s_accent, s_cloud, s_bg_icon;

static inline uint16_t c3(const uint8_t *c) { return rgb565(c[0], c[1], c[2]); }

// Point at angle a (0..1023, clockwise from 12) and radius rad (px) around (cx, cy), in 1/16 px
#define PX(cx, a, rad) ((cx) * 16 + (sin_q14[(a) & 1023] * (rad) >> 10))
#define PY(cy, a, rad) ((cy) * 16 - (sin_q14[((a) + 256) & 1023] * (rad) >> 10))

static void hand(uint16_t *fb, int a, int len, int tail, int r, uint16_t c, bool shadow)
{
    int x0 = PX(CX, a + 512, tail), y0 = PY(CY, a + 512, tail), x1 = PX(CX, a, len), y1 = PY(CY, a, len);
    if (shadow) gfx_aa_capsule(fb, x0 + 24, y0 + 40, x1 + 24, y1 + 40, r + 10, 0, s_pal == &PAL_NIGHT ? 5 : 3);
    else gfx_aa_capsule(fb, x0, y0, x1, y1, r, c, 16);
}

// ---------------------------------------------------------------- weather icons

static void cloud(uint16_t *fb, int cx, int cy, uint16_t c)  // px
{
    gfx_aa_capsule(fb, (cx - 9) * 16, (cy + 3) * 16, (cx + 9) * 16, (cy + 3) * 16, 88, c, 16);
    gfx_aa_capsule(fb, (cx - 7) * 16, (cy + 1) * 16, (cx - 7) * 16, (cy + 1) * 16, 104, c, 16);
    gfx_aa_capsule(fb, (cx + 1) * 16, (cy - 4) * 16, (cx + 1) * 16, (cy - 4) * 16, 136, c, 16);
    gfx_aa_capsule(fb, (cx + 9) * 16, (cy + 1) * 16, (cx + 9) * 16, (cy + 1) * 16, 96, c, 16);
}

static void sun(uint16_t *fb, int cx, int cy, int rad, uint32_t t)
{
    uint16_t c = rgb565(238, 170, 66);
    int spin = (int)(t / 40);
    for (int i = 0; i < 8; i++) {
        int a = spin + i * 128;
        gfx_aa_capsule(fb, PX(cx, a, rad + 4), PY(cy, a, rad + 4), PX(cx, a, rad + 9), PY(cy, a, rad + 9), 12, c, 16);
    }
    gfx_aa_capsule(fb, cx * 16, cy * 16, cx * 16, cy * 16, rad * 16, c, 16);
}

static void moon(uint16_t *fb, int cx, int cy, uint32_t t)
{
    gfx_aa_capsule(fb, cx * 16, cy * 16, cx * 16, cy * 16, 160, rgb565(238, 230, 204), 16);
    gfx_aa_capsule(fb, (cx + 5) * 16, (cy - 4) * 16, (cx + 5) * 16, (cy - 4) * 16, 140, s_bg_icon, 16);
    for (int i = 0; i < 3; i++) {
        int a = sin_u8[(uint8_t)(t / 12 + i * 85)] / 20;
        static const int8_t st[3][2] = {{16, -12}, {22, 4}, {-14, -16}};
        if (a > 1) gfx_aa_capsule(fb, (cx + st[i][0]) * 16, (cy + st[i][1]) * 16, (cx + st[i][0]) * 16, (cy + st[i][1]) * 16, 10, rgb565(240, 236, 220), a);
    }
}

static void icon(uint16_t *fb, int code, bool day, uint32_t t)
{
    int drift = sin_q14[(t / 8) & 1023] * 2 >> 14;
    if (code == 0) {
        if (day) sun(fb, ICX, ICY, 10, t);
        else moon(fb, ICX, ICY, t);
        return;
    }
    if (code <= 2) {
        if (day) sun(fb, ICX + 7, ICY - 7, 7, t);
        else moon(fb, ICX + 8, ICY - 8, t);
        cloud(fb, ICX - 3 + drift, ICY + 4, s_cloud);
        return;
    }
    if (code == 45 || code == 48) {  // fog: drifting bands
        for (int i = 0; i < 4; i++) {
            int off = sin_q14[(t / 6 + i * 200) & 1023] * 4 >> 14, y = ICY - 9 + i * 6;
            gfx_aa_capsule(fb, (ICX - 14 + off) * 16, y * 16, (ICX + 12 + off) * 16, y * 16, 18, s_soft, 14);
        }
        return;
    }
    bool rain = (code >= 51 && code <= 67) || (code >= 80 && code <= 82);
    bool snow = (code >= 71 && code <= 77) || code == 85 || code == 86;
    bool storm = code >= 95;
    uint16_t cc = (rain || storm) ? gfx_blend(s_cloud, s_ink, 3) : s_cloud;
    if (code == 3) cloud(fb, ICX + 7 - drift, ICY - 6, gfx_blend(s_cloud, s_ink, 2));
    if (rain || storm) {
        uint16_t drop = rgb565(104, 146, 200);
        int n = code >= 63 ? 5 : 3;
        for (int i = 0; i < n; i++) {
            int u = (int)((t / 3 + i * 137) % 300), y = ICY + 8 + u * 16 / 300, x = ICX - 9 + i * 18 / (n - 1);
            int a = u < 220 ? 16 : (300 - u) / 5;
            gfx_aa_capsule(fb, x * 16, y * 16, (x - 1) * 16, (y + 4) * 16, 10, drop, a);
        }
    }
    if (snow) {
        for (int i = 0; i < 5; i++) {
            int u = (int)((t / 5 + i * 113) % 400), y = ICY + 8 + u * 16 / 400;
            int x = ICX - 11 + i * 5 + (sin_q14[(u * 3 + i * 200) & 1023] * 2 >> 14);
            gfx_aa_capsule(fb, x * 16, y * 16, x * 16, y * 16, 18, s_pal == &PAL_NIGHT ? rgb565(230, 236, 244) : rgb565(150, 168, 190), 16);
        }
    }
    cloud(fb, ICX - 2 + drift, ICY, cc);
    if (storm && (t / 90) % 23 < 2) {  // a quick flash of lightning
        int16_t bolt[] = {ICX + 1, ICY + 4, ICX - 5, ICY + 14, ICX - 1, ICY + 14, ICX - 4, ICY + 22, ICX + 5, ICY + 10, ICX + 1, ICY + 10, ICX + 4, ICY + 4};
        gfx_poly(fb, bolt, 7, rgb565(250, 204, 70));
    }
}

// ---------------------------------------------------------------- face

void weather_enter(void) {}

static void center_text(uint16_t *fb, const uifont_t *f, int y, const char *s, uint16_t c)
{
    ui_text(fb, f, (W - ui_width(f, s)) / 2, y, s, c, c, FONT_FILL);
}

void weather_render(uint16_t *fb, uint32_t t, uint32_t dt)
{
    (void)dt;
    bool valid = g_wx.valid;
    int code = valid ? g_wx.code : 3;
    bool day = valid ? g_wx.is_day : (!g_now.valid || (g_now.h >= 8 && g_now.h < 20));
    s_pal = !day ? &PAL_NIGHT : code <= 2 ? &PAL_SUN : code < 51 ? &PAL_GREY : &PAL_WET;
    s_ink = c3(s_pal->ink);
    s_soft = c3(s_pal->soft);
    s_accent = c3(s_pal->accent);
    s_cloud = c3(s_pal->cloud);

    // Paper background, a soft vertical wash. The gradient is gentler than one RGB565 step per few
    // rows, so it is ordered-dithered (4x4 Bayer) to avoid visible bands.
    static const uint8_t bayer[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
    for (int y = 0; y < H; y++) {
        int f = y * 256 / H;
        const uint8_t *a = s_pal->top, *b = s_pal->bot;
        int r = a[0] * 16 + ((b[0] - a[0]) * f >> 4), g = a[1] * 16 + ((b[1] - a[1]) * f >> 4), bl = a[2] * 16 + ((b[2] - a[2]) * f >> 4);
        uint16_t c4[4];
        for (int k = 0; k < 4; k++) {
            int d = bayer[y & 3][k];  // 0..15 = a fraction of one 565 step (8 levels red/blue, 4 green)
            int rr = (r + d * 8) >> 4, gg = (g + d * 4) >> 4, bb = (bl + d * 8) >> 4;
            c4[k] = rgb565(rr > 255 ? 255 : rr, gg > 255 ? 255 : gg, bb > 255 ? 255 : bb);
        }
        if (y == ICY) s_bg_icon = c4[0];
        uint16_t *row = fb + y * W;
        for (int x = 0; x < W; x++) row[x] = c4[x & 3];
    }

    // Dial: minute dots and hour batons, no bezel
    for (int i = 0; i < 60; i++) {
        int a = i * 1024 / 60;
        if (i % 5) {
            gfx_aa_capsule(fb, PX(CX, a, 84), PY(CY, a, 84), PX(CX, a, 84), PY(CY, a, 84), 10, s_soft, 16);
        } else {
            int inner = i % 15 ? 74 : 67;
            gfx_aa_capsule(fb, PX(CX, a, inner), PY(CY, a, inner), PX(CX, a, 84), PY(CY, a, 84), 16, i ? s_soft : s_ink, 16);
        }
    }
    char date[24];
    if (g_now.valid) snprintf(date, sizeof(date), "%s %d", clock_wday_name(g_now.wday), g_now.day);
    else strcpy(date, "--");
    center_text(fb, &ui_s, CY + 30, date, s_soft);

    // Hands (10:10 until the time is known); the seconds hand sweeps smoothly
    int h = g_now.valid ? g_now.h : 10, m = g_now.valid ? g_now.m : 10;
    uint32_t mms = g_now.valid ? g_now.minute_ms : 0;
    int a_h = (int)(((h % 12) * 60 + m) * 1024 / 720 + mms * 1024 / 43200000);
    int a_m = (int)((m * 60000 + mms) * 1024 / 3600000);
    int a_s = (int)(mms * 1024 / 60000);
    bool secs = g_app.show_secs && g_now.valid;
    hand(fb, a_h, 46, 9, 42, 0, true);
    hand(fb, a_m, 72, 11, 28, 0, true);
    hand(fb, a_h, 46, 9, 42, s_ink, false);
    hand(fb, a_m, 72, 11, 28, s_ink, false);
    if (secs) {
        hand(fb, a_s, 78, 18, 9, 0, true);
        hand(fb, a_s, 78, 18, 9, s_accent, false);
        gfx_aa_capsule(fb, PX(CX, a_s + 512, 14), PY(CY, a_s + 512, 14), PX(CX, a_s + 512, 14), PY(CY, a_s + 512, 14), 40, s_accent, 16);
    }
    gfx_aa_capsule(fb, CX * 16, CY * 16, CX * 16, CY * 16, 72, s_ink, 16);
    gfx_aa_capsule(fb, CX * 16, CY * 16, CX * 16, CY * 16, 32, secs ? s_accent : c3(s_pal->top), 16);

    // A short rule between the dial and the weather
    gfx_aa_capsule(fb, (CX - 20) * 16, 212 * 16, (CX + 20) * 16, 212 * 16, 6, s_soft, 12);

    // Weather: icon, temperature, max / min, description, place
    icon(fb, code, day, t);
    char buf[40];
    if (valid) {
        int tc = g_wx.temp10 >= 0 ? (g_wx.temp10 + 5) / 10 : -((-g_wx.temp10 + 5) / 10);
        snprintf(buf, sizeof(buf), "%d", tc);
    } else {
        strcpy(buf, "--");
    }
    int tw = font_width(&font_round_m, buf, -2), tx = 166 - tw / 2;
    if (tx + tw > W - 44) tx = W - 44 - tw;
    font_draw(fb, &font_round_m, tx, 220, buf, -2, s_ink, s_ink, FONT_FILL);
    gfx_ring(fb, tx + tw + 6, 228, 3, 5, s_ink);

    if (valid) {
        int hi = (g_wx.hi10 + (g_wx.hi10 >= 0 ? 5 : -5)) / 10, lo = (g_wx.lo10 + (g_wx.lo10 >= 0 ? 5 : -5)) / 10;
        snprintf(buf, sizeof(buf), "max %d    min %d", hi, lo);
        center_text(fb, &ui_s, 282, buf, s_soft);
        center_text(fb, &ui_m, 298, weather_desc(code), s_ink);
    } else {
        center_text(fb, &ui_m, 298, "Sin datos", s_ink);
    }
    // Place in capitals, with a small dot that turns soft when the data is getting old
    int i = 0;
    for (const char *p = g_wx_loc.name; *p && i < (int)sizeof(buf) - 1; p++) buf[i++] = (*p >= 'a' && *p <= 'z') ? *p - 32 : *p;
    buf[i] = 0;
    int lw = ui_width(&ui_s, buf), lx = (W - lw) / 2 + 5;
    ui_text(fb, &ui_s, lx, 318, buf, s_soft, s_soft, FONT_FILL);
    int age = weather_age_s();
    gfx_aa_capsule(fb, (lx - 9) * 16, 326 * 16, (lx - 9) * 16, 326 * 16, 24, age >= 0 && age < 1800 ? rgb565(110, 170, 110) : s_soft, 16);
}
