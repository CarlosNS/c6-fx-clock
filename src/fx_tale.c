// "Cuento" (landscape): a dark-fantasy journey painted like a children's storybook. A giant golden
// tree glows on the horizon, its leaves drift across a watercolour sky, and a little bear knight
// (original character: helmet with a plume, blue cape, wooden sword, round shield) walks towards it.
// Soft washes with paper grain and wobbly brown ink lines; the time is set in calligraphic ink.
#include <string.h>
#include "app.h"
#include "fx.h"
#include "gfx.h"

#define W LAND_W
#define H LAND_H
#define HORIZON 112
#define PATH_Y 152
#define TREE_X 236

static uint16_t s_ink, s_paper;

static inline uint16_t rgb(int r, int g, int b)
{
    return rgb565(r < 0 ? 0 : r > 255 ? 255 : r, g < 0 ? 0 : g > 255 ? 255 : g, b < 0 ? 0 : b > 255 ? 255 : b);
}

// Paper grain: a cheap, stable per-pixel jitter of a few levels
static inline int grain(int x, int y) { return (int)(((x * 37) ^ (y * 91) ^ (x * y)) & 7) - 4; }

static void sky(uint16_t *fb, int pulse)
{
    for (int y = 0; y < HORIZON; y++) {
        int f = y * 256 / HORIZON;
        // Dusky violet at the top into warm honey at the horizon
        int r = 96 + (152 * f >> 8), g = 84 + (118 * f >> 8), b = 128 + (12 * f >> 8);
        uint16_t *row = fb + y * W;
        // Watercolour blooms: slow bands of slightly different strength (one value per 8 px)
        for (int x0 = 0; x0 < W; x0 += 8) {
            int bloom = sin_q14[(x0 * 3 + y * 11 + (x0 * y >> 6)) & 1023] >> 12;
            uint16_t tone[8];
            for (int n = 0; n < 8; n++) tone[n] = rgb(r + n - 4 + bloom, g + n - 4 + bloom, b + n - 4);
            for (int x = x0; x < x0 + 8; x++) row[x] = tone[grain(x, y) + 4];
        }
    }
    gfx_glow(fb, TREE_X, 40, 80, rgb565(255, 214, 120), 6 + pulse / 64);
}

// y_end: where the next (nearer) layer fully covers this one
static void hill(uint16_t *fb, int base, int amp, int freq, int ph, int r, int g, int b, int scroll, int y_end)
{
    uint16_t ink = rgb(r * 6 / 10, g * 6 / 10, b * 6 / 10);
    // Wash tones by distance below the edge (darker right under it, like pigment pooling) x grain
    static uint16_t tone[42][8];
    for (int d = 0; d < 42; d++) {
        int k = d < 6 ? 236 - d * 4 : 212 + (d > 40 ? 0 : (40 - d) / 4);
        for (int n = 0; n < 8; n++) tone[d][n] = rgb((r * k >> 8) + n - 4, (g * k >> 8) + n - 4, (b * k >> 8) + n - 4);
    }
    for (int x = 0; x < W; x++) {
        int wx = x + scroll;
        int top = base - ((sin_q14[(wx * freq + ph) & 1023] * amp) >> 14) - ((sin_q14[(wx * freq * 3 + ph * 2) & 1023] * amp / 4) >> 14);
        if (top < 0) top = 0;
        for (int y = top; y < y_end; y++) {
            int d = y - top;
            fb[y * W + x] = tone[d > 41 ? 41 : d][grain(x, y) + 4];
        }
        // Wobbly ink edge, broken in places like a quick pen stroke
        if ((fx_hash(wx >> 3, base) & 7) != 0) fb[top * W + x] = ink;
    }
}

static void golden_tree(uint16_t *fb, uint32_t t, int pulse)
{
    uint16_t bark = rgb(150, 104, 52), bark_d = rgb(104, 68, 34);
    // Trunk: tapered, slightly twisted
    for (int y = 30; y < HORIZON + 2; y++) {
        int f = (y - 30) * 256 / (HORIZON - 30);
        int half = 1 + (f * 4 >> 8), cx = TREE_X + (sin_q14[(y * 9) & 1023] >> 13);
        for (int x = cx - half; x <= cx + half; x++) fb[y * W + x] = x < cx ? bark_d : bark;
        fb[y * W + cx - half - 1] = s_ink;
        fb[y * W + cx + half + 1] = s_ink;
    }
    // Branches reaching up into the crown
    static const int8_t br[][4] = {{0, -16, -34, -40}, {0, -18, 30, -44}, {0, -24, -14, -54}, {0, -26, 16, -58}, {0, -12, -50, -26}, {0, -12, 52, -28}};
    for (unsigned i = 0; i < sizeof(br) / sizeof(br[0]); i++)
        gfx_line(fb, TREE_X + br[i][0], 54 - br[i][1] - 16, TREE_X + br[i][2], 54 + br[i][3] + 20, bark_d);
    // Crown: a wide, lacy canopy of small golden clusters, each one shimmering
    for (int layer = 0; layer < 3; layer++) {
        static const uint8_t gold[3][3] = {{214, 142, 46}, {244, 188, 68}, {255, 226, 124}};
        int lift = pulse / 12;
        uint16_t c = rgb(gold[layer][0] + lift, gold[layer][1] + lift, gold[layer][2] + lift / 2);
        for (int i = 0; i < 26; i++) {
            uint32_t h = fx_hash(i, 5 + layer);
            int a = (int)(h & 1023), dist = 4 + (int)((h >> 10) % 40) * (4 - layer) / 4;
            int px = TREE_X + (sin_q14[(a + 256) & 1023] * dist * 3 / 2 >> 14);
            int py = 32 + (sin_q14[a & 1023] * dist * 9 / 20 >> 14) - layer * 3;
            int r = 9 - layer * 2 + (int)((h >> 16) % 4) + (sin_q14[(t / 6 + i * 80) & 1023] >> 13);
            if (layer == 0) gfx_circle(fb, px, py, r + 1, s_ink);
            gfx_circle(fb, px, py, r, c);
        }
    }
    // Sparkling leaves on the crown
    for (int i = 0; i < 40; i++) {
        uint32_t h = fx_hash(i, 23);
        int px = TREE_X - 60 + (int)(h % 120), py = 14 + (int)((h >> 8) % 34);
        if (sin_u8[(uint8_t)(t / 10 + i * 29)] > 200) fb[py * W + px] = rgb565(255, 252, 220);
    }
    gfx_glow(fb, TREE_X, 28, 36, rgb565(255, 244, 190), 5 + pulse / 50);
}

// Little bear knight walking right; x = body centre, ground = feet line
static void bear(uint16_t *fb, int x, int ground, uint32_t t)
{
    int step = (int)(t / 220) & 1, bob = step ? -1 : 0;
    int sway = sin_q14[(t / 2) & 1023] * 3 >> 14;
    uint16_t fur = rgb(150, 104, 70), fur_l = rgb(196, 152, 108), steel = rgb(176, 184, 196), steel_l = rgb(222, 228, 236);
    uint16_t cape = rgb(62, 92, 156), cape_d = rgb(44, 66, 118), wood = rgb(176, 128, 72), gold = rgb(232, 186, 70);
    int by = ground - 13 + bob, hy = by - 13;
    // Cape fluttering behind
    gfx_tri(fb, x - 3, hy + 6, x - 15 + sway, by + 10, x + 1, by + 9, s_ink);
    gfx_tri(fb, x - 3, hy + 7, x - 13 + sway, by + 8, x, by + 8, cape);
    gfx_tri(fb, x - 3, hy + 7, x - 9 + sway, by + 8, x - 3, by + 8, cape_d);
    // Legs
    for (int k = 0; k < 2; k++) {
        int lx = x - 3 + k * 6 + (k == step ? 2 : -1);
        gfx_circle(fb, lx, ground - 3, 4, s_ink);
        gfx_circle(fb, lx, ground - 3, 3, k ? fur : rgb(128, 88, 58));
    }
    // Body and belly
    gfx_circle(fb, x, by, 10, s_ink);
    gfx_circle(fb, x, by, 9, fur);
    gfx_circle(fb, x + 3, by + 2, 5, fur_l);
    // Sword over the shoulder, held in the far paw
    gfx_line(fb, x + 2, by - 1, x - 8, hy - 8, s_ink);
    gfx_line(fb, x + 3, by - 1, x - 7, hy - 8, wood);
    gfx_line(fb, x - 1, hy + 4, x + 3, hy + 1, gold);
    // Head with a round helmet; ears poke out, the face shows through the open visor
    gfx_circle(fb, x - 3, hy - 6, 3, s_ink);
    gfx_circle(fb, x - 3, hy - 6, 2, fur);
    gfx_circle(fb, x + 1, hy, 8, s_ink);
    gfx_circle(fb, x + 1, hy, 7, steel);
    gfx_circle(fb, x - 1, hy - 3, 3, steel_l);
    gfx_circle(fb, x + 4, hy + 2, 5, s_ink);
    gfx_circle(fb, x + 4, hy + 2, 4, fur_l);
    gfx_fill_rect(fb, x + 7, hy + 1, 2, 2, s_ink);   // nose
    fb[(hy) * W + x + 4] = s_ink;                     // eye
    fb[(hy + 3) * W + x + 5] = rgb(220, 120, 110);    // blush
    // Plume
    gfx_tri(fb, x - 1, hy - 7, x - 8 + sway, hy - 13, x + 3, hy - 9, rgb(206, 64, 60));
    // Round shield on the near arm
    gfx_circle(fb, x + 7, by + 2, 6, s_ink);
    gfx_circle(fb, x + 7, by + 2, 5, gold);
    gfx_circle(fb, x + 7, by + 2, 3, rgb(140, 40, 40));
    fb[(by + 2) * W + x + 7] = gold;
}

void tale_enter(void)
{
    s_ink = rgb565(70, 44, 30);
    s_paper = rgb565(250, 240, 214);
}

void tale_render(uint16_t *fb, uint32_t t, uint32_t dt)
{
    (void)dt;
    int pulse = sin_u8[(uint8_t)(t / 24)];  // 0..255, slow breath of light
    sky(fb, pulse);

    // Faint first stars in the violet top
    for (int i = 0; i < 18; i++) {
        uint32_t h = fx_hash(i, 3);
        int x = (int)(h % W), y = (int)((h >> 9) % 34);
        int a = sin_u8[(uint8_t)(t / 20 + i * 37)] / 24;
        if (a > 2 && (x < TREE_X - 70 || x > TREE_X + 70)) fb[y * W + x] = gfx_blend(fb[y * W + x], rgb565(255, 250, 230), a);
    }

    int sc = (int)(t / 400);
    hill(fb, 100, 8, 2, 80, 150, 140, 170, sc / 4, 126);  // far, lilac
    golden_tree(fb, t, pulse);
    hill(fb, 112, 10, 3, 300, 120, 150, 110, sc / 2, 148);  // mid, sage
    hill(fb, 136, 9, 4, 650, 108, 160, 84, sc, H);      // near, warm green

    // Path: a pale winding wash with ink dashes
    for (int y = PATH_Y - 4; y < PATH_Y + 8 && y < H; y++) {
        int w = (y - PATH_Y + 4) * 2;
        for (int x = 0; x < W; x++) {
            int edge = (sin_q14[((x + sc) * 4 + y * 20) & 1023] >> 13) + w / 6;
            if (y - (PATH_Y - 4) < 1 + edge / 3) continue;
            int n = grain(x, y);
            fb[y * W + x] = rgb(226 + n, 202 + n, 156 + n);
        }
    }
    for (int x = -(sc % 23); x < W; x += 23) gfx_line(fb, x, PATH_Y + 2, x + 6, PATH_Y + 2, rgb(184, 150, 108));

    // Flowers dotted on the near hill
    static const uint8_t fl[3][3] = {{236, 120, 140}, {250, 226, 140}, {246, 246, 255}};
    for (int i = 0; i < 26; i++) {
        uint32_t h = fx_hash(i, 17);
        int x = ((int)(h % (W + 40)) - sc % (W + 40) + W + 40) % (W + 40) - 20, y = 138 + (int)((h >> 10) % 10);
        const uint8_t *c = fl[(h >> 20) % 3];
        if ((unsigned)x < W - 2) {
            gfx_fill_rect(fb, x, y, 2, 2, rgb(c[0], c[1], c[2]));
            fb[(y + 2) * W + x] = rgb(70, 110, 60);
        }
    }

    // The bear knight crosses the scene, slowly, towards the tree
    int span = W + 60, bx = (int)((t / 60) % (uint32_t)span) - 30;
    gfx_darken_rect(fb, bx - 9, PATH_Y + 1, 20, 2, 1, 1);
    bear(fb, bx, PATH_Y + 2, t);

    // Golden leaves drifting from the tree, tumbling
    for (int i = 0; i < 30; i++) {
        uint32_t h = fx_hash(i, 41);
        int life = 9000 + (int)(h % 6000), u = (int)((t + h) % (uint32_t)life);
        int x = TREE_X - 40 + (int)((h >> 8) % 80) - u * (14 + (int)((h >> 16) % 20)) / 1000 + (sin_q14[(u / 4 + i * 77) & 1023] * 8 >> 14);
        int y = 30 + u * (10 + (int)((h >> 22) % 8)) / 1000;
        if ((unsigned)x >= W - 2 || (unsigned)y >= H - 2) continue;
        bool flat = (u / 160 + i) & 1;
        uint16_t c = (h & 1) ? rgb(250, 206, 80) : rgb(236, 160, 50);
        gfx_fill_rect(fb, x, y, flat ? 3 : 1, flat ? 1 : 2, c);
    }

    // Storybook time in calligraphic ink with a paper halo, date underneath
    clock_row_grad(fb, &font_nib_m, 84, 14, -3, rgb565(96, 58, 36), rgb565(58, 34, 22), s_paper, 0);
    char date[32];
    clock_date_str(date);
    ui_text(fb, &ui_m, 84 - ui_width(&ui_m, date) / 2, 72, date, s_ink, s_paper, FONT_FILL | FONT_OUTLINE);
    if (g_app.show_secs && g_now.valid) {
        int bw = 100, x0 = 84 - bw / 2;
        gfx_line(fb, x0, 92, x0 + bw, 92, rgb(196, 170, 130));
        int p = (int)(bw * g_now.minute_ms / 60000);
        gfx_line(fb, x0, 92, x0 + p, 92, s_ink);
        gfx_fill_rect(fb, x0 + p - 1, 91, 3, 3, rgb(232, 186, 70));
    }
}
