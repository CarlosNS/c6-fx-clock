// "Sakura" (landscape): a cherry tree in bloom by a still pond. Petals drift down, fluttering as
// they turn (edge-on they almost vanish), and leave ripples where they touch the water.
#include <stdio.h>
#include "app.h"
#include "control.h"
#include "fx.h"
#include "gfx.h"

#define W LAND_W
#define H LAND_H
#define WATER 136
#define NPET 56
#define NRIP 8

typedef struct { float x, y, vy, phase, spin, drift; uint8_t tone; } petal_t;
typedef struct { int16_t x, y; uint32_t t0; } ripple_t;

static petal_t pet[NPET];
static ripple_t rip[NRIP];
static int rip_next;

// Tree: trunk and branches as (x0, y0, x1, y1, width)
static const int16_t branches[][5] = {
    {24, 172, 40, 120, 12}, {40, 120, 58, 80, 9},  {58, 80, 96, 52, 6},   {96, 52, 150, 34, 4},
    {150, 34, 200, 30, 3},  {58, 80, 52, 40, 5},   {52, 40, 70, 12, 3},   {96, 52, 122, 70, 3},
    {122, 70, 168, 74, 2},  {40, 120, 14, 92, 5},  {14, 92, 4, 60, 3},    {150, 34, 176, 12, 2},
    {200, 30, 236, 40, 2},  {70, 12, 104, 4, 2},
};
#define NBR (sizeof(branches) / sizeof(branches[0]))

static void spawn(petal_t *p, bool anywhere)
{
    uint32_t h = fx_rand();
    // Petals leave from the blossoms (along the upper branches) or drift in from the top
    p->x = 20 + (float)(h % 240);
    p->y = anywhere ? (float)((h >> 9) % WATER) : 10 + (float)((h >> 9) % 60);
    p->vy = 10 + (float)((h >> 16) % 12);
    p->phase = (float)((h >> 4) % 628) / 100;
    p->spin = 2 + (float)((h >> 20) % 30) / 10;
    p->drift = 8 + (float)((h >> 24) % 10);
    p->tone = (h >> 28) & 3;
}

void sakura_enter(void)
{
    for (int i = 0; i < NPET; i++) spawn(&pet[i], true);
    for (int i = 0; i < NRIP; i++) rip[i].t0 = 0;
}

static inline int wsin(int a, int amp)
{
    return (sin_q14[a & 1023] * amp) >> 14;
}

void sakura_render(uint16_t *fb, uint32_t t, uint32_t dt)
{
    int dx_, dy_;
    app_take_drag(&dx_, &dy_);
    float dts = dt / 1000.0f;

    // Pastel sky, pale sun, layered hills in the haze
    for (int y = 0; y < WATER; y++) {
        int f = y * 256 / WATER;
        gfx_fill_rect(fb, 0, y, W, 1, rgb565(188 + f * 66 / 256, 214 + f * 14 / 256, 240 - f * 8 / 256));
    }
    gfx_glow(fb, 262, 46, 40, rgb565(255, 250, 230), 9);
    gfx_circle(fb, 262, 46, 12, rgb565(255, 252, 240));
    for (int layer = 0; layer < 2; layer++) {
        uint16_t c = layer ? rgb565(178, 164, 204) : rgb565(206, 192, 222);
        for (int x = 0; x < W; x++) {
            int top = WATER - 14 - layer * -8 - 18 + wsin(x * (3 + layer * 2) + layer * 300, 12 - layer * 4) + (layer ? 10 : 0);
            gfx_fill_rect(fb, x, top, 1, WATER - top, c);
        }
    }
    // Pond reflecting the sky, with faint horizontal shimmer
    for (int y = WATER; y < H; y++) {
        int f = (y - WATER) * 256 / (H - WATER);
        uint16_t c = rgb565(176 - f / 10, 200 - f / 12, 226 - f / 16), *row = fb + y * W;
        for (int x = 0; x < W; x++) row[x] = c;
        if (((y + t / 120) % 5) == 0)
            for (int x = (int)(fx_hash(y, t / 400) % 40); x < W; x += 30 + (int)(fx_hash(x, y) % 40))
                gfx_fill_rect(fb, x, y, 10, 1, rgb565(226, 238, 250));
    }
    gfx_fill_rect(fb, 0, WATER, W, 1, rgb565(150, 170, 200));

    // Ripples where petals landed
    for (int i = 0; i < NRIP; i++) {
        if (!rip[i].t0) continue;
        uint32_t age = t - rip[i].t0;
        if (age > 2500) { rip[i].t0 = 0; continue; }
        int r = 2 + age / 90, a = 10 - (int)(age * 10 / 2500);
        for (int k = 0; k < 64; k++) {
            int x = rip[i].x + (sin_q14[k * 16] * r >> 14), y = rip[i].y + (sin_q14[(k * 16 + 256) & 1023] * r / 4 >> 14);
            if ((unsigned)x < W && y >= WATER && y < H) fb[y * W + x] = gfx_blend(fb[y * W + x], rgb565(255, 255, 255), a);
        }
    }

    // Tree: trunk and branches, then blossom clusters swaying in the breeze
    uint16_t bark = rgb565(74, 48, 56), bark_l = rgb565(110, 76, 80);
    for (unsigned b = 0; b < NBR; b++) {
        int w = branches[b][4];
        for (int k = -w / 2; k <= w / 2; k++) {
            gfx_line(fb, branches[b][0] + k, branches[b][1], branches[b][2] + k, branches[b][3], k == -w / 2 ? bark_l : bark);
        }
    }
    static const uint8_t pinks[4][3] = {{255, 196, 214}, {248, 160, 190}, {255, 226, 236}, {236, 132, 170}};
    for (unsigned b = 2; b < NBR; b++) {
        for (int s = 0; s <= 6; s++) {
            int x = branches[b][0] + (branches[b][2] - branches[b][0]) * s / 6;
            int y = branches[b][1] + (branches[b][3] - branches[b][1]) * s / 6;
            uint32_t h = fx_hash(b, s);
            x += (int)(h % 11) - 5 + wsin(t / 7 + x * 5, 2);
            y += (int)((h >> 8) % 11) - 7 + wsin(t / 9 + x * 3, 1);
            for (int c = 0; c < 5; c++) {
                uint32_t hc = fx_hash(h, c);
                const uint8_t *p = pinks[hc & 3];
                gfx_circle(fb, x + (int)(hc >> 4) % 13 - 6, y + (int)(hc >> 10) % 9 - 4, 3 + (hc >> 20) % 3, rgb565(p[0], p[1], p[2]));
            }
            fb[((y - 2 + H) % H) * W + ((x + W) % W)] = rgb565(255, 255, 255);
        }
    }

    // Falling petals: fall, sway, and flutter (their apparent width follows the spin)
    for (int i = 0; i < NPET; i++) {
        petal_t *p = &pet[i];
        float ph = p->phase + t * 0.001f * p->spin;
        p->y += p->vy * dts;
        p->x += (p->drift * sin_q14[(int)(ph * 163) & 1023] / 16384.0f + 6) * dts;
        if (p->x > W + 4) p->x = -4;
        int x = (int)p->x, y = (int)p->y;
        int wdt = (sin_q14[(int)(ph * 326) & 1023] * 3) >> 14;  // -3..3: turning petal
        const uint8_t *c = pinks[p->tone];
        uint16_t col = rgb565(c[0], c[1], c[2]);
        if (wdt >= 2 || wdt <= -2) {
            gfx_fill_rect(fb, x - 1, y, 3, 2, col);
            if ((unsigned)x < W && (unsigned)(y + 2) < H) fb[(y + 2) * W + x] = col;
            if ((unsigned)(x - 1) < W && (unsigned)y < H) fb[y * W + x - 1] = rgb565(255, 236, 244);
        } else {
            gfx_fill_rect(fb, x, y, 1, 2, rgb565(c[0] * 4 / 5, c[1] * 4 / 5, c[2] * 4 / 5));
        }
        if (y >= WATER + 2 + (i % 20)) {  // lands on the pond
            rip[rip_next].x = x;
            rip[rip_next].y = y;
            rip[rip_next].t0 = t ? t : 1;
            rip_next = (rip_next + 1) % NRIP;
            spawn(p, false);
        }
    }

    // Time: calligraphic numerals, white blushing to pink
    clock_row_grad(fb, &font_nib_m, 238, 74, -3, rgb565(255, 255, 255), rgb565(255, 196, 220), rgb565(96, 40, 70), 2);
    char date[24];
    clock_date_str(date);
    ui_text(fb, &ui_m, 238 - ui_width(&ui_m, date) / 2, 140, date, rgb565(255, 255, 255), rgb565(96, 40, 70),
            FONT_FILL | FONT_OUTLINE);
}
