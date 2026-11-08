// Lava lamp metaballs (rendered at half resolution), over the whole screen (wide canvas). Constant cost per frame: every pixel sums
// a fixed number of 1/d^2 falloffs from a lookup table. Ball 0 follows the finger on the phone pad.
#include "app.h"
#include "control.h"
#include "fx.h"
#include "gfx.h"

#define BW (WW / 2)
#define BH (WH / 2)
#define NB 6
#define INV_SHIFT 4
#define INV_N 3072  // covers d^2 up to (BW^2 + BH^2) >> INV_SHIFT

static const struct { uint8_t r; uint8_t sx, sy; uint16_t ph; } balls[NB] = {
    {27, 7, 5, 0}, {21, 5, 8, 300}, {23, 9, 4, 600}, {17, 4, 9, 150}, {20, 6, 6, 800}, {15, 11, 7, 450},
};

static uint16_t inv_t[INV_N];
static uint16_t dx2[NB][BW];
static uint16_t pal[256];
static int s_fx = BW / 2, s_fy = BH / 2;
static bool s_ready;

void lava_enter(void)
{
    if (s_ready) return;
    for (int k = 0; k < INV_N; k++) inv_t[k] = 65535 / (k * (1 << INV_SHIFT) + 8);
    s_ready = true;
}

void lava_render(uint16_t *fb, uint32_t t, uint32_t dt)
{
    int dx, dy;
    app_take_drag(&dx, &dy);

    int bx[NB], by[NB], st[NB];
    for (int i = 0; i < NB; i++) {
        bx[i] = BW / 2 + (sin_q14[(t * balls[i].sx / 64 + balls[i].ph) & 1023] * (BW / 2 - 8) >> 14);
        by[i] = BH / 2 + (sin_q14[(t * balls[i].sy / 64 + balls[i].ph + 256) & 1023] * (BH / 2 - 12) >> 14);
        st[i] = balls[i].r * balls[i].r / 2;  // field reaches ~128 at distance r
    }
    if (g_app.touch) {
        // Ease ball 0 towards the finger
        s_fx += (g_app.tx * BW / 1000 - s_fx) / 3;
        s_fy += (g_app.ty * BH / 1000 - s_fy) / 3;
    } else {
        s_fx += (bx[0] - s_fx) / 8;
        s_fy += (by[0] - s_fy) / 8;
    }
    bx[0] = s_fx;
    by[0] = s_fy;

    // Palette: dark glow halo, bright rim at the surface, warm body inside
    uint8_t hue = t / 60;
    for (int i = 0; i < 256; i++) {
        if (i < 112) pal[i] = hsv565(hue + 170, 255, i * i * 70 / (112 * 112));
        else if (i < 128) pal[i] = hsv565(hue + 10, 90 + (i - 112) * 8, 255);
        else pal[i] = hsv565(hue + (i - 128) / 3, 235, 175 + (i - 128) * 80 / 127);
    }

    for (int i = 0; i < NB; i++)
        for (int x = 0; x < BW; x++) dx2[i][x] = (x - bx[i]) * (x - bx[i]);

    for (int y = 0; y < BH; y++) {
        int dy2[NB];
        for (int i = 0; i < NB; i++) dy2[i] = (y - by[i]) * (y - by[i]);
        uint16_t *r0 = fb + (y * 2) * WW, *r1 = r0 + WW;
        for (int x = 0; x < BW; x++) {
            int sum = 0;
            for (int i = 0; i < NB; i++) {
                int d2 = dx2[i][x] + dy2[i];
                if (d2 >= INV_N << INV_SHIFT) d2 = (INV_N << INV_SHIFT) - 1;
                sum += st[i] * inv_t[d2 >> INV_SHIFT];
            }
            int v = sum >> 8;
            uint16_t c = pal[v > 255 ? 255 : v];
            r0[x * 2] = r0[x * 2 + 1] = r1[x * 2] = r1[x * 2 + 1] = c;
        }
    }

    // Clock: dark "cut-out" digits with a glowing rim read well over both the blobs and the halo
    char date[24];
    clock_date_str(date);
    uint16_t rim = hsv565(hue + 10, 60, 255), hole = rgb565(12, 6, 22);
    gfx_text_ol(fb, (WW - gfx_text_width(date, 2)) / 2, 18, date, rim, hole, 2, 1);
    clock_draw_stacked(fb, &font_round_xl, 56, 14, hole, rim, FONT_FILL | FONT_OUTLINE);
    clock_draw_seconds_bar(fb, 70, 308, WW - 140, 3, rim, hole);
}
