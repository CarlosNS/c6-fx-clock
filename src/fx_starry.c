// "Estrellada" (landscape): after Van Gogh's The Starry Night (1889, public domain).
// The sky is a field of brush-stroke bands bent by a turning vortex and slow domain warping;
// stars and the crescent moon pulse inside concentric halos, the cypress sways like a flame
// and the village windows flicker.
#include <stdio.h>
#include "app.h"
#include "control.h"
#include "fx.h"
#include "gfx.h"

#define W LAND_W
#define H LAND_H
#define SW (W / 2)  // sky computed at half resolution
#define SH (H / 2)

static const uint8_t sky_pal[8][3] = {
    {16, 30, 84}, {28, 52, 128}, {44, 82, 170}, {78, 120, 196},
    {130, 170, 214}, {196, 214, 210}, {92, 132, 200}, {34, 64, 146},
};
static const struct { int16_t x, y, r; } stars[] = {
    {40, 22, 6}, {108, 14, 7}, {150, 46, 5}, {196, 30, 6}, {232, 58, 5}, {258, 22, 7}, {182, 70, 4}, {84, 64, 5},
};

void starry_enter(void) {}

static void halo(uint16_t *fb, int x, int y, int r, uint32_t t, int i)
{
    int p = sin_u8[(uint8_t)(t / 12 + i * 40)] >> 6;  // 0..3 px of breathing
    gfx_ring(fb, x, y, r * 2 + 1 + p, r * 2 + 3 + p, rgb565(150, 170, 120));
    gfx_ring(fb, x, y, r + 3 + p / 2, r * 2 + 1 + p, rgb565(214, 214, 150));
    gfx_circle(fb, x, y, r + 2, rgb565(246, 232, 140));
    gfx_circle(fb, x, y, r - 1, rgb565(255, 252, 214));
}

void starry_render(uint16_t *fb, uint32_t t, uint32_t dt)
{
    int dx_, dy_;
    app_take_drag(&dx_, &dy_);

    // ---- Sky: warped stroke bands with a vortex near the center-left
    int ph1 = t / 9, ph2 = t / 13;
    int vx = 74, vy = 30;
    for (int sy = 0; sy < SH; sy++) {
        int warp_x = (sin_q14[(sy * 23 + ph1) & 1023] * 5) >> 14;
        uint16_t *r0 = fb + (sy * 2) * W, *r1 = r0 + W;
        for (int sx = 0; sx < SW; sx++) {
            int wy = sy + ((sin_q14[(sx * 13 - ph2) & 1023] * 3) >> 14);
            int wx = sx + warp_x;
            int dx = wx - vx, dy = wy - vy;
            int r2 = dx * dx + dy * dy;
            // Rotation that fades with distance: the bands curl into a spiral at the center
            int a = (int)(220000 / (r2 + 260)) + (r2 < 900 ? t / 6 : 0);
            int s = sin_q14[a & 1023], c = sin_q14[(a + 256) & 1023];
            int ry = (dx * s + dy * c) >> 14;
            int v = ry * 3 + (sx >> 2) + (wy >> 1);
            uint32_t h = fx_hash(sx >> 1, sy);  // brush-stroke speckle along the band
            int idx = ((v >> 2) + (h & 1)) & 7;
            const uint8_t *col = sky_pal[idx];
            uint16_t c565 = rgb565(col[0], col[1], col[2]);
            r0[sx * 2] = r0[sx * 2 + 1] = r1[sx * 2] = r1[sx * 2 + 1] = c565;
        }
    }

    // ---- Stars and moon in their halos
    for (unsigned i = 0; i < sizeof(stars) / sizeof(stars[0]); i++) halo(fb, stars[i].x, stars[i].y, stars[i].r, t, i);
    int mx = 292, my = 26;
    int mp = sin_u8[(uint8_t)(t / 16)] >> 6;
    gfx_ring(fb, mx, my, 18 + mp, 22 + mp, rgb565(190, 170, 90));
    gfx_ring(fb, mx, my, 13, 18 + mp, rgb565(232, 196, 92));
    gfx_circle(fb, mx, my, 12, rgb565(255, 216, 90));
    gfx_circle(fb, mx + 5, my - 3, 10, rgb565(232, 196, 92));  // crescent bite

    // ---- Rolling hills with banded strokes
    for (int x = 0; x < W; x++) {
        int h1 = 112 + ((sin_q14[(x * 3 + 100) & 1023] * 8) >> 14) + ((sin_q14[(x * 7) & 1023] * 3) >> 14);
        for (int y = h1; y < H; y++) {
            int band = ((y - h1) + ((sin_q14[(x * 9 + y * 20) & 1023] * 2) >> 14)) / 3;
            static const uint8_t hp[4][3] = {{40, 62, 110}, {28, 46, 92}, {52, 82, 120}, {22, 36, 70}};
            const uint8_t *c = hp[(band + (x >> 4)) & 3];
            fb[y * W + x] = rgb565(c[0], c[1], c[2]);
        }
    }

    // ---- Village: houses with flickering windows and a church spire
    for (int i = 0; i < 16; i++) {
        uint32_t h = fx_hash(i, 5);
        int hx = 96 + i * 13 + (int)(h % 5), hw = 9 + (int)(h % 4), hh = 7 + (int)((h >> 8) % 5);
        int base = 150 + (int)((h >> 12) % 6);
        uint16_t wall = (h & 1) ? rgb565(60, 72, 96) : rgb565(78, 84, 104);
        gfx_fill_rect(fb, hx, base - hh, hw, hh, wall);
        gfx_tri(fb, hx - 1, base - hh, hx + hw, base - hh, hx + hw / 2, base - hh - 5, rgb565(36, 46, 80));
        bool lit = sin_u8[(uint8_t)(t / 30 + i * 53)] > 70;
        gfx_fill_rect(fb, hx + 2, base - hh + 2, 2, 2, lit ? rgb565(255, 220, 90) : rgb565(150, 120, 60));
    }
    gfx_fill_rect(fb, 186, 128, 7, 22, rgb565(60, 70, 98));
    gfx_tri(fb, 184, 128, 195, 128, 189, 96, rgb565(46, 56, 90));

    // ---- Cypress: a dark flame that sways from the base up
    int16_t poly[2 * 48];
    int n = 0;
    for (int k = 0; k <= 22; k++) {  // right edge, bottom to top
        int y = H - k * 7;
        int w = 18 - k * 16 / 22 + (sin_u8[(uint8_t)(k * 37 + t / 8)] >> 6);
        int sway = (sin_q14[(t / 4 + k * 30) & 1023] * k / 3) >> 14;
        poly[n * 2] = 40 + sway + w;
        poly[n * 2 + 1] = y;
        n++;
    }
    for (int k = 22; k >= 0; k--) {  // left edge, top to bottom
        int y = H - k * 7;
        int w = 18 - k * 16 / 22 + (sin_u8[(uint8_t)(k * 53 + t / 7)] >> 6);
        int sway = (sin_q14[(t / 4 + k * 30) & 1023] * k / 3) >> 14;
        poly[n * 2] = 40 + sway - w;
        poly[n * 2 + 1] = y;
        n++;
    }
    gfx_poly(fb, poly, n, rgb565(18, 30, 24));
    for (int s = 0; s < 6; s++) {  // inner strokes
        uint16_t c = s & 1 ? rgb565(54, 70, 40) : rgb565(36, 52, 36);
        int px = 40 + (s - 3) * 4, py = H;
        for (int k = 1; k < 20; k++) {
            int y = H - k * 7;
            int sway = (sin_q14[(t / 4 + k * 30) & 1023] * k / 3) >> 14;
            int x = 40 + sway + (s - 3) * (18 - k * 16 / 22) / 4 + ((sin_q14[(k * 90 + s * 200) & 1023] * 2) >> 14);
            gfx_line(fb, px, py, x, y, c);
            px = x;
            py = y;
        }
    }

    // ---- Clock: warm yellow over the night blues
    clock_row_grad(fb, &font_round_m, 182, 92, -4, rgb565(255, 244, 170), rgb565(240, 176, 60), rgb565(14, 22, 60), 2);
    char date[24];
    clock_date_str(date);
    ui_text(fb, &ui_s, 182 - ui_width(&ui_s, date) / 2, 156, date, rgb565(255, 236, 160), rgb565(14, 22, 60),
            FONT_FILL | FONT_OUTLINE);
}
