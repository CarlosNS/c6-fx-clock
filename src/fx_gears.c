// "Engranajes" (full screen, wide canvas, pixel art on art.c): inside a gothic clock tower. Huge bronze
// and iron gears turn in jerks, once a second, each at the speed its size gives it; chains with
// counterweights hang from the dark, a pendulum swings under the dial (Roman numerals, real time), and
// moonlight falls through a pointed window with dust floating in the beam. The time also reads below.
// An original design: no game's art is copied.
#include <math.h>
#include <stdio.h>
#include "app.h"
#include "art.h"
#include "control.h"
#include "fx.h"
#include "gfx.h"

#define DCX 68   // the dial
#define DCY 70
#define DR 21

static uint16_t C(uint32_t v) { return rgb565(v >> 16, (v >> 8) & 255, v & 255); }

typedef struct {
    float x, y, r;
    int teeth;
    float k;    // turn per tick (bigger gears slower, alternate directions)
    bool iron;
} gear_t;

static const gear_t GEARS[] = {
    {DCX, DCY, 31, 30, 1.0f, false},  {24, 40, 20, 20, -1.5f, true},  {116, 36, 24, 24, -1.25f, false},
    {16, 104, 17, 16, 1.8f, true},    {124, 100, 18, 18, 1.7f, false}, {98, 6, 12, 12, 2.5f, true},
    {40, 6, 14, 14, -2.1f, false},    {58, 132, 10, 10, -3.0f, true},  {92, 128, 13, 12, 2.4f, false},
};
#define NGEARS (int)(sizeof(GEARS) / sizeof(GEARS[0]))

void gears_enter(void) { art_begin(); }

static void draw_gear(const gear_t *g, float a)
{
    static const uint32_t BR[4] = {0x5c3a17, 0x9a6427, 0xd39a4a, 0xf0c27a}, IR[4] = {0x2a2e3a, 0x4d5466, 0x7a8396, 0xaab4c8};
    const uint32_t *pal = g->iron ? IR : BR;
    int R = (int)g->r + 4;
    for (int y = -R; y <= R; y++)
        for (int x = -R; x <= R; x++) {
            float d = sqrtf((float)(x * x + y * y));
            if (d > g->r + 3) continue;
            float ang = atan2f((float)y, (float)x) - a;
            float tooth = fmodf(ang * g->teeth / 6.28318f + 100.0f, 1.0f);
            bool on;
            int shade;
            if (d > g->r) on = tooth < 0.5f, shade = 1;          // teeth
            else if (d > g->r - 4) on = true, shade = 1;          // rim
            else if (d < g->r * 0.3f) on = true, shade = 2;       // hub
            else {                                                 // five spokes
                float sp = fmodf((ang * 5 / 6.28318f) + 100.0f, 1.0f);
                on = sp < 0.14f || sp > 0.86f, shade = 1;
            }
            if (!on) continue;
            // lit from the window, up and to the left
            float lit = (-x - y) / (float)R;
            if (lit > 0.45f && shade == 1) shade = 2;
            if (lit < -0.5f) shade = 0;
            if (d > g->r - 1 && d <= g->r && lit > 0.2f) shade = 3;
            art_px((int)g->x + x, (int)g->y + y, C(pal[shade]));
        }
    art_px((int)g->x, (int)g->y, C(pal[3]));
}

void gears_render(uint16_t *fb, uint32_t t, uint32_t dt)
{
    int dx_, dy_;
    app_take_drag(&dx_, &dy_);
    float ts = t / 1000.0f;
    // stone wall
    for (int y = 0; y < ART_H; y++) {
        int row = y / 7;
        for (int x = 0; x < ART_W; x++) {
            int xx = x + (row & 1) * 6, brick = xx / 12;
            bool edge = y % 7 == 0 || xx % 12 == 0;
            art[y * ART_W + x] = edge ? C(0x0d0c1a) : (brick * 7 + row * 3) % 5 ? C(0x1c1a30) : C(0x22203a);
        }
    }
    // the pointed window and its moonbeam
    for (int y = 6; y < 58; y++) {
        int half = y < 28 ? (int)(12 * sqrtf(1 - (28 - y) * (28 - y) / 484.0f)) : 12;
        art_rect(DCX - half, y, half * 2, 1, C(0x2b3f78));
    }
    art_rect(DCX - 1, 10, 2, 48, C(0x6f8fd8));
    art_rect(DCX - 12, 34, 24, 2, C(0x6f8fd8));
    for (int y = 30; y < ART_H; y++) {
        float s = (y - 30) / 138.0f;
        for (int x = (int)(56 + 4 * s); x < (int)(80 + 40 * s); x++) art_tint(x, y, C(0xa9c0ff), 2);
    }
    // chains with weights, creeping down through the hour
    for (int c = 0; c < 2; c++) {
        int x = c ? 130 : 6;
        float ph = c ? 0.6f : 0.0f;
        int len = 50 + (int)(fmodf((g_now.m * 60 + g_now.s) / 3600.0f + ph, 1.0f) * 40);
        for (int y = 0; y < len; y += 3) art_rect(x, y, 2, 2, y % 6 ? C(0x5a6070) : C(0x8a92a6));
        art_rect(x - 3, len, 8, 12, C(0x3b3f4d));
        art_rect(x - 3, len, 8, 1, C(0x8a92a6));
    }
    // the gears jump once a second (a quick move at the start of each second, then still)
    float tick = (g_now.h * 3600 + g_now.m * 60 + g_now.s) + (g_now.ms < 160 ? g_now.ms / 160.0f : 1.0f);
    for (int i = 0; i < NGEARS; i++) draw_gear(&GEARS[i], fmodf(tick * 0.12f * GEARS[i].k, 6.28318f));
    // the pendulum
    float sw = sinf((g_now.s + g_now.ms / 1000.0f) * 3.14159f) * 0.35f;
    float bx = DCX + sinf(sw) * 36, by = DCY + 24 + cosf(sw) * 36;
    art_thick(DCX, DCY + 22, bx, by, 2, C(0xb08a4a));
    art_disc(bx, by, 6, C(0xd39a4a));
    art_disc(bx - 2, by - 2, 2, C(0xf0c27a));
    // the dial
    art_disc(DCX, DCY, DR + 2, C(0x5c3a17));
    art_disc(DCX, DCY, DR, C(0xe8dcc0));
    for (int i = 0; i < 12; i++) {
        float a = i * 3.14159f / 6;
        if (i % 3) art_px((int)(DCX + sinf(a) * (DR - 3)), (int)(DCY - cosf(a) * (DR - 3)), C(0x3a2410));
    }
    float hA = ((g_now.h % 12) + g_now.m / 60.0f) * 3.14159f / 6, mA = (g_now.m + g_now.s / 60.0f) * 3.14159f / 30;
    art_thick(DCX, DCY, DCX + sinf(hA) * 10, DCY - cosf(hA) * 10, 3, C(0x1a0f05));
    art_thick(DCX, DCY, DCX + sinf(mA) * 16, DCY - cosf(mA) * 16, 2, C(0x1a0f05));
    if (g_app.show_secs) {
        float sA = g_now.s * 3.14159f / 30;
        art_line(DCX, DCY, DCX + sinf(sA) * 18, DCY - cosf(sA) * 18, C(0xb0302a));
    }
    art_rect(DCX - 1, DCY - 1, 2, 2, C(0x9a6427));
    // dust in the moonlight
    for (int i = 0; i < 26; i++) {
        uint32_t h = fx_hash(i, 91);
        float k = (h >> 16 & 255) / 255.0f;
        int x = (int)fmodf((h % ART_W) + sinf(ts * 0.3f + k * 9) * 6, ART_W), y = (int)fmodf((h >> 8) % ART_H + ts * (2 + k * 3), ART_H);
        if (x > 56 && x < 80 + (y - 30) * 40 / 138 && y > 30) art_tint(x, y, C(0xdce6ff), 11);
    }
    art_blit(fb);

    // Roman numerals at the quarters, and the time below (full resolution)
    static const char *const RN[4] = {"XII", "III", "VI", "IX"};
    for (int q = 0; q < 4; q++) {
        float a = q * 3.14159f / 2;
        int x = (int)((DCX + sinf(a) * (DR - 8)) * 2), y = (int)((DCY - cosf(a) * (DR - 8)) * 2);
        gfx_text(fb, x - gfx_text_width(RN[q], 1) / 2, y - 3, RN[q], rgb565(58, 36, 16), 1);
    }
    static uint16_t brass[4];
    if (!brass[0]) brass[0] = rgb565(255, 236, 190), brass[1] = rgb565(240, 194, 122), brass[2] = rgb565(211, 154, 74), brass[3] = rgb565(154, 100, 39);
    char hhmm[8];
    clock_hhmm(hhmm, !g_now.valid || g_now.ms < 500);
    pix_digits_draw(fb, (WW - pix_digits_width("00:00", 3)) / 2, 286, hhmm, 3, brass, rgb565(8, 6, 14));
}
