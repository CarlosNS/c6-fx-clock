// "Corriente" (full screen, native): thousands of particles follow the currents of a slowly changing
// Perlin noise field and leave fine threads of light on true black, like silk or smoke. Where the digits
// are, particles slow down and leave more light, and the digits keep a soft glow of their own, so the time
// is woven into the flow itself. Dragging stirs a whirl round the finger; a tap releases a handful of bright
// particles. Settings: style (silk, smoke, river), palette, time (woven, on top, only after a touch), how
// many particles, turbulence and brightness.
//
// How it works: the light left on the screen is one byte per pixel (PSRAM). Every frame one pass fades it
// through a table, writes it back and turns it into colour through a 256-entry palette, four pixels at a
// time (dark stretches cost almost nothing). Particles add light where they pass. The digits are rendered
// once a minute into a mask (for the slowdown) and runs per row (for their glow).
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
#define MAXP 3400       // particles: up to 3000 from the setting plus the bursts of taps
#define BURST 160
#define DY0 80          // the stacked digits live in rows DY0..DY0+DROWS-1
#define DROWS 368
#define MAX_RUNS 6000

typedef struct {
    float x, y, life;
    uint8_t extra;      // launched by a tap: brighter, goes away when it dies
} part_t;

typedef struct {
    uint8_t light[W * H];       // light left on each pixel, 0..255 (luz 0..1)
    uint8_t mask[W * H];        // where the digits are, 0..255
    uint16_t tmp[W * DROWS];    // the digits rendered once a minute
    uint32_t runs[MAX_RUNS];    // digit runs: x (10 bits), len (10), row (9), level (3)
    part_t part[MAXP];
    int16_t xin[H];             // per row, the first column inside the rounded screen
    uint16_t pal[256];          // light -> colour (square root and brightness included)
    uint8_t fade[256];          // light -> light one frame later
    uint8_t perm[512];
} store_t;
static store_t *s_s;            // PSRAM
static int s_np, s_nruns, s_col = -1, s_bri = -1, s_style = -1;
static char s_txt[8];
static int s_txt_mode = -1;
static float s_T;
static bool s_touch_was, s_moved;
static uint32_t s_touch_t0, s_show_until;
static int s_tx, s_ty, s_ltx, s_lty;

// ---------------------------------------------------------------- Perlin noise (improved, 3D)

static inline float fade5(float t) { return t * t * t * (t * (t * 6 - 15) + 10); }
static inline float grad3(int h, float x, float y, float z)
{
    h &= 15;
    float u = h < 8 ? x : y, v = h < 4 ? y : (h == 12 || h == 14 ? x : z);
    return ((h & 1) ? -u : u) + ((h & 2) ? -v : v);
}
static inline int ifloor(float x)
{
    int i = (int)x;
    return x < i ? i - 1 : i;
}
static float noise3(float x, float y, float z)
{
    const uint8_t *P = s_s->perm;
    int ix = ifloor(x), iy = ifloor(y), iz = ifloor(z), X = ix & 255, Y = iy & 255, Z = iz & 255;
    x -= ix;
    y -= iy;
    z -= iz;
    float u = fade5(x), v = fade5(y), w = fade5(z);
    int A = P[X] + Y, AA = P[A] + Z, AB = P[A + 1] + Z, B = P[X + 1] + Y, BA = P[B] + Z, BB = P[B + 1] + Z;
#define L(a, b, t) ((a) + ((b) - (a)) * (t))
    return L(L(L(grad3(P[AA], x, y, z), grad3(P[BA], x - 1, y, z), u), L(grad3(P[AB], x, y - 1, z), grad3(P[BB], x - 1, y - 1, z), u), v),
             L(L(grad3(P[AA + 1], x, y, z - 1), grad3(P[BA + 1], x - 1, y, z - 1), u),
               L(grad3(P[AB + 1], x, y - 1, z - 1), grad3(P[BB + 1], x - 1, y - 1, z - 1), u), v), w);
#undef L
}

// ---------------------------------------------------------------- styles and palettes

typedef struct {
    float fade, vel, dep;  // light kept per frame (at 60 fps), speed in px per frame, light left per pass
} style_t;
static const style_t STYLES[3] = {{0.975f, 1.1f, 0.05f}, {0.94f, 0.7f, 0.09f}, {0.985f, 1.8f, 0.035f}};  // Seda, Humo, Río

static const uint8_t PALS[5][5][3] = {
    {{0, 0, 0}, {18, 58, 154}, {63, 155, 255}, {191, 232, 255}, {255, 255, 255}},  // Hielo
    {{0, 0, 0}, {11, 90, 82}, {47, 214, 164}, {200, 255, 233}, {255, 255, 255}},   // Aurora
    {{0, 0, 0}, {122, 26, 4}, {255, 122, 30}, {255, 217, 160}, {255, 255, 255}},   // Fuego
    {{0, 0, 0}, {58, 29, 143}, {155, 120, 255}, {232, 222, 255}, {255, 255, 255}}, // Lavanda
    {{0, 0, 0}, {54, 60, 72}, {142, 152, 171}, {223, 229, 239}, {255, 255, 255}},  // Plata
};

static void build_palette(void)
{
    const float bri = g_app.cr_bri / 100.0f;
    for (int l = 0; l < 256; l++) {
        float v = sqrtf(l / 255.0f) * bri * 4;  // the square root brings the faint threads up
        int j = v >= 3 ? 3 : (int)v;
        float f = v - j;
        const uint8_t *a = PALS[g_app.cr_col][j], *b = PALS[g_app.cr_col][j + 1];
        s_s->pal[l] = l ? rgb565(a[0] + (b[0] - a[0]) * f, a[1] + (b[1] - a[1]) * f, a[2] + (b[2] - a[2]) * f) : 0;
    }
    s_col = g_app.cr_col;
    s_bri = g_app.cr_bri;
}

// Light one frame later. Built for 30 fps (the face's usual pace); a step down of at least 1 so the
// faintest threads do go out
static void build_fade(void)
{
    float k = powf(STYLES[g_app.cr_style].fade, 2);
    for (int l = 0; l < 256; l++) {
        int n = (int)(l * k);
        s_s->fade[l] = l && n >= l ? l - 1 : n;
    }
    s_style = g_app.cr_style;
}

// ---------------------------------------------------------------- the time

// The digits, stacked big, once a minute: a mask for the particles and runs per row for their glow
static void build_time(void)
{
    char buf[8];
    clock_hhmm(buf, true);
    if (s_txt_mode == g_app.cr_time && strcmp(buf, s_txt) == 0) return;
    strcpy(s_txt, buf);
    s_txt_mode = g_app.cr_time;
    memset(s_s->mask, 0, sizeof(s_s->mask));
    s_nruns = 0;
    if (g_app.cr_time != 0) return;  // only "woven" needs them
    memset(s_s->tmp, 0, sizeof(s_s->tmp));
    int ow = g_lcd_w, oh = g_lcd_h;
    g_lcd_w = W;
    g_lcd_h = DROWS;  // draw on the band (same width as the screen, so the same stride)
    clock_draw_stacked(s_s->tmp, &font_round_xxl, 0, 8, rgb565(255, 255, 255), 0, FONT_FILL);
    g_lcd_w = ow;
    g_lcd_h = oh;
    for (int r = 0; r < DROWS; r++) {
        const uint16_t *src = s_s->tmp + r * W;
        uint8_t *m = s_s->mask + (DY0 + r) * W;
        for (int x = 0; x < W;) {
            int g = (__builtin_bswap16(src[x]) >> 5) & 63;
            if (!g) {
                x++;
                continue;
            }
            int lev = g * 7 / 63, x0 = x;  // 3-bit level for the glow
            while (x < W && x - x0 < 1023) {
                int g2 = (__builtin_bswap16(src[x]) >> 5) & 63;
                if (!g2 || g2 * 7 / 63 != lev) break;
                m[x] = g2 * 255 / 63;
                x++;
            }
            if (lev && s_nruns < MAX_RUNS) s_s->runs[s_nruns++] = (uint32_t)x0 | (uint32_t)(x - x0) << 10 | (uint32_t)(DY0 + r) << 20 | (uint32_t)lev << 29;
        }
    }
}

// ---------------------------------------------------------------- particles

static void spawn(part_t *p, float x, float y, int extra)
{
    p->x = x;
    p->y = y;
    p->life = 60 + fx_rand() % 240;
    p->extra = extra;
}
static void spawn_random(part_t *p) { spawn(p, fx_rand() % W, fx_rand() % H, 0); }

void flow_enter(void)
{
    if (!s_s) {
        s_s = heap_caps_calloc(1, sizeof(store_t), MALLOC_CAP_SPIRAM);
        if (!s_s) return;
        uint32_t sd = 987654321;
        uint8_t p[256];
        for (int i = 0; i < 256; i++) p[i] = i;
        for (int i = 255; i > 0; i--) {
            sd = sd * 1103515245u + 12345u;
            int j = sd % (i + 1);
            uint8_t t = p[i];
            p[i] = p[j];
            p[j] = t;
        }
        for (int i = 0; i < 512; i++) s_s->perm[i] = p[i & 255];
        for (int y = 0; y < H; y++) {  // rounded corners, 3 px in
            const float ins = 3, r = BOARD_LCD_CORNER_R - ins;
            int x = 0;
            for (; x < W / 2; x++) {
                float cx = x < ins + r ? ins + r : x, cy = y < ins + r ? ins + r : y > H - ins - r ? H - ins - r : y;
                if (x >= ins && y >= ins && y <= H - ins && (x - cx) * (x - cx) + (y - cy) * (y - cy) <= r * r) break;
            }
            s_s->xin[y] = x;
        }
        s_np = 0;
    }
    build_palette();
    build_fade();
    s_txt_mode = -1;
    build_time();
    s_touch_was = false;
    s_show_until = 0;
}

static void handle_touch(uint32_t now)
{
    int dx, dy;
    app_take_drag(&dx, &dy);
    bool touch = g_app.touch;
    s_ltx = s_tx;
    s_lty = s_ty;
    s_tx = g_app.tx * W / 1000;
    s_ty = g_app.ty * H / 1000;
    if (touch) {
        if (!s_touch_was) {
            s_touch_t0 = now;
            s_moved = false;
        }
        if (dx || dy) s_moved = true;
        s_show_until = now + 5000;  // "time only after a touch": show it for 5 s
    } else if (s_touch_was && !s_moved && now - s_touch_t0 < 400) {
        for (int i = 0; i < BURST && s_np < MAXP; i++) {
            float a = (fx_rand() % 6283) / 1000.0f, r = fx_rand() % 14;
            spawn(&s_s->part[s_np++], s_tx + cosf(a) * r, s_ty + sinf(a) * r, 1);
        }
    }
    s_touch_was = touch;
}

void flow_render(uint16_t *fb, uint32_t t, uint32_t dt)
{
    (void)t;
    if (!s_s) {
        gfx_fill(fb, 0);
        return;
    }
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    if (g_app.cr_col != s_col || g_app.cr_bri != s_bri) build_palette();
    if (g_app.cr_style != s_style) build_fade();
    build_time();
    // dt follows the per-face speed: its default, 75 %, is the prototype's pace
    float f60 = (dt > 100 ? 100 : dt) / 1000.0f * (1 / 0.75f) * 60;  // frames at 60 fps this frame stands for
    s_T += f60 / 60;
    handle_touch(now);

    // Particles: as many as the setting (bursts on top, until they die)
    int want = g_app.cr_num * 100;
    int normal = 0;
    for (int i = 0; i < s_np; i++) normal += !s_s->part[i].extra;
    while (normal < want && s_np < MAXP) spawn_random(&s_s->part[s_np++]), normal++;

    const style_t *st = &STYLES[g_app.cr_style];
    const bool woven = g_app.cr_time == 0, whirl = g_app.touch && s_moved;
    const float sc = 0.0055f * g_app.cr_turb / 10.0f, tz = s_T * 0.06f, step = st->vel * f60;
    const int dep = (int)(st->dep * 255 * 2.0f + 0.5f);  // a frame here stands for about two at 60 fps
    const float wx = s_tx, wy = s_ty, R = 70, R2 = R * R;
    for (int i = s_np - 1; i >= 0; i--) {
        part_t *p = &s_s->part[i];
        if (!p->extra && normal > want) {  // fewer particles chosen: let go of the extra ones
            *p = s_s->part[--s_np];
            normal--;
            continue;
        }
        float a = noise3(p->x * sc, p->y * sc, tz) * 6.9115f;  // 2.2 pi
        float vx = cosf(a), vy = sinf(a);
        if (whirl) {
            float dx = p->x - wx, dy = p->y - wy, d2 = dx * dx + dy * dy;
            if (d2 < R2 * 4) {
                float f = expf(-d2 / R2) * 2.4f;
                vx += -dy / R * f;
                vy += dx / R * f;
            }
        }
        int xi = (int)p->x, yi = (int)p->y;
        int m = woven && (unsigned)xi < W && (unsigned)yi < H ? s_s->mask[yi * W + xi] : 0;
        float sp = step * (1 - m * (0.6f / 255)) * (p->extra ? 1.3f : 1);
        p->x += vx * sp;
        p->y += vy * sp;
        p->life -= f60;
        xi = (int)p->x;
        yi = (int)p->y;
        if (p->life < 0 || p->x < 0 || (unsigned)yi >= H || xi < s_s->xin[yi] || xi > W - 1 - s_s->xin[yi]) {
            if (p->extra) *p = s_s->part[--s_np];
            else spawn_random(p);
            continue;
        }
        uint8_t *l = &s_s->light[yi * W + xi];
        int add = dep * (255 + m * 5) / 255 * (p->extra ? 3 : 1) * (whirl ? 14 : 10) / 10;
        int v = *l + add;
        *l = v > 255 ? 255 : v;
    }

    // The digits' own soft glow (woven): the light never drops below it there
    if (woven) {
        const int floor7 = 14;  // a faint glow: the threads crossing the digits do the rest
        for (int k = 0; k < s_nruns; k++) {
            uint32_t r = s_s->runs[k];
            int x = r & 1023, len = (r >> 10) & 1023, y = (r >> 20) & 511, lev = r >> 29;
            uint8_t *l = s_s->light + y * W + x, f = floor7 * lev / 7;
            for (int q = 0; q < len; q++)
                if (l[q] < f) l[q] = f;
        }
    }

    // Fade and colour in one pass, four pixels at a time
    const uint8_t *fd = s_s->fade;
    const uint16_t *pal = s_s->pal;
    uint32_t *lw = (uint32_t *)s_s->light, *fw = (uint32_t *)fb;
    for (int i = 0; i < W * H / 4; i++) {
        uint32_t q = lw[i];
        if (!q) {
            fw[2 * i] = fw[2 * i + 1] = 0;
            continue;
        }
        uint8_t a = fd[q & 255], b = fd[(q >> 8) & 255], c = fd[(q >> 16) & 255], d = fd[q >> 24];
        lw[i] = a | b << 8 | c << 16 | (uint32_t)d << 24;
        fw[2 * i] = pal[a] | (uint32_t)pal[b] << 16;
        fw[2 * i + 1] = pal[c] | (uint32_t)pal[d] << 16;
    }

    // The time on top ("on top", or "only after a touch" for 5 s)
    if (g_app.cr_time == 1 || (g_app.cr_time == 2 && (int32_t)(s_show_until - now) > 0)) {
        const uint8_t *c = PALS[g_app.cr_col][3];
        float k = 0.85f * g_app.cr_bri / 100.0f;
        clock_draw_row(fb, &font_round_m, H / 2 - font_round_m.h / 2, 4, rgb565(c[0] * k, c[1] * k, c[2] * k), 0, FONT_FILL);
    }
}
