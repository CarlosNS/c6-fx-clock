// "Coloso" (full screen, wide canvas, pixel art on art.c): a colossal hourglass seen from below,
// between two stone pillars that converge into a desert dusk sky, with clouds drifting across its
// waist. A tiny cloaked figure at its foot looks up. The sand takes an hour to fall: the heap in the
// near (huge) bulb is the minutes gone, the far bulb what is left. The time is carved on the base.
#include <math.h>
#include <stdio.h>
#include "app.h"
#include "art.h"
#include "control.h"
#include "fx.h"
#include "gfx.h"

#define CX 68
#define NECK 56    // art y of the neck (high up: the near bulb fills the view)
#define BASE 132   // art y where the glass meets the base

static uint16_t C(uint32_t v) { return rgb565(v >> 16, (v >> 8) & 255, v & 255); }

void sand_enter(void) { art_begin(); }

static float lower_w(int y)
{
    float s = (float)(y - NECK - 1) / (BASE - NECK);
    float v = sinf(s * 3.14159f * 0.72f);
    return 3 + 120 * sqrtf(v > 0 ? v : 0);
}

static float upper_w(int y)
{
    float s = (float)(NECK - y) / (NECK - 8);
    float v = sinf(s * 3.14159f * 0.75f);
    return 3 + 34 * sqrtf(v > 0 ? v : 0);
}

void sand_render(uint16_t *fb, uint32_t t, uint32_t dt)
{
    int dx_, dy_;
    app_take_drag(&dx_, &dy_);
    float ts = t / 1000.0f;
    static const uint32_t SKY[] = {0x120b2e, 0x241447, 0x432060, 0x7a3466, 0xb84d5e, 0xe57a4c, 0xf4b162};
    uint16_t sky[7];
    for (int i = 0; i < 7; i++) sky[i] = C(SKY[i]);
    art_bands(sky, 7, 0, 150);
    for (int i = 0; i < 40; i++) {  // stars in the high sky, twinkling
        uint32_t h = fx_hash(i, 77);
        int x = h % ART_W, y = (h >> 8) % 60;
        if (sinf(ts * 2 + i * 1.7f) > -0.3f) art_px(x, y, (h >> 20) & 3 ? C(0x9a8fc0) : C(0xf5ebdc));
    }
    // dunes far away, then the ground
    for (int x = 0; x < ART_W; x++) {
        int h = (int)(6 + sinf(x * 0.07f) * 3 + sinf(x * 0.19f + 1) * 2);
        art_rect(x, 150 - h, 1, h, C(0xa8563f));
        art_px(x, 150 - h, C(0xd77a52));
    }
    for (int y = 150; y < ART_H; y++) art_rect(0, y, ART_W, 1, y < 156 ? C(0xc98a55) : y < 162 ? C(0xb0703f) : C(0x8f5733));
    // the pillars, converging upwards (seen from below)
    for (int side = -1; side <= 1; side += 2)
        for (int y = 0; y < 150; y++) {
            float s = y / 150.0f, w = 4 + 22 * s * s;
            float in = side < 0 ? 10 + (1 - s) * 44 : ART_W - 10 - (1 - s) * 44;
            int x0 = (int)(side < 0 ? in - w : in);
            int block = (int)((150 - y) / (6 + 10 * s)) & 1;
            art_rect(x0, y, (int)w + 1, 1, block ? C(0x3c2a4a) : C(0x47324f));
            art_px((int)(side < 0 ? in - 1 : in), y, C(0x6b4f63));  // the edge lit by the sky
        }
    // caps: small and far above, wide and heavy below
    art_rect(50, 4, 37, 4, C(0x2b1d38));
    art_rect(50, 4, 37, 1, C(0x6b4f63));
    art_rect(6, BASE, 125, 18, C(0x2b1d38));
    art_rect(6, BASE, 125, 1, C(0x7c5c6e));
    art_rect(6, BASE + 17, 125, 1, C(0x1a1024));
    // glass and sand
    float p = g_now.valid ? (g_now.m * 60000.0f + g_now.minute_ms) / 3600000.0f : 0.5f;
    float hU = (1 - p) * 30, hL = 3 + p * 48, mound = 7 * (p * 3 < 1 ? p * 3 : 1);
    for (int y = 8; y < BASE; y++) {
        float w = y <= NECK ? upper_w(y) : lower_w(y), x0 = CX - w / 2;
        for (int x = (int)floorf(x0); x < x0 + w; x++) {
            float d = fabsf((float)(x - CX)) / (w / 2 + 0.01f);
            uint16_t col = (x + y) % 3 ? C(0x26304f) : C(0x2c3860);  // glass, the dark sky behind
            if (y <= NECK) {
                if (y > NECK - hU + (1 - d) * 4) col = (x * 7 + y * 3) % 5 ? C(0xe0a45c) : C(0xc98a48);
            } else if (y > BASE - hL - (1 - d) * mound) {
                col = (x * 5 + y * 7) % 6 ? C(0xe0a45c) : C(0xf2c27a);
            }
            art_px(x, y, col);
        }
        art_px((int)x0, y, C(0x8fa6d8));
        art_px((int)(x0 + w - 1), y, C(0x5a6aa0));
        if (y % 4 < 2) art_px((int)x0 + 3, y, C(0xb9c9f0));  // the glass catching the light
    }
    // the falling thread
    float top = BASE - hL - mound;
    for (int y = NECK + 1; y < top; y++)
        if ((y + (int)(ts * 30)) % 3) art_px(CX, y, C(0xf2c27a));
    // clouds drifting across its waist: it rises above them
    for (int i = 0; i < 5; i++) {
        int x = (int)fmodf(i * 41 + ts * (4 + i), ART_W + 50) - 25, y = 26 + (i % 3) * 7;
        uint16_t c = i & 1 ? C(0xe9a17a) : C(0xd98a72);
        for (int j = 0; j < 30; j++) art_tint(x + j, y, c, 12), art_tint(x + j, y + 1, c, 12), art_tint(x + j, y + 2, c, 12);
        for (int j = 6; j < 22; j++) art_tint(x + j, y - 1, c, 12), art_tint(x + j, y - 2, c, 12);
        for (int j = 3; j < 29; j++) art_tint(x + j, y + 3, C(0xa0566a), 12);
    }
    // the tiny figure looking up, its cloak in the wind, and a long shadow
    int fx = CX - 22, fy = 156;
    art_rect(fx + 1, fy + 8, 14, 1, C(0x6e4227));
    art_rect(fx, fy, 2, 1, C(0x1b1022));
    art_rect(fx - 1, fy + 1, 3, 5, C(0x1b1022));
    art_rect(fx + 2, fy + 2 + (sinf(ts * 3) > 0), 2, 3, C(0x7a2a3a));
    art_rect(fx - 1, fy + 6, 1, 2, C(0x1b1022));
    art_rect(fx + 1, fy + 6, 1, 2, C(0x1b1022));
    for (int i = 0; i < 14; i++)  // sand blown along the ground
        art_rect((int)fmodf(i * 37 + ts * 40, ART_W), 151 + (i * 13) % 16, 2, 1, C(0xe8b47a));
    art_blit(fb);

    // The time, carved in gold on the base (full resolution)
    static const uint16_t *gp;
    static uint16_t gold[4];
    if (!gp) gold[0] = rgb565(255, 244, 210), gold[1] = rgb565(250, 205, 120), gold[2] = rgb565(232, 160, 70),
             gold[3] = rgb565(190, 110, 40), gp = gold;
    char hhmm[8];
    clock_hhmm(hhmm, !g_now.valid || g_now.ms < 500);
    pix_digits_draw(fb, (WW - pix_digits_width("00:00", 3)) / 2, BASE * 2 + 4, hhmm, 3, gold, rgb565(30, 16, 36));
}
