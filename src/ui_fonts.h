#pragma once
#include <stdint.h>

// Bitmap fonts of the Marea Solsticial style (tools/make_fonts.py generates ui_fonts.c). One bit per
// pixel, rows packed little-endian (bit 0 = leftmost pixel). Drawn by ui.c, enlarged by whole numbers.

typedef struct {
    uint16_t cp;      // Unicode code point
    uint8_t w, h;     // bitmap size
    int8_t xo, yo;    // bitmap offset from the pen position and from the top of the line
    uint8_t adv;      // advance width
    uint32_t off;     // offset of the first row in the font's bits
} ui_glyph_t;

typedef struct {
    const ui_glyph_t *glyphs;  // sorted by code point
    uint16_t count;
    const uint8_t *bits;
    uint8_t ascent, descent;   // line height = ascent + descent
} ui_font_t;

extern const ui_font_t ui_jersey;  // Jersey 10: titles, numbers, buttons (capitals 10 px)
extern const ui_font_t ui_vt;      // VT323 at 25 px: labels, data, text (capitals 14 px)
