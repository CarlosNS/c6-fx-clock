#include "gfx.h"
#include <stdlib.h>
#include <string.h>

// Classic 5x7 ASCII font (0x20..0x7E), column-major, LSB at the top
static const uint8_t font5x7[95][5] = {
    {0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x5F,0x00,0x00},{0x00,0x07,0x00,0x07,0x00},{0x14,0x7F,0x14,0x7F,0x14},
    {0x24,0x2A,0x7F,0x2A,0x12},{0x23,0x13,0x08,0x64,0x62},{0x36,0x49,0x55,0x22,0x50},{0x00,0x05,0x03,0x00,0x00},
    {0x00,0x1C,0x22,0x41,0x00},{0x00,0x41,0x22,0x1C,0x00},{0x08,0x2A,0x1C,0x2A,0x08},{0x08,0x08,0x3E,0x08,0x08},
    {0x00,0x50,0x30,0x00,0x00},{0x08,0x08,0x08,0x08,0x08},{0x00,0x60,0x60,0x00,0x00},{0x20,0x10,0x08,0x04,0x02},
    {0x3E,0x51,0x49,0x45,0x3E},{0x00,0x42,0x7F,0x40,0x00},{0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4B,0x31},
    {0x18,0x14,0x12,0x7F,0x10},{0x27,0x45,0x45,0x45,0x39},{0x3C,0x4A,0x49,0x49,0x30},{0x01,0x71,0x09,0x05,0x03},
    {0x36,0x49,0x49,0x49,0x36},{0x06,0x49,0x49,0x29,0x1E},{0x00,0x36,0x36,0x00,0x00},{0x00,0x56,0x36,0x00,0x00},
    {0x08,0x14,0x22,0x41,0x00},{0x14,0x14,0x14,0x14,0x14},{0x00,0x41,0x22,0x14,0x08},{0x02,0x01,0x51,0x09,0x06},
    {0x32,0x49,0x79,0x41,0x3E},{0x7E,0x11,0x11,0x11,0x7E},{0x7F,0x49,0x49,0x49,0x36},{0x3E,0x41,0x41,0x41,0x22},
    {0x7F,0x41,0x41,0x22,0x1C},{0x7F,0x49,0x49,0x49,0x41},{0x7F,0x09,0x09,0x01,0x01},{0x3E,0x41,0x41,0x51,0x32},
    {0x7F,0x08,0x08,0x08,0x7F},{0x00,0x41,0x7F,0x41,0x00},{0x20,0x40,0x41,0x3F,0x01},{0x7F,0x08,0x14,0x22,0x41},
    {0x7F,0x40,0x40,0x40,0x40},{0x7F,0x02,0x04,0x02,0x7F},{0x7F,0x04,0x08,0x10,0x7F},{0x3E,0x41,0x41,0x41,0x3E},
    {0x7F,0x09,0x09,0x09,0x06},{0x3E,0x41,0x51,0x21,0x5E},{0x7F,0x09,0x19,0x29,0x46},{0x46,0x49,0x49,0x49,0x31},
    {0x01,0x01,0x7F,0x01,0x01},{0x3F,0x40,0x40,0x40,0x3F},{0x1F,0x20,0x40,0x20,0x1F},{0x7F,0x20,0x18,0x20,0x7F},
    {0x63,0x14,0x08,0x14,0x63},{0x03,0x04,0x78,0x04,0x03},{0x61,0x51,0x49,0x45,0x43},{0x00,0x7F,0x41,0x41,0x00},
    {0x02,0x04,0x08,0x10,0x20},{0x00,0x41,0x41,0x7F,0x00},{0x04,0x02,0x01,0x02,0x04},{0x40,0x40,0x40,0x40,0x40},
    {0x00,0x01,0x02,0x04,0x00},{0x20,0x54,0x54,0x54,0x78},{0x7F,0x48,0x44,0x44,0x38},{0x38,0x44,0x44,0x44,0x20},
    {0x38,0x44,0x44,0x48,0x7F},{0x38,0x54,0x54,0x54,0x18},{0x08,0x7E,0x09,0x01,0x02},{0x08,0x14,0x54,0x54,0x3C},
    {0x7F,0x08,0x04,0x04,0x78},{0x00,0x44,0x7D,0x40,0x00},{0x20,0x40,0x44,0x3D,0x00},{0x00,0x7F,0x10,0x28,0x44},
    {0x00,0x41,0x7F,0x40,0x00},{0x7C,0x04,0x18,0x04,0x78},{0x7C,0x08,0x04,0x04,0x78},{0x38,0x44,0x44,0x44,0x38},
    {0x7C,0x14,0x14,0x14,0x08},{0x08,0x14,0x14,0x18,0x7C},{0x7C,0x08,0x04,0x04,0x08},{0x48,0x54,0x54,0x54,0x20},
    {0x04,0x3F,0x44,0x40,0x20},{0x3C,0x40,0x40,0x20,0x7C},{0x1C,0x20,0x40,0x20,0x1C},{0x3C,0x40,0x30,0x40,0x3C},
    {0x44,0x28,0x10,0x28,0x44},{0x0C,0x50,0x50,0x50,0x3C},{0x44,0x64,0x54,0x4C,0x44},{0x00,0x08,0x36,0x41,0x00},
    {0x00,0x00,0x7F,0x00,0x00},{0x00,0x41,0x36,0x08,0x00},{0x08,0x04,0x08,0x10,0x08},
};

#define W g_lcd_w
#define H g_lcd_h

void gfx_fill(uint16_t *fb, uint16_t color)
{
    for (int i = 0; i < LCD_W * LCD_H; i++) fb[i] = color;
}

void gfx_fill_rect(uint16_t *fb, int x, int y, int w, int h, uint16_t color)
{
    int x1 = x + w, y1 = y + h;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x1 > W) x1 = W;
    if (y1 > H) y1 = H;
    for (int yy = y; yy < y1; yy++) {
        uint16_t *row = fb + yy * W;
        for (int xx = x; xx < x1; xx++) row[xx] = color;
    }
}

void gfx_line(uint16_t *fb, int x0, int y0, int x1, int y1, uint16_t color)
{
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        if ((unsigned)x0 < (unsigned)W && (unsigned)y0 < (unsigned)H) fb[y0 * W + x0] = color;
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

#define SWAP(a, b) do { int _t = a; a = b; b = _t; } while (0)

void gfx_tri(uint16_t *fb, int x0, int y0, int x1, int y1, int x2, int y2, uint16_t color)
{
    if (y0 > y1) { SWAP(y0, y1); SWAP(x0, x1); }
    if (y1 > y2) { SWAP(y1, y2); SWAP(x1, x2); }
    if (y0 > y1) { SWAP(y0, y1); SWAP(x0, x1); }
    if (y0 == y2) return;
    int ys = y0 < 0 ? 0 : y0;
    int ye = y2 >= H ? H - 1 : y2;
    for (int y = ys; y <= ye; y++) {
        int xa = x0 + (x2 - x0) * (y - y0) / (y2 - y0);
        int xb;
        if (y < y1) xb = x0 + (x1 - x0) * (y - y0) / (y1 - y0);
        else xb = (y2 == y1) ? x1 : x1 + (x2 - x1) * (y - y1) / (y2 - y1);
        if (xa > xb) SWAP(xa, xb);
        if (xa < 0) xa = 0;
        if (xb >= W) xb = W - 1;
        uint16_t *row = fb + y * W;
        for (int x = xa; x <= xb; x++) row[x] = color;
    }
}

void gfx_circle(uint16_t *fb, int cx, int cy, int r, uint16_t color)
{
    for (int dy = -r; dy <= r; dy++) {
        int y = cy + dy;
        if ((unsigned)y >= (unsigned)H) continue;
        int dx = 0;
        while ((dx + 1) * (dx + 1) + dy * dy <= r * r + r) dx++;
        int xa = cx - dx < 0 ? 0 : cx - dx, xb = cx + dx >= W ? W - 1 : cx + dx;
        for (int x = xa; x <= xb; x++) fb[y * W + x] = color;
    }
}

static inline uint16_t unswap(uint16_t c)
{
    return (uint16_t)((c >> 8) | (c << 8));
}

void gfx_darken_rect(uint16_t *fb, int x, int y, int w, int h, int radius, int level)
{
    static const uint16_t masks[4] = {0xFFFF, 0x7BEF, 0x39E7, 0x18E3};  // per-channel shift masks (unswapped)
    for (int yy = 0; yy < h; yy++) {
        int py = y + yy;
        if ((unsigned)py >= (unsigned)H) continue;
        // Rounded corners: inset the span on the first/last `radius` rows
        int inset = 0;
        int cy = yy < radius ? radius - yy : (yy >= h - radius ? yy - (h - 1 - radius) : 0);
        if (cy > 0) {
            while (inset < radius && (radius - inset) * (radius - inset) + cy * cy > radius * radius) inset++;
        }
        for (int xx = inset; xx < w - inset; xx++) {
            int px = x + xx;
            if ((unsigned)px >= (unsigned)W) continue;
            uint16_t c = unswap(fb[py * W + px]);
            c = (c >> level) & masks[level];
            fb[py * W + px] = unswap(c);
        }
    }
}

const uint8_t *gfx_glyph5x7(char c)
{
    if (c < 0x20 || c > 0x7E) c = '?';
    return font5x7[c - 0x20];
}

static void draw_glyph(uint16_t *fb, int x, int y, char c, uint16_t color, int scale)
{
    const uint8_t *g = gfx_glyph5x7(c);
    for (int col = 0; col < 5; col++) {
        for (int row = 0; row < 7; row++) {
            if (g[col] & (1 << row)) gfx_fill_rect(fb, x + col * scale, y + row * scale, scale, scale, color);
        }
    }
}

int gfx_text_width(const char *s, int scale)
{
    return (int)strlen(s) * 6 * scale - scale;
}

void gfx_text(uint16_t *fb, int x, int y, const char *s, uint16_t color, int scale)
{
    uint16_t shadow = rgb565(0, 0, 0);
    for (int i = 0; s[i]; i++) draw_glyph(fb, x + i * 6 * scale + 1, y + 1, s[i], shadow, scale);
    for (int i = 0; s[i]; i++) draw_glyph(fb, x + i * 6 * scale, y, s[i], color, scale);
}

void gfx_text_ol(uint16_t *fb, int x, int y, const char *s, uint16_t color, uint16_t outline, int scale, int ol)
{
    for (int i = 0; s[i]; i++) {
        int gx = x + i * 6 * scale;
        const uint8_t *g = gfx_glyph5x7(s[i]);
        for (int col = 0; col < 5; col++)
            for (int row = 0; row < 7; row++)
                if (g[col] & (1 << row))
                    gfx_fill_rect(fb, gx + col * scale - ol, y + row * scale - ol, scale + 2 * ol, scale + 2 * ol, outline);
    }
    for (int i = 0; s[i]; i++) draw_glyph(fb, x + i * 6 * scale, y, s[i], color, scale);
}

static int glyph_index(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c == ':') return 10;
    if (c == '-') return 11;
    return -1;
}

int font_width(const font_t *f, const char *s, int spacing)
{
    int w = 0, n = 0;
    for (; *s; s++, n++) {
        int gi = glyph_index(*s);
        w += gi >= 0 ? f->glyphs[gi].adv : f->glyphs[0].adv / 2;
    }
    return n ? w + spacing * (n - 1) : 0;
}

void font_draw(uint16_t *fb, const font_t *f, int x, int y, const char *s, int spacing,
               uint16_t fill, uint16_t outline, int mode)
{
    for (; *s; s++) {
        int gi = glyph_index(*s);
        if (gi < 0) {
            x += f->glyphs[0].adv / 2 + spacing;
            continue;
        }
        const font_glyph_t *g = &f->glyphs[gi];
        int gx = x - f->pad, gy = y - f->pad;
        for (int row = 0; row < g->h; row++) {
            int py = gy + row;
            if ((unsigned)py >= (unsigned)H) continue;
            const uint8_t *src = g->px + row * g->w;
            uint16_t *dst = fb + py * W;
            for (int col = 0; col < g->w; col++) {
                uint8_t b = src[col];
                if (!b) continue;
                int px = gx + col;
                if ((unsigned)px >= (unsigned)W) continue;
                int fa = b >> 4, oa = b & 15;
                if ((mode & FONT_OUTLINE) && oa) dst[px] = oa == 15 ? outline : gfx_blend(dst[px], outline, oa);
                if ((mode & FONT_FILL) && fa) dst[px] = fa == 15 ? fill : gfx_blend(dst[px], fill, fa);
            }
        }
        x += g->adv + spacing;
    }
}

int ui_width(const uifont_t *f, const char *s)
{
    int w = 0;
    for (; *s; s++) {
        unsigned c = (unsigned char)*s;
        if (c >= 32 && c < 127) w += f->adv[c - 32];
    }
    return w;
}

void ui_text(uint16_t *fb, const uifont_t *f, int x, int y, const char *s, uint16_t fill, uint16_t outline, int mode)
{
    for (; *s; s++) {
        unsigned c = (unsigned char)*s;
        if (c < 32 || c >= 127) continue;
        int gi = c - 32, gw = f->w[gi];
        const uint8_t *px = f->px + f->off[gi];
        int gx = x - f->pad, gy = y - f->pad;
        for (int row = 0; row < f->h; row++) {
            int py = gy + row;
            if ((unsigned)py >= (unsigned)H) continue;
            uint16_t *dst = fb + py * W;
            for (int col = 0; col < gw; col++) {
                uint8_t b = px[row * gw + col];
                if (!b) continue;
                int pxx = gx + col;
                if ((unsigned)pxx >= (unsigned)W) continue;
                int fa = b >> 4, oa = b & 15;
                if ((mode & FONT_OUTLINE) && oa) dst[pxx] = oa == 15 ? outline : gfx_blend(dst[pxx], outline, oa);
                if ((mode & FONT_FILL) && fa) dst[pxx] = fa == 15 ? fill : gfx_blend(dst[pxx], fill, fa);
            }
        }
        x += f->adv[gi];
    }
}

void gfx_glow(uint16_t *fb, int cx, int cy, int r, uint16_t color, int max_alpha)
{
    int r2 = r * r;
    if (r2 <= 0) return;
    int32_t inv = (1 << 24) / r2;  // the C6 divides slowly: one reciprocal instead of two divisions per pixel
    for (int dy = -r; dy <= r; dy++) {
        int y = cy + dy;
        if ((unsigned)y >= (unsigned)H) continue;
        for (int dx = -r; dx <= r; dx++) {
            int x = cx + dx;
            if ((unsigned)x >= (unsigned)W) continue;
            int d2 = dx * dx + dy * dy;
            if (d2 >= r2) continue;
            int k = r2 - d2;  // quadratic falloff
            int a = (max_alpha * k * inv) >> 24;
            a = ((a * k * inv) >> 24) + a / 2;
            if (a > 0) fb[y * W + x] = gfx_blend(fb[y * W + x], color, a > 16 ? 16 : a);
        }
    }
}

static void glyph_layer(uint16_t *fb, const font_t *f, const font_glyph_t *g, int gx, int gy, bool fill_layer,
                        const uint16_t *row_col, uint16_t col, int alpha_scale)
{
    // Clip once per glyph instead of per pixel
    int c0 = gx < 0 ? -gx : 0, c1 = gx + g->w > W ? W - gx : g->w;
    int r0 = gy < 0 ? -gy : 0, r1 = gy + g->h > H ? H - gy : g->h;
    for (int row = r0; row < r1; row++) {
        const uint8_t *src = g->px + row * g->w;
        uint16_t *dst = fb + (gy + row) * W + gx;
        uint16_t c = row_col ? row_col[row] : col;
        for (int cx = c0; cx < c1; cx++) {
            uint8_t b = src[cx];
            int a = fill_layer ? b >> 4 : b & 15;
            if (!a) continue;
            a = a * alpha_scale >> 4;
            dst[cx] = a >= 15 ? c : gfx_blend(dst[cx], c, a);
        }
    }
}

// Outline and gradient fill of one glyph in a single pass (each byte holds both coverages)
static void glyph_outline_fill(uint16_t *fb, const font_glyph_t *g, int gx, int gy, const uint16_t *row_col,
                               uint16_t outline)
{
    int c0 = gx < 0 ? -gx : 0, c1 = gx + g->w > W ? W - gx : g->w;
    int r0 = gy < 0 ? -gy : 0, r1 = gy + g->h > H ? H - gy : g->h;
    for (int row = r0; row < r1; row++) {
        const uint8_t *src = g->px + row * g->w;
        uint16_t *dst = fb + (gy + row) * W + gx;
        uint16_t c = row_col[row];
        for (int cx = c0; cx < c1; cx++) {
            uint8_t b = src[cx];
            if (!b) continue;
            int ao = b & 15, af = b >> 4;
            uint16_t d = dst[cx];
            if (ao) d = ao >= 15 ? outline : gfx_blend(d, outline, ao);
            if (af) d = af >= 15 ? c : gfx_blend(d, c, af);
            dst[cx] = d;
        }
    }
}

void font_draw_grad(uint16_t *fb, const font_t *f, int x, int y, const char *s, int spacing, uint16_t top,
                    uint16_t bottom, uint16_t outline, int shadow_off)
{
    uint16_t rows[128];
    int gh = f->h + 2 * f->pad;
    for (int r = 0; r < gh && r < 128; r++) {
        int k = (r - f->pad) * 16 / f->h;
        rows[r] = gfx_blend(top, bottom, k < 0 ? 0 : k > 16 ? 16 : k);
    }
    // Two passes: the shadow under every glyph first, then outline + fill glyph by glyph
    for (int layer = 0; layer < 2; layer++) {
        if (layer == 0 && shadow_off <= 0) continue;
        int cx = x;
        for (const char *p = s; *p; p++) {
            int gi = glyph_index(*p);
            if (gi < 0) {
                cx += f->glyphs[0].adv / 2 + spacing;
                continue;
            }
            const font_glyph_t *g = &f->glyphs[gi];
            int gx = cx - f->pad, gy = y - f->pad;
            if (layer == 0) glyph_layer(fb, f, g, gx + shadow_off, gy + shadow_off, false, NULL, rgb565(0, 0, 0), 9);
            else glyph_outline_fill(fb, g, gx, gy, rows, outline);
            cx += g->adv + spacing;
        }
    }
}

// 7x10 pixel digits, chunky 2-px strokes in the style of 16-bit RPG HUDs
static const char *const pixglyph[12][10] = {
    {".#####.", "##...##", "##...##", "##...##", "##...##", "##...##", "##...##", "##...##", "##...##", ".#####."},
    {"...##..", "..###..", ".####..", "...##..", "...##..", "...##..", "...##..", "...##..", "...##..", ".######"},
    {".#####.", "##...##", ".....##", ".....##", "....##.", "...##..", "..##...", ".##....", "##.....", "#######"},
    {".#####.", "##...##", ".....##", ".....##", "..####.", ".....##", ".....##", ".....##", "##...##", ".#####."},
    {"....##.", "...###.", "..####.", ".##.##.", "##..##.", "#######", "....##.", "....##.", "....##.", "....##."},
    {"#######", "##.....", "##.....", "######.", ".....##", ".....##", ".....##", ".....##", "##...##", ".#####."},
    {"..####.", ".##....", "##.....", "##.....", "######.", "##...##", "##...##", "##...##", "##...##", ".#####."},
    {"#######", ".....##", ".....##", "....##.", "....##.", "...##..", "...##..", "..##...", "..##...", "..##..."},
    {".#####.", "##...##", "##...##", "##...##", ".#####.", "##...##", "##...##", "##...##", "##...##", ".#####."},
    {".#####.", "##...##", "##...##", "##...##", "##...##", ".######", ".....##", ".....##", "....##.", ".####.."},
    {"..", "..", "##", "##", "..", "..", "##", "##", "..", ".."},
    {"......", "......", "......", "......", "######", "######", "......", "......", "......", "......"},
};

static int pix_index(char c)
{
    return c >= '0' && c <= '9' ? c - '0' : c == ':' ? 10 : c == '-' ? 11 : -1;
}

static int pix_cols(int gi)
{
    return gi < 0 ? 2 : (int)strlen(pixglyph[gi][0]);
}

int pix_digits_width(const char *s, int scale)
{
    int w = 0;
    for (const char *p = s; *p; p++) w += (pix_cols(pix_index(*p)) + 1) * scale;
    return w ? w - scale : 0;
}

static inline bool pix_on(int gi, int col, int row)
{
    if (gi < 0 || row < 0 || row >= 10 || col < 0 || col >= pix_cols(gi)) return false;
    return pixglyph[gi][row][col] == '#';
}

void pix_digits_draw(uint16_t *fb, int x, int y, const char *s, int scale, const uint16_t grad[4], uint16_t outline)
{
    int ol = scale / 2 < 2 ? 2 : scale / 2;
    for (int layer = 0; layer < 3; layer++) {
        int cx = x;
        for (const char *p = s; *p; p++) {
            int gi = pix_index(*p), cols = pix_cols(gi);
            for (int row = 0; row < 10 && gi >= 0; row++)
                for (int col = 0; col < cols; col++) {
                    if (!pix_on(gi, col, row)) continue;
                    int bx = cx + col * scale, by = y + row * scale;
                    if (layer == 0) {  // translucent drop shadow
                        gfx_darken_rect(fb, bx + ol + 1, by + ol + 1, scale, scale, 0, 2);
                    } else if (layer == 1) {
                        gfx_fill_rect(fb, bx - ol, by - ol, scale + 2 * ol, scale + 2 * ol, outline);
                    } else {
                        gfx_fill_rect(fb, bx, by, scale, scale, grad[row * 4 / 10]);
                        if (!pix_on(gi, col, row - 1)) gfx_fill_rect(fb, bx, by, scale, 1, grad[0]);  // bevel
                    }
                }
            cx += (cols + 1) * scale;
        }
    }
}

static int isqrt(int v)
{
    if (v <= 0) return 0;
    unsigned r = 0, bit = 1u << 30, n = (unsigned)v;
    while (bit > n) bit >>= 2;
    while (bit) {
        if (n >= r + bit) { n -= r + bit; r = (r >> 1) + bit; }
        else r >>= 1;
        bit >>= 2;
    }
    return (int)r;
}

void gfx_ring(uint16_t *fb, int cx, int cy, int r_in, int r_out, uint16_t color)
{
    // Per row: fill the spans between the inner and outer circle (integer square roots: no FPU)
    int ri2 = r_in * r_in, ro2 = r_out * r_out + r_out;
    for (int dy = -r_out; dy <= r_out; dy++) {
        int y = cy + dy;
        if ((unsigned)y >= (unsigned)H) continue;
        int xo = isqrt(ro2 - dy * dy);
        int xi = -1;
        if (dy * dy < ri2) {
            xi = isqrt(ri2 - dy * dy);
            if (xi * xi < ri2 - dy * dy) xi++;
        }
        uint16_t *row = fb + y * W;
        for (int s = -1; s <= 1; s += 2) {
            int a = xi < 0 ? (s < 0 ? -xo : 0) : (s < 0 ? -xo : xi);
            int b = xi < 0 ? (s < 0 ? -1 : xo) : (s < 0 ? -xi : xo);
            for (int dx = a; dx <= b; dx++) {
                int x = cx + dx;
                if ((unsigned)x < (unsigned)W) row[x] = color;
            }
        }
    }
}

void gfx_poly(uint16_t *fb, const int16_t *xy, int n, uint16_t color)
{
    if (n < 3) return;
    int ymin = xy[1], ymax = xy[1];
    for (int i = 1; i < n; i++) {
        if (xy[i * 2 + 1] < ymin) ymin = xy[i * 2 + 1];
        if (xy[i * 2 + 1] > ymax) ymax = xy[i * 2 + 1];
    }
    if (ymin < 0) ymin = 0;
    if (ymax >= H) ymax = H - 1;
    int xs[32];
    for (int y = ymin; y <= ymax; y++) {
        int cnt = 0;
        for (int i = 0, j = n - 1; i < n; j = i++) {
            int yi = xy[i * 2 + 1], yj = xy[j * 2 + 1];
            if ((yi <= y && yj > y) || (yj <= y && yi > y)) {
                int xi = xy[i * 2], xj = xy[j * 2];
                if (cnt < 32) xs[cnt++] = xi + (y - yi) * (xj - xi) / (yj - yi);
            }
        }
        for (int a = 1; a < cnt; a++)  // insertion sort, cnt is small
            for (int b = a; b > 0 && xs[b - 1] > xs[b]; b--) { int t = xs[b]; xs[b] = xs[b - 1]; xs[b - 1] = t; }
        uint16_t *row = fb + y * W;
        for (int k = 0; k + 1 < cnt; k += 2) {
            int x0 = xs[k] < 0 ? 0 : xs[k], x1 = xs[k + 1] >= W ? W - 1 : xs[k + 1];
            for (int x = x0; x < x1; x++) row[x] = color;
        }
    }
}

int gfx_isqrt32(uint32_t v)
{
    uint32_t r = 0, b = 1u << 30;
    while (b > v) b >>= 2;
    while (b) {
        if (v >= r + b) {
            v -= r + b;
            r = (r >> 1) + b;
        } else {
            r >>= 1;
        }
        b >>= 2;
    }
    return (int)r;
}

// Antialiased capsule (a segment with round ends; a == b gives a disc). Coordinates and radius in
// 1/16 px, amax = opacity 0..16.
void gfx_aa_capsule(uint16_t *fb, int ax, int ay, int bx, int by, int r, uint16_t c, int amax)
{
    int x0 = ((ax < bx ? ax : bx) - r - 16) >> 4, x1 = ((ax > bx ? ax : bx) + r + 16) >> 4;
    int y0 = ((ay < by ? ay : by) - r - 16) >> 4, y1 = ((ay > by ? ay : by) + r + 16) >> 4;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > W - 1) x1 = W - 1;
    if (y1 > H - 1) y1 = H - 1;
    int dx = bx - ax, dy = by - ay, len = dx * dx + dy * dy;
    int outer = (r + 8) * (r + 8), inner = r > 8 ? (r - 8) * (r - 8) : -1;
    // Along the body the distance is |cross| / |ab|: one multiply by a reciprocal, no division or
    // square root per pixel. Only the round ends need the point distance.
    int dl = len ? gfx_isqrt32(len) : 1, rs = (1 << 20) / (dl ? dl : 1);
    for (int y = y0; y <= y1; y++) {
        int py = y * 16 + 8 - ay;
        uint16_t *row = fb + y * W;
        for (int x = x0; x <= x1; x++) {
            int px = x * 16 + 8 - ax, a;
            int dot = len ? px * dx + py * dy : 0;
            if (len && dot > 0 && dot < len) {
                int cr = px * dy - py * dx;
                int d = (int)((int64_t)(cr < 0 ? -cr : cr) * rs >> 20);
                if (d >= r + 8) continue;
                a = d <= r - 8 ? amax : (r + 8 - d) * amax / 16;
            } else {
                int ex = dot <= 0 ? px : px - dx, ey = dot <= 0 ? py : py - dy;
                int d2 = ex * ex + ey * ey;
                if (d2 >= outer) continue;
                a = d2 <= inner ? amax : (r + 8 - gfx_isqrt32(d2)) * amax / 16;
            }
            if (a > 0) row[x] = gfx_blend(row[x], c, a);
        }
    }
}
