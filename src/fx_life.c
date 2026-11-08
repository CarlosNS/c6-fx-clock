// Conway's Game of Life on a 68x84 torus over the whole screen (wide canvas), colored by cell age with
// fading trails. The time is written with immortal cells, hours above minutes: life grows around the digits, and when the minute
// changes the old digits become mortal and dissolve into the simulation.
// Touch the phone pad to draw live cells.
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

#define CELL 4
#define GW (WW / CELL)
#define GH (WH / CELL)
#define OX ((WW - GW * CELL) / 2)  // centres the grid
#define GEN_MS 110
#define DIGIT_SCALE 4  // cells per 5x7 font pixel

enum { ALIVE = 1, NEXT = 2, IMMORTAL = 4 };

// In PSRAM: internal RAM is scarce and these are touched once per cell per frame
EXT_RAM_BSS_ATTR static uint8_t st[GH][GW], age[GH][GW], heat[GH][GW];
static uint32_t acc;
static int last_pop, stagnant, gens;
static char shown[8];  // time currently written in immortal cells

static void seed(void)
{
    for (int y = 0; y < GH; y++)
        for (int x = 0; x < GW; x++) {
            st[y][x] = (st[y][x] & IMMORTAL) | ((fx_rand() % 100) < 22 ? ALIVE : 0);
            age[y][x] = 0;
        }
    stagnant = 0;
    gens = 0;
}

static void stamp_char(int cx, int cy, char c)
{
    const uint8_t *g = gfx_glyph5x7(c);
    for (int col = 0; col < 5; col++)
        for (int row = 0; row < 7; row++)
            if (g[col] & (1 << row))
                for (int yy = 0; yy < DIGIT_SCALE; yy++)
                    for (int xx = 0; xx < DIGIT_SCALE; xx++)
                        st[cy + row * DIGIT_SCALE + yy][cx + col * DIGIT_SCALE + xx] |= IMMORTAL | ALIVE;
}

// Releases the old digits (they stay alive but mortal) and writes the new ones
static void write_time(const char *s)
{
    for (int y = 0; y < GH; y++)
        for (int x = 0; x < GW; x++) st[y][x] &= ~IMMORTAL;
    // "HH:MM": two digits per row, hours above minutes, and the colon as two dots between the rows
    int dw = 5 * DIGIT_SCALE, dh = 7 * DIGIT_SCALE, gap = 4, vgap = 10;
    int x0 = (GW - (2 * dw + gap)) / 2, y0 = (GH - (2 * dh + vgap)) / 2 - 4;
    for (int i = 0; i < 5 && s[i]; i++) {
        if (i == 2) {
            if (s[i] == ':')
                for (int k = 0; k < 2; k++)
                    for (int yy = 0; yy < 3; yy++)
                        for (int xx = 0; xx < 3; xx++)
                            st[y0 + dh + vgap / 2 - 1 + yy][GW / 2 - 6 + k * 9 + xx] |= IMMORTAL | ALIVE;
            continue;
        }
        int col = i < 2 ? i : i - 3, row = i < 2 ? 0 : 1;
        if (s[i] != ' ') stamp_char(x0 + col * (dw + gap), y0 + row * (dh + vgap), s[i]);
    }
    strcpy(shown, s);
}

static void step(void)
{
    int pop = 0;
    for (int y = 0; y < GH; y++) {
        int yu = (y + GH - 1) % GH, yd = (y + 1) % GH;
        for (int x = 0; x < GW; x++) {
            int xl = (x + GW - 1) % GW, xr = (x + 1) % GW;
            int n = (st[yu][xl] & 1) + (st[yu][x] & 1) + (st[yu][xr] & 1) + (st[y][xl] & 1) + (st[y][xr] & 1) +
                    (st[yd][xl] & 1) + (st[yd][x] & 1) + (st[yd][xr] & 1);
            uint8_t s = st[y][x];
            bool alive = (s & IMMORTAL) || ((s & ALIVE) ? (n == 2 || n == 3) : (n == 3));
            if (alive) st[y][x] |= NEXT;
            if (!(s & IMMORTAL)) pop += alive;
        }
    }
    for (int y = 0; y < GH; y++)
        for (int x = 0; x < GW; x++) {
            uint8_t s = st[y][x];
            bool was = s & ALIVE, now = s & NEXT;
            if (was && now) { if (age[y][x] < 255) age[y][x]++; }
            else if (now) age[y][x] = 0;
            else if (was) heat[y][x] = 255;
            st[y][x] = (s & IMMORTAL) | (now ? ALIVE : 0);
        }
    stagnant = (pop == last_pop) ? stagnant + 1 : 0;
    last_pop = pop;
    if (stagnant > 40 || pop < 30 || ++gens > 1500) seed();
}

void life_enter(void)
{
    memset(st, 0, sizeof(st));
    memset(heat, 0, sizeof(heat));
    shown[0] = 0;
    acc = 0;
    seed();
}

void life_render(uint16_t *fb, uint32_t t, uint32_t dt)
{
    int dx, dy;
    app_take_drag(&dx, &dy);
    if (g_app.touch) {
        int gx = g_app.tx * GW / 1000, gy = g_app.ty * GH / 1000;
        for (int oy = -1; oy <= 1; oy++)
            for (int ox = -1; ox <= 1; ox++)
                if (fx_rand() & 1) {
                    int x = (gx + ox + GW) % GW, y = (gy + oy + GH) % GH;
                    st[y][x] |= ALIVE;
                    age[y][x] = 0;
                }
    }

    char now[8];
    clock_hhmm(now, true);
    if (strcmp(now, shown) != 0) write_time(now);

    acc += dt;
    for (int n = 0; acc >= GEN_MS && n < 3; n++) {
        acc -= GEN_MS;
        step();
    }
    if (acc > GEN_MS * 3) acc = 0;

    gfx_fill(fb, rgb565(4, 5, 12));
    uint8_t fade = dt / 2 + 1;
    uint8_t hue0 = t / 60;
    uint16_t digit = hsv565(hue0 + 100, 60, 255);
    for (int y = 0; y < GH; y++) {
        for (int x = 0; x < GW; x++) {
            uint16_t c;
            uint8_t s = st[y][x];
            if (s & IMMORTAL) {
                c = digit;
            } else if (s & ALIVE) {
                int a = age[y][x] > 60 ? 60 : age[y][x];
                c = hsv565(hue0 + 120 + a * 2, 170 + a, 200 - a);
            } else if (heat[y][x]) {
                c = hsv565(hue0 + 200, 255, heat[y][x] / 4);
                heat[y][x] = heat[y][x] > fade ? heat[y][x] - fade : 0;
            } else {
                continue;
            }
            gfx_fill_rect(fb, OX + x * CELL, y * CELL, CELL - 1, CELL - 1, c);
        }
    }

    char date[24];
    clock_date_str(date);
    gfx_text_ol(fb, (WW - gfx_text_width(date, 2)) / 2, WH - 46, date, rgb565(200, 210, 255), rgb565(0, 0, 0), 2, 1);
    clock_draw_seconds_bar(fb, WW / 2 - 50, WH - 20, 100, 3, digit, rgb565(30, 30, 50));
}
