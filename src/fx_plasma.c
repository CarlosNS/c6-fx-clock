// Classic demoscene plasma: four interfering sine fields mapped through a cycling palette, over the
// whole screen. The time stacks big in the middle on a frosted panel; dragging stirs the plasma.
// The field is smooth, so it is worked out on 2x2 blocks; the panel uses a darker copy of the palette
// in the same pass instead of darkening the pixels afterwards. The 176 px digits only change once a
// minute: they are rendered once into runs per row and laid over each plasma row before it is copied
// to the screen, instead of being drawn over the whole frame every time.
#include <string.h>
#include "app.h"
#include "esp_heap_caps.h"
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

// Digits: rows DY0..DY0+DROWS-1 of the screen. Each item covers `len` pixels from x with one alpha
// (0..16): x in bits 0-9, len in 10-19, alpha in 20-24. s_row[r]..s_row[r+1] are row r's items.
#define DY0 80
#define DROWS 368
#define MAX_ITEMS 24000
static uint16_t *s_tmp;      // PSRAM: the digits rendered on black, once a minute
static uint32_t *s_items;    // PSRAM
static uint16_t s_row[DROWS + 1];
static char s_cached[8];

static void build_digits(const char *hhmm)
{
    if (!s_tmp) s_tmp = heap_caps_malloc(FW * DROWS * 2, MALLOC_CAP_SPIRAM);
    if (!s_items) s_items = heap_caps_malloc(MAX_ITEMS * 4, MALLOC_CAP_SPIRAM);
    if (!s_tmp || !s_items) return;
    memset(s_tmp, 0, FW * DROWS * 2);
    int oh = g_lcd_h;
    g_lcd_h = DROWS;  // draw on the band (same width as the screen, so the same stride)
    clock_draw_stacked(s_tmp, &font_round_xxl, 0, 8, rgb565(255, 255, 255), 0, FONT_FILL);
    g_lcd_h = oh;
    int n = 0;
    for (int r = 0; r < DROWS; r++) {
        s_row[r] = n;
        const uint16_t *src = s_tmp + r * FW;
        for (int x = 0; x < FW && n < MAX_ITEMS;) {
            int g = (__builtin_bswap16(src[x]) >> 5) & 63, a = (g * 16 + 31) / 63;
            if (!a) { x++; continue; }
            int x0 = x;
            while (x < FW && x - x0 < 1023) {
                int g2 = (__builtin_bswap16(src[x]) >> 5) & 63;
                if ((g2 * 16 + 31) / 63 != a) break;
                x++;
            }
            s_items[n++] = (uint32_t)x0 | (uint32_t)(x - x0) << 10 | (uint32_t)a << 20;
        }
    }
    s_row[DROWS] = n;
    strcpy(s_cached, hhmm);
}

static void lay_digits(uint16_t *row, int r)
{
    const uint16_t white = rgb565(255, 255, 255);
    for (int i = s_row[r]; i < s_row[r + 1]; i++) {
        uint32_t it = s_items[i];
        int x = it & 1023, len = (it >> 10) & 1023, a = it >> 20;
        if (a >= 16) {
            for (int k = 0; k < len; k++) row[x + k] = white;
        } else {
            for (int k = 0; k < len; k++) row[x + k] = gfx_blend(row[x + k], white, a);
        }
    }
}

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
    char hhmm[8];
    clock_hhmm(hhmm, true);
    if (strcmp(hhmm, s_cached) != 0) build_digits(hhmm);
    bool digits = s_cached[0] != 0;

    // One row of 2x2 blocks is worked out into internal RAM (two equal pixels per 32-bit word), then
    // copied to both screen rows: PSRAM is written in bursts instead of pixel by pixel
    static uint32_t line[BW];
    static uint16_t row[FW];
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
        for (int k = 0; k < 2; k++) {
            int r = y + k - DY0;
            if (digits && r >= 0 && r < DROWS && s_row[r] != s_row[r + 1]) {
                memcpy(row, line, FW * 2);
                lay_digits(row, r);
                memcpy(fb + (y + k) * FW, row, FW * 2);
            } else {
                memcpy(fb + (y + k) * FW, line, FW * 2);
            }
        }
    }

    // Date on the straight part of the top edge, the time big on the panel, seconds below
    char date[24];
    clock_date_str(date);
    uint16_t white = rgb565(255, 255, 255), ink = rgb565(10, 8, 30);
    gfx_text_ol(fb, (FW - gfx_text_width(date, 3)) / 2, 30, date, white, ink, 3, 1);
    if (!digits) clock_draw_stacked(fb, &font_round_xxl, DY0, 8, white, ink, FONT_FILL);  // no PSRAM: draw them    clock_draw_seconds_bar(fb, 92, 466, FW - 184, 5, white, rgb565(70, 70, 90));
}
