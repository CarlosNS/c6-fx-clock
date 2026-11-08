// "Arena" (full screen, wide canvas): an hourglass with real falling-sand physics. A gate at the neck lets
// exactly enough grains through for the top bulb to empty in one minute; when the minute
// changes the hourglass turns over.
#include <stdio.h>
#include "esp_attr.h"
#include <string.h>
#include "esp_attr.h"
#include "app.h"
#include "esp_attr.h"
#include "control.h"
#include "esp_attr.h"
#include "fx.h"
#include "esp_attr.h"
#include "gfx.h"
#include "esp_attr.h"

#define W WW
#define H WH
#define CELL 2
#define GW 56            // grid columns (112 px wide)
#define GH 104           // grid rows (208 px tall)
#define GX ((W - GW * CELL) / 2)
#define GY 94
#define NECK (GH / 2)
#define FLIP_MS 900

EXT_RAM_BSS_ATTR static uint8_t g[GH][GW];        // 0 empty, 1..255 grain (shade)
static uint8_t s_half[GH];       // glass half-width in cells for each row
static int s_total, s_passed, s_last_min = -1;
static uint32_t s_flip_t;
static bool s_flipping;

static void build_glass(void)
{
    for (int y = 0; y < GH; y++) {
        int d = y < NECK ? NECK - y : y - NECK + 1;  // distance from the neck
        int w = 1 + d * d * 26 / (NECK * NECK) + d / 3;
        if (w > GW / 2 - 1) w = GW / 2 - 1;
        if (y < 3 || y > GH - 4) w = GW / 2 - 1 - (y < 3 ? 3 - y : y - (GH - 4));
        s_half[y] = w;
    }
}

static inline bool inside(int x, int y)
{
    return y >= 0 && y < GH && abs(x * 2 + 1 - GW) < s_half[y] * 2;
}

// Top bulb filled up to `frac` (0..256) of its capacity, the rest piled in the bottom
static void fill(int frac)
{
    memset(g, 0, sizeof(g));
    int cap = 0;
    for (int y = 0; y < NECK; y++)
        for (int x = 0; x < GW; x++) cap += inside(x, y);
    int top = cap * frac / 256, bottom = cap - top;
    s_total = top;
    s_passed = 0;
    for (int y = NECK - 1; y >= 0 && top > 0; y--)  // top bulb settles at the bottom of the bulb
        for (int x = 0; x < GW && top > 0; x++)
            if (inside(x, y)) { g[y][x] = 160 + (fx_hash(x, y) & 63); top--; }
    for (int y = GH - 1; y > NECK && bottom > 0; y--)
        for (int x = 0; x < GW && bottom > 0; x++)
            if (inside(x, y)) { g[y][x] = 160 + (fx_hash(x, y + 7) & 63); bottom--; }
}

void sand_enter(void)
{
    build_glass();
    fill(g_now.valid ? 256 - (int)(g_now.minute_ms * 256 / 60000) : 256);
    s_flipping = false;
    s_last_min = g_now.valid ? g_now.m : -1;
}

static void step(int target)
{
    for (int y = GH - 2; y >= 0; y--) {
        bool ltr = fx_hash(y, s_passed) & 1;
        for (int i = 0; i < GW; i++) {
            int x = ltr ? i : GW - 1 - i;
            if (!g[y][x]) continue;
            if (y == NECK - 1 && s_passed >= target) continue;  // the gate keeps time
            int ny = y + 1;
            int nx = x;
            if (!(inside(x, ny) && !g[ny][x])) {
                int d = (fx_hash(x, y + s_passed) & 1) ? 1 : -1;
                if (inside(x + d, ny) && !g[ny][x + d]) nx = x + d;
                else if (inside(x - d, ny) && !g[ny][x - d]) nx = x - d;
                else continue;
            }
            g[ny][nx] = g[y][x];
            g[y][x] = 0;
            if (y == NECK - 1) s_passed++;
        }
    }
}

void sand_render(uint16_t *fb, uint32_t t, uint32_t dt)
{
    int dx_, dy_;
    app_take_drag(&dx_, &dy_);

    if (g_now.valid && g_now.m != s_last_min) {
        s_last_min = g_now.m;
        s_flipping = true;
        s_flip_t = t;
    }
    float squash = 1;
    if (s_flipping) {
        uint32_t ft = t - s_flip_t;
        if (ft >= FLIP_MS) {
            s_flipping = false;
            // The sand that piled at the bottom is now the top: mirror the grid
            for (int y = 0; y < GH / 2; y++)
                for (int x = 0; x < GW; x++) {
                    uint8_t k = g[y][x];
                    g[y][x] = g[GH - 1 - y][x];
                    g[GH - 1 - y][x] = k;
                }
            s_total = 0;
            for (int y = 0; y < NECK; y++)
                for (int x = 0; x < GW; x++) s_total += g[y][x] != 0;
            s_passed = 0;
        } else {
            squash = (float)(ft < FLIP_MS / 2 ? FLIP_MS / 2 - ft : ft - FLIP_MS / 2) / (FLIP_MS / 2);
        }
    }
    if (!s_flipping) {
        int target = g_now.valid ? (int)((int64_t)s_total * g_now.minute_ms / 59000) : s_passed + 2;
        for (int k = 0; k < 2; k++) step(target);
    }

    // Background: dark wooden desk with a warm lamp glow
    for (int y = 0; y < H; y++) {
        int f = y * 256 / H;
        gfx_fill_rect(fb, 0, y, W, 1, rgb565(30 + f / 10, 20 + f / 14, 18 + f / 20));
    }

    // Hourglass: draw rows through a vertical squash for the turning animation
    int cy = GY + GH;  // center of the glass in pixels
    int half_h = GH * CELL / 2 + 14;
    bool upside = s_flipping && (t - s_flip_t) > FLIP_MS / 2;
    uint16_t wood = rgb565(120, 70, 36), wood_l = rgb565(170, 110, 60);
    int ph = (int)(half_h * squash);
    gfx_fill_rect(fb, GX - 10, cy - ph - 8, GW * CELL + 20, 8, wood_l);
    gfx_fill_rect(fb, GX - 10, cy + ph, GW * CELL + 20, 8, wood);
    gfx_fill_rect(fb, GX - 8, cy - ph, 5, ph * 2, wood);
    gfx_fill_rect(fb, GX + GW * CELL + 3, cy - ph, 5, ph * 2, wood);

    uint16_t glass = rgb565(140, 170, 190);
    for (int py = cy - GH * CELL / 2 * squash; py < cy + GH * CELL / 2 * squash; py++) {
        if (py < 0 || py >= H) continue;
        int gy = (int)((py - cy) / (CELL * (squash < 0.05f ? 0.05f : squash))) + GH / 2;
        if (gy < 0 || gy >= GH) continue;
        if (upside) gy = GH - 1 - gy;
        int hw = s_half[gy] * CELL;
        uint16_t *row = fb + py * W;
        int x0 = W / 2 - hw, x1 = W / 2 + hw;
        for (int x = x0; x < x1; x++) row[x] = gfx_blend(row[x], glass, 3);
        row[x0 - 1] = row[x1] = glass;
        if (gy < GH / 2 - 4) row[x0 + 3] = gfx_blend(row[x0 + 3], rgb565(255, 255, 255), 8);  // reflection
        for (int gx = 0; gx < GW; gx++) {
            uint8_t s = g[gy][gx];
            if (!s) continue;
            uint16_t c = rgb565(s, s * 3 / 4, s / 3);
            int x = GX + gx * CELL;
            row[x] = row[x + 1] = c;
        }
    }
    // Falling stream through the neck
    if (!s_flipping && s_passed < s_total) {
        for (int y = cy; y < cy + 40; y += 2)
            if (fx_hash(y, t / 30) & 1) fb[y * W + W / 2] = rgb565(220, 170, 80);
    }

    clock_row_grad(fb, &font_round_m, W / 2, 6, -5, rgb565(255, 240, 210), rgb565(230, 170, 90), rgb565(40, 20, 10), 2);
    char date[24];
    clock_date_str(date);
    ui_text(fb, &ui_s, (W - ui_width(&ui_s, date)) / 2, H - 16, date, rgb565(230, 200, 150), rgb565(30, 18, 10),
            FONT_FILL | FONT_OUTLINE);
}
