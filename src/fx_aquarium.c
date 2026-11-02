// "Acuario" (landscape): an aquarium with light shafts, caustics dancing on the sand, swaying
// seaweed, fish at two depths and bubbles rising from a little treasure chest.
#include <stdio.h>
#include "app.h"
#include "control.h"
#include "fx.h"
#include "gfx.h"

#define W LAND_W
#define H LAND_H
#define FLOOR 142

static const struct { uint8_t r, g, b, r2, g2, b2; } fish_col[] = {
    {255, 140, 40, 255, 255, 255}, {60, 120, 255, 255, 220, 60}, {250, 220, 60, 40, 40, 60},
    {255, 90, 120, 255, 200, 210}, {120, 230, 160, 40, 120, 90}, {200, 120, 255, 255, 255, 255},
};

void aquarium_enter(void) {}

static inline int wsin(int a, int amp)
{
    return (sin_q14[a & 1023] * amp) >> 14;
}

static void fish(uint16_t *fb, int x, int y, int len, int dir, int kind, uint32_t t, int depth)
{
    uint16_t body = rgb565(fish_col[kind].r, fish_col[kind].g, fish_col[kind].b);
    uint16_t stripe = rgb565(fish_col[kind].r2, fish_col[kind].g2, fish_col[kind].b2);
    uint16_t water = rgb565(20, 80, 120);
    if (depth) { body = gfx_blend(body, water, 8); stripe = gfx_blend(stripe, water, 8); }
    int hh = len * 2 / 5;
    for (int k = -len / 2; k <= len / 2; k++) {  // body: a lens shape
        int w = hh * (len * len / 4 - k * k) / (len * len / 4) / 2;
        int px = x + k * dir;
        gfx_fill_rect(fb, px, y - w, 1, w * 2 + 1, body);
        if (k > -len / 6 && k < -len / 6 + 2) gfx_fill_rect(fb, px, y - w, 1, w * 2 + 1, stripe);
        if (k > len / 6 && k < len / 6 + 2) gfx_fill_rect(fb, px, y - w, 1, w * 2 + 1, stripe);
    }
    int flap = wsin(t + x * 7, hh / 2 + 1);
    int tx = x - dir * len / 2;
    gfx_tri(fb, tx, y, tx - dir * len / 3, y - hh / 2 - 2 + flap, tx - dir * len / 3, y + hh / 2 + 2 + flap, body);
    gfx_tri(fb, x, y - hh / 2, x - dir * len / 4, y - hh / 2 - 4, x + dir * len / 6, y - hh / 2 + 1, stripe);  // dorsal fin
    gfx_circle(fb, x + dir * (len / 2 - 4), y - 1, 2, rgb565(255, 255, 255));
    gfx_fill_rect(fb, x + dir * (len / 2 - 4), y - 1, 1, 1, rgb565(0, 0, 0));
}

void aquarium_render(uint16_t *fb, uint32_t t, uint32_t dt)
{
    int dx_, dy_;
    app_take_drag(&dx_, &dy_);

    // Water: bright near the surface, deep blue below
    for (int y = 0; y < FLOOR; y++) {
        int f = y * 256 / FLOOR;
        gfx_fill_rect(fb, 0, y, W, 1, rgb565(40 - f / 10, 150 - f / 3, 190 - f / 5));
    }
    // Surface ripple line
    for (int x = 0; x < W; x++) {
        int y = 3 + wsin(x * 12 + t / 3, 2);
        gfx_fill_rect(fb, x, 0, 1, y, rgb565(170, 230, 250));
    }
    // Light shafts slanting down, slowly drifting
    for (int s = 0; s < 5; s++) {
        int x0 = (int)((s * 83 + t / 40) % (W + 80)) - 40;
        for (int y = 4; y < FLOOR; y += 1) {
            int x = x0 + y / 3, w = 10 + y / 10;
            int a = 3 - y * 3 / FLOOR;
            if (a > 0) for (int k = 0; k < w; k++) if ((unsigned)(x + k) < W) fb[y * W + x + k] = gfx_blend(fb[y * W + x + k], rgb565(220, 255, 255), a);
        }
    }

    // Sand with caustics
    for (int y = FLOOR; y < H; y++) {
        uint16_t *row = fb + y * W;
        for (int x = 0; x < W; x++) {
            int c = wsin(x * 9 + t / 4, 64) + wsin(x * 5 - y * 20 + t / 6, 64) + wsin(y * 30 + x * 3 - t / 5, 64);
            int g = 196 + (int)(fx_hash(x >> 1, y >> 1) & 15) - (y - FLOOR);
            uint16_t sand = rgb565(g, g * 85 / 100, g * 60 / 100);
            row[x] = c > 90 ? gfx_blend(sand, rgb565(255, 255, 230), (c - 90) / 6 > 10 ? 10 : (c - 90) / 6) : sand;
        }
    }

    // Seaweed swaying from the sand
    for (int i = 0; i < 9; i++) {
        uint32_t h = fx_hash(i, 12);
        int bx = 10 + i * 36 + (int)(h % 14), height = 50 + (int)((h >> 8) % 60);
        uint16_t c = (h >> 16) & 1 ? rgb565(40, 150, 80) : rgb565(70, 170, 70);
        int px = bx, py = FLOOR + 6;
        for (int k = 1; k <= 12; k++) {
            int y = FLOOR + 6 - k * height / 12;
            int x = bx + wsin(t / 3 + k * 60 + i * 100, k * 3 / 2);
            for (int w = -1; w <= 1; w++) gfx_line(fb, px + w, py, x + w, y, c);
            px = x;
            py = y;
        }
    }
    // Rocks and a treasure chest
    gfx_circle(fb, 250, FLOOR + 8, 16, rgb565(90, 96, 110));
    gfx_circle(fb, 270, FLOOR + 12, 11, rgb565(110, 116, 128));
    gfx_fill_rect(fb, 60, FLOOR - 4, 34, 18, rgb565(120, 70, 30));
    gfx_fill_rect(fb, 60, FLOOR - 10, 34, 6, rgb565(150, 90, 40));
    gfx_fill_rect(fb, 75, FLOOR - 4, 4, 6, rgb565(240, 200, 60));
    gfx_glow(fb, 77, FLOOR - 6, 12, rgb565(255, 220, 90), 6);

    // Fish: far school first, then the big ones in front
    for (int i = 0; i < 8; i++) {
        int dir = i & 1 ? 1 : -1, span = W + 80;
        int x = (int)((t / (28 + i * 3) + i * 57) % span) - 40;
        if (dir < 0) x = W - x;
        fish(fb, x, 40 + i * 9 + wsin(t / 6 + i * 90, 5), 12, dir, i % 6, t / 2, 1);
    }
    for (int i = 0; i < 4; i++) {
        int dir = i & 1 ? -1 : 1, span = W + 120;
        int x = (int)((t / (14 + i * 4) + i * 113) % span) - 60;
        if (dir < 0) x = W - x;
        fish(fb, x, 76 + i * 16 + wsin(t / 5 + i * 200, 8), 26 + i * 3, dir, (i * 2 + 1) % 6, t, 0);
    }

    // Bubbles rising from the chest, wobbling
    for (int i = 0; i < 14; i++) {
        uint32_t h = fx_hash(i, 30);
        int life = (int)((t / 6 + h % 300) % 300);
        int y = FLOOR - 10 - life * (FLOOR - 10) / 300, x = 78 + wsin(life * 12 + i * 90, 4) + (int)(h % 7) - 3;
        int r = 1 + life / 90 + (int)(h % 2);
        gfx_ring(fb, x, y, r, r + 1, rgb565(210, 245, 255));
        fb[(y - r / 2) * W + x - r / 2] = rgb565(255, 255, 255);
    }

    clock_row_grad(fb, &font_round_m, W / 2 + 60, 14, -4, rgb565(240, 255, 255), rgb565(150, 230, 255), rgb565(10, 50, 80), 2);
    char date[24];
    clock_date_str(date);
    ui_text(fb, &ui_s, W / 2 + 60 - ui_width(&ui_s, date) / 2, 78, date, rgb565(220, 250, 255), rgb565(10, 50, 80),
            FONT_FILL | FONT_OUTLINE);
}
