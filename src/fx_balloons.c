// "Globos" (full screen, native): a real sky (photos from Poly Haven, CC0) that pans slowly and changes
// with the time of day (dawn, day, sunset, night, cross-fading over half an hour), the time big and white
// in the middle, and coloured balloons rising behind it (small, slow, a little hazy), in front of it, and
// now and then one drifting right past the eye. A tap pops a balloon (pieces fall, the string tumbles, a
// pop from the speaker); dragging blows wind. On the hour, a big release. Settings: balloon colours, how
// many, the sky (by the time of day or always day) and the pop sound.
//
// How it works: each sky is a JPEG built into the firmware (src/sky/), decoded once into PSRAM. The camera
// moves only a pixel every few frames, so most frames put the sky back only where that buffer showed
// balloons and the time two frames ago (rectangles); the whole sky is copied when the camera or the fade
// moves on. The balloons are worked out per pixel: an egg shape narrowing to the knot, shaded like a shiny
// ball (light from the upper left, a highlight, the sky's colour on the rim); far ones take some of the
// sky's colour. The time and its shadow are turned into a list of pixels once a minute.
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "app.h"
#include "audio.h"
#include "control.h"
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "fx.h"
#include "gfx.h"

#define W FW  // native: 410 x 502 px, the whole screen
#define H FH
#define MAXB 16     // balloons at once (more would slow the frame down)
#define MAXBITS 64  // pieces of popped balloons
#define MAXSTR 6    // falling strings
#define PAN_S 600   // the camera pans across the photo and back in this many seconds

enum { SK_DAWN, SK_DAY, SK_DUSK, SK_NIGHT, SK_COUNT };
extern const uint8_t sky0_start[] asm("_binary_amanecer_jpg_start"), sky0_end[] asm("_binary_amanecer_jpg_end");
extern const uint8_t sky1_start[] asm("_binary_dia_jpg_start"), sky1_end[] asm("_binary_dia_jpg_end");
extern const uint8_t sky2_start[] asm("_binary_atardecer_jpg_start"), sky2_end[] asm("_binary_atardecer_jpg_end");
extern const uint8_t sky3_start[] asm("_binary_noche_jpg_start"), sky3_end[] asm("_binary_noche_jpg_end");
static const uint8_t *const SKY_JPG[SK_COUNT][2] = {{sky0_start, sky0_end}, {sky1_start, sky1_end}, {sky2_start, sky2_end}, {sky3_start, sky3_end}};
static const int SKY_W[SK_COUNT] = {1336, 1238, 1336, 1281};  // their widths (all 502 high)
static const float SKY_LIGHT[SK_COUNT] = {0.85f, 1.0f, 0.78f, 0.5f};  // how lit the balloons are

typedef struct {
    int id;          // which sky (-1: none)
    uint16_t *px;    // PSRAM, SKY_W x H
    float avg[3];    // its average colour
} sky_t;
static sky_t s_sky[2] = {{-1, NULL}, {-1, NULL}};
static uint8_t *s_pool;  // TJpgDec's work area (PSRAM)

// What each framebuffer shows: the sky's position and fade, and the boxes drawn on it (put back next time)
#define MAXR 96
typedef struct {
    int16_t x0, y0, x1, y1;
} rect_t;
typedef struct {
    uint16_t *fb;
    int ida, idb, oa, ob, al, n;
    rect_t r[MAXR];
} shown_t;
static shown_t *s_shown;    // PSRAM: two of them
EXT_RAM_BSS_ATTR static rect_t s_rects[MAXR];
static int s_nrects;
static int64_t s_last_us;
static unsigned s_frame;
static void mark(int x0, int y0, int x1, int y1)
{
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > W - 1) x1 = W - 1;
    if (y1 > H - 1) y1 = H - 1;
    if (x0 > x1 || y0 > y1) return;
    if (s_nrects == MAXR) {  // full: grow the last box (rare)
        rect_t *r = &s_rects[MAXR - 1];
        if (x0 < r->x0) r->x0 = x0;
        if (y0 < r->y0) r->y0 = y0;
        if (x1 > r->x1) r->x1 = x1;
        if (y1 > r->y1) r->y1 = y1;
        return;
    }
    s_rects[s_nrects++] = (rect_t){x0, y0, x1, y1};
}

// The time and its shadow, once a minute: one entry per pixel touched, offset (18 bits), shadow alpha (5)
// and fill alpha (5)
static uint32_t *s_tpx;     // PSRAM
static int s_ntpx;
static char s_ttxt[8];
static rect_t s_trect;
// sqrt(1 - s^2) for s in 0..1 (the ball's depth across a row)
EXT_RAM_BSS_ATTR static float s_sq[257];

typedef struct {
    float x, y, s, vy, phase, bal, tilt, sx;
    uint8_t layer;   // 0 behind the time, 1 in front, 2 right in front of the eye
    uint8_t col[3];
} balloon_t;
typedef struct {
    float x, y, vx, vy, life;
    uint8_t col[3], layer, size;
} bit_t;
typedef struct {
    float x, y, vy, len, s, life, spin;
    uint8_t layer;
} string_t;
// In PSRAM like everything sizeable here: internal RAM is what the watch runs short of (the microSD
// driver needs some of it to read the card)
EXT_RAM_BSS_ATTR static balloon_t s_b[MAXB];
EXT_RAM_BSS_ATTR static bit_t s_bits[MAXBITS];
EXT_RAM_BSS_ATTR static string_t s_str[MAXSTR];
static int s_nb, s_nbits, s_nstr, s_hour = -1;
static float s_T, s_next, s_wind;
static bool s_touch_was, s_moved;
static uint32_t s_touch_t0;
static int s_tx, s_ty;

static const uint8_t PAL_MIX[8][3] = {{232, 34, 46}, {255, 138, 0}, {255, 210, 26}, {43, 178, 76}, {30, 123, 230}, {123, 63, 224}, {255, 79, 163}, {0, 184, 200}};
static const uint8_t PAL_PASTEL[8][3] = {{255, 179, 193}, {255, 214, 165}, {253, 255, 182}, {202, 255, 191}, {155, 246, 255}, {160, 196, 255}, {189, 178, 255}, {255, 198, 255}};
static const uint8_t PAL_RED[4][3] = {{212, 16, 30}, {232, 34, 46}, {176, 10, 24}, {255, 58, 70}};
static const uint8_t PAL_METAL[5][3] = {{216, 176, 74}, {192, 198, 208}, {217, 142, 142}, {154, 122, 216}, {74, 163, 216}};

static inline float frand(float a, float b) { return a + (fx_rand() % 10000) * (b - a) / 10000.0f; }
static inline float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }

// ---------------------------------------------------------------- skies

static bool load_sky(sky_t *k, int id)
{
    if (k->id == id) return true;
    if (!k->px) k->px = heap_caps_malloc(1336 * H * 2, MALLOC_CAP_SPIRAM);  // the widest
    if (!k->px || !s_pool) return false;
    if (!jpeg_decode(SKY_JPG[id][0], SKY_JPG[id][1] - SKY_JPG[id][0], k->px, SKY_W[id], H, s_pool)) return false;
    k->id = id;
    float r = 0, g = 0, b = 0;
    int n = 0;
    for (int y = 10; y < H; y += 40)
        for (int x = 10; x < SKY_W[id]; x += 40, n++) {
            uint16_t c = __builtin_bswap16(k->px[y * SKY_W[id] + x]);
            r += (c >> 11) << 3;
            g += ((c >> 5) & 63) << 2;
            b += (c & 31) << 3;
        }
    k->avg[0] = r / n, k->avg[1] = g / n, k->avg[2] = b / n;
    return true;
}

// Which sky now, and how far into the fade to the next: night until 5:30, dawn 6:00-7:30, day 8:00-18:00,
// sunset 18:30-20:00, night from 20:30 (the gaps are the half-hour fades)
static void sky_now(int *a, int *b, float *f)
{
    static const struct { float h; int id; } T[] = {{0, SK_NIGHT}, {5.5f, SK_NIGHT}, {6, SK_DAWN}, {7.5f, SK_DAWN}, {8, SK_DAY}, {18, SK_DAY},
                                                    {18.5f, SK_DUSK}, {20, SK_DUSK}, {20.5f, SK_NIGHT}, {24, SK_NIGHT}};
    *a = *b = SK_DAY;
    *f = 0;
    if (g_app.bal_sky == 1 || !g_now.valid) return;
    float h = g_now.h + g_now.m / 60.0f + g_now.s / 3600.0f;
    for (int i = 0; i < 9; i++)
        if (h >= T[i].h && h < T[i + 1].h) {
            *a = T[i].id;
            *b = T[i + 1].id;
            *f = *a == *b ? 0 : (h - T[i].h) / (T[i + 1].h - T[i].h);
            return;
        }
}

// ---------------------------------------------------------------- balloons

static void pick_colour(uint8_t *c)
{
    const uint8_t(*p)[3];
    int n;
    switch (g_app.bal_col) {
    case 1: p = PAL_PASTEL, n = 8; break;
    case 2: p = PAL_RED, n = 4; break;
    case 3: p = PAL_METAL, n = 5; break;
    default: p = PAL_MIX, n = 8; break;
    }
    memcpy(c, p[fx_rand() % n], 3);
}

static void add_balloon(float x, int layer, float delay)
{
    if (s_nb == MAXB) return;
    balloon_t *b = &s_b[s_nb];
    float s = layer == 2 ? frand(2.4f, 3.4f) : layer ? frand(0.8f, 1.55f) : frand(0.32f, 0.8f);
    float sv = s > 1.6f ? 1.6f : s;
    *b = (balloon_t){x, H + 70 * s + delay, s, (layer == 2 ? frand(70, 90) : layer ? frand(84, 116) : frand(36, 56)) * sv,
                     frand(0, 6.28f), frand(0.6f, 1.4f), frand(-0.12f, 0.12f), frand(0, 100), (uint8_t)layer, {0}};
    pick_colour(b->col);
    s_nb++;
    // keep them sorted far to near, so they are drawn in order
    for (int i = s_nb - 1; i > 0 && (s_b[i].layer < s_b[i - 1].layer || (s_b[i].layer == s_b[i - 1].layer && s_b[i].s < s_b[i - 1].s)); i--) {
        balloon_t t = s_b[i];
        s_b[i] = s_b[i - 1];
        s_b[i - 1] = t;
    }
}

static void release(int n)  // on the hour: most of them far away (smaller, and lighter to draw)
{
    for (int i = 0; i < n; i++) add_balloon(frand(30, W - 30), fx_rand() % 100 < 30, frand(0, 520));  // spread out: they come in one by one
}

// One balloon: per row the egg's half width; per pixel a ball's normal, lit from the upper left
static void draw_balloon(uint16_t *fb, const balloon_t *b, float light, const float *sky)
{
    const float r = 30 * b->s, cx = b->x, cy = b->y, sway = sinf(s_T * b->bal + b->phase);
    const bool metal = g_app.bal_col == 3;
    const float haze = b->layer == 0 ? 0.28f : 0;
    float c0[3];
    for (int q = 0; q < 3; q++) c0[q] = (b->col[q] + (sky[q] - b->col[q]) * haze) * light;
    // the string, first (the balloon covers its top): a wavy line below the knot
    {
        float px = cx, py = cy + r * 1.08f, L = r * 3.2f;
        uint16_t sc = rgb565(clampf((235 + (sky[0] - 235) * haze) * light, 0, 255), clampf((235 + (sky[1] - 235) * haze) * light, 0, 255),
                             clampf((240 + (sky[2] - 240) * haze) * light, 0, 255));
        int alpha = b->layer ? 11 : 7;
        for (int k = 1; k <= 12; k++) {
            float t = k / 12.0f;
            float nx = cx + sinf(s_T * 1.7f + b->sx + t * 3.5f) * r * 0.28f * t - sway * r * 0.5f * t, ny = cy + r * 1.08f + L * t;
            // a thin anti-aliased segment (one step per pixel, two pixels across)
            float dx = nx - px, dy = ny - py;
            int n = (int)(fabsf(dx) > fabsf(dy) ? fabsf(dx) : fabsf(dy)) + 1;
            for (int s = 0; s < n; s++) {
                float x = px + dx * s / n, y = py + dy * s / n;
                int ix = (int)x, iy = (int)y;
                if ((unsigned)iy >= H || ix < 0 || ix >= W - 1) continue;
                int f = (int)((x - ix) * 16);
                uint16_t *p = fb + iy * W + ix;
                p[0] = gfx_blend(p[0], sc, alpha * (16 - f) / 16);
                p[1] = gfx_blend(p[1], sc, alpha * f / 16);
            }
            px = nx, py = ny;
        }
    }
    // light (upper left, towards the viewer) and the half vector for the highlight
    const float Lx = -0.45f, Ly = -0.6f, Lz = 0.66f;
    const float Hn = 1 / sqrtf(Lx * Lx + Ly * Ly + (Lz + 1) * (Lz + 1)), Hx = Lx * Hn, Hy = Ly * Hn, Hz = (Lz + 1) * Hn;
    const float vc = -0.12f, bb = 1.05f, tilt = b->tilt + sway * 0.07f;
    int y0 = (int)(cy - 1.2f * r), y1 = (int)(cy + 1.0f * r);
    if (y0 < 0) y0 = 0;
    if (y1 > H - 1) y1 = H - 1;
    // the one right in front of the eye is huge and out of focus: worked out on 2x2 blocks, solid inside
    const int st = b->layer == 2 ? 2 : 1;
    const float hs = st * 0.5f;
    for (int y = y0; y <= y1; y += st) {
        float v = (y + hs - cy) / r, t = (v - vc) / bb;
        if (t <= -1 || t >= 1) continue;
        float ct = sqrtf(1 - t * t), taper = 1 - 0.45f * (t > 0 ? powf(t, 1.5f) : 0);
        float a = 0.88f * ct * taper;            // half width of this row, in radii
        float xc = cx + tilt * (v - vc) * r;     // a slight lean
        int x0 = (int)(xc - a * r - 1), x1 = (int)(xc + a * r + 1);
        if (x0 < 0) x0 = 0;
        if (x1 > W - 1) x1 = W - 1;
        float edge_v = (1 - fabsf(t)) * bb * r;  // px to the top or bottom edge
        const float ir = 1 / r, ia = 1 / a;
        uint16_t *row = fb + y * W;
        for (int x = x0; x <= x1; x += st) {
            float u = (x + hs - xc) * ir, s = u * ia;
            if (s <= -1 || s >= 1) continue;
            float al = clampf((a - fabsf(u)) * r, 0, 1);
            if (edge_v < 1) al *= edge_v;
            float nx = s * ct, ny = t, nz = ct * s_sq[(int)(fabsf(s) * 256)];
            float diff = nx * Lx + ny * Ly + nz * Lz;
            diff = diff < 0 ? 0 : diff;
            float sp = nx * Hx + ny * Hy + nz * Hz;
            sp = sp < 0 ? 0 : sp;
            sp *= sp, sp *= sp, sp *= sp, sp *= sp;  // ^16
            if (!metal) sp *= sp;                    // ^32: a tighter highlight on rubber
            float k = metal ? 0.25f + 0.95f * diff : 0.42f + 0.7f * diff, rim = (1 - nz) * (1 - nz) * 0.3f;
            float spec = sp * (metal ? 1.0f : 0.85f) * light * 255;
            int R = (int)(c0[0] * k + sky[0] * light * rim + spec), G = (int)(c0[1] * k + sky[1] * light * rim + spec),
                B = (int)(c0[2] * k + sky[2] * light * rim + spec);
            uint16_t c = rgb565(R > 255 ? 255 : R, G > 255 ? 255 : G, B > 255 ? 255 : B);
            if (st == 1) {
                row[x] = gfx_blend(row[x], c, (int)(al * 15.4f));  // a little see-through
            } else {
                int a16 = (int)(al * 16);
                for (int dy = 0; dy < 2 && y + dy < H; dy++)
                    for (int dx = 0; dx < 2 && x + dx < W; dx++) {
                        uint16_t *q = row + dy * W + x + dx;
                        *q = a16 >= 16 ? c : gfx_blend(*q, c, a16);
                    }
            }
        }
    }
    // knot: a small darker trapezoid under the egg, narrowing downwards
    uint16_t kc = rgb565(c0[0] * 0.55f, c0[1] * 0.55f, c0[2] * 0.55f);
    float kx = cx + tilt * (0.95f - vc) * r;
    int ky0 = (int)(cy + 0.93f * r), ky1 = (int)(cy + 1.06f * r);
    for (int y = ky0; y <= ky1; y++) {
        if ((unsigned)y >= H) continue;
        float k = ky1 > ky0 ? (float)(y - ky0) / (ky1 - ky0) : 0, hw = r * (0.09f - 0.05f * k) + 0.5f;
        for (int x = (int)(kx - hw); x <= (int)(kx + hw); x++)
            if ((unsigned)x < W) fb[y * W + x] = gfx_blend(fb[y * W + x], kc, 13);
    }
    // what to put back next time: the body, and the string below it (narrower)
    mark((int)(cx - 1.05f * r) - 2, (int)(cy - 1.25f * r) - 2, (int)(cx + 1.05f * r) + 2, (int)(cy + 1.14f * r) + 2);
    mark((int)(cx - 0.85f * r) - 2, (int)(cy + 1.0f * r), (int)(cx + 0.85f * r) + 2, (int)(cy + 4.35f * r) + 2);
}

static bool hit(const balloon_t *b, int x, int y)
{
    float r = 30 * b->s, dx = (x - b->x) / (r * 1.1f), dy = (y - b->y) / (r * 1.2f);
    return dx * dx + dy * dy < 1;
}

static void pop(int i)
{
    balloon_t *b = &s_b[i];
    float r = 30 * b->s;
    for (int k = 0; k < 14 && s_nbits < MAXBITS; k++) {
        float a = frand(0, 6.28f), v = frand(80, 220) * (b->s > 1.6f ? 1.6f : b->s);
        s_bits[s_nbits++] = (bit_t){b->x + cosf(a) * r * 0.6f, b->y + sinf(a) * r * 0.6f, cosf(a) * v, sinf(a) * v - 60, 1.4f,
                                    {b->col[0], b->col[1], b->col[2]}, b->layer, (uint8_t)(2 + b->s * 3)};
    }
    if (s_nstr < MAXSTR) s_str[s_nstr++] = (string_t){b->x, b->y + r * 1.08f, 0, r * 3.2f, b->s, 2.5f, frand(-2, 2), b->layer};
    if (g_app.bal_sound) audio_pop();
    memmove(&s_b[i], &s_b[i + 1], sizeof(balloon_t) * (s_nb - i - 1));
    s_nb--;
}

static void draw_layer(uint16_t *fb, int layer, float light, const float *sky)
{
    for (int i = 0; i < s_nb; i++)
        if (s_b[i].layer == layer) draw_balloon(fb, &s_b[i], light, sky);
    for (int i = 0; i < s_nbits; i++) {
        const bit_t *p = &s_bits[i];
        if (p->layer != layer) continue;
        uint16_t c = rgb565(p->col[0] * light, p->col[1] * light, p->col[2] * light);
        int a = (int)(clampf(p->life, 0, 1) * 16);
        for (int y = (int)p->y; y < (int)p->y + p->size; y++)
            for (int x = (int)p->x; x < (int)p->x + p->size + 1; x++)
                if ((unsigned)x < W && (unsigned)y < H) fb[y * W + x] = gfx_blend(fb[y * W + x], c, a);
        mark((int)p->x - 1, (int)p->y - 1, (int)p->x + p->size + 2, (int)p->y + p->size + 1);
    }
    for (int i = 0; i < s_nstr; i++) {
        const string_t *s = &s_str[i];
        if (s->layer != layer) continue;
        uint16_t c = rgb565(235 * light, 235 * light, 240 * light);
        int a = (int)(clampf(s->life, 0, 1) * 11);
        for (int k = 0; k < (int)s->len; k++) {
            float t = k / s->len, x = s->x + sinf(s_T * 3 + t * 5) * 6 * t + sinf(s->spin) * k * 0.3f, y = s->y + k * cosf(s->spin * 0.3f);
            if ((unsigned)(int)x < W && (unsigned)(int)y < H) fb[(int)y * W + (int)x] = gfx_blend(fb[(int)y * W + (int)x], c, a);
        }
        mark((int)(s->x - s->len * 0.4f) - 8, (int)(s->y - s->len) - 2, (int)(s->x + s->len * 0.4f) + 8, (int)(s->y + s->len) + 2);
    }
}

// The time: big and white, with a soft shadow below so it reads on bright clouds. Built once a minute:
// the fill and the shadow (the glyphs' coverage spread over a few offsets) into a list of pixels
static void build_time(void)
{
    char buf[8];
    clock_hhmm(buf, true);
    if (strcmp(buf, s_ttxt) == 0 && s_tpx) return;
    strcpy(s_ttxt, buf);
    const font_t *f = &font_round_xl;
    const int sp = 6, x = (W - font_width(f, "00:00", sp)) / 2, y = H / 2 - f->h / 2;
    const int bx0 = x - f->pad - 3, by0 = y - f->pad, bw = font_width(f, "00:00", sp) + 2 * f->pad + 6, bh = f->h + 2 * f->pad + 8;
    static const int8_t SH[4][2] = {{0, 4}, {2, 5}, {-2, 5}, {0, 7}};
    uint8_t *fill = heap_caps_calloc(1, bw * bh * 2, MALLOC_CAP_SPIRAM), *shad = fill ? fill + bw * bh : NULL;
    if (!s_tpx) s_tpx = heap_caps_malloc(bw * bh * 4, MALLOC_CAP_SPIRAM);
    if (!fill || !s_tpx) {
        heap_caps_free(fill);
        return;
    }
    int gx0 = x;
    for (const char *c = buf; *c; c++) {
        int gi = *c >= '0' && *c <= '9' ? *c - '0' : *c == ':' ? 10 : *c == '-' ? 11 : -1;
        if (gi < 0) {
            gx0 += f->glyphs[0].adv / 2 + sp;
            continue;
        }
        const font_glyph_t *g = &f->glyphs[gi];
        for (int row = 0; row < g->h; row++)
            for (int col = 0; col < g->w; col++) {
                int fa = g->px[row * g->w + col] >> 4;
                if (!fa) continue;
                int px = gx0 - f->pad + col - bx0, py = y - f->pad + row - by0;
                if ((unsigned)px < (unsigned)bw && (unsigned)py < (unsigned)bh && fa > fill[py * bw + px]) fill[py * bw + px] = fa;
                for (int k = 0; k < 4; k++) {
                    int qx = px + SH[k][0], qy = py + SH[k][1];
                    if ((unsigned)qx < (unsigned)bw && (unsigned)qy < (unsigned)bh) {
                        int v = shad[qy * bw + qx] + fa / 5;
                        shad[qy * bw + qx] = v > 12 ? 12 : v;
                    }
                }
            }
        gx0 += g->adv + sp;
    }
    s_ntpx = 0;
    for (int py = 0; py < bh; py++)
        for (int px = 0; px < bw; px++) {
            int fa = fill[py * bw + px], sa = shad[py * bw + px], X = bx0 + px, Y = by0 + py;
            if ((fa || sa) && (unsigned)X < W && (unsigned)Y < H)
                s_tpx[s_ntpx++] = (uint32_t)(Y * W + X) | (uint32_t)sa << 18 | (uint32_t)(fa == 15 ? 16 : fa ? fa + 1 : 0) << 23;
        }
    s_trect = (rect_t){bx0, by0, bx0 + bw - 1, by0 + bh - 1};
    heap_caps_free(fill);
}

static void draw_time(uint16_t *fb)
{
    build_time();
    const uint16_t shade = rgb565(10, 22, 45), white = rgb565(255, 255, 255);
    for (int i = 0; i < s_ntpx; i++) {
        uint32_t e = s_tpx[i];
        uint16_t *p = fb + (e & 0x3FFFF);
        int sa = (e >> 18) & 31, fa = e >> 23;
        if (sa) *p = gfx_blend(*p, shade, sa);
        if (fa) *p = fa == 16 ? white : gfx_blend(*p, white, fa);
    }
    mark(s_trect.x0, s_trect.y0, s_trect.x1, s_trect.y1);
}

// ---------------------------------------------------------------- the face

void balloons_enter(void)
{
    if (!s_pool) s_pool = heap_caps_malloc(4096, MALLOC_CAP_SPIRAM);
    if (!s_shown) s_shown = heap_caps_calloc(2, sizeof(shown_t), MALLOC_CAP_SPIRAM);
    for (int i = 0; i <= 256; i++) s_sq[i] = sqrtf(1 - (i / 256.0f) * (i / 256.0f));
    if (s_shown) memset(s_shown, 0, 2 * sizeof(shown_t));
    s_ttxt[0] = 0;
    if (s_nb == 0)  // a few already up, so the face does not open empty
        for (int i = 0; i < 5; i++) {
            add_balloon(frand(40, W - 40), i % 2 == 0, 0);
            s_b[s_nb - 1].y = frand(80, H);
        }
    s_touch_was = false;
    s_next = 1.5f;
}

static void handle_touch(uint32_t now)
{
    int dx, dy;
    app_take_drag(&dx, &dy);
    bool touch = g_app.touch;
    s_tx = g_app.tx * W / 1000;
    s_ty = g_app.ty * H / 1000;
    if (touch) {
        if (!s_touch_was) {
            s_touch_t0 = now;
            s_moved = false;
        }
        if (dx || dy) s_moved = true;
        s_wind = clampf(s_wind + dx * 4, -260, 260);
    } else if (s_touch_was && !s_moved && now - s_touch_t0 < 400) {
        for (int i = s_nb - 1; i >= 0; i--)  // the nearest one under the finger
            if (hit(&s_b[i], s_tx, s_ty)) {
                pop(i);
                break;
            }
    }
    s_touch_was = touch;
}

void balloons_render(uint16_t *fb, uint32_t t, uint32_t dt)
{
    (void)t;
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    // dt follows the per-face speed: its default, 75 %, is the prototype's pace
    float fdt = (dt > 100 ? 100 : dt) / 1000.0f * (1 / 0.75f);
    s_T += fdt;
    handle_touch(now);

    // The sky: the one for now, and the next one while they cross-fade (its buffer only then)
    int a, b;
    float f;
    sky_now(&a, &b, &f);
    if (!load_sky(&s_sky[0], a)) {
        gfx_fill(fb, rgb565(70, 120, 200));
        return;
    }
    bool fade = f > 0.004f && load_sky(&s_sky[1], b);
    if (!fade && s_sky[1].px && f <= 0.004f) {  // the fade is over: give its memory back
        heap_caps_free(s_sky[1].px);
        s_sky[1].px = NULL;
        s_sky[1].id = -1;
    }
    const float pan = 0.5f - 0.5f * cosf(s_T * 6.2832f / PAN_S);
    {
        const uint16_t *A = s_sky[0].px, *B = fade ? s_sky[1].px : NULL;
        int wa = SKY_W[s_sky[0].id], oa = (int)((wa - W) * pan);
        int wb = fade ? SKY_W[s_sky[1].id] : 0, ob = fade ? (int)((wb - W) * pan) : 0, al = fade ? (int)(f * 16 + 0.5f) : 0;
        int64_t nowus = esp_timer_get_time();
        if (nowus - s_last_us > 150000 && s_shown) memset(s_shown, 0, 2 * sizeof(shown_t));  // after a pause: all
        s_last_us = nowus;
        shown_t *sh = !s_shown ? NULL : s_shown[0].fb == fb ? &s_shown[0] : s_shown[1].fb == fb ? &s_shown[1] : s_shown[0].fb ? &s_shown[1] : &s_shown[0];
        bool full = !sh || sh->fb != fb || (++s_frame & 63) == 0 || sh->ida != s_sky[0].id || sh->oa != oa || sh->al != al ||
                    (fade && (sh->idb != s_sky[1].id || sh->ob != ob));
        int nr = full ? 1 : sh->n;
        for (int k = 0; k < nr; k++) {
            rect_t r = full ? (rect_t){0, 0, W - 1, H - 1} : sh->r[k];
            int w = r.x1 - r.x0 + 1;
            for (int y = r.y0; y <= r.y1; y++) {
                const uint16_t *ra = A + y * wa + oa + r.x0;
                uint16_t *d = fb + y * W + r.x0;
                if (!fade) {
                    memcpy(d, ra, w * 2);
                } else {
                    const uint16_t *rb = B + y * wb + ob + r.x0;
                    for (int x = 0; x < w; x++) d[x] = gfx_blend(ra[x], rb[x], al);
                }
            }
        }
        if (sh) *sh = (shown_t){.fb = fb, .ida = s_sky[0].id, .idb = fade ? s_sky[1].id : -1, .oa = oa, .ob = ob, .al = al, .n = 0};
        s_nrects = 0;
    }
    float sky[3], light;
    for (int q = 0; q < 3; q++) sky[q] = fade ? s_sky[0].avg[q] + (s_sky[1].avg[q] - s_sky[0].avg[q]) * f : s_sky[0].avg[q];
    light = fade ? SKY_LIGHT[a] + (SKY_LIGHT[b] - SKY_LIGHT[a]) * f : SKY_LIGHT[a];
    // at night, a few stars twinkle
    float night = a == SK_NIGHT ? (b == SK_NIGHT ? 1 : 1 - f) : b == SK_NIGHT ? f : 0;
    if (night > 0)
        for (int i = 0; i < 40; i++) {
            int x = (int)((i * 97.3f)) % W, y = (int)(i * 53.7f) % (H * 3 / 4);
            float tw = 0.5f + 0.5f * sinf(s_T * (1 + i % 5) + i);
            fb[y * W + x] = gfx_blend(fb[y * W + x], 0xFFFF, (int)(9 * night * tw));
            mark(x, y, x, y);
        }

    // Balloons: irregular gaps, sometimes a little group, now and then one right in front; on the hour, many
    if (g_now.valid && g_now.h != s_hour) {
        if (s_hour >= 0) release(8);
        s_hour = g_now.h;
    }
    s_next -= fdt * g_app.bal_num / 10.0f;
    if (s_next <= 0) {
        int n = fx_rand() % 100 < 25 ? (fx_rand() % 100 < 40 ? 3 : 2) : 1;
        float x = frand(50, W - 50);
        for (int i = 0; i < n; i++) add_balloon(clampf(x + frand(-40, 40), 30, W - 30), fx_rand() % 100 < 45, i * frand(20, 60));
        if (fx_rand() % 100 < 12) add_balloon(frand(60, W - 60), 2, frand(0, 120));
        s_next = frand(1.2f, 5);
    }
    s_wind *= expf(-fdt * 0.8f);
    static const float WIND[3] = {0.45f, 1, 1.4f};
    for (int i = 0; i < s_nb;) {
        balloon_t *p = &s_b[i];
        float sv = p->s > 1.6f ? 1.6f : p->s;
        p->y -= p->vy * fdt;
        p->x += (sinf(s_T * 0.5f * p->bal + p->phase) * 6 + s_wind * WIND[p->layer]) * fdt * sv;
        if (p->y < -140 * p->s || p->x < -60 - 60 * p->s || p->x > W + 60 + 60 * p->s) {
            memmove(p, p + 1, sizeof(balloon_t) * (s_nb - i - 1));
            s_nb--;
        } else {
            i++;
        }
    }
    for (int i = 0; i < s_nbits;) {
        bit_t *p = &s_bits[i];
        p->vy += 420 * fdt;
        p->vx *= expf(-fdt * 1.5f);
        p->x += p->vx * fdt;
        p->y += p->vy * fdt;
        p->life -= fdt;
        if (p->life <= 0 || p->y > H + 20) *p = s_bits[--s_nbits];
        else i++;
    }
    for (int i = 0; i < s_nstr;) {
        string_t *s = &s_str[i];
        s->vy += 300 * fdt;
        s->y += s->vy * fdt;
        s->spin += fdt * 2;
        s->life -= fdt;
        if (s->life <= 0 || s->y > H + 80) *s = s_str[--s_nstr];
        else i++;
    }

    draw_layer(fb, 0, light, sky);
    draw_time(fb);
    draw_layer(fb, 1, light, sky);
    draw_layer(fb, 2, light, sky);
    if (s_shown) {
        shown_t *sh = s_shown[0].fb == fb ? &s_shown[0] : &s_shown[1];
        memcpy(sh->r, s_rects, s_nrects * sizeof(rect_t));
        sh->n = s_nrects;
    }
}
