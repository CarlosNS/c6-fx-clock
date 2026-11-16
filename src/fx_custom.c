// "Mis esferas" (full screen, native 410x502): the custom face chosen in the phone app. Each one lives
// on the microSD in /esferas/<id>/:
//   face.json  {"name":..,"clock":"digital|analog|edge","font":"round|seg|nib|pix|jersey","size":n,
//               "x":cx,"y":top (digital),"ax":cx,"ay":cy (analog centre),"color":"#rrggbb",
//               "dx":cx,"dy":top (the date where it was moved; -1 or absent: in its usual place),
//               "fx":0 none|1 outline|2 shadow,"secs":0|1,"date":0|1,
//               "slots":[{"from":minutes,"pic":"bg_0.jpg"|"anim_1.bin"},...]}
//              The slots are the stretches of the day, sorted, the first from 0: each one shows its
//              picture until the next one starts, so the whole day is covered. Faces from before the
//              slots have none, and one picture: bg.jpg, or anim.bin with "anim":1.
//   *.jpg      a still background, 410 x 502, or
//   *.bin      an animation: "MAN1", u16 frames, u16 0, then per frame u16 delay ms, u16 0, u32 offset,
//              u32 size, then the JPEG frames, 274 x 336 each (enlarged x1.5 on screen)
// The clock: digital (the fonts below), "analog" (classic hands from a centre that can be moved, the
// size sets the radius) or "edge" (hands that come in from the rounded edge of the screen towards the
// middle without reaching it, so the picture shows; the size sets their thickness).
// The app crops and encodes everything; the watch only decodes (TJpgDec in ROM) and draws the time. An
// animation loops while the screen is on (with it off nothing is drawn, so it costs nothing then), and
// starts from its first frame each time the screen comes on.
//
// The time is drawn by the same rules as the app's preview (src/index.html, drawTime) from the same font
// data, so what the editor shows is what the watch shows: glyph alpha into a mask, then an outline or a
// shadow, then the fill colour.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "app.h"
#include "apps.h"
#include "cJSON.h"
#include "control.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "fonts.h"
#include "fx.h"
#include "gfx.h"
#include "rom/tjpgd.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "ui.h"
#include "ui_fonts.h"
#include "ble.h"
#include "settings.h"
#include "sys.h"

#define W FW
#define H FH
#define AW 274   // animation frames
#define AH 336

static const char *TAG = "custom";

#define SLOTS_MAX 8
#define CUSTOM_TRY_ID 999  // /esferas/999: what the editor sends to try before saving (never listed)
typedef struct {
    int font, size, x, y, fx, secs, date, anim;
    int clock, ax, ay;                   // 0 digital, 1 classic hands, 2 hands from the edge; analog centre
    int dx, dy;                          // the date moved there (centre, top); dx < 0: in its usual place
    int nslots, from[SLOTS_MAX];         // the stretches of the day (minutes), each until the next
    char pic[SLOTS_MAX][32];
    uint8_t r, g, b;
    char name[40];
} face_t;
static int s_slot = -1;                  // the stretch whose picture is loaded

static face_t s_face;
static int s_loaded = -1;          // id loaded (-1 none)
static char s_err[48];             // why it could not be shown
static uint16_t *s_bg;             // still image or the current frame enlarged, W x H
static uint8_t *s_mask;            // text alpha 0..15, W x H
static uint8_t *s_anim;            // anim.bin in PSRAM
static size_t s_anim_len;
static uint16_t *s_frame;          // one decoded frame, AW x AH
static int s_nframes, s_cur_frame = -1;
static uint32_t s_anim_t0, s_last_render;
static char s_text_key[48];        // what the mask holds
static int s_bx0, s_by0, s_bx1, s_by1;  // its bounding box
static int s_try_id, s_try_prev = -1;
static void again(void);
static uint32_t s_try_until;

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

// ---------------------------------------------------------------- JPEG (TJpgDec in ROM)

typedef struct {
    const uint8_t *src;
    size_t len, pos;
    uint16_t *dst;
    int dw, dh;
} jctx_t;

static UINT jin(JDEC *jd, BYTE *buf, UINT n)
{
    jctx_t *c = jd->device;
    if (c->pos + n > c->len) n = c->len - c->pos;
    if (buf) memcpy(buf, c->src + c->pos, n);
    c->pos += n;
    return n;
}

static UINT jout(JDEC *jd, void *bitmap, JRECT *r)
{
    jctx_t *c = jd->device;
    const uint8_t *p = bitmap;  // RGB888
    for (int y = r->top; y <= r->bottom; y++)
        for (int x = r->left; x <= r->right; x++, p += 3)
            if (x < c->dw && y < c->dh) c->dst[y * c->dw + x] = rgb565(p[0], p[1], p[2]);
    return 1;
}

static bool jpeg_decode(const uint8_t *src, size_t len, uint16_t *dst, int dw, int dh, uint8_t pool[4096])
{
    JDEC jd;
    jctx_t c = {src, len, 0, dst, dw, dh};
    if (jd_prepare(&jd, jin, pool, 4096, &c) != JDR_OK) return false;
    return jd_decomp(&jd, jout, 0) == JDR_OK;
}

// ---------------------------------------------------------------- fonts (the app's fuentes.json mirrors these)

enum { F_ROUND, F_SEG, F_NIB, F_PIX, F_JERSEY };
static const font_t *const AA[3][4] = {{&font_round_s, &font_round_m, &font_round_xl, &font_round_xxl},
                                       {&font_seg_s, &font_seg_m, &font_seg_xl, &font_seg_xxl},
                                       {&font_nib_s, &font_nib_m, NULL, NULL}};
static int aa_sizes(int f) { return f == F_NIB ? 2 : 4; }

static int font_from(const char *s)
{
    static const char *const N[] = {"round", "seg", "nib", "pix", "jersey"};
    for (int i = 0; i < 5; i++)
        if (s && !strcmp(s, N[i])) return i;
    return F_ROUND;
}

static const ui_glyph_t *ui_glyph(const ui_font_t *f, unsigned cp)
{
    int lo = 0, hi = f->count - 1;
    while (lo <= hi) {
        int m = (lo + hi) / 2;
        if (f->glyphs[m].cp == cp) return &f->glyphs[m];
        if (f->glyphs[m].cp < cp) lo = m + 1;
        else hi = m - 1;
    }
    return NULL;
}

static unsigned next_cp(const char **s)
{
    const uint8_t *p = (const uint8_t *)*s;
    unsigned c = *p++;
    if (c >= 0xC0 && c < 0xE0 && *p) c = (c & 0x1F) << 6 | (*p++ & 0x3F);
    *s = (const char *)p;
    return c;
}

static void mput(int x, int y, int a)
{
    if ((unsigned)x < W && (unsigned)y < H && a > s_mask[y * W + x]) s_mask[y * W + x] = a;
}

// Draws (mask != 0) or measures a run of text; returns its width, *h its height
static int text_run(int font, int size, int x, int y, const char *s, bool draw, int *h)
{
    int pen = x;
    if (font >= 0 && font <= F_NIB) {
        const font_t *f = AA[font][size < aa_sizes(font) ? size : aa_sizes(font) - 1];
        *h = f->h;
        for (; *s; s++) {
            int gi = *s >= '0' && *s <= '9' ? *s - '0' : *s == ':' ? 10 : -1;
            if (gi < 0) {
                pen += f->glyphs[0].adv / 2;
                continue;
            }
            const font_glyph_t *g = &f->glyphs[gi];
            if (draw)
                for (int j = 0; j < g->h; j++)
                    for (int i = 0; i < g->w; i++) {
                        int a = g->px[j * g->w + i] >> 4;
                        if (a) mput(pen - f->pad + i, y - f->pad + j, a);
                    }
            pen += g->adv;
        }
        return pen - x;
    }
    if (font == F_PIX) {
        int sc = size + 2;
        *h = 10 * sc;
        for (; *s; s++) {
            int cols = pix_glyph_cols(*s);
            if (draw)
                for (int row = 0; row < 10; row++)
                    for (int col = 0; col < cols; col++)
                        if (pix_glyph_on(*s, col, row))
                            for (int dy = 0; dy < sc; dy++)
                                for (int dx = 0; dx < sc; dx++) mput(pen + col * sc + dx, y + row * sc + dy, 15);
            pen += (cols + 1) * sc;
        }
        return pen - x - sc;
    }
    // bitmap fonts: Jersey for the time (size 0 = scale 2), VT323 for the date (font = -1, size = scale)
    const ui_font_t *f = font == F_JERSEY ? &ui_jersey : &ui_vt;
    int sc = font == F_JERSEY ? size + 2 : size;
    *h = (f->ascent + f->descent) * sc;
    while (*s) {
        const ui_glyph_t *g = ui_glyph(f, next_cp(&s));
        if (!g) continue;
        if (draw) {
            int bpr = (g->w + 7) / 8;
            const uint8_t *row = f->bits + g->off;
            for (int j = 0; j < g->h; j++, row += bpr)
                for (int i = 0; i < g->w; i++)
                    if (row[i >> 3] >> (i & 7) & 1)
                        for (int dy = 0; dy < sc; dy++)
                            for (int dx = 0; dx < sc; dx++) mput(pen + (g->xo + i) * sc + dx, y + (g->yo + j) * sc + dy, 15);
        }
        pen += g->adv * sc;
    }
    return pen - x;
}

// The time (and seconds and date) into the mask, laid out like the app's preview
static void build_mask(const char *hhmm, const char *ss, const char *date)
{
    if (s_bx1 > s_bx0)
        for (int y = s_by0; y < s_by1; y++) memset(s_mask + y * W + s_bx0, 0, s_bx1 - s_bx0);
    const face_t *f = &s_face;
    int h1, h2 = 0, hd = 0;
    int w1 = text_run(f->font, f->size, 0, 0, hhmm, false, &h1);
    // seconds: the next size down; for the scaled fonts half the scale (size n is scale n + 2)
    int ssize = f->font <= F_NIB ? (f->size > 0 ? f->size - 1 : 0) : ((f->size + 2) / 2 > 1 ? (f->size + 2) / 2 : 1) - 2;
    int w2 = f->secs ? text_run(f->font, ssize, 0, 0, ss, false, &h2) : 0;
    int gap = h1 / 16 > 2 ? h1 / 16 : 2, total = w1 + (f->secs ? gap + w2 : 0);
    int left = f->x - total / 2;
    text_run(f->font, f->size, left, f->y, hhmm, true, &h1);
    if (f->secs) text_run(f->font, ssize, left + w1 + gap, f->y + h1 - h2, ss, true, &h2);
    int bottom = f->y + h1, top = f->y, dcx = f->x, wd = 0;
    if (f->date) {  // under the time, or where it was moved
        int ds = h1 >= 100 ? 2 : 1;
        wd = text_run(-1, ds, 0, 0, date, false, &hd);
        int dy = bottom + (h1 / 12 > 4 ? h1 / 12 : 4);
        if (f->dx >= 0) dcx = f->dx, dy = f->dy;
        text_run(-1, ds, dcx - wd / 2, dy, date, true, &hd);
        if (dy + hd > bottom) bottom = dy + hd;
        if (dy < top) top = dy;
    }
    int m = 8;  // room for the pad of the glyphs, the outline and the shadow
    s_bx0 = left - m - 6 < 0 ? 0 : left - m - 6;
    s_bx1 = left + total + m + 6 > W ? W : left + total + m + 6;
    if (f->date) {
        if (dcx - wd / 2 - m < s_bx0) s_bx0 = dcx - wd / 2 - m < 0 ? 0 : dcx - wd / 2 - m;
        if (dcx + wd / 2 + m > s_bx1) s_bx1 = dcx + wd / 2 + m > W ? W : dcx + wd / 2 + m;
    }
    s_by0 = top - m < 0 ? 0 : top - m;
    s_by1 = bottom + m + 6 > H ? H : bottom + m + 6;
}

static inline uint16_t mix(uint16_t dst, int r, int g, int b, int a)
{
    uint16_t c = (uint16_t)((dst >> 8) | (dst << 8));  // panel order -> RGB565
    int dr = (c >> 11) << 3, dg = ((c >> 5) & 63) << 2, db = (c & 31) << 3;
    dr += (r - dr) * a / 15, dg += (g - dg) * a / 15, db += (b - db) * a / 15;
    return rgb565(dr, dg, db);
}

static void draw_text(uint16_t *fb)
{
    const face_t *f = &s_face;
    int hgt = s_by1 - s_by0, r = hgt / 45 < 1 ? 1 : hgt / 45 > 4 ? 4 : hgt / 45, o = r + 1;
    bool dark = f->r * 3 + f->g * 6 + f->b < 800;  // a dark text gets a light outline
    int orr = dark ? 255 : 0;
    for (int y = s_by0; y < s_by1; y++)
        for (int x = s_bx0; x < s_bx1; x++) {
            uint16_t *p = fb + y * W + x;
            if (f->fx) {
                // outline: the strongest alpha within r; shadow: the same, moved by o
                int sx = f->fx == 2 ? x - o : x, sy = f->fx == 2 ? y - o : y, best = 0;
                for (int dy = -r; dy <= r && best < 15; dy++)
                    for (int dx = -r; dx <= r; dx++) {
                        if (dx * dx + dy * dy > r * r + r) continue;
                        int xx = sx + dx, yy = sy + dy;
                        if ((unsigned)xx < W && (unsigned)yy < H && s_mask[yy * W + xx] > best) best = s_mask[yy * W + xx];
                    }
                if (best) *p = f->fx == 2 ? mix(*p, 0, 0, 0, best * 9 / 15) : mix(*p, orr, orr, orr, best);
            }
            int a = s_mask[y * W + x];
            if (a) *p = mix(*p, f->r, f->g, f->b, a);
        }
}

// ---------------------------------------------------------------- the analog clocks (as the app's preview)

// Where a ray from the middle of the screen (unit direction dx, dy) meets the rounded screen, m px in
static void edge_point(float dx, float dy, float m, float *ex, float *ey)
{
    float a = W / 2.0f - m, b = H / 2.0f - m, r = BOARD_LCD_CORNER_R - m;
    float tx = fabsf(dx) > 1e-6f ? a / fabsf(dx) : 1e9f, ty = fabsf(dy) > 1e-6f ? b / fabsf(dy) : 1e9f;
    float t = tx < ty ? tx : ty, px = dx * t, py = dy * t;
    if (fabsf(px) > a - r && fabsf(py) > b - r) {  // in a corner: on its circle instead
        float cx = (px > 0 ? 1 : -1) * (a - r), cy = (py > 0 ? 1 : -1) * (b - r);
        float bb = dx * cx + dy * cy, disc = bb * bb - (cx * cx + cy * cy - r * r);
        t = bb + sqrtf(disc > 0 ? disc : 0);
        px = dx * t, py = dy * t;
    }
    *ex = W / 2.0f + px, *ey = H / 2.0f + py;
}

typedef struct { float x0, y0, x1, y1, r; } seg_t;

// The hands (and the marks) as round-ended segments: outlines or shadows first, then the colour
static void analog_draw(uint16_t *fb)
{
    const face_t *f = &s_face;
    seg_t s[24];
    int n = 0;
    const float PI_ = 3.14159265f;
    float ah = ((g_now.h % 12) + g_now.m / 60.0f) * PI_ / 6, am = (g_now.m + g_now.s / 60.0f) * PI_ / 30, as = g_now.s * PI_ / 30;
    if (f->clock == 1) {  // classic: from the centre
        float cx = f->ax, cy = f->ay, R = 100 + 30 * f->size;
        for (int i = 0; i < 12; i++) {
            float a = i * PI_ / 6, dx = sinf(a), dy = -cosf(a), in = i % 3 ? 0.9f : 0.82f;
            s[n++] = (seg_t){cx + dx * R * in, cy + dy * R * in, cx + dx * R, cy + dy * R, i % 3 ? 1.5f : 2.5f};
        }
        s[n++] = (seg_t){cx - sinf(ah) * R * 0.12f, cy + cosf(ah) * R * 0.12f, cx + sinf(ah) * R * 0.52f, cy - cosf(ah) * R * 0.52f, 4.0f + f->size};
        s[n++] = (seg_t){cx - sinf(am) * R * 0.12f, cy + cosf(am) * R * 0.12f, cx + sinf(am) * R * 0.8f, cy - cosf(am) * R * 0.8f, 2.8f + 0.7f * f->size};
        if (f->secs) s[n++] = (seg_t){cx - sinf(as) * R * 0.22f, cy + cosf(as) * R * 0.22f, cx + sinf(as) * R * 0.9f, cy - cosf(as) * R * 0.9f, 1.0f};
        s[n++] = (seg_t){cx, cy, cx, cy, 6.0f + f->size};
    } else {  // from the edge towards the middle, never reaching it
        float ex, ey;
        for (int i = 0; i < 12; i++) {
            edge_point(sinf(i * PI_ / 6), -cosf(i * PI_ / 6), 14, &ex, &ey);
            s[n++] = (seg_t){ex, ey, ex, ey, i % 3 ? 2.5f : 4.0f};
        }
        edge_point(sinf(ah), -cosf(ah), 24, &ex, &ey);
        s[n++] = (seg_t){ex, ey, ex - sinf(ah) * 82, ey + cosf(ah) * 82, 5.0f + 1.5f * f->size};
        edge_point(sinf(am), -cosf(am), 24, &ex, &ey);
        s[n++] = (seg_t){ex, ey, ex - sinf(am) * 125, ey + cosf(am) * 125, 3.5f + f->size};
        if (f->secs) {
            edge_point(sinf(as), -cosf(as), 14, &ex, &ey);
            s[n++] = (seg_t){ex, ey, ex, ey, 5.0f};
        }
    }
    bool dark = f->r * 3 + f->g * 6 + f->b < 800;  // a dark colour gets a light outline, as the text
    uint16_t oc = dark ? rgb565(255, 255, 255) : rgb565(0, 0, 0), c = rgb565(f->r, f->g, f->b);
    for (int pass = f->fx ? 0 : 1; pass < 2; pass++)
        for (int i = 0; i < n; i++) {
            int x0 = (int)(s[i].x0 * 16), y0 = (int)(s[i].y0 * 16), x1 = (int)(s[i].x1 * 16), y1 = (int)(s[i].y1 * 16), r = (int)(s[i].r * 16);
            if (pass == 1) gfx_aa_capsule(fb, x0, y0, x1, y1, r, c, 16);
            else if (f->fx == 1) gfx_aa_capsule(fb, x0, y0, x1, y1, r + 32, oc, 16);
            else gfx_aa_capsule(fb, x0 + 48, y0 + 48, x1 + 48, y1 + 48, r, rgb565(0, 0, 0), 9);
        }
}

// The date alone, for the analog clocks: under the centre of the classic one, in the middle for the edge one
static void build_date_mask(const char *date)
{
    if (s_bx1 > s_bx0)
        for (int y = s_by0; y < s_by1; y++) memset(s_mask + y * W + s_bx0, 0, s_bx1 - s_bx0);
    const face_t *f = &s_face;
    int hd, wd = text_run(-1, 2, 0, 0, date, false, &hd);
    int cx = f->clock == 1 ? f->ax : W / 2, cy = f->clock == 1 ? f->ay + (100 + 30 * f->size) * 42 / 100 : H / 2 - hd / 2;
    if (f->dx >= 0) cx = f->dx, cy = f->dy;  // moved
    text_run(-1, 2, cx - wd / 2, cy, date, true, &hd);
    int m = 8;
    s_bx0 = cx - wd / 2 - m < 0 ? 0 : cx - wd / 2 - m;
    s_bx1 = cx + wd / 2 + m > W ? W : cx + wd / 2 + m;
    s_by0 = cy - m < 0 ? 0 : cy - m;
    s_by1 = cy + hd + m > H ? H : cy + hd + m;
}

// ---------------------------------------------------------------- loading

// The stretch of the day now: the last one that has started (they are sorted, the first from 0)
static int cur_slot(const face_t *f)
{
    int m = g_now.valid ? g_now.h * 60 + g_now.m : 0, k = 0;
    for (int i = 0; i < f->nslots; i++)
        if (f->from[i] <= m) k = i;
    return k;
}

// ---------------------------------------------------------------- loading, in the background
// A swipe between custom faces must not wait for the card (about half a second each: mounting it,
// reading, decoding). A low-priority task loads them into a small cache in PSRAM: first the face asked
// for, then its neighbours in the swipe order, so the next swipe finds its face ready. Meanwhile the
// main loop keeps drawing what it has (and the face's name shows at once). Only the main loop draws;
// the task never frees the entry being drawn.

typedef struct {
    int id;           // 0: a free place
    int slot;         // the stretch of the day its picture is for
    face_t face;
    uint16_t *still;  // a still picture, decoded (W x H), or
    uint8_t *anim;    // a whole animation file
    size_t anim_len, bytes;
    uint32_t used;
} entry_t;

#define CACHE_N 4
#define CACHE_BYTES (3 * 1024 * 1024)
#define PREFETCH_MAX (1536 * 1024)  // a bigger animation is only read when it is shown
#define ANIM_MAX (3 * 1024 * 1024)

static entry_t s_cache[CACHE_N];
static entry_t *s_cur;               // the one being drawn
static SemaphoreHandle_t s_lock;
static TaskHandle_t s_task;
static int s_want, s_near[2];        // what to load: the face to show, then its neighbours (under s_lock)
static uint32_t s_gen;               // bumped when files change: loads begun before are dropped
static int s_fail_id;                // the last face asked for that could not be loaded, and why
static char s_fail_err[48];
static volatile bool s_flush;

static void again(void)
{
    s_flush = true;
}

static void entry_free(entry_t *e)
{
    free(e->still);
    free(e->anim);
    memset(e, 0, sizeof(*e));
}

// Reads face.json and the picture for the stretch of the day it is in (the loader task)
static bool load_entry(int id, entry_t *e, char *err, size_t errn, size_t max_anim, uint8_t *pool)
{
    if (id <= 0) return strlcpy(err, TR("elige una en la app", "pick one in the app"), errn), false;
    if (g_app.sd_state != SD_OK) sd_check();
    if (!sd_hold(true)) return strlcpy(err, TR("falta la microSD", "no microSD"), errn), false;  // mounted once for both files
    bool ok = false;
    char path[64];
    size_t len;
    snprintf(path, sizeof(path), "/esferas/%d/face.json", id);
    char *js = (char *)sd_read_file(path, 8192, &len);
    cJSON *j = NULL;
    if (!js) {
        strlcpy(err, TR("no está en la microSD", "not on the microSD"), errn);
        goto out;
    }
    js[len] = 0;
    j = cJSON_Parse(js);
    free(js);
    if (!j) {
        strlcpy(err, TR("ajustes dañados", "broken settings"), errn);
        goto out;
    }
    face_t f = {0};
    cJSON *v;
    f.font = font_from(cJSON_GetStringValue(cJSON_GetObjectItem(j, "font")));
    f.size = (v = cJSON_GetObjectItem(j, "size")) ? v->valueint : 1;
    f.x = (v = cJSON_GetObjectItem(j, "x")) ? v->valueint : W / 2;
    f.y = (v = cJSON_GetObjectItem(j, "y")) ? v->valueint : 200;
    f.fx = (v = cJSON_GetObjectItem(j, "fx")) ? v->valueint : 1;
    f.secs = (v = cJSON_GetObjectItem(j, "secs")) && v->valueint;
    f.date = (v = cJSON_GetObjectItem(j, "date")) && v->valueint;
    f.anim = (v = cJSON_GetObjectItem(j, "anim")) && v->valueint;
    const char *col = cJSON_GetStringValue(cJSON_GetObjectItem(j, "color"));
    unsigned rgb = col && col[0] == '#' ? (unsigned)strtoul(col + 1, NULL, 16) : 0xFFFFFF;
    f.r = rgb >> 16, f.g = rgb >> 8, f.b = rgb;
    const char *nm = cJSON_GetStringValue(cJSON_GetObjectItem(j, "name"));
    strlcpy(f.name, nm ? nm : "", sizeof(f.name));
    const char *ck = cJSON_GetStringValue(cJSON_GetObjectItem(j, "clock"));
    f.clock = ck && !strcmp(ck, "analog") ? 1 : ck && !strcmp(ck, "edge") ? 2 : 0;
    f.ax = (v = cJSON_GetObjectItem(j, "ax")) ? v->valueint : W / 2;
    f.ay = (v = cJSON_GetObjectItem(j, "ay")) ? v->valueint : H / 2;
    f.dx = (v = cJSON_GetObjectItem(j, "dx")) && cJSON_IsNumber(v) ? v->valueint : -1;
    f.dy = (v = cJSON_GetObjectItem(j, "dy")) && cJSON_IsNumber(v) ? v->valueint : -1;
    cJSON *sl = cJSON_GetObjectItem(j, "slots");
    for (int k = 0; cJSON_IsArray(sl) && k < cJSON_GetArraySize(sl) && f.nslots < SLOTS_MAX; k++) {
        cJSON *el = cJSON_GetArrayItem(sl, k);
        const char *pic = cJSON_GetStringValue(cJSON_GetObjectItem(el, "pic"));
        if (!pic || strchr(pic, '/')) continue;  // a file in the face's own folder, nothing else
        f.from[f.nslots] = (v = cJSON_GetObjectItem(el, "from")) ? v->valueint : 0;
        strlcpy(f.pic[f.nslots++], pic, sizeof(f.pic[0]));
    }
    if (!f.nslots) {  // a face from before the stretches: one picture all day
        f.nslots = 1;
        strlcpy(f.pic[0], f.anim ? "anim.bin" : "bg.jpg", sizeof(f.pic[0]));
    }
    f.from[0] = 0;  // the day starts with the first one, whatever it says
    int k = cur_slot(&f);
    const char *pic = f.pic[k];
    size_t pl = strlen(pic);
    snprintf(path, sizeof(path), "/esferas/%d/%.31s", id, pic);
    if (pl > 4 && !strcmp(pic + pl - 4, ".bin")) {
        e->anim = sd_read_file(path, max_anim, &e->anim_len);
        if (!e->anim || e->anim_len < 8 || memcmp(e->anim, "MAN1", 4)) {
            strlcpy(err, TR("animación dañada", "broken animation"), errn);
            goto out;
        }
        e->bytes = e->anim_len;
        f.anim = 1;
    } else {
        uint8_t *jpg = sd_read_file(path, 1024 * 1024, &len);
        e->still = heap_caps_malloc(W * H * 2, MALLOC_CAP_SPIRAM);
        bool good = jpg && e->still && jpeg_decode(jpg, len, e->still, W, H, pool);
        free(jpg);
        if (!good) {
            strlcpy(err, TR("imagen dañada", "broken image"), errn);
            goto out;
        }
        e->bytes = W * H * 2;
        f.anim = 0;
    }
    e->id = id;
    e->slot = k;
    e->face = f;
    ok = true;
out:
    cJSON_Delete(j);
    sd_hold(false);
    if (!ok) entry_free(e);
    return ok;
}

// The cached entry of face id for the stretch it is in now (under s_lock)
static entry_t *find(int id)
{
    entry_t *best = NULL;
    for (int i = 0; i < CACHE_N; i++)
        if (s_cache[i].id == id && s_cache[i].slot == cur_slot(&s_cache[i].face) && (!best || s_cache[i].used > best->used))
            best = &s_cache[i];
    return best;
}

// Into the cache, making room by dropping the least recently used ones (never the one drawn) (under s_lock)
static void insert(entry_t *n)
{
    size_t total = n->bytes;
    for (int i = 0; i < CACHE_N; i++) total += s_cache[i].bytes;
    for (;;) {
        int fr = -1, lru = -1;
        for (int i = 0; i < CACHE_N; i++) {
            if (!s_cache[i].id) fr = i;
            else if (&s_cache[i] != s_cur && (lru < 0 || s_cache[i].used < s_cache[lru].used)) lru = i;
        }
        if (fr >= 0 && (total <= CACHE_BYTES || lru < 0)) {
            s_cache[fr] = *n;
            return;
        }
        if (lru < 0) {  // nothing but the one drawn, and no free place (cannot happen with CACHE_N > 1)
            entry_free(n);
            return;
        }
        total -= s_cache[lru].bytes;
        entry_free(&s_cache[lru]);
    }
}

static void loader(void *arg)
{
    static uint8_t pool[4096];  // TJpgDec's work area, this task's own
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        for (;;) {
            int id = 0, which = -1;
            xSemaphoreTake(s_lock, portMAX_DELAY);
            int c[3] = {s_want, s_near[0], s_near[1]};
            for (int k = 0; k < 3 && !id; k++)
                if (c[k] > 0 && !(k == 0 && c[k] == s_fail_id) && !find(c[k])) id = c[k], which = k;
            uint32_t gen = s_gen;
            xSemaphoreGive(s_lock);
            if (!id) break;
            entry_t e = {0};
            char err[48] = "";
            uint32_t t0 = now_ms();
            bool ok = load_entry(id, &e, err, sizeof(err), which == 0 ? ANIM_MAX : PREFETCH_MAX, pool);
            ESP_LOGI(TAG, "%s %d \"%s\" in %u ms%s", which == 0 ? "loaded" : "preloaded", id, ok ? e.face.name : "",
                     (unsigned)(now_ms() - t0), ok ? "" : " (failed)");
            xSemaphoreTake(s_lock, portMAX_DELAY);
            if (ok && gen == s_gen) {
                e.used = now_ms();
                insert(&e);
            } else {
                entry_free(&e);
                if (!ok && which == 0 && gen == s_gen) s_fail_id = id, strlcpy(s_fail_err, err, sizeof(s_fail_err));
                if (!ok && which > 0 && s_near[which - 1] == id) s_near[which - 1] = 0;  // not again until asked again
            }
            xSemaphoreGive(s_lock);
        }
    }
}

static void loader_init(void)
{
    if (s_task) return;
    if (!s_lock) s_lock = xSemaphoreCreateMutex();
    // its stack in PSRAM: internal RAM is short while Bluetooth runs, and the task never writes the flash
    const size_t stack = 8192;
    static StaticTask_t tcb;
    static StackType_t *st;
    if (!st) st = heap_caps_malloc(stack, MALLOC_CAP_SPIRAM);
    if (st && s_lock) s_task = xTaskCreateStaticPinnedToCore(loader, "custload", stack, NULL, 1, st, &tcb, tskNO_AFFINITY);
}

// What to load: the face to show (0: none) and the custom faces next to the current one in the swipe order
static void request(int want)
{
    loader_init();
    if (!s_task) return;
    int near[2] = {0, 0};
    if (!s_try_id) {
        static face_ref_t list[FX_ORDER_MAX];
        int n = fx_order(list, false), cur = -1;
        for (int k = 0; k < n; k++)
            if (list[k].fx == g_app.fx && (g_app.fx != FX_CUSTOM || list[k].cust == g_app.cust)) cur = k;
        if (cur >= 0 && n > 1) {
            const face_ref_t *a = &list[(cur + 1) % n], *b = &list[(cur + n - 1) % n];
            if (a->fx == FX_CUSTOM) near[0] = a->cust;
            if (b->fx == FX_CUSTOM) near[1] = b->cust;
        }
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool changed = s_want != want || s_near[0] != near[0] || s_near[1] != near[1];
    s_want = want, s_near[0] = near[0], s_near[1] = near[1];
    xSemaphoreGive(s_lock);
    if (changed) xTaskNotifyGive(s_task);
}

void custom_prefetch_around(void)
{
    request(g_app.fx == FX_CUSTOM ? (s_try_id ? s_try_id : g_app.cust) : 0);
}

// The frame k of the animation into s_bg, enlarged x1.5
static void show_frame(int k)
{
    static uint8_t pool[4096];
    if (k == s_cur_frame || k >= s_nframes) return;
    const uint8_t *e = s_anim + 8 + k * 12;
    uint32_t off = e[4] | e[5] << 8 | e[6] << 16 | (uint32_t)e[7] << 24, sz = e[8] | e[9] << 8 | e[10] << 16 | (uint32_t)e[11] << 24;
    if (off + sz > s_anim_len || !jpeg_decode(s_anim + off, sz, s_frame, AW, AH, pool)) return;
    for (int y = 0; y < H; y++) {
        const uint16_t *src = s_frame + (y * 2 / 3) * AW;
        uint16_t *dst = s_bg + y * W;
        for (int x = 0; x < W; x++) dst[x] = src[x * 2 / 3];
    }
    s_cur_frame = k;
}

static void flush(void);

void custom_enter(void)
{
    if (!s_bg) s_bg = heap_caps_malloc(W * H * 2, MALLOC_CAP_SPIRAM);
    if (!s_frame) s_frame = heap_caps_malloc(AW * AH * 2, MALLOC_CAP_SPIRAM);
    if (!s_mask) s_mask = heap_caps_calloc(W * H, 1, MALLOC_CAP_SPIRAM);
    loader_init();
    if (s_lock) {  // coming from another face: not the custom face shown last time, while this one loads
        xSemaphoreTake(s_lock, portMAX_DELAY);
        flush();
        if (s_cur && s_cur->id != (s_try_id ? s_try_id : g_app.cust)) s_cur = NULL, s_loaded = -1;
        xSemaphoreGive(s_lock);
    }
    custom_prefetch_around();
    s_anim_t0 = now_ms();
    s_last_render = 0;
}

// Files rewritten (again()): everything from the card again. The main loop only, under s_lock.
static void flush(void)
{
    if (!s_flush) return;
    s_flush = false;
    for (int i = 0; i < CACHE_N; i++) entry_free(&s_cache[i]);
    s_cur = NULL;
    s_loaded = -1;
    s_gen++;
    s_fail_id = 0;
    s_want = 0;  // so the next request wakes the loader
}

// Picks what to draw this frame: the face wanted if it is ready, else the one drawn so far. false:
// nothing to draw (s_err says why, or is empty while it loads).
static bool pick(int want, uint32_t now)
{
    loader_init();
    if (!s_task) return strlcpy(s_err, TR("sin memoria", "out of memory"), sizeof(s_err)), false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    flush();
    entry_t *e = find(want);
    if (e && e != s_cur) {
        s_cur = e;
        s_face = e->face;
        s_loaded = want;
        s_slot = e->slot;
        s_anim = e->anim, s_anim_len = e->anim_len;
        s_nframes = e->anim ? (s_anim[4] | s_anim[5] << 8) : 0;
        s_cur_frame = -1;
        s_anim_t0 = now;
        s_text_key[0] = 0;
    }
    if (e) e->used = now;
    bool failed = !e && want == s_fail_id;
    if (failed) {
        strlcpy(s_err, s_fail_err, sizeof(s_err));
        s_cur = NULL;
        s_loaded = -1;
    }
    xSemaphoreGive(s_lock);
    if (!e && !failed) request(want);  // (its neighbours too)
    if (failed) return false;
    if (s_cur) return true;  // the one before, until this one is ready
    s_err[0] = 0;
    return false;
}

void custom_render(uint16_t *fb, uint32_t t, uint32_t dt)
{
    int dx_, dy_;
    app_take_drag(&dx_, &dy_);
    uint32_t now = now_ms();
    if (s_try_id && (int32_t)(now - s_try_until) > 0) {  // "try" is over: back to the face before
        s_try_id = 0;
        if (s_try_prev >= 0) g_app.fx = s_try_prev;
        s_try_prev = -1;
    }
    int want = s_try_id ? s_try_id : g_app.cust;
    if (!pick(want, now)) {  // nothing to show yet: say why (or only the time while it loads)
        ui_begin(fb);
        ui_background(false);
        ui_veil(60, H, 2);
        ui_print_c(&ui_jersey, 4, W / 2, 150, TR("Mis esferas", "My faces"), MS_ORO, true);
        if (s_err[0]) ui_print_c(&ui_vt, 2, W / 2, 240, s_err, MS_CREMA, false);
        char hhmm[8];
        clock_hhmm(hhmm, true);
        ui_print_c(&ui_jersey, 6, W / 2, 300, hhmm, MS_CREMA, true);
        return;
    }
    if (s_anim) {
        // The screen was off (no frames for a while): from the first frame again; then round and round
        if (now - s_last_render > 500) s_anim_t0 = now;
        uint32_t total = 0;
        for (int k = 0; k < s_nframes; k++) total += s_anim[8 + k * 12] | s_anim[9 + k * 12] << 8;
        uint32_t el = total ? (now - s_anim_t0) % total : 0, acc = 0;
        int k = 0;
        for (; k < s_nframes - 1; k++) {
            uint32_t d = s_anim[8 + k * 12] | s_anim[9 + k * 12] << 8;
            if (el < acc + d) break;
            acc += d;
        }
        show_frame(k);
    }
    s_last_render = now;
    memcpy(fb, s_anim ? s_bg : s_cur->still, W * H * 2);
    // The text, rebuilt only when it changes
    char hhmm[8], ss[4], date[24], key[48];
    clock_hhmm(hhmm, true);
    snprintf(ss, sizeof(ss), "%02d", g_now.s);
    static const char *const WD_EN[7] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
    static const char *const MO_EN[12] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
    if (g_app.lang) snprintf(date, sizeof(date), "%s %d %s", WD_EN[g_now.wday % 7], g_now.day, MO_EN[(g_now.mon + 11) % 12]);
    else snprintf(date, sizeof(date), "%s %d %s", clock_wday_name(g_now.wday), g_now.day, clock_month_name(g_now.mon));
    if (s_face.clock) {  // analog: the date (if wanted) under the hands
        if (s_face.date) {
            if (strcmp(date, s_text_key)) {
                strlcpy(s_text_key, date, sizeof(s_text_key));
                build_date_mask(date);
            }
            draw_text(fb);
        }
        analog_draw(fb);
        return;
    }
    snprintf(key, sizeof(key), "%s%s%s", hhmm, s_face.secs ? ss : "", s_face.date ? date : "");
    if (strcmp(key, s_text_key)) {
        strlcpy(s_text_key, key, sizeof(s_text_key));
        build_mask(hhmm, ss, date);
    }
    draw_text(fb);
}

// ---------------------------------------------------------------- commands from the app

bool custom_command(const char *cmd)
{
    int id;
    if (strcmp(cmd, "cust list") == 0) {  // {"t":"cust","sd":"ok|none|bad","list":[{"id":3,"name":"Playa"}],"sel":3}
        static char buf[3072];
        size_t n = custom_list_json(buf, sizeof(buf));
        if (n) ble_send_json(buf);
        return true;
    }
    if (sscanf(cmd, "cust sel %d", &id) == 1) {  // show it (and keep it as the chosen one)
        settings_set("cust", id);
        again();  // load again: it may have just been rewritten
        settings_set("fx", FX_CUSTOM);
        sys_request("home");
        return true;
    }
    if (sscanf(cmd, "cust try %d", &id) == 1) {  // show it for 8 s without saving anything
        s_try_id = id;
        s_try_until = now_ms() + 8000;
        if (g_app.fx != FX_CUSTOM) s_try_prev = g_app.fx;
        again();
        g_app.fx = FX_CUSTOM;
        sys_request("home");
        return true;
    }
    if (sscanf(cmd, "cust del %d", &id) == 1) {
        char path[32];
        snprintf(path, sizeof(path), "/esferas/%d", id);
        sd_remove_dir(path);
        custom_list_dirty();
        if (g_app.cust == id) settings_set("cust", 0);
        again();
        return true;
    }
    if (sscanf(cmd, "cust reload %d", &id) == 1) {  // its files were rewritten: draw them again
        again();
        return true;
    }
    return false;
}

// The list as last read, for a swipe on the face (reading every face.json each time would be slow)
static int custom_read(int *ids, char (*names)[32], int max);
static int s_c_n = -1, s_c_sd, s_c_ids[24];
static char s_c_names[24][32];

void custom_list_dirty(void) { s_c_n = -1; }

int custom_list_cached(const int **ids, const char (**names)[32])
{
    if (s_c_n < 0 || s_c_sd != g_app.sd_state) custom_list(NULL, NULL, 0);
    *ids = s_c_ids;
    *names = (const char(*)[32])s_c_names;
    return s_c_n;
}

int custom_list(int *ids, char (*names)[32], int max)
{
    int n = custom_read(s_c_ids, s_c_names, 24);
    s_c_n = n, s_c_sd = g_app.sd_state;
    if (n > max) n = max;
    if (ids) memcpy(ids, s_c_ids, n * sizeof(int));
    if (names) memcpy(names, s_c_names, n * 32);
    return n;
}

static int custom_read(int *ids, char (*names)[32], int max)
{
    if (g_app.sd_state != SD_OK) sd_check();
    if (g_app.sd_state != SD_OK) return 0;
    static char dirs[24][16];
    int count = sd_list_names("/esferas", dirs, 24), n = 0;
    for (int i = 0; i < count && n < max; i++) {
        int id = atoi(dirs[i]);
        if (id <= 0 || id == CUSTOM_TRY_ID) continue;  // not the editor's trial
        char path[48];
        size_t len;
        snprintf(path, sizeof(path), "/esferas/%d/face.json", id);
        char *js = (char *)sd_read_file(path, 8192, &len);
        if (!js) continue;  // no settings yet: not a face (still arriving, or broken)
        js[len] = 0;
        cJSON *j = cJSON_Parse(js);
        const char *v = j ? cJSON_GetStringValue(cJSON_GetObjectItem(j, "name")) : NULL;
        snprintf(names[n], 32, "%s", v && *v ? v : "Sin nombre");
        cJSON_Delete(j);
        free(js);
        ids[n++] = id;
    }
    // in order of their number, as the phone lists them
    for (int a = 0; a < n; a++)
        for (int b = a + 1; b < n; b++)
            if (ids[b] < ids[a]) {
                int t = ids[a];
                ids[a] = ids[b], ids[b] = t;
                char tn[32];
                memcpy(tn, names[a], 32), memcpy(names[a], names[b], 32), memcpy(names[b], tn, 32);
            }
    return n;
}

size_t custom_list_json(char *buf, size_t size)
{
    if (g_app.sd_state != SD_OK) sd_check();
    size_t n = snprintf(buf, size, "{\"t\":\"cust\",\"sd\":\"%s\",\"sel\":%d,\"list\":[",
                        g_app.sd_state == SD_OK ? "ok" : g_app.sd_state == SD_BAD ? "bad" : "none", g_app.cust);
    static char names[24][16];
    int count = g_app.sd_state == SD_OK ? sd_list_names("/esferas", names, 24) : 0;
    int first = 1;
    for (int i = 0; i < count && n + 80 < size; i++) {
        int id = atoi(names[i]);
        if (id <= 0 || id == CUSTOM_TRY_ID) continue;  // not the editor's trial
        char path[48], nm[48] = "";
        size_t len;
        snprintf(path, sizeof(path), "/esferas/%d/face.json", id);
        char *js = (char *)sd_read_file(path, 8192, &len);
        if (js) {
            js[len] = 0;
            cJSON *j = cJSON_Parse(js);
            const char *v = j ? cJSON_GetStringValue(cJSON_GetObjectItem(j, "name")) : NULL;
            for (int k = 0, o = 0; v && v[k] && o < (int)sizeof(nm) - 2; k++)  // JSON-safe
                if (v[k] != '"' && v[k] != '\\' && (uint8_t)v[k] >= 32) nm[o++] = v[k], nm[o] = 0;
            cJSON_Delete(j);
            free(js);
        }
        n += snprintf(buf + n, size - n, "%s{\"id\":%d,\"name\":\"%s\"}", first ? "" : ",", id, nm);
        first = 0;
    }
    if (n + 3 < size) n += snprintf(buf + n, size - n, "]}");
    return n < size ? n : 0;
}
