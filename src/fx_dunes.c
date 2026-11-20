// "Dunas" (full screen, native): a desert made of dots of light on true black. Perlin noise is used as a
// height map (sharp ridges plus a softer layer) and seen through a low, tilted camera, so it reads as dunes
// drifting slowly towards you. Near dots are big, far ones tiny and fading into the dark; faces turned to
// the light glow and the rest stay in shade. The time floats in the sky, made of smaller dots that slide
// into place when the minute changes. Dragging lights up the sand (and the sky) under the finger; a tap
// sends a ripple across the ground. Settings: palette, animation, speed, brightness, dot size and time.
//
// How it stays fast: the ground is a grid of DNX columns by DNZ rows, rows spaced geometrically so every
// ground cell is square. Heights are worked out once per dot, then one pass from near to far finds what
// each column's nearer crests hide (a dot only shows if it rises above everything in front of it), and a
// second pass draws the visible ones from far to near. Along a row the noise's y part is the same for
// every dot, so it is worked out once per row. Discs are anti-aliased in fixed point from the squared
// distance (no square roots) and coloured through a 256-entry palette rebuilt only when a setting changes.
// The face is mostly black: each frame only clears what that buffer showed two frames ago (the ground
// from its highest dot down, and the box round the time), with a full clear now and then as a safety net.
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "app.h"
#include "board.h"
#include "control.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "fx.h"
#include "gfx.h"

#define W FW  // native: 410 x 502 px, the whole screen
#define H FH

// Camera: focal length, horizon row, eye height; dunes up to AMP high; ground from Z0 to Z1 away
#define FOC 300.0f
#define HOR 200.0f
#define CAMY 1.0f
#define AMP 0.55f
#define Z0 0.45f
#define Z1 9.5f
#define SPREAD 0.56f    // half the grid's width on screen, as a fraction of W
#define MAX_COLS 54
#define MAX_ROWS 110
#define MAX_DOTS (MAX_COLS * MAX_ROWS)
#define SKY_Y0 24       // the time's dots live in rows SKY_Y0..SKY_Y1
#define SKY_Y1 196
#define BAND_H 200      // the time is rendered once a minute on a band this tall
#define MAX_SKY 2400
#define MAX_RIPPLES 4

typedef struct {
    float h, e, g;      // height; glow left by the finger; total glow this frame (finger + ripples)
    float y;            // screen row this frame
    uint8_t v, vis;     // brightness 0..255 this frame; visible (not hidden by a nearer crest, on screen)
} dot_t;

typedef struct {
    int16_t x, y;
    float m, mt, e, rate;  // how much of the time it shows (and its target), glow, morph speed
} sky_t;

typedef struct {
    float wx, z, t;
} ripple_t;

static dot_t *s_dot;          // PSRAM
static sky_t *s_sky;          // PSRAM
static uint16_t *s_band;      // PSRAM: the time on black, once a minute
// Tables, in PSRAM like everything else here: internal RAM is what the watch runs short of
typedef struct {
    float rowz[MAX_ROWS], rowinv[MAX_ROWS], rowrad[MAX_ROWS], rowfog[MAX_ROWS];
    float colu[2][MAX_COLS];  // column position -1..1 for even and odd rows (odd ones half a step over)
    float colx[2][MAX_COLS];  // its screen column
    float miny[MAX_COLS];     // per column, the highest row drawn so far (nearer crests hide what is below)
    uint16_t pal[256];        // brightness -> colour, with the brightness setting applied
    uint8_t ramp[256][3];     // brightness -> palette colour (for the glow, mixed towards white)
    uint8_t perm[512];        // Perlin noise permutation
} tables_t;
static tables_t *s_t;
#define s_rowz (s_t->rowz)
#define s_rowinv (s_t->rowinv)
#define s_rowrad (s_t->rowrad)
#define s_rowfog (s_t->rowfog)
#define s_colu (s_t->colu)
#define s_colx (s_t->colx)
#define s_miny (s_t->miny)
#define s_pal (s_t->pal)
#define s_ramp (s_t->ramp)
#define P (s_t->perm)
static int16_t *s_xin;             // PSRAM: per screen row, the first column inside the rounded screen
// What each framebuffer shows, so the next frame only clears that: the ground from row gy0 down and the
// box round the time's dots
typedef struct {
    uint16_t *fb;
    int gy0, sx0, sy0, sx1, sy1;
} drawn_t;
static drawn_t s_drawn[2];
static int64_t s_last_us;
static unsigned s_frame;
static int s_cols, s_rows, s_nsky, s_dots_cfg = -1, s_sky_cfg = -1;
static float s_T, s_sizek;
static uint8_t s_hora[3];
static int s_pal_col = -1, s_pal_bri = -1;
static char s_band_txt[8];
static int s_band_cfg = -1;
static ripple_t s_rip[MAX_RIPPLES];
static int s_nrip;
static bool s_touch_was;
static int s_lastx, s_lasty, s_moved;
static uint32_t s_touch_t0, s_show_until;

// ---------------------------------------------------------------- Perlin noise (2D, improved)


static void perm_init(void)
{
    uint32_t s = 1234567;
    uint8_t p[256];
    for (int i = 0; i < 256; i++) p[i] = i;
    for (int i = 255; i > 0; i--) {
        s = s * 1103515245u + 12345u;
        int j = s % (i + 1);
        uint8_t t = p[i];
        p[i] = p[j];
        p[j] = t;
    }
    for (int i = 0; i < 512; i++) P[i] = p[i & 255];
}

static inline float fade(float t) { return t * t * t * (t * (t * 6 - 15) + 10); }
static inline float grad2(int h, float x, float y)
{
    switch (h & 7) {
    case 0: return x + y;
    case 1: return -x + y;
    case 2: return x - y;
    case 3: return -x - y;
    case 4: return x;
    case 5: return -x;
    case 6: return y;
    default: return -y;
    }
}
static inline int ifloor(float x)
{
    int i = (int)x;
    return x < i ? i - 1 : i;
}
// The part of a noise lookup that only depends on y (the same along a whole row of dots)
typedef struct {
    int Y;
    float y, v;
} nrow_t;
static inline nrow_t noise_row(float y)
{
    int iy = ifloor(y);
    float f = y - iy;
    return (nrow_t){iy & 255, f, fade(f)};
}
static inline float noise2(float x, const nrow_t *r)
{
    int ix = ifloor(x), X = ix & 255;
    x -= ix;
    float u = fade(x), y = r->y;
    int A = P[X] + r->Y, B = P[X + 1] + r->Y;
    float a = grad2(P[A], x, y), b = grad2(P[B], x - 1, y);
    float c = grad2(P[A + 1], x, y - 1), d = grad2(P[B + 1], x - 1, y - 1);
    float ab = a + (b - a) * u, cd = c + (d - c) * u;
    return (ab + (cd - ab) * r->v) * 0.7f;  // about -0.7..0.7, like the 3D noise of the prototype
}
// 1/sqrt(x), one Newton step: plenty for lighting
static inline float rsqrt(float x)
{
    union {
        float f;
        uint32_t i;
    } u = {x};
    u.i = 0x5f3759df - (u.i >> 1);
    return u.f * (1.5f - 0.5f * x * u.f * u.f);
}

static inline float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }
static inline float sstep(float a, float b, float v)
{
    v = clampf((v - a) / (b - a), 0, 1);
    return v * v * (3 - 2 * v);
}

// Inside the rounded screen, `ins` px in from the edge
static bool inside_slow(float x, float y, float ins)
{
    float r = BOARD_LCD_CORNER_R - ins, l = ins, t = ins, rr = W - ins, b = H - ins;
    if (x < l || x > rr || y < t || y > b) return false;
    float cx = x < l + r ? l + r : x > rr - r ? rr - r : x;
    float cy = y < t + r ? t + r : y > b - r ? b - r : y;
    return (x - cx) * (x - cx) + (y - cy) * (y - cy) <= r * r;
}

// ---------------------------------------------------------------- palettes

typedef struct {
    uint8_t st[4][3];  // dim .. bright
    uint8_t hora[3];   // the time's dots
} pal_t;
static const pal_t PALS[6] = {
    {{{10, 31, 92}, {31, 95, 224}, {108, 196, 255}, {230, 247, 255}}, {242, 251, 255}},  // Hielo
    {{{4, 22, 58}, {11, 63, 143}, {29, 134, 201}, {127, 224, 255}}, {191, 242, 255}},    // Abismo
    {{{13, 26, 74}, {26, 111, 138}, {63, 214, 168}, {216, 255, 240}}, {234, 255, 247}},  // Aurora
    {{{26, 15, 69}, {81, 51, 184}, {169, 139, 255}, {241, 234, 255}}, {247, 242, 255}},  // Lavanda
    {{{42, 13, 0}, {154, 58, 5}, {255, 154, 60}, {255, 240, 214}}, {255, 246, 230}},     // Ámbar
    {{{20, 24, 32}, {74, 85, 104}, {169, 180, 198}, {255, 255, 255}}, {255, 255, 255}},  // Plata
};

static void build_palette(void)
{
    const pal_t *p = &PALS[g_app.dune_col];
    float bri = g_app.dune_bri / 100.0f;
    for (int i = 0; i < 256; i++) {
        float v = i / 255.0f * 3;
        int j = v >= 2 ? 2 : (int)v;
        float f = v - j;
        for (int q = 0; q < 3; q++) s_ramp[i][q] = p->st[j][q] + (p->st[j + 1][q] - p->st[j][q]) * f;
        float I = (0.1f + 0.9f * i / 255.0f) * bri;
        s_pal[i] = rgb565(s_ramp[i][0] * I, s_ramp[i][1] * I, s_ramp[i][2] * I);
    }
    memcpy(s_hora, p->hora, 3);
    s_pal_col = g_app.dune_col;
    s_pal_bri = g_app.dune_bri;
}

// ---------------------------------------------------------------- the grids

static void build_ground(void)
{
    static const int SPACING[3] = {11, 12, 15};  // finer than 11 px drops below 20 fps
    int esp = SPACING[g_app.dune_dots];
    s_cols = (44 * 12 + esp / 2) / esp;
    if (s_cols > MAX_COLS) s_cols = MAX_COLS;
    // rows spaced geometrically so every ground cell is square: depth step = width step at that distance
    float q = 1 + 2 * W * SPREAD / (FOC * (s_cols - 1));
    s_rows = (int)ceilf(logf(Z1 / Z0) / logf(q)) + 1;
    if (s_rows > MAX_ROWS) s_rows = MAX_ROWS;
    s_sizek = sqrtf(12.0f / esp);
    for (int k = 0; k < s_rows; k++) {  // k = 0 is the farthest row
        float z = Z1 / powf(q, k);
        s_rowz[k] = z;
        s_rowinv[k] = 1 / z;
        float r = 0.45f + 3.1f / powf(z, 0.9f);
        s_rowrad[k] = (r > 4.6f ? 4.6f : r) * s_sizek;
        s_rowfog[k] = 1 - sstep(3.5f, 9.5f, z) * 0.85f;
    }
    for (int par = 0; par < 2; par++)
        for (int i = 0; i < s_cols; i++) {
            s_colu[par][i] = (i + par * 0.5f) / (s_cols - 1) * 2 - 1;
            s_colx[par][i] = W / 2 + s_colu[par][i] * W * SPREAD;
        }
    memset(s_dot, 0, sizeof(dot_t) * MAX_DOTS);
    s_dots_cfg = g_app.dune_dots;
    s_nrip = 0;
}

static void build_sky(void)
{
    int s = g_app.dune_time == 1 ? 6 : 8;  // the small time needs a finer grid to stay readable
    s_nsky = 0;
    for (int y = SKY_Y0; y < SKY_Y1; y += s)
        for (int x = 5; x < W && s_nsky < MAX_SKY; x += s)
            if (inside_slow(x, y, 6)) s_sky[s_nsky++] = (sky_t){x, y, 0, 0, 0, 1.5f + (fx_rand() % 1000) * 0.003f};
    s_sky_cfg = g_app.dune_time;
    s_band_cfg = -1;
}

// The time on black on a band at the top, then sampled at every sky dot (once a minute)
static void build_time(bool snap)
{
    char buf[8];
    clock_hhmm(buf, true);
    if (s_band_cfg == g_app.dune_time && strcmp(buf, s_band_txt) == 0) return;
    if (!s_band) return;
    memset(s_band, 0, W * BAND_H * 2);
    const font_t *f = g_app.dune_time == 1 ? &font_round_m : &font_round_xl;
    // Draw on the band, W wide: on entering from a wide-canvas face the canvas may still be 274 px wide
    int ow = g_lcd_w, oh = g_lcd_h;
    g_lcd_w = W;
    g_lcd_h = BAND_H;
    font_draw(s_band, f, (W - font_width(f, "00:00", 6)) / 2, 112 - f->h / 2, buf, 6, rgb565(255, 255, 255), 0, FONT_FILL);
    g_lcd_w = ow;
    g_lcd_h = oh;
    int k = g_app.dune_time == 1 ? 2 : 3;
    for (int i = 0; i < s_nsky; i++) {
        sky_t *d = &s_sky[i];
        int a = 0, n = 0;
        for (int dy = -k; dy <= k; dy += k)
            for (int dx = -k; dx <= k; dx += k) {
                int X = d->x + dx, Y = d->y + dy;
                if (X < 0 || X >= W || Y < 0 || Y >= BAND_H) continue;
                a += (__builtin_bswap16(s_band[Y * W + X]) >> 5) & 63;
                n++;
            }
        d->mt = n ? sstep(0.1f, 0.5f, a / (n * 63.0f)) : 0;  // generous: the font's strokes are thin
        if (snap) d->m = d->mt;
    }
    strcpy(s_band_txt, buf);
    s_band_cfg = g_app.dune_time;
}

void dunes_enter(void)
{
    if (!s_t) {
        s_t = heap_caps_calloc(1, sizeof(tables_t), MALLOC_CAP_SPIRAM);
        if (!s_t) return;
        perm_init();
    }
    if (!s_dot) s_dot = heap_caps_malloc(sizeof(dot_t) * MAX_DOTS, MALLOC_CAP_SPIRAM);
    if (!s_sky) s_sky = heap_caps_malloc(sizeof(sky_t) * MAX_SKY, MALLOC_CAP_SPIRAM);
    if (!s_band) s_band = heap_caps_malloc(W * BAND_H * 2, MALLOC_CAP_SPIRAM);
    if (!s_xin) {
        s_xin = heap_caps_malloc(H * sizeof(int16_t), MALLOC_CAP_SPIRAM);
        if (s_xin)
            for (int y = 0; y < H; y++) {
                int x = 0;
                while (x < W / 2 && !inside_slow(x, y, 3)) x++;
                s_xin[y] = x;
            }
    }
    if (!s_dot || !s_sky || !s_band || !s_xin) return;
    memset(s_drawn, 0, sizeof(s_drawn));
    build_palette();
    build_ground();
    build_sky();
    build_time(true);
    s_touch_was = false;
    s_show_until = 0;
}

// ---------------------------------------------------------------- drawing

// Anti-aliased disc in 1/16 px fixed point: coverage falls linearly with the squared distance across the
// edge pixel. Its box is added to bb (x0, y0, x1, y1) for the next clear.
static void disc(uint16_t *fb, float fcx, float fcy, float fr, uint16_t c, int *bb)
{
    int cx = (int)(fcx * 16), cy = (int)(fcy * 16), r = (int)(fr * 16);
    int r0 = r - 8, r1 = r + 8;
    if (r0 < 0) r0 = 0;
    int r02 = r0 * r0, r12 = r1 * r1, k = (16 << 16) / (r12 - r02);
    int x0 = (cx - r1) >> 4, x1 = (cx + r1) >> 4, y0 = (cy - r1) >> 4, y1 = (cy + r1) >> 4;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > W - 1) x1 = W - 1;
    if (y1 > H - 1) y1 = H - 1;
    if (x0 > x1 || y0 > y1) return;
    if (x0 < bb[0]) bb[0] = x0;
    if (y0 < bb[1]) bb[1] = y0;
    if (x1 > bb[2]) bb[2] = x1;
    if (y1 > bb[3]) bb[3] = y1;
    for (int y = y0; y <= y1; y++) {
        int dy = y * 16 + 8 - cy, dy2 = dy * dy;
        uint16_t *row = fb + y * W;
        for (int x = x0; x <= x1; x++) {
            int dx = x * 16 + 8 - cx, d2 = dx * dx + dy2;
            if (d2 >= r12) continue;
            if (d2 <= r02) {
                row[x] = c;
            } else {
                int a = ((r12 - d2) * k) >> 16;
                if (a > 0) row[x] = gfx_blend(row[x], c, a > 16 ? 16 : a);
            }
        }
    }
}

// Palette colour pushed towards white by the glow e
static uint16_t glow_color(int vi, float e, float I, const uint8_t *tint, float tmix)
{
    float r = s_ramp[vi][0], g = s_ramp[vi][1], b = s_ramp[vi][2];
    if (tint) {
        r += (tint[0] - r) * tmix;
        g += (tint[1] - g) * tmix;
        b += (tint[2] - b) * tmix;
    }
    float we = clampf(e * 0.45f, 0, 0.7f);
    r += (255 - r) * we;
    g += (255 - g) * we;
    b += (255 - b) * we;
    return rgb565(clampf(r * I, 0, 255), clampf(g * I, 0, 255), clampf(b * I, 0, 255));
}

// The finger moved from (ax,ay) to (bx,by): light up the visible dots near that stroke
static void light_stroke(float ax, float ay, float bx, float by)
{
    const float R = 34 + 12 * s_sizek, R2 = R * R;
    float vx = bx - ax, vy = by - ay, L = vx * vx + vy * vy;
    if (L < 1) L = 1;
    for (int k = 0; k < s_rows; k++)
        for (int i = 0; i < s_cols; i++) {
            dot_t *d = &s_dot[k * s_cols + i];
            if (!d->vis) continue;
            float x = s_colx[k & 1][i], y = d->y;
            float t = clampf(((x - ax) * vx + (y - ay) * vy) / L, 0, 1);
            float qx = ax + vx * t - x, qy = ay + vy * t - y, q = qx * qx + qy * qy;
            if (q < R2) d->e = fminf(1.4f, d->e + (1 - q / R2) * 0.9f);
        }
    for (int i = 0; i < s_nsky; i++) {
        sky_t *d = &s_sky[i];
        float t = clampf(((d->x - ax) * vx + (d->y - ay) * vy) / L, 0, 1);
        float qx = ax + vx * t - d->x, qy = ay + vy * t - d->y, q = qx * qx + qy * qy;
        if (q < R2) d->e = fminf(1.4f, d->e + (1 - q / R2) * 0.9f);
    }
}

// A tap: a ripple on the ground, centred on the visible dot nearest the finger
static void tap_at(int px, int py)
{
    int best = -1;
    float bd = 1e9f;
    for (int k = 0; k < s_rows; k++)
        for (int i = 0; i < s_cols; i++) {
            dot_t *d = &s_dot[k * s_cols + i];
            if (!d->vis) continue;
            float x = s_colx[k & 1][i] - px, y = d->y - py, q = x * x + y * y;
            if (q < bd) bd = q, best = k * s_cols + i;
        }
    if (best < 0) return;
    if (s_nrip == MAX_RIPPLES) memmove(s_rip, s_rip + 1, sizeof(ripple_t) * --s_nrip);
    int k = best / s_cols, i = best % s_cols;
    s_rip[s_nrip++] = (ripple_t){s_colu[k & 1][i] * s_rowz[k] * W * SPREAD / FOC, s_rowz[k], 0};
}

static void handle_touch(uint32_t now)
{
    int dx, dy;
    app_take_drag(&dx, &dy);
    bool touch = g_app.touch;
    int px = g_app.tx * W / 1000, py = g_app.ty * H / 1000;
    if (touch) {
        if (!s_touch_was) {
            s_touch_t0 = now;
            s_moved = 0;
            s_lastx = px;
            s_lasty = py;
        }
        s_moved += abs(dx) + abs(dy);
        light_stroke(s_lastx, s_lasty, px, py);
        s_lastx = px;
        s_lasty = py;
        s_show_until = now + 5000;  // "time only after a touch": show it for 5 s
    } else if (s_touch_was && s_moved == 0 && now - s_touch_t0 < 400) {
        tap_at(s_lastx, s_lasty);
    }
    s_touch_was = touch;
}

void dunes_render(uint16_t *fb, uint32_t t, uint32_t dt)
{
    (void)t;
    if (!s_t || !s_dot || !s_sky || !s_band || !s_xin) {
        gfx_fill(fb, 0);
        return;
    }
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    if (g_app.dune_col != s_pal_col || g_app.dune_bri != s_pal_bri) build_palette();
    if (g_app.dune_dots != s_dots_cfg) build_ground();
    if (g_app.dune_time != s_sky_cfg) build_sky(), build_time(true);
    build_time(false);

    float fdt = (dt > 100 ? 100 : dt) / 1000.0f;
    s_T += fdt * (1 / 0.75f);  // dt follows the per-face speed: its default, 75 %, is the prototype's pace
    const float T = s_T, decay = expf(-fdt * 1.3f);
    handle_touch(now);
    for (int r = 0; r < s_nrip; r++) s_rip[r].t += fdt;
    while (s_nrip && s_rip[0].t > 1.8f) memmove(s_rip, s_rip + 1, sizeof(ripple_t) * --s_nrip);

    // 1. Heights. Ridges (1 - |noise|) make sharp crests; the animation setting picks how sharp.
    const int anim = g_app.dune_anim;
    const float tipo = anim == 1 ? 0.25f : 0.7f, ka = tipo * AMP, kb = (1 - tipo) * 0.6f * AMP;
    const float sc = 0.55f, scroll = T * 0.32f, drift = T * 0.03f, wx = W * SPREAD / FOC * sc;
    for (int k = 0; k < s_rows; k++) {
        float z = s_rowz[k], Z = (z + scroll) * sc;
        nrow_t r1 = noise_row(Z * 1.2f), r2 = noise_row(Z * 2.6f + T * 0.02f);
        dot_t *row = s_dot + k * s_cols;
        const float *cu = s_colu[k & 1];
        float zw = z * wx;
        for (int i = 0; i < s_cols; i++) {
            float X = cu[i] * zw + drift;
            float ridge = 1 - fabsf(noise2(X * 1.2f, &r1)) * 2.2f;
            float soft = noise2(X * 2.6f + 5.1f, &r2);
            float h = ridge * ka + soft * kb;
            row[i].h = h < -0.1f ? -0.1f : h;  // flat sand floor between dunes keeps the foreground filled
        }
    }

    // 2. Near to far: light, project and find what nearer crests hide
    static const float LX = -0.6f / 1.0989f, LY = 0.65f / 1.0989f, LZ = -0.45f / 1.0989f;  // light, normalised
    for (int i = 0; i < s_cols; i++) s_miny[i] = 1e9f;
    for (int k = s_rows - 1; k >= 0; k--) {
        int kf = k > 0 ? k - 1 : k, kn = k < s_rows - 1 ? k + 1 : k;
        float z = s_rowz[k], dzi = 1 / ((s_rowz[kf] - s_rowz[kn]) + 1e-6f);
        float wstep = z * (W * SPREAD / FOC) * 2 / (s_cols - 1), dxi = 1 / (2 * wstep);
        float fog = s_rowfog[k], rad06 = s_rowrad[k] * 0.6f, fy = FOC * s_rowinv[k];
        dot_t *row = s_dot + k * s_cols, *far = s_dot + kf * s_cols, *near = s_dot + kn * s_cols;
        const float *cx = s_colx[k & 1], *cu = s_colu[k & 1];
        for (int i = 0; i < s_cols; i++) {
            dot_t *d = &row[i];
            float y = HOR + (CAMY - d->h) * fy;
            bool vis = y < s_miny[i] + rad06;
            if (y < s_miny[i]) s_miny[i] = y;
            d->e *= decay;
            d->y = y;
            int iy = (int)y;
            float x = cx[i];
            if (!vis || iy < 0 || iy >= H || x < s_xin[iy] || x > W - 1 - s_xin[iy]) {
                d->vis = 0;
                continue;
            }
            int il = i > 0 ? i - 1 : i, ir = i < s_cols - 1 ? i + 1 : i;
            float dhx = (row[ir].h - row[il].h) * dxi, dhz = (far[i].h - near[i].h) * dzi;
            float L = (-dhx * LX + LY - dhz * LZ) * rsqrt(dhx * dhx + 1 + dhz * dhz);
            float v;
            if (anim == 2) {  // contour lines drawn on the sand
                float f = d->h * 16 - T * 0.25f, fr = f - ifloor(f), dl = fr < 1 - fr ? fr : 1 - fr;
                v = (0.08f + 0.92f * (1 - sstep(0.04f, 0.2f, dl))) * (0.55f + 0.45f * clampf(L, 0, 1));
            } else {
                v = clampf((L - 0.05f) * 1.35f, 0, 1) * 0.8f + clampf(d->h * (1 / AMP), 0, 1) * 0.2f;
            }
            float e = d->e;
            for (int r = 0; r < s_nrip; r++) {
                const ripple_t *o = &s_rip[r];
                float ddx = cu[i] * z * (W * SPREAD / FOC) - o->wx, ddz = z - o->z;
                float d2 = ddx * ddx + ddz * ddz, w = 0.12f + 0.05f * z, r0 = o->t * 1.8f;
                if (d2 > (r0 + w) * (r0 + w) || (r0 > w && d2 < (r0 - w) * (r0 - w))) continue;
                float a = 1 - fabsf(d2 * rsqrt(d2 + 1e-9f) - r0) / w;
                if (a > 0) e += a * (1 - o->t / 1.8f) * 1.2f;
            }
            v = clampf(v * fog + e * 0.8f, 0, 1);
            d->v = (uint8_t)(v * 255);
            d->g = e;  // the ripple's light is not kept in e: it only shows while the ring passes
            d->vis = e > 0.04f ? 2 : 1;  // 2: drawn with the glow colour
        }
    }

    // 3. Clear what this buffer showed two frames ago (all of it after a pause, and now and then anyway:
    //    something else may have drawn on it meanwhile, a system screen, a toast)
    int64_t nowus = esp_timer_get_time();
    if (nowus - s_last_us > 150000) memset(s_drawn, 0, sizeof(s_drawn));
    s_last_us = nowus;
    bool sweep = (++s_frame & 31) < 2;
    drawn_t *dr = s_drawn[0].fb == fb ? &s_drawn[0] : s_drawn[1].fb == fb ? &s_drawn[1] : NULL;
    if (!dr) dr = s_drawn[0].fb ? &s_drawn[1] : &s_drawn[0];
    if (sweep || dr->fb != fb) {
        memset(fb, 0, W * H * 2);
    } else {
        if (dr->gy0 < H) memset(fb + dr->gy0 * W, 0, (H - dr->gy0) * W * 2);
        for (int y = dr->sy0; y <= dr->sy1 && y < dr->gy0; y++) memset(fb + y * W + dr->sx0, 0, (dr->sx1 - dr->sx0 + 1) * 2);
    }

    // 4. The ground from far to near
    const float bri = g_app.dune_bri / 100.0f;
    int gb[4] = {W, H, -1, -1};
    for (int k = 0; k < s_rows; k++) {
        float rad = s_rowrad[k];
        dot_t *row = s_dot + k * s_cols;
        const float *cx = s_colx[k & 1];
        for (int i = 0; i < s_cols; i++) {
            dot_t *d = &row[i];
            if (!d->vis) continue;
            float vf = d->v * (1 / 255.0f), r = rad * (0.75f + 0.35f * vf);
            uint16_t c;
            if (d->vis == 2) {
                c = glow_color(d->v, d->g, (0.1f + 0.9f * vf) * bri, NULL, 0);
                r += d->g * 1.2f;
            } else {
                c = s_pal[d->v];
            }
            disc(fb, cx[i], d->y, r, c, gb);
        }
    }

    // 5. The time in the sky
    bool show = g_app.dune_time != 2 || (int32_t)(s_show_until - now) > 0;
    const float big = g_app.dune_time == 1 ? 2.1f : 2.7f;
    int sb[4] = {W, H, -1, -1};
    for (int i = 0; i < s_nsky; i++) {
        sky_t *d = &s_sky[i];
        float mt = show ? d->mt : 0;
        float kk = fdt * d->rate * (show ? 1 : 0.7f);
        d->m += (mt - d->m) * (kk > 1 ? 1 : kk);
        d->e *= decay;
        if (d->m < 0.03f && d->e < 0.03f) continue;
        float v = clampf(d->m * 0.92f + d->e * 0.6f, 0, 1);
        uint16_t c = glow_color((int)(v * 255), d->e, v * bri, s_hora, 0.55f);
        disc(fb, d->x, d->y, 0.6f + big * fminf(1, d->m) + d->e, c, sb);
    }
    *dr = (drawn_t){.fb = fb, .gy0 = gb[1] < H ? gb[1] : H, .sx0 = sb[0], .sy0 = sb[1], .sx1 = sb[2], .sy1 = sb[3]};
    if (sb[2] < 0) dr->sy1 = -1;  // nothing in the sky
}
