// "Lluvia" (portrait): a rainy night seen through a window. The city behind is out of focus
// (bokeh lights, blurred windows); drops on the glass act as tiny lenses showing the scene
// upside down, some slide down leaving trails; the time glows on a flickering neon sign.
#include <stdio.h>
#include <string.h>
#include "app.h"
#include "control.h"
#include "fx.h"
#include "gfx.h"

#define W LCD_W
#define H LCD_H
#define NDROPS 26

typedef struct { float x, y, v; uint8_t r; } drop_t;
static drop_t drops[NDROPS];
static uint16_t lens[17 * 17];

static void reset_drop(drop_t *d, bool anywhere)
{
    uint32_t h = fx_rand();
    d->x = (float)(h % W);
    d->y = anywhere ? (float)((h >> 9) % H) : -10;
    d->r = 2 + (h >> 20) % 5;
    d->v = 0;
}

void rain_enter(void)
{
    for (int i = 0; i < NDROPS; i++) reset_drop(&drops[i], true);
}

static void draw_drop(uint16_t *fb, int cx, int cy, int r)
{
    // Copy what is behind, then draw it inverted and brighter inside the drop (a tiny lens)
    int n = 0;
    for (int y = -r; y <= r; y++)
        for (int x = -r; x <= r; x++) {
            int px = cx + x, py = cy + y;
            lens[n++] = ((unsigned)px < W && (unsigned)py < H) ? fb[py * W + px] : 0;
        }
    int d = r * 2 + 1;
    for (int y = -r; y <= r; y++)
        for (int x = -r; x <= r; x++) {
            if (x * x + y * y > r * r) continue;
            int px = cx + x, py = cy + y;
            if ((unsigned)px >= W || (unsigned)py >= H) continue;
            uint16_t c = lens[(r - y) * d + (r - x)];  // flipped both ways
            c = gfx_blend(c, rgb565(255, 255, 255), 5);
            bool rim = x * x + y * y > (r - 1) * (r - 1);
            if (rim && y > 0) c = gfx_blend(c, 0, 9);                          // dark lower rim
            else if (rim && y < 0) c = gfx_blend(c, rgb565(220, 230, 255), 9);  // lit upper rim
            fb[py * W + px] = c;
        }
    int hx = cx - r / 2, hy = cy - r / 2;
    if (r > 2 && (unsigned)hx < W && (unsigned)hy < H) fb[hy * W + hx] = rgb565(255, 255, 255);  // highlight
}

void rain_render(uint16_t *fb, uint32_t t, uint32_t dt)
{
    int dx_, dy_;
    app_take_drag(&dx_, &dy_);

    // Night sky and blurred skyline
    for (int y = 0; y < H; y++) {
        int f = y * 256 / H;
        gfx_fill_rect(fb, 0, y, W, 1, rgb565(10 + f / 20, 12 + f / 24, 30 + f / 14));
    }
    for (int b = 0; b < 9; b++) {
        uint32_t h = fx_hash(b, 40);
        int bx = b * 20 - 6, bw = 22 + (int)(h % 10), top = 120 + (int)((h >> 8) % 90);
        gfx_fill_rect(fb, bx, top, bw, H - top, rgb565(20, 24, 44));
        for (int wy = top + 6; wy < H - 20; wy += 9)
            for (int wx = bx + 3; wx < bx + bw - 3; wx += 6)
                if (fx_hash(wx, wy) % 3 == 0) gfx_fill_rect(fb, wx, wy, 3, 4, rgb565(120, 100, 60));
    }
    // Bokeh: soft out-of-focus lights, slowly pulsing
    static const uint8_t bc[5][3] = {{255, 170, 80}, {255, 80, 120}, {80, 200, 255}, {255, 220, 140}, {120, 255, 160}};
    for (int i = 0; i < 18; i++) {
        uint32_t h = fx_hash(i, 41);
        int x = (int)(h % W), y = 150 + (int)((h >> 8) % 160), r = 6 + (int)((h >> 16) % 10);
        int a = 4 + (sin_u8[(uint8_t)(t / 20 + i * 47)] >> 6);
        const uint8_t *c = bc[(h >> 24) % 5];
        gfx_glow(fb, x, y, r, rgb565(c[0], c[1], c[2]), a);
    }
    // Rain streaks outside
    for (int i = 0; i < 40; i++) {
        uint32_t h = fx_hash(i, 42);
        int x = (int)(h % (W + 40)) - 20, y = (int)((t / 2 + (h >> 8)) % (H + 40)) - 20;
        gfx_line(fb, x, y, x + 3, y + 12, rgb565(80, 90, 120));
    }

    // Neon sign with the time (buzzing flicker now and then)
    bool off = (fx_hash(t / 90, 7) % 37) == 0;
    uint16_t neon = off ? rgb565(80, 30, 60) : rgb565(255, 90, 190);
    clock_row_grad(fb, &font_seg_m, W / 2, 30, -2, off ? neon : rgb565(255, 230, 250), neon, rgb565(60, 0, 40), 0);
    char date[24];
    clock_date_str(date);
    ui_text(fb, &ui_m, (W - ui_width(&ui_m, date)) / 2, 98, date, rgb565(120, 230, 255), rgb565(0, 30, 60),
            FONT_FILL | FONT_OUTLINE);

    // Drops on the glass
    float dts = dt / 1000.0f;
    for (int i = 0; i < NDROPS; i++) {
        drop_t *d = &drops[i];
        // Big drops start sliding, accelerate, and leave tiny droplets behind
        if (d->r >= 5 || d->v > 0) {
            d->v += 60 * dts;
            if (d->v > 90) d->v = 90;
            d->y += d->v * dts;
            d->x += (fx_hash(i, t / 200) & 1 ? 0.3f : -0.3f);
            if ((fx_hash(i, (uint32_t)d->y) & 7) == 0) {
                int tx = (int)d->x, ty = (int)d->y - d->r - 2;
                if ((unsigned)tx < W && (unsigned)ty < H) fb[ty * W + tx] = rgb565(150, 160, 190);
            }
        } else if (fx_rand() % 500 == 0) {
            d->r++;  // drops slowly grow until they slide
        }
        if (d->y > H + 10) reset_drop(d, false);
        draw_drop(fb, (int)d->x, (int)d->y, d->r);
    }
}
