// "Disfruta" (full screen, wide canvas, pixel art on art.c): free time. A meadow at sunset: grass and a
// lone tree bending in a gentle, gusting wind, slow clouds, seeds drifting, a low sun with its halo,
// flowers among the grass. No time on purpose: only "Disfruta" ("Relax" in English).
#include <math.h>
#include <stdio.h>
#include "app.h"
#include "art.h"
#include "control.h"
#include "fx.h"
#include "gfx.h"

#define NBLADES 260

static uint16_t C(uint32_t v) { return rgb565(v >> 16, (v >> 8) & 255, v & 255); }

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

void relax_render(uint16_t *fb, uint32_t t, uint32_t dt)
{
    int dx_, dy_;
    app_take_drag(&dx_, &dy_);
    s_ts = t / 1000.0f;
    static const uint32_t SKY[] = {0x4a6fa5, 0x6f8fbd, 0xa3b5cf, 0xe8c9a0, 0xf2b27a, 0xf59e6b};
    uint16_t sky[6];
    for (int i = 0; i < 6; i++) sky[i] = C(SKY[i]);
    art_bands(sky, 6, 0, 110);
    // the low sun and its halo
    for (int r = 18; r > 6; r -= 3)
        for (int y = -r; y <= r; y++)
            for (int x = -r; x <= r; x++)
                if (x * x + y * y <= r * r) art_tint(30 + x, 92 + y, C(0xffe2a8), 2);
    art_disc(30, 92, 6, C(0xfff1c9));
    // slow clouds
    for (int i = 0; i < 3; i++) {
        int x = (int)fmodf(i * 60 + s_ts * 3, ART_W + 60) - 30, y = 20 + i * 14;
        art_rect(x, y, 26, 3, C(0xf7e3cf));
        art_rect(x + 5, y - 2, 14, 2, C(0xf7e3cf));
        art_rect(x + 2, y + 3, 22, 1, C(0xd9b9a8));
    }
    // hills, then the field
    for (int x = 0; x < ART_W; x++) {
        int h1 = (int)(12 + sinf(x * 0.04f + 1) * 5), h2 = (int)(8 + sinf(x * 0.07f + 3) * 4);
        art_rect(x, 112 - h1, 1, h1, C(0x6d8a8f));
        art_rect(x, 118 - h2, 1, h2 + 2, C(0x55784f));
    }
    art_rect(0, 118, ART_W, ART_H - 118, C(0x4f7a3a));
    // the tree on the right, its crown swaying more than its trunk
    art_rect(104, 80, 5, 40, C(0x4a3326));
    art_rect(104, 80, 1, 40, C(0x6b4a35));
    static const int8_t LEAF[6][3] = {{106, 66, 15}, {94, 74, 11}, {118, 74, 11}, {106, 52, 11}, {97, 60, 9}, {116, 60, 9}};
    for (int i = 0; i < 6; i++) {
        float lx = LEAF[i][0], ly = LEAF[i][1], r = LEAF[i][2], sw = wind(lx, 0.8f * (1 - ly / 120));
        art_disc(lx + sw, ly, r, C(0x3e6b34));
        art_disc(lx + sw - 2, ly - 2, r * 0.6f, C(0x5d8f45));
        art_rect((int)(lx + sw - r * 0.6f), (int)(ly - r * 0.7f), 2, 1, C(0xe9b46a));  // the sun on the leaves
    }
    // grass blades bending with the gusts, flowers on some
    static const uint32_t GR[3] = {0x6fa04a, 0x8cbf55, 0x5a8a3c};
    for (int i = 0; i < NBLADES; i++) {
        const blade_t *b = &s_bl[i];
        float w = wind(b->x, 1);
        for (int k = 0; k < b->h; k++) {
            float f = (float)k / b->h, bend = w * f * f * (b->h / 10.0f);
            art_px((int)(b->x + bend), b->base - k, k > b->h - 3 && b->shade == 1 ? C(0xc9d97a) : C(GR[b->shade]));
        }
        if (b->flower) {
            float bend = w * (b->h / 10.0f);
            art_rect((int)(b->x + bend) - 1, b->base - b->h - 1, 3, 2, i & 1 ? C(0xf3d06a) : C(0xf2a2b8));
        }
    }
    // seeds drifting on the wind
    for (int i = 0; i < 12; i++) {
        uint32_t h = fx_hash(i, 304);
        float k = (h >> 16 & 255) / 255.0f;
        art_px((int)fmodf((h % ART_W) + s_ts * (6 + k * 6), ART_W), (int)(40 + (h >> 8) % 80 + sinf(s_ts + k * 9) * 4), C(0xfff6dc));
    }
    art_blit(fb);

    // The word, soft and big, with a gentle shadow (full resolution)
    const char *word = g_app.lang ? "Relax" : "Disfruta";
    int sc = 4, x = (WW - gfx_text_width(word, sc)) / 2, y = 58;
    gfx_text(fb, x + 4, y + 4, word, rgb565(120, 80, 90), sc);  // soft shadow
    gfx_text_ol(fb, x, y, word, rgb565(255, 246, 230), rgb565(92, 60, 78), sc, 2);
}
