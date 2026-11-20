// "Mariposa" (full screen, native): a particle of light follows a strange attractor (Lorenz by default, or
// Aizawa, Thomas, Halvorsen) and leaves a trail that fades with age, drawn in 3D on true black and turning
// slowly by itself. Far parts are dimmer; where the trail crosses itself it glows brighter (lines add up).
// Dragging turns the figure (it keeps turning a little after you let go); a tap launches a second particle
// a millionth away from the first, in another colour: they travel together, then part (the butterfly
// effect). Every hour the particle restarts somewhere else and the figure is drawn again from nothing.
// Settings: attractor, palette, where the time goes, trail length and brightness.
//
// How it stays fast: each particle keeps its last points in a ring (PSRAM); every frame they are rotated
// and projected and joined by short anti-aliased lines in fixed point, added onto the frame. Consecutive
// points are only 1-3 px apart, so with long trails (or several particles) only every 2nd or 3rd point is
// joined, keeping the work under BUDGET segments a frame whatever the settings. The face is
// mostly black, so a frame only clears the box that buffer showed two frames ago (with a full clear now
// and then as a safety net).
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
#define MAXP 4000      // points kept per particle (the longest trail)
#define NPART 3        // the main particle and up to two launched by a tap
#define EXTRA_LIFE 45  // seconds a launched particle lasts
#define SPIN 0.12f     // turn by itself, radians per second (a turn in under a minute)
#define FOCAL 700.0f
#define SIZE 128.0f    // the attractor's half size on screen, px
#define BUDGET 2400    // segments drawn per frame at most: longer trails join every 2nd or 3rd point

typedef struct {
    float dt;          // integration step
    int spf;           // steps per frame at 60 fps
    float c[3], r;     // where it lives: centre and half size (measured beforehand)
    float p0[3];       // a point on it, to start from
    float yaw0;        // the angle it looks best from
} form_t;
static const form_t FORMS[4] = {
    {0.005f, 6, {-0.3099f, -0.5944f, 24.6749f}, 25.7262f, {17.30381f, 18.90221f, 37.93737f}, -0.78f},  // Lorenz
    {0.01f, 9, {-0.0034f, 0.0323f, 0.7416f}, 1.4852f, {-0.21004f, 0.23458f, -0.29722f}, 0},            // Aizawa
    {0.05f, 12, {1.3530f, 1.3523f, 1.3526f}, 2.4650f, {2.25577f, 2.51742f, 2.80288f}, 0},              // Thomas
    {0.006f, 6, {-2.9101f, -2.9100f, -2.9103f}, 9.2290f, {0.63362f, -3.99566f, 0.52020f}, 0},         // Halvorsen
};

static void deriv(int form, const float *p, float *d)
{
    float x = p[0], y = p[1], z = p[2];
    switch (form) {
    case 0:
        d[0] = 10 * (y - x);
        d[1] = x * (28 - z) - y;
        d[2] = x * y - 8.0f / 3 * z;
        break;
    case 1: {
        const float a = .95f, b = .7f, c = .6f, dd = 3.5f, e = .25f, f = .1f;
        d[0] = (z - b) * x - dd * y;
        d[1] = dd * x + (z - b) * y;
        d[2] = c + a * z - z * z * z / 3 - (x * x + y * y) * (1 + e * z) + f * z * x * x * x;
        break;
    }
    case 2: {
        const float b = 0.208186f;
        d[0] = sinf(y) - b * x;
        d[1] = sinf(z) - b * y;
        d[2] = sinf(x) - b * z;
        break;
    }
    default: {
        const float a = 1.89f;
        d[0] = -a * x - 4 * y - 4 * z - y * y;
        d[1] = -a * y - 4 * z - 4 * x - z * z;
        d[2] = -a * z - 4 * x - 4 * y - x * x;
        break;
    }
    }
}

static void rk4(int form, float *p, float dt)
{
    float a[3], b[3], c[3], d[3], q[3];
    deriv(form, p, a);
    for (int i = 0; i < 3; i++) q[i] = p[i] + a[i] * dt / 2;
    deriv(form, q, b);
    for (int i = 0; i < 3; i++) q[i] = p[i] + b[i] * dt / 2;
    deriv(form, q, c);
    for (int i = 0; i < 3; i++) q[i] = p[i] + c[i] * dt;
    deriv(form, q, d);
    for (int i = 0; i < 3; i++) p[i] += (a[i] + 2 * b[i] + 2 * c[i] + d[i]) * dt / 6;
}

// ---------------------------------------------------------------- palettes

typedef struct {
    uint8_t st[4][3];  // trail, dim .. bright
    uint8_t other[3];  // the launched particles
    uint8_t hora[3];   // the time
} pal_t;
static const pal_t PALS[5] = {
    {{{6, 20, 61}, {31, 95, 224}, {127, 208, 255}, {240, 250, 255}}, {255, 178, 90}, {207, 234, 255}},   // Hielo
    {{{4, 32, 30}, {19, 138, 120}, {88, 240, 192}, {234, 255, 247}}, {255, 122, 184}, {212, 255, 240}},  // Aurora
    {{{42, 6, 0}, {184, 50, 10}, {255, 174, 69}, {255, 243, 216}}, {108, 196, 255}, {255, 230, 196}},    // Fuego
    {{{20, 10, 58}, {90, 54, 208}, {184, 156, 255}, {246, 240, 255}}, {127, 240, 200}, {232, 222, 255}}, // Lavanda
    {{{16, 19, 24}, {79, 90, 110}, {181, 192, 210}, {255, 255, 255}}, {255, 178, 90}, {255, 255, 255}},  // Plata
};

// ---------------------------------------------------------------- state

typedef struct {
    float p[3];
    float *buf;        // PSRAM: MAXP points, x y z
    int n, head;
    bool extra;
    float life, alpha;
} part_t;

typedef struct {
    uint8_t ramp[256][3];   // brightness -> palette colour
    float agepow[256];      // age^1.15 (older segments dimmer)
    int16_t xin[H];         // per screen row, the first column inside the rounded screen
    float bufs[NPART][MAXP * 3];
} store_t;
static store_t *s_s;        // PSRAM
static part_t s_part[NPART];
static int s_npart, s_form = -1, s_col = -1, s_hour = -1;
static float s_yaw, s_pitch = -0.3f, s_vyaw, s_vpitch;
static bool s_touch_was;
static int s_moved;
static uint32_t s_touch_t0, s_show_until;
typedef struct {
    uint16_t *fb;
    int x0, y0, x1, y1;  // what it shows (x1 < x0: nothing)
} drawn_t;
static drawn_t s_drawn[2];
static int64_t s_last_us;
static unsigned s_frame;
static int s_bb[4];      // box drawn this frame

static void build_palette(void)
{
    const pal_t *p = &PALS[g_app.lz_col];
    for (int i = 0; i < 256; i++) {
        float v = i / 255.0f * 3;
        int j = v >= 2 ? 2 : (int)v;
        float f = v - j;
        for (int q = 0; q < 3; q++) s_s->ramp[i][q] = p->st[j][q] + (p->st[j + 1][q] - p->st[j][q]) * f;
    }
    s_col = g_app.lz_col;
}

static void new_part(part_t *P, const float *p, bool extra)
{
    memcpy(P->p, p, sizeof(P->p));
    P->n = P->head = 0;
    P->extra = extra;
    P->life = extra ? EXTRA_LIFE : 1e9f;
    P->alpha = 1;
}

// A fresh start somewhere on the attractor (a little different each time); launched particles go away
static void restart(void)
{
    const form_t *F = &FORMS[g_app.lz_form];
    float p[3] = {F->p0[0], F->p0[1], F->p0[2]};
    int k = fx_rand() % 3000;
    for (int i = 0; i < k; i++) rk4(g_app.lz_form, p, F->dt);
    s_part[0].buf = s_s->bufs[0];
    new_part(&s_part[0], p, false);
    s_npart = 1;
}

static void launch(void)
{
    if (s_npart == NPART) {  // the oldest launched one makes room
        float *b = s_part[1].buf;
        s_part[1] = s_part[2];
        s_part[2].buf = b;
        s_npart--;
    }
    part_t *P = &s_part[s_npart];
    float *spare = NULL;
    for (int k = 0; k < NPART && !spare; k++) {  // a ring no live particle uses
        bool used = false;
        for (int j = 0; j < s_npart; j++) used |= s_part[j].buf == s_s->bufs[k];
        if (!used) spare = s_s->bufs[k];
    }
    P->buf = spare;
    float p[3] = {s_part[0].p[0] + 1e-6f * FORMS[g_app.lz_form].r, s_part[0].p[1], s_part[0].p[2]};
    new_part(P, p, true);
    s_npart++;
}

static void step(part_t *P, int steps)
{
    const form_t *F = &FORMS[g_app.lz_form];
    for (int s = 0; s < steps; s++) {
        rk4(g_app.lz_form, P->p, F->dt);
        float *q = P->buf + P->head * 3;
        q[0] = P->p[0];
        q[1] = P->p[1];
        q[2] = P->p[2];
        if (++P->head == MAXP) P->head = 0;
        if (P->n < MAXP) P->n++;
    }
}

void lorenz_enter(void)
{
    if (!s_s) {
        s_s = heap_caps_calloc(1, sizeof(store_t), MALLOC_CAP_SPIRAM);
        if (!s_s) return;
        for (int i = 0; i < 256; i++) s_s->agepow[i] = powf(i / 255.0f, 1.15f);
        for (int y = 0; y < H; y++) {  // rounded corners, 3 px in
            const float ins = 3, r = BOARD_LCD_CORNER_R - ins;
            int x = 0;
            for (; x < W / 2; x++) {
                float cx = x < ins + r ? ins + r : x, cy = y < ins + r ? ins + r : y > H - ins - r ? H - ins - r : y;
                if (x >= ins && y >= ins && y <= H - ins && (x - cx) * (x - cx) + (y - cy) * (y - cy) <= r * r) break;
            }
            s_s->xin[y] = x;
        }
    }
    if (s_form != g_app.lz_form) {
        s_form = g_app.lz_form;
        s_yaw = FORMS[s_form].yaw0;
        restart();
    }
    build_palette();
    memset(s_drawn, 0, sizeof(s_drawn));
    s_touch_was = false;
    s_show_until = 0;
}

// ---------------------------------------------------------------- drawing

// Adds colour (r 0..31, g 0..63, b 0..31, times 16) at alpha a/16 onto one pixel, saturating
static inline void add_px(uint16_t *fb, int x, int y, int r, int g, int b, int a)
{
    if ((unsigned)y >= H || x < s_s->xin[y] || x > W - 1 - s_s->xin[y] || a <= 0) return;
    uint16_t *p = fb + y * W;
    uint32_t d = __builtin_bswap16(p[x]);
    int R = (d >> 11) + ((r * a) >> 8), G = ((d >> 5) & 63) + ((g * a) >> 8), B = (d & 31) + ((b * a) >> 8);
    if (R > 31) R = 31;
    if (G > 63) G = 63;
    if (B > 31) B = 31;
    p[x] = __builtin_bswap16((uint16_t)(R << 11 | G << 5 | B));
}

// Anti-aliased line (Wu): one step per pixel along the longer axis, shared between the two pixels across
// it; the other coordinate advances in 16.16 fixed point (no division in the loop). Ends are 1/16 px. The
// last pixel is left to the next segment, so joints are not lit twice; a segment shorter than a pixel
// draws nothing and returns false (the caller then keeps its start for the next one).
static bool line(uint16_t *fb, int x0, int y0, int x1, int y1, int r, int g, int b)
{
    int dx = x1 - x0, dy = y1 - y0;
    if (abs(dx) >= abs(dy)) {
        int ix0 = x0 >> 4, ix1 = x1 >> 4, n = abs(ix1 - ix0);
        if (!n) return false;
        int sx = ix1 > ix0 ? 1 : -1;
        int32_t y = (y0 << 12) + (1 << 11), ys = (int32_t)(((int64_t)dy << 12) / n);
        for (int k = 0, x = ix0; k < n; k++, x += sx, y += ys) {
            int iy = y >> 16, f = (y >> 12) & 15;
            add_px(fb, x, iy, r, g, b, 16 - f);
            add_px(fb, x, iy + 1, r, g, b, f);
        }
    } else {
        int iy0 = y0 >> 4, iy1 = y1 >> 4, n = abs(iy1 - iy0);
        if (!n) return false;
        int sy = iy1 > iy0 ? 1 : -1;
        int32_t x = (x0 << 12) + (1 << 11), xs = (int32_t)(((int64_t)dx << 12) / n);
        for (int k = 0, y = iy0; k < n; k++, y += sy, x += xs) {
            int ix = x >> 16, f = (x >> 12) & 15;
            add_px(fb, ix, y, r, g, b, 16 - f);
            add_px(fb, ix + 1, y, r, g, b, f);
        }
    }
    int lx = (x0 < x1 ? x0 : x1) >> 4, hx = ((x0 > x1 ? x0 : x1) >> 4) + 1;
    int ly = (y0 < y1 ? y0 : y1) >> 4, hy = ((y0 > y1 ? y0 : y1) >> 4) + 1;
    if (lx < s_bb[0]) s_bb[0] = lx;
    if (ly < s_bb[1]) s_bb[1] = ly;
    if (hx > s_bb[2]) s_bb[2] = hx;
    if (hy > s_bb[3]) s_bb[3] = hy;
    return true;
}

// The particle itself: a small soft star
static void star(uint16_t *fb, float cx, float cy, float rad, const uint8_t *c, float amax)
{
    int R = (int)ceilf(rad), x0 = (int)cx, y0 = (int)cy;
    int r = c[0] >> 3 << 4, g = c[1] >> 2 << 4, b = c[2] >> 3 << 4;
    for (int j = -R; j <= R; j++)
        for (int i = -R; i <= R; i++) {
            float d = sqrtf((float)(i * i + j * j)) / rad;
            if (d >= 1) continue;
            add_px(fb, x0 + i, y0 + j, r, g, b, (int)((1 - d) * (1 - d) * amax * 16));
        }
    if (x0 - R < s_bb[0]) s_bb[0] = x0 - R;
    if (y0 - R < s_bb[1]) s_bb[1] = y0 - R;
    if (x0 + R > s_bb[2]) s_bb[2] = x0 + R;
    if (y0 + R > s_bb[3]) s_bb[3] = y0 + R;
}

static void handle_touch(uint32_t now, float fdt)
{
    int dx, dy;
    app_take_drag(&dx, &dy);
    bool touch = g_app.touch;
    if (touch) {
        if (!s_touch_was) {
            s_touch_t0 = now;
            s_moved = 0;
        }
        s_moved += abs(dx) + abs(dy);
        if (dx || dy) {
            s_yaw += dx * 0.01f;
            s_pitch += dy * 0.01f;
            if (fdt > 0.005f) {  // remember the speed, for the turn that goes on after letting go
                s_vyaw = s_vyaw * 0.5f + dx * 0.01f / fdt * 0.5f;
                s_vpitch = s_vpitch * 0.5f + dy * 0.01f / fdt * 0.5f;
            }
        }
        s_show_until = now + 5000;  // "time only after a touch": show it for 5 s
    } else if (s_touch_was && s_moved == 0 && now - s_touch_t0 < 400) {
        launch();
    }
    s_touch_was = touch;
}

void lorenz_render(uint16_t *fb, uint32_t t, uint32_t dt)
{
    (void)t;
    if (!s_s) {
        gfx_fill(fb, 0);
        return;
    }
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    if (g_app.lz_form != s_form) {
        s_form = g_app.lz_form;
        s_yaw = FORMS[s_form].yaw0;
        restart();
    }
    if (g_app.lz_col != s_col) build_palette();
    if (g_now.valid && g_now.h != s_hour) {  // a new hour: the figure starts again
        if (s_hour >= 0) restart();
        s_hour = g_now.h;
    }
    // dt follows the per-face speed: its default, 75 %, is the prototype's pace
    float fdt = (dt > 100 ? 100 : dt) / 1000.0f * (1 / 0.75f);
    handle_touch(now, fdt);

    // Turn: by itself, plus what is left of a drag; the tilt drifts back to its rest
    if (!g_app.touch) {
        s_yaw += (SPIN + s_vyaw) * fdt;
        s_pitch += s_vpitch * fdt;
        s_vyaw *= expf(-fdt * 1.6f);
        s_vpitch *= expf(-fdt * 2.5f);
        s_pitch += (-0.3f - s_pitch) * fdt * 0.3f;
    }
    if (s_pitch < -1.4f) s_pitch = -1.4f;
    if (s_pitch > 1.4f) s_pitch = 1.4f;

    // Move the particles
    const form_t *F = &FORMS[s_form];
    int steps = (int)(F->spf * fdt * 60 + 0.5f);
    if (steps < 1) steps = 1;
    for (int i = 0; i < s_npart; i++) {
        part_t *P = &s_part[i];
        step(P, steps);
        if (P->extra) {
            P->life -= fdt;
            if (P->life < 3) P->alpha = P->life > 0 ? P->life / 3 : 0;
        }
    }
    for (int i = 1; i < s_npart;)
        if (s_part[i].life <= 0) {
            float *b = s_part[i].buf;
            memmove(&s_part[i], &s_part[i + 1], sizeof(part_t) * (s_npart - i - 1));
            s_part[--s_npart].buf = b;
        } else {
            i++;
        }

    // Clear what this buffer showed two frames ago (everything after a pause, and now and then anyway:
    // something else may have drawn on it meanwhile)
    int64_t nowus = esp_timer_get_time();
    if (nowus - s_last_us > 150000) memset(s_drawn, 0, sizeof(s_drawn));
    s_last_us = nowus;
    bool sweep = (++s_frame & 31) < 2;
    drawn_t *dr = s_drawn[0].fb == fb ? &s_drawn[0] : s_drawn[1].fb == fb ? &s_drawn[1] : NULL;
    if (!dr) dr = s_drawn[0].fb ? &s_drawn[1] : &s_drawn[0];
    if (sweep || dr->fb != fb) {
        memset(fb, 0, W * H * 2);
    } else if (dr->x1 >= dr->x0) {
        int x0 = dr->x0 < 0 ? 0 : dr->x0, x1 = dr->x1 > W - 1 ? W - 1 : dr->x1;
        int y0 = dr->y0 < 0 ? 0 : dr->y0, y1 = dr->y1 > H - 1 ? H - 1 : dr->y1;
        if (x0 == 0 && x1 == W - 1) memset(fb + y0 * W, 0, (y1 - y0 + 1) * W * 2);
        else
            for (int y = y0; y <= y1; y++) memset(fb + y * W + x0, 0, (x1 - x0 + 1) * 2);
    }
    s_bb[0] = W;
    s_bb[1] = H;
    s_bb[2] = s_bb[3] = -1;

    // The trails: rotate (yaw round the vertical, then tilt), perspective, lines from oldest to newest
    const bool below = g_app.lz_time == 1;
    const float cx = W / 2, cy = below ? 222 : 284, S = SIZE / F->r;
    const float cyw = cosf(s_yaw), syw = sinf(s_yaw), cp = cosf(s_pitch), sp = sinf(s_pitch);
    const float bri = g_app.lz_bri / 100.0f;
    const pal_t *pal = &PALS[s_col];
    const int len = g_app.lz_trail * 100;
    int total = 0;
    for (int pi = 0; pi < s_npart; pi++) {
        int l = s_part[pi].extra ? len / 2 : len;
        total += s_part[pi].n < l ? s_part[pi].n : l;
    }
    const int stride = (total + BUDGET - 1) / BUDGET > 1 ? (total + BUDGET - 1) / BUDGET : 1;
    for (int pi = 0; pi < s_npart; pi++) {
        part_t *P = &s_part[pi];
        int L = P->n < (P->extra ? len / 2 : len) ? P->n : (P->extra ? len / 2 : len);  // launched: half a trail
        if (L < 2) continue;
        int start = P->head - L;
        if (start < 0) start += MAXP;
        int px0 = 0, py0 = 0;
        float lx = 0, ly = 0;
        const float kI = bri * P->alpha * 0.85f, invL = 255.0f / L;
        bool first = true;
        for (int s = (L - 1) % stride; s < L; s += stride) {  // ends on the newest point
            int idx = start + s;
            if (idx >= MAXP) idx -= MAXP;
            const float *q = P->buf + idx * 3;
            float a = (q[0] - F->c[0]) * S, b = (q[1] - F->c[1]) * S, u = (q[2] - F->c[2]) * S;
            float xr = a * cyw - b * syw, yr = a * syw + b * cyw;
            float yy = -u * cp - yr * sp, dz = -u * sp + yr * cp;
            float k = FOCAL / (FOCAL + dz);
            float sx = cx + xr * k, sy = cy + yy * k;
            int x16 = (int)(sx * 16), y16 = (int)(sy * 16);
            if (!first) {
                int ai = (int)(s * invL);
                float depth = 0.62f - dz * (1 / (SIZE * 2.6f));
                depth = depth < 0.25f ? 0.25f : depth > 1 ? 1 : depth;
                float v = s_s->agepow[ai] * depth;
                const uint8_t *c = s_s->ramp[(int)(v * 255)];
                float R = c[0], G = c[1], B = c[2];
                if (P->extra) {
                    float m = 0.75f * ai / 255.0f;
                    R += (pal->other[0] - R) * m;
                    G += (pal->other[1] - G) * m;
                    B += (pal->other[2] - B) * m;
                }
                float I = (0.25f + 0.75f * v) * kI * 24;  // Wu lines light half as many pixels
                if (line(fb, px0, py0, x16, y16, (int)(R * I) >> 3, (int)(G * I) >> 2, (int)(B * I) >> 3)) {
                    px0 = x16;
                    py0 = y16;
                }
            } else {
                first = false;
                px0 = x16;
                py0 = y16;
            }
            lx = sx;
            ly = sy;
        }
        static const uint8_t white[3] = {255, 255, 255};
        const uint8_t *hc = P->extra ? pal->other : white;
        star(fb, lx, ly, 7, hc, 0.55f * bri * P->alpha);
        star(fb, lx, ly, 2.2f, white, bri * P->alpha);
    }

    // The time, soft, above or below the figure
    if (g_app.lz_time != 2 || (int32_t)(s_show_until - now) > 0) {
        char buf[8];
        clock_hhmm(buf, true);
        const font_t *f = &font_round_m;  // 60 px, as in the prototype: the figure is the star
        int tw = font_width(f, "00:00", 4), tx = (W - tw) / 2, ty = (below ? 428 : 78) - f->h / 2;
        float k = 0.82f * bri;
        font_draw(fb, f, tx, ty, buf, 4, rgb565(pal->hora[0] * k, pal->hora[1] * k, pal->hora[2] * k), 0, FONT_FILL);
        if (tx - f->pad < s_bb[0]) s_bb[0] = tx - f->pad;
        if (ty - f->pad < s_bb[1]) s_bb[1] = ty - f->pad;
        if (tx + tw + f->pad > s_bb[2]) s_bb[2] = tx + tw + f->pad;
        if (ty + f->h + f->pad > s_bb[3]) s_bb[3] = ty + f->h + f->pad;
    }
    *dr = (drawn_t){fb, s_bb[0], s_bb[1], s_bb[2], s_bb[3]};
}
