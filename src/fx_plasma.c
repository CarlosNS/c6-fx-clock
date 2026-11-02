// Classic demoscene plasma: four interfering sine fields mapped through a cycling palette
#include "app.h"
#include "control.h"
#include "fx.h"
#include "gfx.h"

static uint16_t pal[256];
static uint8_t ax[LCD_W], cx[LCD_W];
static uint8_t by[LCD_H], cy[LCD_H];
static int s_shift;

void plasma_enter(void) {}

void plasma_render(uint16_t *fb, uint32_t t, uint32_t dt)
{
    int dx, dy;
    app_take_drag(&dx, &dy);
    s_shift += dx + dy;  // dragging on the phone stirs the plasma

    uint8_t hue = t / 40;
    for (int i = 0; i < 256; i++) pal[i] = hsv565(hue + (i >> 1), 235, 70 + sin_u8[(uint8_t)(i * 3)] * 185 / 255);

    uint8_t p1 = t / 13 + s_shift, p2 = t / 17, p3 = t / 11, p4 = t / 23 - s_shift;
    for (int x = 0; x < LCD_W; x++) {
        ax[x] = sin_u8[(uint8_t)(x * 2 + p1)];
        cx[x] = sin_u8[(uint8_t)(x * 3 + p4)] >> 1;
    }
    for (int y = 0; y < LCD_H; y++) {
        by[y] = sin_u8[(uint8_t)(y * 3 / 2 - p2)];
        cy[y] = sin_u8[(uint8_t)(y + p3 * 2)] >> 1;
    }
    for (int y = 0; y < LCD_H; y++) {
        uint16_t *row = fb + y * LCD_W;
        uint8_t b = by[y], c = cy[y];
        uint8_t d = y + p3;
        for (int x = 0; x < LCD_W; x++) {
            int v = ax[x] + b + sin_u8[(uint8_t)(x + d)] + sin_u8[(uint8_t)(cx[x] + c)];
            row[x] = pal[v >> 2];
        }
    }

    // Clock: frosted panel, big HH / MM, date on top, seconds as a thin bar
    char date[24];
    clock_date_str(date);
    uint16_t white = rgb565(255, 255, 255), ink = rgb565(10, 8, 30);
    gfx_text_ol(fb, (LCD_W - gfx_text_width(date, 2)) / 2, 16, date, white, ink, 2, 1);
    gfx_darken_rect(fb, 12, 44, LCD_W - 24, 252, 14, 1);
    clock_draw_stacked(fb, &font_round_xl, 58, 14, white, ink, FONT_FILL | FONT_OUTLINE);
    clock_draw_seconds_bar(fb, 26, 286, LCD_W - 52, 3, white, rgb565(70, 70, 90));
}
