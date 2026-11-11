// "Disfruta" (full screen, wide canvas, pixel art on art.c): free time. A meadow: grass and a lone tree
// bending in a gentle, gusting wind, slow clouds, seeds drifting, flowers among the grass. A slow day
// goes by (3 minutes at normal speed): a golden afternoon, the sun setting behind the hills, a night
// with the moon, stars, fireflies over the grass and now and then fireworks, then dawn. No time on
// purpose: only "Disfruta" ("Relax" in English).
#include <math.h>
#include <stdio.h>
#include "app.h"
#include "art.h"
#include "control.h"
#include "fx.h"
#include "gfx.h"

#define NBLADES 260
#define CYCLE 180.0f   // seconds of animation time for the whole day

static uint16_t C(uint32_t v) { return rgb565(v >> 16, (v >> 8) & 255, v & 255); }
static int s_dark;  // 0..11: how much dusk or night darkens the land
// A colour of the land (hills, grass, the tree) as the light goes
static uint16_t D(uint32_t v) { return gfx_blend(C(v), C(0x0a1030), s_dark); }

typedef struct {
    float x;
    uint8_t h, base, shade;
    bool flower;
} blade_t;

static blade_t s_bl[NBLADES];
static bool s_built;

void relax_enter(void)
{
    art_begin();
    if (s_built) return;
    // Blades sorted far to near (by their base), so the near ones are drawn on top
    for (int i = 0; i < NBLADES; i++) {
        uint32_t h = fx_hash(i, 303);
        s_bl[i] = (blade_t){.x = (float)(h % ART_W), .h = 6 + (h >> 8) % 16, .base = 118 + (h >> 13) % 50,
                            .shade = (h >> 20) % 3, .flower = (h >> 24) % 12 == 0};
    }
    for (int i = 1; i < NBLADES; i++)
        for (int j = i; j > 0 && s_bl[j].base < s_bl[j - 1].base; j--) {
            blade_t t = s_bl[j];
            s_bl[j] = s_bl[j - 1];
            s_bl[j - 1] = t;
        }
    s_built = true;
}

static float s_ts;
static float wind(float x, float s)
{
    return (sinf(s_ts * 1.1f + x * 0.045f) * 1.6f + sinf(s_ts * 0.37f + 2) * 1.2f + sinf(s_ts * 2.3f + x * 0.2f) * 0.3f) * s;
}

// How much night it is, 0..1, at this point of the day
static float night_at(float c)
{
    if (c < 60) return 0;
    if (c < 90) return (c - 60) / 30;
    if (c < 160) return 1;
    return 1 - (c - 160) / 20;
}

// One firework every 2.2 s at night: rises 0.8 s, flashes and bursts into 32 sparks that fall and fade in 1.6 s
static void firework(int k, float since)
{
    uint32_t h = fx_hash(k, 305);
    float x = 20 + h % 97, top = 18 + (h >> 8) % 34;
    static const uint32_t COL[5] = {0xff6a8a, 0xffd36a, 0x7ad8ff, 0xb48cff, 0x8affb0};
    uint16_t c = C(COL[(h >> 16) % 5]), c2 = C(0xfff4dc);
    if (since < 0.8f) {  // the rocket going up, with a little trail
        float y = 112 - (112 - top) * (since / 0.8f);
        art_px((int)x, (int)y, c2);
        art_tint((int)x, (int)y + 1, c, 10);
        art_tint((int)x, (int)y + 2, c, 6);
        return;
    }
    float t = since - 0.8f;
    if (t > 1.6f) return;
    int fade = t < 0.6f ? 16 : (int)(16 * (1.6f - t));
    if (t < 0.12f) art_disc(x, top, 4 - t * 20, c2);  // the flash
    for (int i = 0; i < 32; i++) {
        float a = i * 6.28318f / 32 + (h & 7) * 0.1f, v = 20 + ((h >> (i % 24)) & 3) * 4;
        float px = x + cosf(a) * v * t, py = top + sinf(a) * v * t + 9 * t * t;
        uint16_t pc = t < 0.25f ? c2 : c;
        art_tint((int)px, (int)py, pc, fade);
        art_tint((int)px + 1, (int)py, pc, fade * 2 / 3);
        if (t < 1.0f)  // a short trail behind each spark
            for (int k = 1; k <= 3; k++) art_tint((int)(px - cosf(a) * k * 2), (int)(py - sinf(a) * k * 2 - k * t), c, fade / (k + 1));
    }
}

void relax_render(uint16_t *fb, uint32_t t, uint32_t dt)
{
    int dx_, dy_;
    app_take_drag(&dx_, &dy_);
    s_ts = t / 1000.0f;
    float cyc = fmodf(s_ts, CYCLE), n = night_at(cyc);
    int na = (int)(n * 16);
    s_dark = na * 11 / 16;
    static const uint32_t DAY[] = {0x4a6fa5, 0x6f8fbd, 0xa3b5cf, 0xe8c9a0, 0xf2b27a, 0xf59e6b};
    static const uint32_t NIGHT[] = {0x070a22, 0x0c1233, 0x131b45, 0x1b2654, 0x243060, 0x2c3868};
    uint16_t sky[6];
    for (int i = 0; i < 6; i++) sky[i] = gfx_blend(C(DAY[i]), C(NIGHT[i]), na);
    art_bands(sky, 6, 0, 110);
    // stars come out
    if (n > 0.4f)
        for (int i = 0; i < 46; i++) {
            uint32_t h = fx_hash(i, 306);
            if (sinf(s_ts * 1.7f + i) > -0.2f) art_tint(h % ART_W, (h >> 8) % 95, (h >> 20) & 3 ? C(0xb8c4ff) : C(0xfff6e0), (int)((n - 0.4f) * 26));
        }
    // the sun sinks behind the hills as evening comes; the moon rises at night
    float sy = 92 + n * 40;
    if (n < 0.8f) {  // set for good by full night (its halo would linger on the horizon)
        for (int r = 18; r > 6; r -= 3)
            for (int y = -r; y <= r; y++)
                for (int x = -r; x <= r; x++)
                    if (x * x + y * y <= r * r) art_tint(30 + x, (int)sy + y, C(0xffe2a8), 2);
        art_disc(30, sy, 6, gfx_blend(C(0xfff1c9), C(0xff9a5a), na));
    }
    if (n > 0.5f) {
        float my = 26 - (n - 0.5f) * 24;  // above the word (art y < 29)
        art_disc(112, my, 7, C(0xf2ecd8));
        art_disc(115, my - 2, 6, sky[0]);  // a crescent
    }
    // fireworks, now and then, deep in the night
    if (n > 0.8f) {
        int k = (int)(cyc / 2.2f);
        for (int j = k - 1; j <= k; j++)
            if (fx_hash(j, 307) % 3) firework(j, cyc - j * 2.2f);
    }
    // slow clouds (darker at night)
    uint16_t cl = gfx_blend(C(0xf7e3cf), C(0x3a4470), na), cl2 = gfx_blend(C(0xd9b9a8), C(0x262f58), na);
    for (int i = 0; i < 3; i++) {
        int x = (int)fmodf(i * 60 + s_ts * 3, ART_W + 60) - 30, y = 20 + i * 14;
        art_rect(x, y, 26, 3, cl);
        art_rect(x + 5, y - 2, 14, 2, cl);
        art_rect(x + 2, y + 3, 22, 1, cl2);
    }
    // hills, then the field
    for (int x = 0; x < ART_W; x++) {
        int h1 = (int)(12 + sinf(x * 0.04f + 1) * 5), h2 = (int)(8 + sinf(x * 0.07f + 3) * 4);
        art_rect(x, 112 - h1, 1, h1 + 6, D(0x6d8a8f));  // down to the field: no row left unpainted
        art_rect(x, 118 - h2, 1, h2 + 2, D(0x55784f));
    }
    art_rect(0, 118, ART_W, ART_H - 118, D(0x4f7a3a));
    // the tree on the right, its crown swaying more than its trunk
    art_rect(104, 80, 5, 40, D(0x4a3326));
    art_rect(104, 80, 1, 40, D(0x6b4a35));
    static const int8_t LEAF[6][3] = {{106, 66, 15}, {94, 74, 11}, {118, 74, 11}, {106, 52, 11}, {97, 60, 9}, {116, 60, 9}};
    for (int i = 0; i < 6; i++) {
        float lx = LEAF[i][0], ly = LEAF[i][1], r = LEAF[i][2], sw = wind(lx, 0.8f * (1 - ly / 120));
        art_disc(lx + sw, ly, r, D(0x3e6b34));
        art_disc(lx + sw - 2, ly - 2, r * 0.6f, D(0x5d8f45));
        if (n < 0.5f) art_rect((int)(lx + sw - r * 0.6f), (int)(ly - r * 0.7f), 2, 1, D(0xe9b46a));  // the sun on the leaves
    }
    // grass blades bending with the gusts, flowers on some
    static const uint32_t GR[3] = {0x6fa04a, 0x8cbf55, 0x5a8a3c};
    for (int i = 0; i < NBLADES; i++) {
        const blade_t *b = &s_bl[i];
        float w = wind(b->x, 1);
        for (int k = 0; k < b->h; k++) {
            float f = (float)k / b->h, bend = w * f * f * (b->h / 10.0f);
            art_px((int)(b->x + bend), b->base - k, k > b->h - 3 && b->shade == 1 ? D(0xc9d97a) : D(GR[b->shade]));
        }
        if (b->flower) {
            float bend = w * (b->h / 10.0f);
            art_rect((int)(b->x + bend) - 1, b->base - b->h - 1, 3, 2, i & 1 ? D(0xf3d06a) : D(0xf2a2b8));
        }
    }
    // seeds by day, fireflies by night
    if (n < 0.5f)
        for (int i = 0; i < 12; i++) {
            uint32_t h = fx_hash(i, 304);
            float k = (h >> 16 & 255) / 255.0f;
            art_px((int)fmodf((h % ART_W) + s_ts * (6 + k * 6), ART_W), (int)(40 + (h >> 8) % 80 + sinf(s_ts + k * 9) * 4), C(0xfff6dc));
        }
    else
        for (int i = 0; i < 20; i++) {
            uint32_t h = fx_hash(i, 308);
            float k = (h >> 16 & 255) / 255.0f;
            int x = (int)fmodf((h % ART_W) + sinf(s_ts * 0.4f + k * 7) * 10 + s_ts * 1.5f, ART_W);
            int y = 112 + (h >> 8) % 50 + (int)(sinf(s_ts * 0.7f + k * 11) * 6);
            float glow = sinf(s_ts * (1.5f + k) + k * 20);
            if (glow > 0.2f) {
                int a = (int)((glow - 0.2f) * 20 * (n - 0.5f) * 2);
                art_tint(x, y, C(0xe8ff7a), a > 16 ? 16 : a);
                art_tint(x - 1, y, C(0xb8e050), a / 3), art_tint(x + 1, y, C(0xb8e050), a / 3);
                art_tint(x, y - 1, C(0xb8e050), a / 3), art_tint(x, y + 1, C(0xb8e050), a / 3);
            }
        }
    art_blit(fb);

    // The word, soft and big, with a gentle shadow (full resolution)
    const char *word = g_app.lang ? "Relax" : "Disfruta";
    int sc = 4, x = (WW - gfx_text_width(word, sc)) / 2, y = 58;
    gfx_text(fb, x + 4, y + 4, word, n > 0.5f ? rgb565(10, 14, 40) : rgb565(120, 80, 90), sc);  // soft shadow
    gfx_text_ol(fb, x, y, word, rgb565(255, 246, 230), n > 0.5f ? rgb565(40, 46, 96) : rgb565(92, 60, 78), sc, 2);
}
