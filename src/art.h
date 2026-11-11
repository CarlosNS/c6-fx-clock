// Pixel-art canvas for faces (art.c): 137 x 168 "art pixels", each shown as 2 x 2 pixels of the wide
// canvas (so 3 x 3 on the panel). Faces drawn in chunky pixel art (Coloso, Engranajes, Disfruta, El
// escultor) paint here and then art_blit() puts it on the wide framebuffer; the time goes on top at
// full resolution.
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define ART_W 137
#define ART_H 168

extern uint16_t *art;  // ART_W x ART_H, rgb565 in panel order (art_begin allocates it once)

void art_begin(void);
static inline void art_px(int x, int y, uint16_t c)
{
    if ((unsigned)x < ART_W && (unsigned)y < ART_H) art[y * ART_W + x] = c;
}
static inline uint16_t art_get(int x, int y)
{
    return (unsigned)x < ART_W && (unsigned)y < ART_H ? art[y * ART_W + x] : 0;
}
void art_rect(int x, int y, int w, int h, uint16_t c);
// Vertical bands from cols[0] (y0) to cols[n-1] (y1), with a checkerboard where two bands meet
void art_bands(const uint16_t *cols, int n, int y0, int y1);
void art_disc(float cx, float cy, float r, uint16_t c);
void art_line(float x0, float y0, float x1, float y1, uint16_t c);
// A thick line (round ends)
void art_thick(float x0, float y0, float x1, float y1, float w, uint16_t c);
// Filled polygon (xy pairs, n points)
void art_poly(const float *xy, int n, uint16_t c);
// alpha 0..16 over what is there
void art_tint(int x, int y, uint16_t c, int alpha);
// Doubles the art canvas onto the wide framebuffer (WW x WH)
void art_blit(uint16_t *fb);
