// Classic demoscene plasma: four interfering sine fields mapped through a cycling palette, over the
// whole screen. The time stacks big in the middle on a frosted panel; dragging stirs the plasma.
// The field is smooth, so it is worked out on 2x2 blocks; the panel uses a darker copy of the palette
// in the same pass instead of darkening the pixels afterwards.
#include <string.h>
#include "app.h"
#include "control.h"
#include "fx.h"
#include "gfx.h"

#define BW (FW / 2)
#define BH (FH / 2 + 1)
#define PX 58           // frosted panel
#define PY 70
#define PW_ (FW - 2 * PX)
#define PH_ 384
#define PR 26           // its corner radius

static uint16_t pal[256], dark[256];
static uint8_t ax[BW], cx[BW], ex[BW];
static uint8_t by[BH], cy[BH];
static int16_t s_inset[PH_];   // panel row inset from its sides (rounded corners)
static int s_shift;

void plasma_enter(void)
{
    for (int y = 0; y < PH_; y++) {
        int d = y < PR ? PR - y : y >= PH_ - PR ? y - (PH_ - PR - 1) : 0;
        int in = 0;
        while (in < PR && (PR - in) * (PR - in) + d * d > PR * PR) in++;
        s_inset[y] = d ? in : 0;
    }
    for (int x = 0; x < BW; x++) ex[x] = (uint8_t)(x * 4 / 5);
}

static inline uint16_t darker(uint16_t p)
{
    uint16_t c = __builtin_bswap16(p);
    c = (c >> 1) & 0x7BEF;
    return __builtin_bswap16(c);
}

void plasma_render(uint16_t *fb, uint32_t t, uint32_t dt)
{
    int dx, dy;
    app_take_drag(&dx, &dy);
    s_shift += (dx + dy) / 2;  // the finger stirs the plasma

    uint8_t hue = t / 40;
    for (int i = 0; i < 256; i++) {
        pal[i] = hsv565(hue + (i >> 1), 235, 70 + sin_u8[(uint8_t)(i * 3)] * 185 / 255);
        dark[i] = darker(pal[i]);
    }

    uint8_t p1 = t / 13 + s_shift, p2 = t / 17, p3 = t / 11, p4 = t / 23 - s_shift;
    for (int x = 0; x < BW; x++) {
        int u = x * 4 / 5;
        ax[x] = sin_u8[(uint8_t)(u + p1)];
        cx[x] = sin_u8[(uint8_t)(u * 3 / 2 + p4)] >> 1;
    }
    for (int y = 0; y < BH; y++) {
        int v = y * 6 / 5;
        by[y] = sin_u8[(uint8_t)(v - p2)];
        cy[y] = sin_u8[(uint8_t)(v * 2 / 3 + p3 * 2)] >> 1;
    }
    // One row of 2x2 blocks is worked out into internal RAM (two equal pixels per 32-bit word), then
    // copied to both screen rows: PSRAM is written in bursts instead of pixel by pixel
    static uint32_t line[BW];
    for (int by_i = 0; by_i < FH / 2; by_i++) {
        uint8_t b = by[by_i], c = cy[by_i], d = by_i * 4 / 5 + p3;
        int y = by_i * 2, py = y - PY, l = FW, rgt = -1;
        if (py >= 0 && py < PH_) {  // the panel's span on this row (rounded corners)
            l = PX + s_inset[py];
            rgt = PX + PW_ - 1 - s_inset[py];
        }
        for (int x2 = 0; x2 < BW; x2++) {
            int v = ax[x2] + b + sin_u8[(uint8_t)(ex[x2] + d)] + sin_u8[(uint8_t)(cx[x2] + c)];
            int x = x2 * 2;
            uint32_t col = (x >= l && x <= rgt) ? dark[v >> 2] : pal[v >> 2];
            line[x2] = col | col << 16;
        }
        memcpy(fb + y * FW, line, FW * 2);
        memcpy(fb + (y + 1) * FW, line, FW * 2);
    }

    // Date on the straight part of the top edge, the time big on the panel, seconds below
    char date[24];
    clock_date_str(date);
    uint16_t white = rgb565(255, 255, 255), ink = rgb565(10, 8, 30);
    gfx_text_ol(fb, (FW - gfx_text_width(date, 3)) / 2, 30, date, white, ink, 3, 1);
    clock_draw_stacked(fb, &font_round_xxl, 80, 8, white, ink, FONT_FILL);  // the dark panel is contrast enough
    clock_draw_seconds_bar(fb, 92, 466, FW - 184, 5, white, rgb565(70, 70, 90));
}
