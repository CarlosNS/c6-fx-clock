// "Engranajes" (portrait): a skeleton mechanical watch. Brass gears that really mesh (speeds in
// inverse ratio to their teeth), a balance wheel swinging back and forth, and an open dial with
// hour, minute and sweeping second hands. A small digital readout sits below.
#include <math.h>
#include <stdio.h>
#include "app.h"
#include "control.h"
#include "fx.h"
#include "gfx.h"

#define W LCD_W
#define H LCD_H
#define DX 86
#define DY 100
#define DR 78

typedef struct { int16_t x, y, r, teeth; int8_t dir; } gear_t;

// Chain of meshing gears: each one's pitch radius touches the previous one
static const gear_t gears[] = {
    {86, 100, 30, 20, 1},    // center wheel behind the dial
    {136, 152, 40, 26, -1},
    {70, 210, 44, 28, 1},
    {130, 262, 30, 20, -1},
    {36, 272, 26, 16, -1},
};
#define NG (sizeof(gears) / sizeof(gears[0]))

void gears_enter(void) {}

// sin/cos from the 1024-entry table (the C6 has no FPU: libm sinf/cosf are slow)
static inline int isn(int a) { return sin_q14[a & 1023]; }
static inline int ics(int a) { return sin_q14[(a + 256) & 1023]; }

static void gear(uint16_t *fb, int cx, int cy, int r, int teeth, float ang, uint16_t body, uint16_t dark, uint16_t lit)
{
    int16_t pts[2 * 160];
    int n = 0;
    int tooth = 4 + r / 12;
    int base = (int)(ang * 162.975f * 256), step = 1024 * 256 / teeth;  // angles in 1/256 table units
    static const uint8_t frac[4] = {0, 46, 128, 174};                   // tooth profile: 0, .18, .5, .68
    for (int k = 0; k < teeth && n < 156; k++) {
        for (int j = 0; j < 4; j++) {
            int a = (base + k * step + frac[j] * step / 256) >> 8;
            int rr = j == 0 || j == 3 ? r - tooth / 2 : r + tooth / 2;
            pts[n * 2] = cx + (ics(a) * rr >> 14);
            pts[n * 2 + 1] = cy + (isn(a) * rr >> 14);
            n++;
        }
    }
    gfx_poly(fb, pts, n, body);
    gfx_ring(fb, cx, cy, r - tooth / 2 - 3, r - tooth / 2 - 1, lit);
    // Spoked wheel: cut-outs between five spokes
    for (int s = 0; s < 5; s++) {
        int a = (base >> 8) + s * 205 + 102;
        gfx_circle(fb, cx + (ics(a) * r * 52 / 100 >> 14), cy + (isn(a) * r * 52 / 100 >> 14), r / 4, dark);
    }
    gfx_circle(fb, cx, cy, r / 5 + 2, lit);
    gfx_circle(fb, cx, cy, r / 7, dark);
}

static void hand(uint16_t *fb, float a, int len, int tail, int width, uint16_t c)
{
    float s = sinf(a), co = -cosf(a);  // 0 = 12 o'clock, clockwise
    float px = -co, py = s;           // perpendicular
    int16_t pts[] = {
        (int16_t)(DX - s * tail + px * width), (int16_t)(DY - co * tail + py * width),
        (int16_t)(DX + s * len * 0.8f + px * width), (int16_t)(DY + co * len * 0.8f + py * width),
        (int16_t)(DX + s * len), (int16_t)(DY + co * len),
        (int16_t)(DX + s * len * 0.8f - px * width), (int16_t)(DY + co * len * 0.8f - py * width),
        (int16_t)(DX - s * tail - px * width), (int16_t)(DY - co * tail - py * width),
    };
    gfx_poly(fb, pts, 5, c);
}

void gears_render(uint16_t *fb, uint32_t t, uint32_t dt)
{
    int dx_, dy_;
    app_take_drag(&dx_, &dy_);

    // Dark steel plate with a radial brushed sheen
    for (int y = 0; y < H; y++) {
        int f = y * 200 / H;
        gfx_fill_rect(fb, 0, y, W, 1, rgb565(24 + f / 20, 26 + f / 20, 32 + f / 16));
    }
    for (int i = 0; i < 18; i++) {  // screws and jewels
        uint32_t h = fx_hash(i, 8);
        int x = 8 + (int)(h % (W - 16)), y = 190 + (int)((h >> 8) % 120);
        gfx_circle(fb, x, y, 3, (h >> 20) & 1 ? rgb565(170, 20, 50) : rgb565(120, 124, 132));
    }

    // Gear train: the drive rotates continuously; each gear turns by its teeth ratio
    float drive = t * 0.0009f;
    uint16_t brass = rgb565(196, 150, 70), brass_d = rgb565(44, 36, 30), brass_l = rgb565(240, 206, 120);
    for (int i = NG - 1; i >= 0; i--) {
        const gear_t *gr = &gears[i];
        float ang = drive * gears[0].teeth / gr->teeth * gr->dir + i * 0.13f;
        uint16_t body = i & 1 ? rgb565(176, 132, 62) : brass;
        gear(fb, gr->x, gr->y, gr->r, gr->teeth, ang, body, brass_d, brass_l);
    }

    // Balance wheel: swings back and forth five times a second
    float bal = sinf(t * 0.0157f * 2.5f) * 2.4f;
    int bx = 128, by = 214;
    gfx_ring(fb, bx, by, 20, 23, rgb565(210, 214, 220));
    for (int s = 0; s < 3; s++) {
        float a = bal + s * 2.094f;
        gfx_line(fb, bx, by, bx + (int)(cosf(a) * 20), by + (int)(sinf(a) * 20), rgb565(190, 194, 200));
    }
    for (int k = 0; k < 40; k++) {  // hairspring
        int a = k * 81 + (int)(bal * 65), r = 2 + k * 35 / 100;
        fb[(by + (isn(a) * r >> 14)) * W + bx + (ics(a) * r >> 14)] = rgb565(150, 170, 220);
    }
    gfx_circle(fb, bx, by, 3, rgb565(170, 20, 50));

    // Open dial: chapter ring with minute track and Roman numerals; the center stays open
    uint16_t ring = rgb565(236, 228, 206), ink = rgb565(30, 28, 34);
    gfx_ring(fb, DX, DY, DR - 18, DR, ring);
    gfx_ring(fb, DX, DY, DR - 1, DR + 1, rgb565(180, 140, 70));
    gfx_ring(fb, DX, DY, DR - 20, DR - 18, rgb565(180, 140, 70));
    for (int i = 0; i < 60; i++) {
        int a = i * 1024 / 60;
        int r0 = i % 5 ? DR - 4 : DR - 7;
        gfx_line(fb, DX + (isn(a) * r0 >> 14), DY - (ics(a) * r0 >> 14), DX + (isn(a) * (DR - 2) >> 14),
                 DY - (ics(a) * (DR - 2) >> 14), ink);
    }
    static const char *rn[12] = {"XII", "I", "II", "III", "IV", "V", "VI", "VII", "VIII", "IX", "X", "XI"};
    for (int i = 0; i < 12; i++) {
        int a = i * 1024 / 12, r = DR - 13;
        int w = ui_width(&ui_s, rn[i]);
        ui_text(fb, &ui_s, DX + (isn(a) * r >> 14) - w / 2, DY - (ics(a) * r >> 14) - 6, rn[i], ink, 0, FONT_FILL);
    }

    // Hands (smooth: they move with the milliseconds)
    float sec = g_now.valid ? g_now.s + g_now.ms / 1000.0f : 0;
    float min = g_now.valid ? g_now.m + sec / 60 : 0;
    float hr = g_now.valid ? (g_now.h % 12) + min / 60 : 0;
    uint16_t blued = rgb565(40, 60, 140);
    hand(fb, hr * 0.5236f, 40, 8, 4, rgb565(10, 12, 30));
    hand(fb, hr * 0.5236f, 38, 6, 2, blued);
    hand(fb, min * 0.10472f, 62, 10, 3, rgb565(10, 12, 30));
    hand(fb, min * 0.10472f, 60, 8, 2, blued);
    if (g_app.show_secs) {
        float a = sec * 0.10472f;
        gfx_line(fb, DX - (int)(sinf(a) * 16), DY + (int)(cosf(a) * 16), DX + (int)(sinf(a) * 70), DY - (int)(cosf(a) * 70),
                 rgb565(220, 40, 50));
        gfx_circle(fb, DX - (int)(sinf(a) * 14), DY + (int)(cosf(a) * 14), 3, rgb565(220, 40, 50));
    }
    gfx_circle(fb, DX, DY, 5, brass_l);
    gfx_circle(fb, DX, DY, 2, rgb565(170, 20, 50));

    // Date on top, digital readout on a small plate
    char date[24];
    clock_date_str(date);
    ui_text(fb, &ui_s, (W - ui_width(&ui_s, date)) / 2, 2, date, rgb565(220, 200, 150), rgb565(20, 20, 24),
            FONT_FILL | FONT_OUTLINE);
    gfx_darken_rect(fb, 6, H - 46, 70, 40, 6, 2);
    clock_row_grad(fb, &font_seg_s, 41, H - 42, -2, rgb565(255, 230, 160), rgb565(230, 170, 70), rgb565(10, 10, 14), 0);
}
