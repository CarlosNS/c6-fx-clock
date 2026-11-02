// Conway's Game of Life on an 80x43 torus (landscape), colored by cell age with fading trails.
// The time is written with immortal cells: life grows around the digits, and when the minute
// changes the old digits become mortal and dissolve into the simulation.
// Touch the phone pad to draw live cells.
#include <string.h>
#include "app.h"
#include "control.h"
#include "fx.h"
#include "gfx.h"

#define CELL 4
#define GW (LAND_W / CELL)
#define GH (LAND_H / CELL)
#define GEN_MS 110
#define DIGIT_SCALE 3  // cells per 5x7 font pixel

enum { ALIVE = 1, NEXT = 2, IMMORTAL = 4 };

static uint8_t st[GH][GW], age[GH][GW], heat[GH][GW];
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
    if (c == ':') {
        for (int k = 0; k < 2; k++)
            for (int yy = 0; yy < 2; yy++)
                for (int xx = 0; xx < 2; xx++) st[cy + 5 + k * 9 + yy][cx + xx] |= IMMORTAL | ALIVE;
        return;
    }
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
    int dw = 5 * DIGIT_SCALE, w = 4 * dw + 4 * 3 + 2;  // 4 digits + 2-cell colon, 4 gaps of 3
    int x = (GW - w) / 2, y = (GH - 7 * DIGIT_SCALE) / 2;
    for (int i = 0; s[i]; i++) {
        if (s[i] == ':') {
            stamp_char(x, y, ':');
            x += 2 + 3;
        } else {
            if (s[i] != ' ') stamp_char(x, y, s[i]);
            x += dw + 3;
        }
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
            gfx_fill_rect(fb, x * CELL, y * CELL, CELL - 1, CELL - 1, c);
        }
    }

    char date[24];
    clock_date_str(date);
    gfx_text_ol(fb, 6, LAND_H - 12, date, rgb565(200, 210, 255), rgb565(0, 0, 0), 1, 1);
    clock_draw_seconds_bar(fb, LAND_W - 86, LAND_H - 8, 80, 3, digit, rgb565(30, 30, 50));
}
