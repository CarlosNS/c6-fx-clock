// "Aventura" (full screen, wide canvas, portrait): a travelling scene in the spirit of modern pixel-art RPGs.
// Native resolution, layered parallax with atmospheric perspective (each layer fades into the
// haze), a lake that reflects everything above it, and lighting that follows the real time of day:
// sun and moon with glow, tinted clouds, lanterns that light up at dusk, fireflies at night.
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "app.h"
#include "control.h"
#include "fx.h"
#include "gfx.h"
#include "sprites.h"

#define W WW
#define H WH
#define LAKE_TOP 196
#define LAKE_BOT 230
#define PATH_TOP 248
#define PATH_BOT 266
#define FEET_Y 263

typedef struct { int r, g, b; } rgb_t;

// Time-of-day keyframes: sky top, horizon, ambient light (x/256), haze
static const struct { float h; rgb_t top, hor, amb, fog; } keys[] = {
    {0,    {10, 12, 40},   {40, 40, 92},    {92, 102, 165},  {40, 46, 96}},
    {5,    {20, 20, 62},   {92, 62, 122},   {112, 106, 172}, {82, 66, 122}},
    {6.5,  {82, 92, 172},  {255, 165, 125}, {235, 175, 165}, {240, 172, 152}},
    {8,    {72, 142, 232}, {192, 226, 255}, {256, 250, 240}, {182, 212, 242}},
    {16.5, {72, 142, 232}, {192, 226, 255}, {256, 250, 240}, {182, 212, 242}},
    {18.5, {92, 72, 172},  {255, 142, 92},  {250, 172, 132}, {250, 152, 122}},
    {20,   {42, 32, 92},   {152, 72, 122},  {152, 112, 172}, {112, 72, 132}},
    {21.5, {12, 14, 45},   {45, 45, 102},   {96, 102, 166},  {45, 50, 102}},
    {24,   {10, 12, 40},   {40, 40, 92},    {92, 102, 165},  {40, 46, 96}},
};
#define NKEYS (sizeof(keys) / sizeof(keys[0]))

static rgb_t s_top, s_hor, s_amb, s_fog;
static int s_night;  // 0 = day .. 256 = deep night

static inline uint32_t hash(uint32_t x, uint32_t y)
{
    uint32_t h = x * 374761393u + y * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

static inline rgb_t mix(rgb_t a, rgb_t b, int f)  // f 0..256
{
    return (rgb_t){a.r + ((b.r - a.r) * f >> 8), a.g + ((b.g - a.g) * f >> 8), a.b + ((b.b - a.b) * f >> 8)};
}

static inline uint16_t c565(rgb_t c)
{
    return rgb565(c.r > 255 ? 255 : c.r < 0 ? 0 : c.r, c.g > 255 ? 255 : c.g < 0 ? 0 : c.g, c.b > 255 ? 255 : c.b < 0 ? 0 : c.b);
}

// Lit by the ambient light, then pushed into the haze by `depth` (0 = near, 256 = lost in haze)
static uint16_t shade(int r, int g, int b, int depth)
{
    rgb_t c = {r * s_amb.r >> 8, g * s_amb.g >> 8, b * s_amb.b >> 8};
    return c565(mix(c, s_fog, depth));
}

static void update_light(float hf)
{
    unsigned k = 0;
    while (k < NKEYS - 2 && keys[k + 1].h <= hf) k++;
    int f = (int)((hf - keys[k].h) / (keys[k + 1].h - keys[k].h) * 256);
    s_top = mix(keys[k].top, keys[k + 1].top, f);
    s_hor = mix(keys[k].hor, keys[k + 1].hor, f);
    s_amb = mix(keys[k].amb, keys[k + 1].amb, f);
    s_fog = mix(keys[k].fog, keys[k + 1].fog, f);
    float n = hf < 5 ? 1 : hf < 7.5f ? (7.5f - hf) / 2.5f : hf < 18 ? 0 : hf < 21 ? (hf - 18) / 3 : 1;
    s_night = (int)(n * 256);
}

static void hline_blend(uint16_t *fb, int y, uint16_t c, int a)
{
    if (a <= 0 || y < 0 || y >= H) return;
    uint16_t *row = fb + y * W;
    for (int x = 0; x < W; x++) row[x] = gfx_blend(row[x], c, a > 16 ? 16 : a);
}

static void cloud(uint16_t *fb, int cx, int cy, int size, uint32_t seed)
{
    rgb_t lit = mix((rgb_t){255, 255, 255}, s_hor, 90), mid = mix(lit, s_top, 70), dark = mix(s_hor, s_top, 150);
    lit = mix(lit, s_top, s_night * 3 / 4);
    mid = mix(mid, s_top, s_night * 3 / 4);
    dark = mix(dark, s_top, s_night * 3 / 4);
    struct { int dx, dy, r; } puff[7];
    for (int i = 0; i < 7; i++) {
        uint32_t h = hash(seed, i);
        puff[i].dx = (i - 3) * size * 5 / 8 + (int)(h % 5) - 2;
        puff[i].r = size * (5 + (int)((h >> 8) % 4)) / 8 - (i == 0 || i == 6 ? size / 4 : 0);
        puff[i].dy = -(puff[i].r / 3) + (i == 0 || i == 6 ? size / 4 : 0);
    }
    const uint16_t tones[3] = {c565(dark), c565(mid), c565(lit)};
    for (int layer = 0; layer < 3; layer++)
        for (int i = 0; i < 7; i++) {
            int r = puff[i].r - layer * 2;
            if (r > 0) gfx_circle(fb, cx + puff[i].dx - layer, cy + puff[i].dy - layer * 2, r, tones[layer]);
        }
}

// Height of a mountain range at world x: ridged sines (sharp peaks) plus a little detail
static int ridge(int wx, int base, int a1, int f1, int a2, int f2, int ph)
{
    int r1 = 16384 - abs(sin_q14[(wx * f1 + ph) & 1023]);
    int r2 = 16384 - abs(sin_q14[(wx * f2 + ph * 3) & 1023]);
    int r3 = sin_q14[(wx * f2 * 3 + ph) & 1023];
    return base - (((r1 * r1 >> 14) * a1 + r2 * a2 + r3 * 2) >> 14);
}

static void mountains(uint16_t *fb, int scroll, int base, int a1, int f1, int a2, int f2, int ph, rgb_t col,
                      int depth, bool snow, bool sun_left)
{
    uint16_t lit = shade(col.r + 34, col.g + 32, col.b + 26, depth), dark = shade(col.r - 6, col.g - 4, col.b, depth);
    uint16_t rim = shade(col.r + 80, col.g + 70, col.b + 56, depth - 30);
    uint16_t snow_l = shade(240, 244, 255, depth - 40), snow_d = shade(178, 188, 222, depth);
    int hts[W + 8];
    for (int i = 0; i < W + 8; i++) hts[i] = ridge(scroll + i - 4, base, a1, f1, a2, f2, ph);
    for (int x = 0; x < W; x++) {
        int top = hts[x + 4];
        // Which way the slope faces, measured over a few pixels so small bumps don't flicker
        int slope = hts[x + 7] - hts[x + 1];
        bool facing_sun = sun_left ? slope > 0 : slope < 0;
        if (top < 0) top = 0;
        uint16_t body = facing_sun ? lit : dark;
        int snowline = base - a1 * 2 / 3;
        for (int y = top; y < LAKE_TOP; y++) {
            uint16_t c = body;
            if (snow && y < snowline && y - top < 5 + ((x * 7) & 3)) c = facing_sun ? snow_l : snow_d;
            else if (y == top) c = facing_sun ? rim : body;
            fb[y * W + x] = c;
        }
    }
}

static void treeline(uint16_t *fb, int scroll)
{
    uint16_t dark = shade(38, 82, 66, 70), mid = shade(52, 104, 78, 60), rim = shade(110, 160, 110, 50);
    for (int x = 0; x < W; x++) {
        int wx = x + scroll, best = LAKE_TOP;
        for (int i = wx / 7 - 2; i <= wx / 7 + 2; i++) {
            uint32_t h = hash(i, 11);
            int c = i * 7 + (int)(h % 4), r = 4 + (int)((h >> 8) % 4), d = wx - c;
            if (d < -r || d > r) continue;
            int k = 0;
            while ((k + 1) * (k + 1) <= r * r - d * d) k++;
            int top = LAKE_TOP - 5 - (int)((h >> 16) % 5) - k;
            if (top < best) best = top;
        }
        for (int y = best; y < LAKE_TOP; y++) fb[y * W + x] = y == best ? rim : (y < best + 3 ? mid : dark);
    }
}

static void lake(uint16_t *fb, uint32_t t, int sun_x, bool sun_up)
{
    uint16_t water = c565(mix(mix(s_top, (rgb_t){20, 45, 80}, 120), (rgb_t){0, 0, 0}, s_night / 3));
    uint16_t glint = c565(mix((rgb_t){255, 240, 200}, s_hor, 60));
    for (int y = LAKE_TOP; y < LAKE_BOT; y++) {
        int d = y - LAKE_TOP;
        int src = LAKE_TOP - 1 - d * 9 / 8;
        if (src < 0) src = 0;
        int amp = 1 + d / 7;
        int off = (sin_q14[(y * 61 + t / 4) & 1023] * amp) >> 14;
        int a = 5 + d / 5;
        uint16_t *row = fb + y * W, *srow = fb + src * W;
        for (int x = 0; x < W; x++) {
            int sx = x + off;
            sx = sx < 0 ? 0 : sx >= W ? W - 1 : sx;
            row[x] = gfx_blend(srow[sx], water, a);
        }
        // A few short glints on the water under the sun / moon
        if (d & 1) continue;
        int spread = 8 + d * 2, n = sun_up ? 2 + d / 6 : 1 + d / 10;
        for (int i = 0; i < n; i++) {
            uint32_t hsh = hash(i * 31 + y, t / 260);
            int gx = sun_x + (int)(hsh % (2 * spread)) - spread, len = 2 + (int)((hsh >> 10) % 3);
            for (int x = gx; x < gx + len; x++)
                if ((unsigned)x < W) row[x] = gfx_blend(row[x], glint, sun_up ? 11 : 8);
        }
    }
    hline_blend(fb, LAKE_TOP, c565(mix(s_hor, (rgb_t){255, 255, 255}, 80)), 6);
}

static void ground(uint16_t *fb, int scroll, uint32_t t)
{
    uint16_t g_lit = shade(108, 182, 84, 0), g_mid = shade(84, 150, 72, 0), g_dark = shade(56, 112, 62, 0);
    uint16_t p_lit = shade(208, 170, 118, 0), p_mid = shade(186, 146, 100, 0), p_dark = shade(150, 112, 80, 0);
    uint16_t stone = shade(176, 172, 160, 0), shore = shade(130, 120, 100, 0);
    static const uint8_t flowers[3][3] = {{235, 85, 95}, {250, 215, 85}, {245, 245, 255}};
    for (int x = 0; x < W; x++) {
        int wx = x + scroll;
        int edge = LAKE_BOT - 1 + (int)(hash(wx / 3, 3) % 3);
        int ptop = PATH_TOP + (sin_q14[(wx * 5) & 1023] >> 13), pbot = PATH_BOT + (sin_q14[(wx * 3 + 200) & 1023] >> 13);
        for (int y = edge - 1; y < H; y++) {
            uint16_t c;
            if (y < edge + 1) c = shore;
            else if (y < ptop) c = (hash(wx >> 1, y >> 1) & 3) == 0 ? g_dark : (y < edge + 4 ? g_lit : g_mid);
            else if (y < pbot) {
                uint32_t h = hash(wx >> 2, y >> 1);
                c = (y == ptop || y == pbot - 1) ? p_dark : (h % 11 == 0 ? p_dark : (h % 13 == 0 ? stone : (y < ptop + 3 ? p_lit : p_mid)));
            } else {
                uint32_t h = hash(wx, y);
                c = (hash(wx >> 1, y >> 1) & 3) == 0 ? g_dark : g_mid;
                if (y > pbot + 2 && h % 41 == 0) {
                    const uint8_t *f = flowers[(h >> 8) % 3];
                    c = shade(f[0], f[1], f[2], 0);
                }
            }
            fb[y * W + x] = c;
        }
        // Grass blades on the shore, swaying slightly
        if (hash(wx, 9) % 3 == 0) {
            int bh = 2 + (int)(hash(wx, 10) % 4);
            for (int k = 1; k <= bh; k++) {
                int y = edge - k;
                if (y >= 0) fb[y * W + x] = g_lit;
            }
        }
    }
}

static void lantern(uint16_t *fb, int x, int night)
{
    uint16_t wood = shade(80, 58, 44, 0), frame = shade(50, 40, 40, 0);
    int top = PATH_TOP - 30;
    gfx_fill_rect(fb, x, top + 6, 2, PATH_TOP - top - 5, wood);
    gfx_fill_rect(fb, x - 3, top, 8, 7, frame);
    rgb_t off = {150, 140, 120}, on = {255, 214, 130};
    gfx_fill_rect(fb, x - 2, top + 1, 6, 5, c565(mix(mix(off, (rgb_t){60, 60, 80}, s_night / 2), on, night)));
    gfx_fill_rect(fb, x - 4, top - 1, 10, 2, frame);
    if (night > 20) {
        gfx_glow(fb, x + 1, top + 3, 30, rgb565(255, 190, 100), night * 9 / 256);
        gfx_glow(fb, x + 1, PATH_TOP + 7, 22, rgb565(255, 190, 100), night * 5 / 256);
    }
}

static void shadow(uint16_t *fb, int cx, int cy, int rx)
{
    for (int y = -2; y <= 2; y++) {
        int w = rx * (9 - y * y) / 9;
        gfx_darken_rect(fb, cx - w, cy + y, w * 2, 1, 0, 1);
    }
}

void quest_enter(void) {}

void quest_render(uint16_t *fb, uint32_t t, uint32_t dt)
{
    int dx, dy;
    app_take_drag(&dx, &dy);
    float hf = g_now.valid ? g_now.h + g_now.m / 60.0f : 12.0f;
    update_light(hf);
    int night = s_night;
    uint64_t tt = t;

    // Sky gradient
    for (int y = 0; y < LAKE_TOP; y++) {
        int f = y * 256 / LAKE_TOP;
        uint16_t c = c565(mix(s_top, s_hor, f * f >> 8));
        uint16_t *row = fb + y * W;
        for (int x = 0; x < W; x++) row[x] = c;
    }
    // Stars and the odd shooting star
    if (night > 60) {
        for (int i = 0; i < 70; i++) {
            uint32_t h = hash(i, 77);
            int x = h % W, y = (h >> 9) % 170;
            int tw = sin_u8[(uint8_t)(t / 18 + i * 41)];
            int a = (tw - 60) * night / 256 / 10;
            if (a > 0) fb[y * W + x] = gfx_blend(fb[y * W + x], (h & 7) == 0 ? rgb565(255, 210, 170) : rgb565(220, 230, 255), a > 16 ? 16 : a);
        }
        uint32_t cyc = t % 14000;
        if (cyc < 700) {
            int sx = 50 + (int)(hash(t / 14000, 5) % 150) + cyc / 5, sy = 100 + (int)(hash(t / 14000, 6) % 40) + cyc / 12;
            for (int k = 0; k < 14; k++) {
                int x = sx - k * 2, y = sy - k * 2 / 5;
                if ((unsigned)x < W && (unsigned)y < LAKE_TOP) fb[y * W + x] = gfx_blend(fb[y * W + x], rgb565(255, 255, 255), 16 - k);
            }
        }
    }

    // Sun 6h-20h, moon 19h-7h, travelling along an arc with a soft glow
    bool sun_up = hf > 6 && hf < 20;
    int body_x = W / 2, body_y = 200;
    if (sun_up) {
        float p = (hf - 6) / 14;
        body_x = 24 + (int)(p * (W - 48));
        body_y = 188 - (int)(sinf(p * 3.14159f) * 80);
        gfx_glow(fb, body_x, body_y, 46, c565(mix((rgb_t){255, 230, 150}, s_hor, 80)), 10);
        gfx_circle(fb, body_x, body_y, 9, rgb565(255, 236, 170));
        gfx_circle(fb, body_x, body_y, 7, rgb565(255, 250, 225));
    } else {
        float mh = hf >= 19 ? hf - 19 : hf + 5;
        float p = mh / 12;
        body_x = 24 + (int)(p * (W - 48));
        body_y = 188 - (int)(sinf(p * 3.14159f) * 76);
        gfx_glow(fb, body_x, body_y, 30, rgb565(170, 190, 255), 7);
        gfx_circle(fb, body_x, body_y, 7, rgb565(236, 236, 220));
        gfx_circle(fb, body_x - 2, body_y - 1, 2, rgb565(205, 205, 195));
        gfx_circle(fb, body_x + 3, body_y + 2, 1, rgb565(210, 210, 200));
    }

    int sc_cloud = (int)(tt * 3 / 1000), sc_far = (int)(tt * 4 / 1000), sc_mid = (int)(tt * 9 / 1000),
        sc_trees = (int)(tt * 16 / 1000), sc_near = (int)(tt * 30 / 1000), sc_fore = (int)(tt * 46 / 1000);

    for (int i = 0; i < 5; i++) {
        int span = W + 120;
        int x = ((i * 97 + 40 - sc_cloud) % span + span) % span - 60;
        cloud(fb, x, 104 + (i % 2) * 24 + i * 4, 13 + (i % 3) * 4, i * 31 + 5);
    }

    bool sun_left = body_x < W / 2;
    mountains(fb, sc_far, 184, 56, 2, 18, 5, 90, (rgb_t){118, 128, 178}, 150, true, sun_left);
    for (int y = 164; y < LAKE_TOP; y++) hline_blend(fb, y, c565(s_fog), (y - 164) / 3);  // haze
    mountains(fb, sc_mid, 192, 32, 4, 12, 9, 400, (rgb_t){78, 110, 128}, 95, false, sun_left);
    for (int y = 180; y < LAKE_TOP; y++) hline_blend(fb, y, c565(s_fog), (y - 180) / 2);
    treeline(fb, sc_trees);
    lake(fb, t, body_x, sun_up);
    ground(fb, sc_near, t);

    // Lanterns along the path (behind the party)
    for (int lx = -(sc_near % 210); lx < W + 20; lx += 210) lantern(fb, lx + 40, night);

    // The party: warrior leads, mage follows half a step behind
    bool step_a = (t / 160) & 1, step_m = ((t + 80) / 160) & 1;
    uint16_t party_cmap[128];
    int pl = 256 - night / 3;
    sprite_build_cmap(party_cmap, pl);
    shadow(fb, 158, FEET_Y, 9);
    shadow(fb, 130, FEET_Y, 9);
    sprite_draw(fb, step_a ? &spr_warrior_a : &spr_warrior_b, 150, FEET_Y - 23 - (step_a ? 0 : 1), 1, false, party_cmap);
    sprite_draw(fb, step_m ? &spr_mage_a : &spr_mage_b, 122, FEET_Y - 21 - (step_m ? 0 : 1), 1, false, party_cmap);

    // Foreground grass clumps, faster than the path
    uint16_t fg = shade(40, 92, 52, 0), fg_l = shade(70, 130, 70, 0);
    for (int i = sc_fore / 37 - 1; i <= (sc_fore + W) / 37 + 1; i++) {
        uint32_t h = hash(i, 21);
        if (h % 3) continue;
        int bx = i * 37 + (int)(h % 20) - sc_fore;
        for (int k = 0; k < 7; k++) {
            int hgt = 12 + (int)((h >> (k * 3)) % 16);
            int sway = (sin_q14[(t / 3 + i * 90 + k * 40) & 1023] * 2) >> 14;
            gfx_line(fb, bx + k * 2, H, bx + k * 2 + sway + (k - 3), H - hgt, k & 1 ? fg : fg_l);
        }
    }

    // Fireflies at night, birds by day
    if (night > 90) {
        for (int i = 0; i < 20; i++) {
            uint32_t h = hash(i, 51);
            int x = (int)(h % W) + ((sin_q14[(t / 7 + i * 73) & 1023] * 14) >> 14);
            int y = LAKE_BOT + 4 + (int)((h >> 9) % 76) + ((sin_q14[(t / 5 + i * 151) & 1023] * 8) >> 14);
            int a = sin_u8[(uint8_t)(t / 9 + i * 37)] * night / 256;
            if (a > 120) {
                gfx_glow(fb, x, y, 5, rgb565(200, 255, 120), (a - 120) / 10);
                if ((unsigned)x < W && (unsigned)y < H) fb[y * W + x] = rgb565(240, 255, 190);
            }
        }
    } else {
        uint16_t bird = shade(40, 40, 60, 60);
        for (int i = 0; i < 3; i++) {
            int x = W - (int)((tt / 35 + i * 140) % (W + 200)) + 100, y = 116 + i * 9 + ((sin_q14[(t / 4 + i * 200) & 1023] * 3) >> 14);
            int flap = ((t / 150 + i) & 1) ? 2 : -1;
            gfx_line(fb, x - 3, y - flap, x, y, bird);
            gfx_line(fb, x, y, x + 3, y - flap, bird);
        }
    }

    // HUD: clean time + date, soft outline
    // Pixel-art time in the style of 16-bit RPG HUDs: cream-to-gold bands, dark outline, soft shadow
    const uint16_t gold[4] = {rgb565(255, 252, 236), rgb565(255, 236, 168), rgb565(255, 204, 104), rgb565(232, 150, 64)};
    char hhmm[8];
    clock_hhmm(hhmm, !g_now.valid || g_now.ms < 500);
    pix_digits_draw(fb, (W - pix_digits_width("00:00", 5)) / 2, 30, hhmm, 5, gold, rgb565(52, 26, 40));
    char date[32];
    if (g_now.valid) snprintf(date, sizeof(date), "%s %d %s", clock_wday_name(g_now.wday), g_now.day, clock_month_name(g_now.mon));
    else strcpy(date, "SIN HORA");
    ui_text(fb, &ui_m, (W - ui_width(&ui_m, date)) / 2, 72, date, gold[0], rgb565(52, 26, 40), FONT_FILL | FONT_OUTLINE);
    if (g_app.show_secs && g_now.valid) {
        int bw = 120, bx = (W - bw) / 2;
        gfx_fill_rect(fb, bx - 1, 93, bw + 2, 4, rgb565(52, 26, 40));
        gfx_fill_rect(fb, bx, 94, (int)(bw * g_now.minute_ms / 60000), 2, gold[1]);
    }
}
