// "Synthwave" (landscape): a retro-futuristic sunset. Striped sun sinking behind neon-edged
// mountains, a scrolling perspective grid, palm silhouettes and chrome time digits.
#include <stdio.h>
#include "app.h"
#include "control.h"
#include "fx.h"
#include "gfx.h"

#define W LAND_W
#define H LAND_H
#define HORIZON 104

void synth_enter(void) {}

static void palm(uint16_t *fb, int x, int base, int h, int dir, uint32_t t)
{
    uint16_t c = rgb565(14, 0, 26);
    int tx = x + dir * 10, ty = base - h;
    for (int k = 0; k < h; k += 2) {  // slightly curved trunk
        int cx = x + dir * 10 * k * k / (h * h);
        gfx_fill_rect(fb, cx - 2 + k / 40, base - k, 4 - k / 30, 2, c);
    }
    int sway = (sin_q14[(t / 6 + x * 7) & 1023] * 3) >> 14;
    // Fronds arch up and droop at the tips, with leaflets hanging from them
    static const int8_t fr[7][2] = {{-30, 4}, {-24, -6}, {-10, -12}, {8, -12}, {24, -6}, {30, 4}, {4, 6}};
    for (int f = 0; f < 7; f++) {
        int px = tx, py = ty;
        for (int s = 1; s <= 8; s++) {
            int fx = tx + fr[f][0] * s / 8 + sway * s / 8;
            int fy = ty + fr[f][1] * s / 8 - (s * (8 - s)) / 3 + (s * s) / 6;  // arch, then droop
            for (int w = 0; w < 2; w++) gfx_line(fb, px, py + w, fx, fy + w, c);
            if (s > 2) gfx_line(fb, fx, fy, fx + (fr[f][0] > 0 ? -1 : 1), fy + 4 + s / 2, c);  // leaflet
            px = fx;
            py = fy;
        }
    }
    gfx_circle(fb, tx, ty + 2, 3, c);  // coconuts
}

void synth_render(uint16_t *fb, uint32_t t, uint32_t dt)
{
    int dx_, dy_;
    app_take_drag(&dx_, &dy_);

    // Sky: violet to magenta to orange at the horizon
    for (int y = 0; y < HORIZON; y++) {
        int f = y * 256 / HORIZON;
        int r = f < 160 ? 20 + f * 160 / 160 : 180 + (f - 160) * 70 / 96;
        int g = f < 160 ? f / 8 : 20 + (f - 160) * 80 / 96;
        int b = f < 160 ? 50 + f * 90 / 160 : 140 - (f - 160) * 80 / 96;
        gfx_fill_rect(fb, 0, y, W, 1, rgb565(r, g, b));
    }
    for (int i = 0; i < 40; i++) {
        uint32_t h = fx_hash(i, 2);
        int x = h % W, y = (h >> 9) % 60;
        if (sin_u8[(uint8_t)(t / 14 + i * 33)] > 120) fb[y * W + x] = rgb565(255, 220, 255);
    }

    // Sun with scan-line cuts that slide downwards
    int sx = W / 2, sy = 86, sr = 46;
    gfx_glow(fb, sx, sy, sr + 22, rgb565(255, 80, 160), 6);
    int slide = (t / 60) % 12;
    for (int y = -sr; y <= sr; y++) {
        int py = sy + y;
        if (py >= HORIZON) break;
        if (y > -6) {  // lower half: stripes that get thicker towards the bottom
            int period = 12, gap = 1 + (y + 6) * 5 / sr;
            if (((y + slide) % period + period) % period < gap) continue;
        }
        int w = 0;
        while ((w + 1) * (w + 1) + y * y <= sr * sr) w++;
        int f = (y + sr) * 256 / (2 * sr);
        gfx_fill_rect(fb, sx - w, py, w * 2, 1, rgb565(255, 230 - f * 170 / 256, 90 + f * 60 / 256));
    }

    // Mountains: dark with neon edges, one range each side
    for (int x = 0; x < W; x++) {
        int d = x < W / 2 ? x : W - 1 - x;
        int h = HORIZON - 4 - ((sin_q14[(d * 9 + 100) & 1023] * 16) >> 14) - (d < 110 ? (110 - d) / 3 : 0) -
                ((abs(sin_q14[(d * 23) & 1023]) * 6) >> 14);
        if (d > 120) h = HORIZON - ((d > 140 ? 0 : 140 - d) / 5);
        for (int y = h; y < HORIZON; y++) fb[y * W + x] = rgb565(30, 4, 54);
        if (h < HORIZON) fb[h * W + x] = rgb565(80, 240, 255);
    }

    // Perspective grid floor
    for (int y = HORIZON; y < H; y++) gfx_fill_rect(fb, 0, y, W, 1, rgb565(16, 0, 34 + (y - HORIZON) / 3));
    uint16_t grid = rgb565(255, 60, 200), grid_glow = rgb565(140, 20, 120);
    int phase = (t / 8) % 16;  // horizontal lines at evenly spaced depths, rushing towards the viewer
    for (int k = 1; k < 40; k++) {
        int z = k * 16 - phase;
        if (z <= 2) continue;
        int y = HORIZON + 1100 / z;
        if (y <= HORIZON || y >= H) continue;
        gfx_fill_rect(fb, 0, y, W, 1, grid);
        if (y + 1 < H) gfx_fill_rect(fb, 0, y + 1, W, 1, grid_glow);
    }
    for (int k = -16; k <= 16; k++) {
        int xb = W / 2 + k * 40;
        gfx_line(fb, W / 2 + k * 3, HORIZON, xb, H - 1, k ? grid : rgb565(120, 240, 255));
    }
    gfx_fill_rect(fb, 0, HORIZON, W, 1, rgb565(255, 200, 255));

    palm(fb, 28, H, 96, 1, t);
    palm(fb, 292, H, 104, -1, t);
    palm(fb, 58, H, 64, 1, t + 400);

    // Chrome time with a neon glow
    clock_row_grad(fb, &font_round_m, W / 2, 6, -4, rgb565(220, 250, 255), rgb565(255, 70, 190), rgb565(30, 0, 50), 3);
    char date[24];
    clock_date_str(date);
    ui_text(fb, &ui_s, (W - ui_width(&ui_s, date)) / 2, 70, date, rgb565(120, 240, 255), rgb565(30, 0, 50),
            FONT_FILL | FONT_OUTLINE);
}
