// "El grito" (portrait): after Edvard Munch's The Scream (1893, public domain).
// A sky of undulating orange and red bands, the dark swirling fjord, the bridge rushing
// away in perspective with two far-off figures, and the figure in front swaying, hands to
// the face, mouth opening and closing.
#include <math.h>
#include <stdio.h>
#include "app.h"
#include "control.h"
#include "fx.h"
#include "gfx.h"

#define W LCD_W
#define H LCD_H

static inline int wsin(int a, int amp)
{
    return (sin_q14[a & 1023] * amp) >> 14;
}

void scream_enter(void) {}

// Horizontal sway of the figure at height y (the whole body undulates)
static inline int sway(int y, uint32_t t)
{
    return wsin(y * 5 + t / 5, 5) + wsin(t / 9, 3);
}

void scream_render(uint16_t *fb, uint32_t t, uint32_t dt)
{
    int dx_, dy_;
    app_take_drag(&dx_, &dy_);

    static const uint8_t sky[6][3] = {{232, 128, 44}, {214, 84, 40}, {244, 186, 82}, {200, 70, 46},
                                     {236, 150, 60}, {120, 110, 90}};
    static const uint8_t fjord[4][3] = {{30, 50, 90}, {52, 80, 120}, {24, 38, 62}, {70, 96, 110}};
    static const uint8_t land[4][3] = {{70, 80, 40}, {110, 90, 46}, {50, 60, 40}, {140, 110, 60}};

    // Sky, fjord and land at half resolution: everything flows in curved bands
    for (int sy = 0; sy < 120; sy++) {
        uint16_t *r0 = fb + (sy * 2) * W, *r1 = r0 + W;
        for (int sx = 0; sx < W / 2; sx++) {
            int x = sx * 2, y = sy * 2;
            const uint8_t *c;
            if (y < 128) {
                int v = y * 3 + wsin(x * 6 + t / 7, 18) + wsin(x * 2 + y * 3 - t / 11, 12);
                c = sky[((v >> 4) + 60) % 6];
            } else {
                int shore = 150 + wsin(x * 4 + 200, 14) + x / 6;  // where the land meets the water
                if (x > 40 && y < shore + 60 && y > 128) {
                    int ddx = x - 120, ddy = y - 180;
                    int v = (ddx * ddx + ddy * ddy * 3) / 60 + wsin(x * 9 + t / 6, 6) + t / 90;
                    c = fjord[(v >> 2) & 3];
                } else {
                    int v = y * 2 + x + wsin(y * 8 + x * 3 + t / 10, 8);
                    c = land[(v >> 3) & 3];
                }
            }
            uint16_t cc = rgb565(c[0], c[1], c[2]);
            r0[sx * 2] = r0[sx * 2 + 1] = r1[sx * 2] = r1[sx * 2 + 1] = cc;
        }
    }

    // Bridge deck rushing to the upper left, streaked along the perspective
    const int vx = -60, vy = 150;  // vanishing point
    for (int y = 160; y < H; y++) {
        int rail_x = (y - 160) * 172 / 90;  // x where the railing crosses this row
        uint16_t *row = fb + y * W;
        for (int x = 0; x < W; x++) {
            if (y < 240 && x > rail_x) continue;  // above the railing: background stays
            int ang = ((x - vx) * 64) / (y - vy + 1);
            int v = ang + wsin(y * 3 + t / 8, 3);
            static const uint8_t deck[4][3] = {{196, 128, 64}, {160, 92, 46}, {214, 152, 80}, {130, 76, 44}};
            const uint8_t *c = deck[(v >> 4) & 3];
            row[x] = rgb565(c[0], c[1], c[2]);
        }
    }
    // Railing: three rails and posts
    uint16_t rail = rgb565(110, 60, 36), rail_d = rgb565(70, 36, 24);
    for (int k = 0; k < 3; k++) {
        gfx_line(fb, 0, 150 + k * 6, W, 236 + k * 9, k == 0 ? rail : rail_d);
        gfx_line(fb, 0, 151 + k * 6, W, 237 + k * 9, rail_d);
    }
    for (int i = 0; i < 9; i++) {
        int x = i * 22, y = 150 + x * 86 / W;
        gfx_line(fb, x, y, x, y + 14 + x / 10, rail_d);
    }
    // Two figures far away on the bridge
    for (int i = 0; i < 2; i++) {
        int fx = 14 + i * 9, fy = 136 + i * 3;
        gfx_circle(fb, fx, fy, 2, rgb565(30, 26, 30));
        gfx_fill_rect(fb, fx - 2, fy + 2, 5, 12, rgb565(30, 26, 30));
    }

    // ---- The figure: dark sinuous body, skull-like head, hands to the face
    int16_t body[2 * 40];
    int n = 0;
    for (int y = 246; y <= H; y += 8) {  // right edge downwards
        int w = 12 + (y - 246) * 30 / 74;
        body[n * 2] = 92 + sway(y, t) + w;
        body[n * 2 + 1] = y;
        n++;
    }
    for (int y = H; y >= 246; y -= 8) {
        int w = 12 + (y - 246) * 30 / 74;
        body[n * 2] = 92 + sway(y, t) - w - 4;
        body[n * 2 + 1] = y;
        n++;
    }
    gfx_poly(fb, body, n, rgb565(26, 30, 46));
    for (int s = 0; s < 3; s++) {  // folds in the coat
        int px = 0, py = 0;
        for (int y = 256; y <= H; y += 8) {
            int x = 92 + sway(y, t) + (s - 1) * (8 + (y - 246) / 6);
            if (py) gfx_line(fb, px, py, x, y, rgb565(44, 50, 70));
            px = x;
            py = y;
        }
    }
    int hx = 92 + sway(214, t), hy = 214;
    uint16_t skin = rgb565(222, 206, 150), skin_d = rgb565(170, 160, 104);
    // Head: a bald, elongated skull, wider at the temples and narrowing to the chin
    uint16_t outline = rgb565(70, 60, 40);
    for (int y = -32; y <= 32; y++) {
        float fy = y / 32.0f;
        int w = (int)(17 * sqrtf(1 - fy * fy) * (y > 0 ? 1 - fy * 0.35f : 1));
        if (w <= 0) continue;
        gfx_fill_rect(fb, hx - w - 1, hy + y, w * 2 + 2, 1, outline);
        gfx_fill_rect(fb, hx - w, hy + y, w * 2, 1, skin);
        gfx_fill_rect(fb, hx + w / 3, hy + y, w - w / 3, 1, skin_d);  // shadowed side
    }
    // Eyes, nostrils and the open mouth (breathing)
    int open = 7 + (sin_u8[(uint8_t)(t / 10)] >> 5);
    gfx_circle(fb, hx - 8, hy - 6, 5, rgb565(40, 40, 30));
    gfx_circle(fb, hx + 7, hy - 6, 5, rgb565(40, 40, 30));
    gfx_fill_rect(fb, hx - 2, hy + 4, 2, 4, rgb565(90, 80, 50));
    gfx_fill_rect(fb, hx + 2, hy + 4, 2, 4, rgb565(90, 80, 50));
    for (int y = -open; y <= open; y++) {
        int w = (int)(5 * (1 - (float)(y * y) / (open * open + 1))) + 1;
        gfx_fill_rect(fb, hx - w, hy + 18 + y, w * 2, 1, rgb565(46, 36, 30));
    }
    // Hands pressed to the cheeks
    for (int s = -1; s <= 1; s += 2) {
        int16_t hand[] = {(int16_t)(hx + s * 14), (int16_t)(hy - 10), (int16_t)(hx + s * 22), (int16_t)(hy - 8),
                          (int16_t)(hx + s * 24), (int16_t)(hy + 20), (int16_t)(hx + s * 20), (int16_t)(hy + 34),
                          (int16_t)(hx + s * 14), (int16_t)(hy + 30)};
        int16_t rim[10];
        for (int k = 0; k < 5; k++) {
            rim[k * 2] = hand[k * 2] + (hand[k * 2] > hx ? 1 : -1);
            rim[k * 2 + 1] = hand[k * 2 + 1];
        }
        gfx_poly(fb, rim, 5, rgb565(40, 34, 30));
        gfx_poly(fb, hand, 5, s < 0 ? skin : skin_d);
        for (int f = 0; f < 3; f++)  // fingers
            gfx_line(fb, hx + s * 15, hy - 6 + f * 6, hx + s * 21, hy - 4 + f * 6, outline);
        gfx_line(fb, hx + s * 20, hy + 32, hx + s * 26, hy + 52, rgb565(26, 30, 46));  // arm
        gfx_line(fb, hx + s * 21, hy + 32, hx + s * 27, hy + 52, rgb565(26, 30, 46));
    }

    // Clock in the burning sky
    clock_row_grad(fb, &font_round_m, W / 2, 8, -5, rgb565(255, 246, 210), rgb565(255, 206, 120), rgb565(70, 24, 20), 2);
    char date[24];
    clock_date_str(date);
    ui_text(fb, &ui_s, (W - ui_width(&ui_s, date)) / 2, 72, date, rgb565(255, 240, 200), rgb565(70, 24, 20),
            FONT_FILL | FONT_OUTLINE);
}
