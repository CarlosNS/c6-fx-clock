// "El escultor" (full screen, wide canvas, pixel art on art.c): the daily steps goal as a sculpture. A
// sculptor carves a runner out of a block of stone; there are 11 scenes, one per 10 % of the goal
// (0-9 %, 10-19 % ... 90-99 %) with the carving coming down the block and the sculptor up on the
// scaffold where he works, and at 100 % or more the finished statue in marble and gold, with rays
// and sparkles, and the sculptor celebrating. The steps and the percentage read below.
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "app.h"
#include "art.h"
#include "control.h"
#include "esp_attr.h"
#include "fx.h"
#include "gfx.h"
#include "steps.h"

#define BX0 46   // the block of stone
#define BX1 96
#define BY0 52
#define BY1 128

static uint16_t C(uint32_t v) { return rgb565(v >> 16, (v >> 8) & 255, v & 255); }

EXT_RAM_BSS_ATTR static uint8_t s_mask[ART_H][ART_W];  // the runner statue
static bool s_built;

typedef struct {
    float x, y, vx, vy;
    int life;
} chip_t;
static chip_t s_chip[16];

void sculpt_enter(void)
{
    art_begin();
    if (s_built) return;
    // Draw the runner once on the (cleared) art canvas and keep its shape
    memset(art, 0, ART_W * ART_H * 2);
    const float ox = 50, oy = 58;
    uint16_t on = 0xFFFF;
    art_disc(ox + 20, oy + 7, 5, on);
    art_thick(ox + 20, oy + 14, ox + 17, oy + 38, 7, on);                                           // torso
    art_thick(ox + 19, oy + 17, ox + 28, oy + 25, 4, on), art_thick(ox + 28, oy + 25, ox + 34, oy + 18, 4, on);  // front arm
    art_thick(ox + 19, oy + 17, ox + 10, oy + 26, 4, on), art_thick(ox + 10, oy + 26, ox + 5, oy + 33, 4, on);   // back arm
    art_thick(ox + 17, oy + 38, ox + 28, oy + 50, 5, on), art_thick(ox + 28, oy + 50, ox + 26, oy + 66, 5, on);  // front leg
    art_thick(ox + 17, oy + 38, ox + 9, oy + 52, 5, on), art_thick(ox + 9, oy + 52, ox + 1, oy + 60, 5, on);     // back leg
    art_thick(ox - 2, oy + 68, ox + 40, oy + 68, 6, on);                                           // its base
    for (int y = 0; y < ART_H; y++)
        for (int x = 0; x < ART_W; x++) s_mask[y][x] = art[y * ART_W + x] == on;
    s_built = true;
}

static void thousands(char *b, size_t n, int v)
{
    if (v >= 1000) snprintf(b, n, "%d.%03d", v / 1000, v % 1000);
    else snprintf(b, n, "%d", v);
}

void sculpt_render(uint16_t *fb, uint32_t t, uint32_t dt)
{
    int dx_, dy_;
    app_take_drag(&dx_, &dy_);
    float ts = t / 1000.0f;
    int goal = g_app.step_goal > 0 ? g_app.step_goal : 10000, st = steps_today();
    int pct = (int)((int64_t)st * 100 / goal), k = pct >= 100 ? 10 : pct / 10;
    bool done = k >= 10;

    static const uint32_t DAY[] = {0x2b2340, 0x4a3550, 0x7a4f55, 0xb0705a, 0xd6996a}, GLORY[] = {0x2a3b7a, 0x4b5fa8, 0x8a8fd0, 0xf0c27a, 0xf7d98f};
    uint16_t sky[5];
    for (int i = 0; i < 5; i++) sky[i] = C(done ? GLORY[i] : DAY[i]);
    art_bands(sky, 5, 0, 140);
    // the atelier: three arches behind, the floor
    for (int a = 0; a < 3; a++) {
        int ax = 8 + a * 46;
        art_rect(ax, 60, 30, 80, done ? C(0x3a3f73) : C(0x3a2c40));
        uint16_t open = done ? C(0xf3cf86) : C(0xc88a62);
        for (int y = -9; y <= 0; y++)
            for (int x = -9; x <= 9; x++)
                if (x * x + y * y <= 81) art_px(ax + 15 + x, 78 + y, open);
        art_rect(ax + 6, 78, 18, 30, open);
    }
    for (int y = 138; y < ART_H; y++) art_rect(0, y, ART_W, 1, y < 142 ? C(0x8a7660) : (y % 6 < 3 ? C(0x6e5e4c) : C(0x665646)));
    // glory rays when it is finished
    if (done)
        for (int i = 0; i < 10; i++) {
            float a = ts * 0.2f + i * 3.14159f / 5;
            for (int r = 10; r < 140; r++)
                for (int w = -r / 14; w <= r / 14; w++)
                    art_tint((int)(70 + cosf(a) * r - sinf(a) * w), (int)(90 + sinf(a) * r + cosf(a) * w), C(0xffe9a8), 3);
        }
    // the plinth
    art_rect(40, 128, 62, 12, C(0x7d6a5a));
    art_rect(40, 128, 62, 1, C(0xb39c86));
    art_rect(44, 140, 54, 2, C(0x4c3f35));
    // block and statue: above the carving line only the statue is left, rough; finished, it shines
    int line = BY0 + (BY1 - BY0) * k / 10;
    for (int y = BY0; y < BY1; y++)
        for (int x = BX0; x < BX1; x++) {
            bool s = s_mask[y][x];
            if (done) {
                if (s) art_px(x, y, (x + y) % 7 == 0 ? C(0xfff4c2) : x < 70 ? C(0xf3e7d2) : C(0xd8c6aa));
            } else if (y < line) {
                if (s) art_px(x, y, (x * 3 + y) % 5 ? C(0xc9bba6) : C(0xa9987f));
            } else {
                art_px(x, y, (x * 7 + y * 3) % 11 == 0 ? C(0x5a4e44) : x < 52 ? C(0x9a8a76) : C(0x85776a));
            }
        }
    if (done) {  // gold along its lit side, and sparkles
        for (int y = BY0; y < BY1; y++)
            for (int x = BX0; x < BX1; x++)
                if (s_mask[y][x] && !s_mask[y][x - 1]) art_px(x, y, C(0xf9b25a));
        for (int i = 0; i < 6; i++) {
            float a = ts * 2 + i;
            int sx = (int)(70 + cosf(a * 1.3f) * 26), sy = (int)(85 + sinf(a) * 30);
            if (sinf(ts * 6 + i) > 0.3f) art_rect(sx, sy, 1, 3, C(0xffffff)), art_rect(sx - 1, sy + 1, 3, 1, C(0xffffff));
        }
    }
    // scaffold, and the sculptor at the carving height (on the ground, arms up, when it is finished)
    int sy = done ? 128 : (line + 6 < 60 ? 60 : line + 6 > 124 ? 124 : line + 6), sx = done ? 112 : 104;
    if (!done) {  // from the first blow: the carving starts at the top
        art_rect(101, 50, 2, 80, C(0x6b4a2f));
        art_rect(113, 50, 2, 80, C(0x6b4a2f));
        for (int y = 54; y < 128; y += 8) art_rect(101, y, 14, 1, C(0x8a6a45));
        art_rect(99, sy, 18, 2, C(0x9c7a50));
    }
    art_rect(sx + 2, sy - 12, 4, 4, C(0xe8b98f));
    art_rect(sx + 1, sy - 8, 6, 6, done ? C(0x3f6fb0) : C(0x7a3b3b));
    art_rect(sx + 1, sy - 2, 2, 2, C(0x3a2a2a));
    art_rect(sx + 5, sy - 2, 2, 2, C(0x3a2a2a));
    if (done) {
        art_rect(sx - 1, sy - 15, 2, 6, C(0xe8b98f));
        art_rect(sx + 7, sy - 15, 2, 6, C(0xe8b98f));
    } else {
        bool up = sinf(ts * 8) > 0;
        art_rect(sx - 3, sy - (up ? 10 : 7), 5, 2, C(0xe8b98f));  // the arm with the chisel
        art_rect(sx - 6, sy - (up ? 11 : 7), 3, 2, C(0xc0c0c8));
        static bool was_up;
        if (was_up && !up)  // each blow throws a few chips
            for (int i = 0; i < 16; i++)
                if (s_chip[i].life <= 0) {
                    uint32_t h = fx_rand();
                    s_chip[i] = (chip_t){sx - 8.0f, sy - 7.0f, -0.6f - (h % 100) / 100.0f, -0.8f - (h >> 8) % 100 / 100.0f, 18};
                    if ((h >> 16) % 3 == 0) break;
                }
        was_up = up;
    }
    for (int i = 0; i < 16; i++) {
        chip_t *c = &s_chip[i];
        if (c->life <= 0) continue;
        c->x += c->vx, c->y += c->vy, c->vy += 0.12f, c->life--;
        art_px((int)c->x, (int)c->y, C(0xd8ccb8));
    }
    art_blit(fb);

    // The time above, the steps below (full resolution)
    static uint16_t cream[4], gold[4];
    if (!cream[0]) {
        cream[0] = rgb565(255, 252, 240), cream[1] = rgb565(245, 235, 220), cream[2] = rgb565(220, 205, 185), cream[3] = rgb565(190, 172, 150);
        gold[0] = rgb565(255, 244, 200), gold[1] = rgb565(250, 205, 120), gold[2] = rgb565(232, 160, 70), gold[3] = rgb565(190, 110, 40);
    }
    char hhmm[8], n[16], line_s[48];
    clock_hhmm(hhmm, !g_now.valid || g_now.ms < 500);
    pix_digits_draw(fb, (WW - pix_digits_width("00:00", 4)) / 2, 14, hhmm, 4, cream, rgb565(26, 16, 34));
    thousands(n, sizeof(n), st);
    snprintf(line_s, sizeof(line_s), g_app.lang ? "%s steps  %d %%" : "%s pasos  %d %%", n, pct);
    ui_text(fb, &ui_m, (WW - ui_width(&ui_m, line_s)) / 2, 292, line_s, done ? gold[1] : cream[1], rgb565(26, 16, 34),
            FONT_FILL | FONT_OUTLINE);
}
