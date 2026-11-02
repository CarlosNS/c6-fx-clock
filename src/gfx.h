#pragma once
#include <stdint.h>
#include "fonts.h"
#include "lcd.h"
#include "uifont.h"

// All primitives clip to the current orientation (g_lcd_w x g_lcd_h)

void gfx_fill(uint16_t *fb, uint16_t color);
void gfx_fill_rect(uint16_t *fb, int x, int y, int w, int h, uint16_t color);
void gfx_line(uint16_t *fb, int x0, int y0, int x1, int y1, uint16_t color);
void gfx_tri(uint16_t *fb, int x0, int y0, int x1, int y1, int x2, int y2, uint16_t color);
void gfx_circle(uint16_t *fb, int cx, int cy, int r, uint16_t color);
// Annulus between r_in and r_out (r_in may be 0)
void gfx_ring(uint16_t *fb, int cx, int cy, int r_in, int r_out, uint16_t color);
// Filled polygon (even-odd rule), up to 160 vertices given as x0,y0,x1,y1,...
void gfx_poly(uint16_t *fb, const int16_t *xy, int n, uint16_t color);
// Darkens a rounded rectangle: level 1 = 50%, 2 = 75%, 3 = 87%
void gfx_darken_rect(uint16_t *fb, int x, int y, int w, int h, int radius, int level);
// Blends src over dst (both byte-swapped RGB565); alpha 0..16.
// Spreads R, G and B into separate bit fields of a 32-bit word so one multiply blends all three.
static inline uint16_t gfx_blend(uint16_t dst, uint16_t src, int alpha)
{
    uint32_t d = __builtin_bswap16(dst), s = __builtin_bswap16(src);
    d = (d | (d << 16)) & 0x07E0F81F;
    s = (s | (s << 16)) & 0x07E0F81F;
    uint32_t r = ((((s - d) * (uint32_t)(alpha * 2)) >> 5) + d) & 0x07E0F81F;
    return __builtin_bswap16((uint16_t)((r >> 16) | r));
}

// 5x7 font, each glyph is 6*scale px wide. gfx_text draws a 1 px drop shadow,
// gfx_text_ol a solid outline `ol` px thick.
void gfx_text(uint16_t *fb, int x, int y, const char *s, uint16_t color, int scale);
void gfx_text_ol(uint16_t *fb, int x, int y, const char *s, uint16_t color, uint16_t outline, int scale, int ol);
int gfx_text_width(const char *s, int scale);
// Column bitmaps (5 bytes, LSB on top) of a 5x7 glyph
const uint8_t *gfx_glyph5x7(char c);

// Large anti-aliased clock digits ("0123456789:" only, others are skipped as spaces)
#define FONT_FILL    1
#define FONT_OUTLINE 2
int font_width(const font_t *f, const char *s, int spacing);
// Same glyphs with a vertical gradient fill (top -> bottom), outline and an offset soft shadow
void font_draw_grad(uint16_t *fb, const font_t *f, int x, int y, const char *s, int spacing, uint16_t top,
                    uint16_t bottom, uint16_t outline, int shadow_off);

// Pixel-art clock digits (7x10 grid, "0123456789:-" and space), each grid cell = scale px.
// Banded gradient fill (grad[0] top .. grad[3] bottom), bevel highlight, outline and translucent shadow.
int pix_digits_width(const char *s, int scale);
void pix_digits_draw(uint16_t *fb, int x, int y, const char *s, int scale, const uint16_t grad[4], uint16_t outline);
// Anti-aliased proportional UI text; y = top of the line box
int ui_width(const uifont_t *f, const char *s);
void ui_text(uint16_t *fb, const uifont_t *f, int x, int y, const char *s, uint16_t fill, uint16_t outline, int mode);
// Additive-looking glow: blends `color` over a disc with alpha falling off from the center (max 0..16)
void gfx_glow(uint16_t *fb, int cx, int cy, int r, uint16_t color, int max_alpha);
// Antialiased capsule (segment with round ends; a == b draws a disc). Coordinates and radius in
// 1/16 px, amax = opacity 0..16.
void gfx_aa_capsule(uint16_t *fb, int ax, int ay, int bx, int by, int r, uint16_t c, int amax);
// Integer square root
int gfx_isqrt32(uint32_t v);
void font_draw(uint16_t *fb, const font_t *f, int x, int y, const char *s, int spacing,
               uint16_t fill, uint16_t outline, int mode);
