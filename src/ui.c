#include "ui.h"
#include <stdio.h>
#include <string.h>
#include "app.h"
#include "board.h"
#include "clock.h"
#include "esp_heap_caps.h"

static uint16_t *s_fb;

uint16_t ui_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    uint16_t c = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
    return (uint16_t)((c >> 8) | (c << 8));
}

uint16_t ui_mix(uint16_t a, uint16_t b, int a_256)
{
    uint32_t x = __builtin_bswap16(a), y = __builtin_bswap16(b);
    x = (x | (x << 16)) & 0x07E0F81F;
    y = (y | (y << 16)) & 0x07E0F81F;
    uint32_t r = ((x * (uint32_t)(a_256 >> 3) + y * (uint32_t)(32 - (a_256 >> 3))) >> 5) & 0x07E0F81F;
    return __builtin_bswap16((uint16_t)((r >> 16) | r));
}

void ui_begin(uint16_t *fb) { s_fb = fb; }
uint16_t *ui_fb(void) { return s_fb; }

// ---------------------------------------------------------------- shapes

static inline bool clip(int *x, int *y, int *w, int *h)
{
    if (*x < 0) { *w += *x; *x = 0; }
    if (*y < 0) { *h += *y; *y = 0; }
    if (*x + *w > UI_W) *w = UI_W - *x;
    if (*y + *h > UI_H) *h = UI_H - *y;
    return *w > 0 && *h > 0;
}

void ui_fill(uint16_t c)
{
    for (int i = 0; i < UI_W * UI_H; i++) s_fb[i] = c;
}

void ui_rect(int x, int y, int w, int h, uint16_t c)
{
    if (!clip(&x, &y, &w, &h)) return;
    for (int j = 0; j < h; j++) {
        uint16_t *p = s_fb + (y + j) * UI_W + x;
        for (int i = 0; i < w; i++) p[i] = c;
    }
}

void ui_frame(int x, int y, int w, int h, int t, uint16_t c)
{
    ui_rect(x, y, w, t, c);
    ui_rect(x, y + h - t, w, t, c);
    ui_rect(x, y + t, t, h - 2 * t, c);
    ui_rect(x + w - t, y + t, t, h - 2 * t, c);
}

static inline uint16_t darken(uint16_t p, int level)
{
    uint16_t c = __builtin_bswap16(p);
    // each step halves the brightness of R, G and B at once (masks keep the fields apart)
    if (level >= 2) c = (c >> 1) & 0x7BEF;
    if (level >= 3) c = (c >> 1) & 0x7BEF;
    if (level == 1) c = (uint16_t)(((c >> 1) & 0x7BEF) + ((c >> 2) & 0x39E7));  // 75 %
    return __builtin_bswap16(c);
}

void ui_shade(int x, int y, int w, int h, int level)
{
    if (!clip(&x, &y, &w, &h)) return;
    for (int j = 0; j < h; j++) {
        uint16_t *p = s_fb + (y + j) * UI_W + x;
        for (int i = 0; i < w; i++) p[i] = darken(p[i], level);
    }
}

void ui_panel(int x, int y, int w, int h, uint16_t fill, uint16_t border, int g, bool shadow)
{
    if (shadow) ui_shade(x + 6 + g, y + 8 + g, w, h, 2);
    ui_rect(x + g, y + g, w - 2 * g, h - 2 * g, fill);
    // the border stops g px short of each corner: the "cut corner" pixel look
    ui_rect(x + g, y, w - 2 * g, g, border);
    ui_rect(x + g, y + h - g, w - 2 * g, g, border);
    ui_rect(x, y + g, g, h - 2 * g, border);
    ui_rect(x + w - g, y + g, g, h - 2 * g, border);
}

void ui_veil(int y0, int y1, int max_level)
{
    // Banded, not smooth: three steps of darkening down to max_level
    int band = (y1 - y0) / 3;
    for (int k = 0; k < 3; k++) {
        int lv = (k + 1) * max_level / 3;
        if (lv > 0) ui_shade(0, y0 + k * band, UI_W, k == 2 ? UI_H - (y0 + 2 * band) : band, lv);
    }
}

bool ui_visible(int x, int y)
{
    const int r = BOARD_LCD_CORNER_R, e = BOARD_LCD_EDGE;
    if (x < e || y < e || x >= UI_W - e || y >= UI_H - e) return false;
    int cx = x < r ? r : x > UI_W - 1 - r ? UI_W - 1 - r : x;
    int cy = y < r ? r : y > UI_H - 1 - r ? UI_H - 1 - r : y;
    int dx = x - cx, dy = y - cy;
    return dx * dx + dy * dy <= r * r;
}

// ---------------------------------------------------------------- text

static uint32_t next_cp(const char **s)
{
    const uint8_t *p = (const uint8_t *)*s;
    uint32_t c = *p++;
    if (c >= 0xF0 && p[0] && p[1] && p[2]) { c = ((c & 7) << 18) | ((p[0] & 63) << 12) | ((p[1] & 63) << 6) | (p[2] & 63); p += 3; }
    else if (c >= 0xE0 && p[0] && p[1]) { c = ((c & 15) << 12) | ((p[0] & 63) << 6) | (p[1] & 63); p += 2; }
    else if (c >= 0xC0 && p[0]) { c = ((c & 31) << 6) | (p[0] & 63); p += 1; }
    *s = (const char *)p;
    return c;
}

static const ui_glyph_t *glyph(const ui_font_t *f, uint32_t cp)
{
    int lo = 0, hi = f->count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (f->glyphs[mid].cp == cp) return &f->glyphs[mid];
        if (f->glyphs[mid].cp < cp) lo = mid + 1;
        else hi = mid - 1;
    }
    return cp == '?' ? NULL : glyph(f, '?');
}

int ui_line_h(const ui_font_t *f, int scale) { return (f->ascent + f->descent) * scale; }

int ui_print_w(const ui_font_t *f, int scale, const char *s)
{
    int w = 0;
    while (*s) {
        const ui_glyph_t *g = glyph(f, next_cp(&s));
        if (g) w += g->adv * scale;
    }
    return w;
}

static void draw_glyph(const ui_font_t *f, const ui_glyph_t *g, int scale, int x, int y, uint16_t color, bool shade)
{
    int bpr = (g->w + 7) / 8;
    const uint8_t *row = f->bits + g->off;
    int x0 = x + g->xo * scale, y0 = y + g->yo * scale;
    for (int j = 0; j < g->h; j++, row += bpr) {
        for (int i = 0; i < g->w; i++) {
            if (!(row[i >> 3] >> (i & 7) & 1)) continue;
            if (shade) ui_shade(x0 + i * scale, y0 + j * scale, scale, scale, 2);
            else ui_rect(x0 + i * scale, y0 + j * scale, scale, scale, color);
        }
    }
}

static int text_run(const ui_font_t *f, int scale, int x, int y, const char *s, uint16_t color, bool shade)
{
    int x0 = x;
    while (*s) {
        const ui_glyph_t *g = glyph(f, next_cp(&s));
        if (!g) continue;
        if (g->w) draw_glyph(f, g, scale, x, y, color, shade);
        x += g->adv * scale;
    }
    return x - x0;
}

int ui_print(const ui_font_t *f, int scale, int x, int y, const char *s, uint16_t color)
{
    return text_run(f, scale, x, y, s, color, false);
}

int ui_print_sh(const ui_font_t *f, int scale, int x, int y, const char *s, uint16_t color)
{
    int off = scale < 2 ? 1 : scale;
    text_run(f, scale, x + off, y + off, s, 0, true);
    return text_run(f, scale, x, y, s, color, false);
}

void ui_print_c(const ui_font_t *f, int scale, int cx, int y, const char *s, uint16_t color, bool sh)
{
    int x = cx - ui_print_w(f, scale, s) / 2;
    if (sh) ui_print_sh(f, scale, x, y, s, color);
    else ui_print(f, scale, x, y, s, color);
}

void ui_print_r(const ui_font_t *f, int scale, int x, int y, const char *s, uint16_t color)
{
    ui_print(f, scale, x - ui_print_w(f, scale, s), y, s, color);
}

int ui_fit(const ui_font_t *f, int max_scale, int min_scale, int max_w, const char *s)
{
    for (int k = max_scale; k > min_scale; k--)
        if (ui_print_w(f, k, s) <= max_w) return k;
    return min_scale;
}

// ---------------------------------------------------------------- widgets

void ui_button(int x, int y, int w, int h, const char *label, int kind, bool pressed)
{
    uint16_t bg = kind == UI_BTN_MAIN ? MS_ORO : kind == UI_BTN_DANGER ? MS_CORAL : MS_PANEL2;
    uint16_t fg = kind == UI_BTN_SECONDARY ? MS_CREMA : MS_NOCHE;
    int d = pressed ? 2 : 0, sh = pressed ? 2 : 4;
    ui_shade(x + d + sh, y + d + sh, w, h, 2);
    ui_rect(x + d, y + d, w, h, bg);
    int sc = ui_fit(&ui_jersey, 3, 2, w - 16, label);
    ui_print_c(&ui_jersey, sc, x + d + w / 2, y + d + (h - ui_line_h(&ui_jersey, sc)) / 2 + sc, label, fg, false);
}

void ui_bar(int x, int y, int w, int h, int permille, uint16_t color)
{
    if (permille < 0) permille = 0;
    if (permille > 1000) permille = 1000;
    ui_rect(x, y, w, h, MS_BARRA_OFF);
    ui_rect(x, y, w * permille / 1000, h, color);
}

void ui_switch(int x, int y, bool on)
{
    ui_rect(x, y, 64, 32, on ? MS_HALO_VERDE : MS_PANEL2);
    ui_frame(x, y, 64, 32, 3, on ? MS_VERDE : MS_LINEA);
    ui_rect(on ? x + 36 : x + 6, y + 6, 22, 20, on ? MS_VERDE : MS_GRIS);
}

// Lightning bolt over the battery while charging, 5 x 7 pixel-art cells
static const char *const BOLT[7] = {"...##", "..##.", ".##..", "#####", "..##.", ".##..", "##..."};

void ui_battery(int x, int y, int w, int h, int percent, bool charging)
{
    int t = h >= 20 ? 3 : 2;
    ui_frame(x, y, w - t * 2, h, t, MS_CREMA);
    ui_rect(x + w - t * 2, y + h / 3, t * 2, h - 2 * (h / 3), MS_CREMA);
    if (percent < 0) return;
    uint16_t c = charging ? MS_VERDE : percent <= 15 ? MS_CORAL : MS_ORO;
    int inner = w - t * 2 - t * 4;
    ui_rect(x + t * 2, y + t * 2, inner * (percent > 100 ? 100 : percent) / 100, h - t * 4, c);
    if (!charging) return;
    // The bolt, centred on the body, with a night outline so it reads over the fill and the frame
    int p = h / 8 < 1 ? 1 : h / 8, bx = x + (w - t * 2 - 5 * p) / 2, by = y + (h - 7 * p) / 2;
    for (int pass = 0; pass < 2; pass++)
        for (int j = 0; j < 7; j++)
            for (int i = 0; i < 5; i++) {
                if (BOLT[j][i] != '#') continue;
                if (pass) ui_rect(bx + i * p, by + j * p, p, p, MS_CREMA);
                else ui_rect(bx + i * p - 1, by + j * p - 1, p + 2, p + 2, MS_NOCHE);
            }
}

// Battery with its percentage, as the status bar shows it: the number goes to the right of the icon
void ui_battery_pct(int x, int y, int percent, bool charging)
{
    ui_battery(x, y, 30, 16, percent, charging);
    if (percent < 0) return;
    char b[8];
    snprintf(b, sizeof(b), "%d%%", percent > 100 ? 100 : percent);
    ui_print(&ui_vt, 1, x + 34, y - 5, b, charging ? MS_VERDE : percent <= 15 ? MS_CORAL : MS_CREMA);
}

void ui_dots(int cx, int y, int n, int cur)
{
    int x = cx - (n * 18 - 8) / 2;
    for (int i = 0; i < n; i++, x += 18) {
        if (i == cur) ui_rect(x, y, 10, 10, MS_ORO);
        else ui_frame(x, y, 10, 10, 2, MS_LINEA);
    }
}

// Icon palette: '.' transparent, 'k' night (outline), 'o' gold, 'O' light gold, 'r' ember, 'm' rose,
// 'c' cream, 'g' grey, 'v' green, 'l' line, 'b' deep blue, 'a' water, 'x' coral
static int icon_color(char ch, uint16_t *c)
{
    switch (ch) {
    case 'k': *c = MS_NOCHE; return 1;
    case 'o': *c = MS_ORO; return 1;
    case 'O': *c = MS_ORO2; return 1;
    case 'r': *c = MS_SWAP(0xEBEA); return 1;  // #EC7F55
    case 'm': *c = MS_SWAP(0xC2AC); return 1;  // #C75466
    case 'c': *c = MS_CREMA; return 1;
    case 'g': *c = MS_GRIS; return 1;
    case 'v': *c = MS_VERDE; return 1;
    case 'l': *c = MS_LINEA; return 1;
    case 'b': *c = MS_SWAP(0x220D); return 1;  // #22406E
    case 'a': *c = MS_SWAP(0x3BD0); return 1;  // #3F7A86
    case 'x': *c = MS_CORAL; return 1;
    }
    return 0;
}

void ui_icon(const char *const rows[16], int x, int y, int scale)
{
    for (int j = 0; j < 16; j++) {
        for (int i = 0; i < 16 && rows[j][i]; i++) {
            uint16_t c;
            if (icon_color(rows[j][i], &c)) ui_rect(x + i * scale, y + j * scale, scale, scale, c);
        }
    }
}

// Bluetooth rune, 7 x 11 cells
static const char *const BT_RUNE[11] = {"...#...", "...##..", ".#.#.#.", "..###..", "...#...", "..###..",
                                       ".#.#.#.", "...##..", "...#...", ".......", "......."};

// microSD (the corner cut at the top right), the same with a "!" when it cannot be read, and the USB
// trident; 7 x 9 like the Bluetooth rune, in 3 px blocks
static const char *const SD_RUNE[11] = {"####...", "#...#..", "#.#.##.", "#.....#", "#.....#", "#.....#",
                                        "#.....#", "#.....#", "#######", ".......", "......."};
static const char *const SD_BAD_RUNE[11] = {"####...", "#...#..", "#..#.#.", "#..#..#", "#..#..#", "#.....#",
                                            "#..#..#", "#.....#", "#######", ".......", "......."};
static const char *const USB_RUNE[11] = {"...#...", "..###..", "...#.##", "#..#.##", "#..#..#", ".#.#.#.",
                                         "..###..", "...#...", "..###..", ".......", "......."};

static void rune(const char *const *r, int x, uint16_t c)
{
    for (int j = 0; j < 9; j++)
        for (int i = 0; i < 7; i++)
            if (r[j][i] == '#') ui_rect(x + i * 3, 26 + j * 3, 3, 3, c);
}

void ui_status_bar(void)
{
    char t[8];
    clock_hhmm(t, true);
    // The top edge is straight only between the rounded corners: keep everything inside x 70..340
    ui_print_c(&ui_jersey, 3, UI_W / 2, 14, t, MS_CREMA, true);
    // Left of the time: Bluetooth, then the microSD and a USB data link when there are. They end just
    // before the time; at y 26 the rounded corner leaves everything from x ~50 visible
    const char *const *icons[3];
    uint16_t cols[3];
    int n = 0;
    icons[n] = BT_RUNE, cols[n++] = g_app.phone_linked ? MS_VERDE : MS_PILOTO_OFF;
    if (g_app.sd_state == SD_OK) icons[n] = SD_RUNE, cols[n++] = MS_CREMA;
    else if (g_app.sd_state == SD_BAD) icons[n] = SD_BAD_RUNE, cols[n++] = MS_CORAL;
    if (g_app.usb_data) icons[n] = USB_RUNE, cols[n++] = MS_VERDE;
    int x = UI_W / 2 - ui_print_w(&ui_jersey, 3, t) / 2 - 10 - n * 24 + 3;
    if (x > 92) x = 92;  // Bluetooth alone stays where it always was
    if (x < 54) x = 54;
    for (int k = 0; k < n; k++) rune(icons[k], x + k * 24, cols[k]);
    ui_battery_pct(274, 30, g_app.bat_pct, g_app.bat_chg);
}

// ---------------------------------------------------------------- background

// Built once at 205 x 251 "art pixels" and shown 2x2, the way the guide's backgrounds are pixel art
#define AW 205
#define AH 251
static uint16_t *s_bg[2];

static uint32_t s_seed;
static uint32_t rnd(void)
{
    s_seed ^= s_seed << 13;
    s_seed ^= s_seed >> 17;
    s_seed ^= s_seed << 5;
    return s_seed;
}

static void art_put(uint16_t *a, int x, int y, uint16_t c)
{
    if (x >= 0 && y >= 0 && x < AW && y < AH) a[y * AW + x] = c;
}

static void art_disc(uint16_t *a, int cx, int cy, int r, uint16_t c)
{
    for (int y = -r; y <= r; y++)
        for (int x = -r; x <= r; x++)
            if (x * x + y * y <= r * r + r) art_put(a, cx + x, cy + y, c);
}

static void build_scene(uint16_t *a, bool dusk)
{
    static const uint32_t night_sky[] = {0x05061A, 0x0A0D2B, 0x121A45, 0x1A2A5C, 0x22406E, 0x2E5A7A};
    static const uint32_t dusk_sky[] = {0x1C1440, 0x33215E, 0x3D2A5C, 0x5A2A6E, 0x8E3A6F, 0xC75466, 0xEC7F55};
    const uint32_t *sky = dusk ? dusk_sky : night_sky;
    int nsky = dusk ? 7 : 6, horizon = 150;
    s_seed = dusk ? 0x5EA5u : 0x7A1Du;
#define RGB(v) ui_rgb((v) >> 16, ((v) >> 8) & 0xFF, (v) & 0xFF)
    // Sky: flat bands with a checkerboard dither where two bands meet
    for (int y = 0; y < horizon; y++) {
        int band = y * nsky / horizon, in = y * nsky % horizon;
        for (int x = 0; x < AW; x++) {
            int b = band;
            if (band + 1 < nsky && in > horizon - horizon / nsky / 3 && ((x + y) & 1)) b = band + 1;
            a[y * AW + x] = RGB(sky[b]);
        }
    }
    // Stars, a few brighter ones as small crosses
    for (int i = 0; i < 90; i++) {
        int x = rnd() % AW, y = rnd() % (horizon - 30);
        uint16_t c = (rnd() & 3) ? MS_GRIS : MS_CREMA;
        art_put(a, x, y, c);
        if (!(rnd() % 9)) {
            art_put(a, x - 1, y, MS_GRIS);
            art_put(a, x + 1, y, MS_GRIS);
            art_put(a, x, y - 1, MS_GRIS);
            art_put(a, x, y + 1, MS_GRIS);
        }
    }
    // Moon (full by night, a crescent at dusk) with a halo of dots; the sun half sunk at dusk
    int mx = 172, my = dusk ? 86 : 92;  // lower right: titles sit at the top centre
    if (!dusk) {
        for (int k = 0; k < 64; k++) {
            float ang = k * 6.2831853f / 64;
            int r = 18 + (k & 1) * 2;
            art_put(a, mx + (int)(r * __builtin_cosf(ang)), my + (int)(r * __builtin_sinf(ang)), MS_GRIS);
        }
        art_disc(a, mx, my, 12, MS_CREMA);
        art_disc(a, mx - 4, my - 3, 2, MS_GRIS);
        art_disc(a, mx + 3, my + 4, 3, MS_GRIS);
        art_disc(a, mx + 5, my - 5, 1, MS_GRIS);
    } else {
        art_disc(a, mx, my, 8, MS_CREMA);
        art_disc(a, mx + 4, my - 2, 8, RGB(sky[1]));
        art_disc(a, 70, horizon + 2, 22, MS_ORO);
        art_disc(a, 70, horizon + 2, 17, MS_ORO2);
    }
    // Thin clouds with a lit edge
    for (int c = 0; c < 3; c++) {
        int cy = 30 + c * 22 + (rnd() % 8), cx = rnd() % AW, len = 30 + rnd() % 40;
        for (int x = 0; x < len; x++) {
            art_put(a, cx + x - len / 2, cy, dusk ? MS_ORO2 : MS_LINEA);
            if (x > 3 && x < len - 3) art_put(a, cx + x - len / 2, cy + 1, RGB(sky[1]));
        }
    }
    // Sea: bands from the horizon down, with short wave strokes
    static const uint32_t night_sea[] = {0x22406E, 0x1A2A5C, 0x121A45, 0x0A0D2B};
    static const uint32_t dusk_sea[] = {0x8E3A6F, 0x5A2A6E, 0x3D2A5C, 0x1C1440};
    const uint32_t *sea = dusk ? dusk_sea : night_sea;
    for (int y = horizon; y < AH; y++) {
        int band = (y - horizon) * 4 / (AH - horizon);
        for (int x = 0; x < AW; x++) a[y * AW + x] = RGB(sea[band]);
    }
    for (int i = 0; i < 70; i++) {
        int y = horizon + 3 + rnd() % (AH - horizon - 3), x = rnd() % AW, len = 2 + rnd() % 6;
        for (int k = 0; k < len; k++) art_put(a, x + k, y, RGB(dusk ? 0xC75466 : 0x3F7A86));
    }
    // Reflection of the moon (or sun): broken horizontal strokes under it
    int rx = dusk ? 70 : mx;
    for (int y = horizon + 2; y < AH - 20; y += 3) {
        int w = 10 - (y - horizon) / 14;
        if (w < 3) w = 3;
        int off = (int)(rnd() % 5) - 2;
        for (int k = -w / 2; k <= w / 2; k++)
            if ((rnd() % 5) != 0) art_put(a, rx + off + k, y, dusk ? MS_ORO : MS_CREMA);
    }
    // Islands on the horizon, one with a lighthouse
    uint16_t isl = RGB(dusk ? 0x1C1440 : 0x05061A);
    for (int x = 0; x < AW; x++) {
        int h1 = x > 110 && x < 185 ? (int)(7 - (x - 147) * (x - 147) / 200) : 0;
        int h2 = x > 0 && x < 40 ? (int)(5 - (x - 18) * (x - 18) / 90) : 0;
        int h = h1 > h2 ? h1 : h2;
        for (int y = 0; y < h; y++) art_put(a, x, horizon - 1 - y, isl);
    }
    for (int y = 0; y < 14; y++) art_put(a, 160, horizon - 7 - y, isl), art_put(a, 161, horizon - 7 - y, isl);
    art_put(a, 160, horizon - 22, MS_ORO2);
    art_put(a, 161, horizon - 22, MS_ORO2);
    art_put(a, 159, horizon - 22, MS_ORO);
    art_put(a, 162, horizon - 22, MS_ORO);
    // Cliff with a pine on the left, and a small campfire with sparks
    for (int y = 175; y < AH; y++) {
        int edge = 24 + (y - 175) / 3 + (int)(rnd() % 2);
        for (int x = 0; x < edge; x++) art_put(a, x, y, isl);
    }
    for (int y = 0; y < 34; y++) art_put(a, 14, 175 - y, isl);
    for (int t = 0; t < 7; t++) {
        int y = 145 + t * 4, w = 3 + t * 2;
        for (int k = -w; k <= w; k++) art_put(a, 14 + k, y, isl), art_put(a, 14 + k / 2, y - 1, isl);
    }
    art_put(a, 26, 182, MS_ORO);
    art_put(a, 27, 182, MS_ORO2);
    art_put(a, 26, 181, MS_SWAP(0xEBEA));
    art_put(a, 27, 180, MS_ORO);
    art_put(a, 28, 177, MS_ORO2);
    art_put(a, 25, 175, MS_ORO);
#undef RGB
}

void ui_background(bool dusk)
{
    uint16_t **bg = &s_bg[dusk ? 1 : 0];
    if (!*bg) {
        *bg = heap_caps_malloc(UI_W * UI_H * 2, MALLOC_CAP_SPIRAM);
        uint16_t *art = heap_caps_malloc(AW * AH * 2, MALLOC_CAP_SPIRAM);
        if (!*bg || !art) {
            ui_fill(MS_NOCHE);
            return;
        }
        build_scene(art, dusk);
        for (int y = 0; y < UI_H; y++)
            for (int x = 0; x < UI_W; x++) (*bg)[y * UI_W + x] = art[(y / 2 < AH ? y / 2 : AH - 1) * AW + (x / 2 < AW ? x / 2 : AW - 1)];
        heap_caps_free(art);
    }
    memcpy(s_fb, *bg, UI_W * UI_H * 2);
}

void ui_background_band(bool dusk, int y0, int y1)
{
    uint16_t *bg = s_bg[dusk ? 1 : 0];
    if (y0 < 0) y0 = 0;
    if (y1 > UI_H) y1 = UI_H;
    if (!bg || y1 <= y0) {
        ui_rect(0, y0, UI_W, y1 - y0, MS_NOCHE);
        return;
    }
    memcpy(s_fb + y0 * UI_W, bg + y0 * UI_W, (size_t)(y1 - y0) * UI_W * 2);
}
