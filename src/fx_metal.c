// "Trueno y peluches" (full screen, wide canvas, pixel art on art.c): a comic strip of a day in the life
// of a thrash metal band (a generic band in leather and long hair: no real band's name, logo or members).
// The scene follows the hour, each with a little animation and a yellow narration box, comic style
// (thick ink outlines, halftone dots):
//   00-08 asleep in the bunk, hugging Rosita, the big pink plush      08-11 messy hair, both hands on coffee
//   11-13 glaring at the tuner of a guitar that will not stay in tune  13-16 crossed guitars and the amp
//   16-19 the tour bus on the road at sunset                           19-23 the show: spotlights, crowd,
//   23-24 slumped on the sofa, a lonely slice of pizza on the floor          and the time big in the middle
#include <math.h>
#include <stdio.h>
#include "app.h"
#include "art.h"
#include "control.h"
#include "fx.h"
#include "gfx.h"

static uint16_t C(uint32_t v) { return rgb565(v >> 16, (v >> 8) & 255, v & 255); }
#define INK C(0x120c14)

static float s_ts;

void metal_enter(void) { art_begin(); }

// ---- comic helpers: shapes with a black ink line around them

static void ink_disc(float x, float y, float r, uint32_t c)
{
    art_disc(x, y, r + 1, INK);
    art_disc(x, y, r, C(c));
}

static void ink_thick(float x0, float y0, float x1, float y1, float w, uint32_t c)
{
    art_thick(x0, y0, x1, y1, w + 2, INK);
    art_thick(x0, y0, x1, y1, w, C(c));
}

static void ink_rect(int x, int y, int w, int h, uint32_t c)
{
    art_rect(x - 1, y - 1, w + 2, h + 2, INK);
    art_rect(x, y, w, h, C(c));
}

static void ink_poly(const float *xy, int n, uint32_t c)
{
    // the outline: the same polygon nudged one pixel each way, in ink, then the fill
    float o[32];
    for (int d = 0; d < 4; d++) {
        for (int i = 0; i < n; i++) o[2 * i] = xy[2 * i] + (d == 0) - (d == 1), o[2 * i + 1] = xy[2 * i + 1] + (d == 2) - (d == 3);
        art_poly(o, n, INK);
    }
    art_poly(xy, n, C(c));
}

// Halftone: printed dots darkening a band of the picture
static void halftone(int y0, int y1, uint32_t c, int alpha)
{
    for (int y = y0; y < y1; y += 3)
        for (int x = (y / 3) & 1 ? 1 : 0; x < ART_W; x += 3) art_tint(x, y, C(c), alpha);
}

static void sky(const uint32_t *cols, int n, int y1)
{
    uint16_t b[8];
    for (int i = 0; i < n; i++) b[i] = C(cols[i]);
    art_bands(b, n, 0, y1);
}

// A long-haired head seen from the front: hair, face, eyes (open, sleepy or shut)
static void head(float x, float y, int eyes, bool messy)
{
    ink_disc(x, y + 1, 9, 0x2a1d1a);                                    // hair behind
    if (messy)
        for (int i = 0; i < 7; i++) ink_thick(x, y - 4, x - 12 + i * 4, y - 13 - (i & 1) * 3, 2, 0x2a1d1a);
    else
        ink_rect((int)x - 9, (int)y, 18, 14, 0x2a1d1a);                 // long hair down the back
    ink_disc(x, y + 1, 6, 0xe8b48a);                                    // face
    art_rect((int)x - 6, (int)y - 5, 12, 3, C(0x2a1d1a));               // fringe
    if (eyes == 0) art_px((int)x - 2, (int)y + 1, INK), art_px((int)x + 2, (int)y + 1, INK);           // open
    else art_rect((int)x - 3, (int)y + 1, 2, 1, INK), art_rect((int)x + 2, (int)y + 1, 2, 1, INK);      // shut / sleepy
}

// ---- the seven scenes

static void scene_sleep(void)  // 00-08
{
    static const uint32_t S[] = {0x070a1e, 0x0c1230, 0x121a3e, 0x18224a};
    sky(S, 4, ART_H);
    halftone(0, ART_H, 0x000000, 5);
    // the window with the moon
    ink_rect(14, 26, 34, 30, 0x1e2c5a);
    art_disc(38, 36, 5, C(0xe8e2c8));
    art_disc(40, 34, 4, C(0x1e2c5a));
    art_rect(30, 26, 1, 30, INK), art_rect(14, 41, 34, 1, INK);
    for (int i = 0; i < 6; i++) art_px(17 + (int)(fx_hash(i, 501) % 28), 29 + (int)(fx_hash(i, 502) % 24), C(0xb8c4ff));
    // the bunk: frame, mattress, blanket
    ink_rect(8, 104, 122, 6, 0x5a3a24);
    ink_rect(8, 92, 4, 46, 0x5a3a24), ink_rect(126, 92, 4, 46, 0x5a3a24);
    ink_rect(12, 96, 114, 8, 0x8a8aa0);
    // the rocker lying on his side, peaceful, hugging Rosita
    head(30, 88, 1, false);
    art_rect(34, 92, 4, 1, C(0xd08a6a));                                  // a little smile
    ink_rect(36, 90, 60, 12, 0x1a1a22);                                   // black shirt / body
    ink_rect(66, 92, 52, 10, 0x3a4a7a);                                   // the blanket over the legs
    // Rosita: a big pink plush with round ears, squeezed in his arms
    ink_disc(56, 84, 11, 0xff6fb5);
    ink_disc(48, 74, 4, 0xff6fb5), ink_disc(64, 74, 4, 0xff6fb5);
    art_disc(52, 84, 1.2f, INK), art_disc(60, 84, 1.2f, INK);
    art_disc(56, 88, 2, C(0xffb0d8));
    ink_thick(40, 94, 62, 90, 3, 0xe8b48a);                               // the arm around her
    // pillow
    ink_rect(14, 90, 14, 6, 0xd8d8e8);
}

static void scene_coffee(void)  // 08-11
{
    static const uint32_t S[] = {0xf3c58a, 0xf0b07a, 0xe89a72, 0xd88a70};
    sky(S, 4, 110);
    art_rect(0, 110, ART_W, ART_H - 110, C(0x8a5a3a));
    halftone(110, ART_H, 0x000000, 3);
    // the kitchen window, the sun just up
    ink_rect(84, 20, 40, 40, 0xffe2a8);
    art_disc(104, 52, 9, C(0xfff4cc));
    art_rect(84, 46, 40, 14, C(0xc88a62));
    art_rect(103, 20, 1, 40, INK), art_rect(84, 39, 40, 1, INK);
    // the rocker, half awake: messy hair, leather jacket, both hands round a mug
    ink_poly((const float[]){26, 168, 34, 104, 82, 104, 90, 168}, 4, 0x1a1a22);  // jacket
    art_rect(56, 104, 4, 40, C(0x3a3a46));                                          // the zip
    head(58, 86, 1, true);
    art_rect(54, 93, 8, 1, C(0x9a5a4a));                                            // a yawn of a mouth
    ink_thick(36, 118, 50, 126, 6, 0x1a1a22), ink_thick(80, 118, 66, 126, 6, 0x1a1a22);  // arms
    ink_rect(50, 116, 16, 18, 0xe8e2d0);                                            // the mug
    art_rect(66, 120, 4, 8, INK), art_rect(67, 121, 2, 6, C(0xe8e2d0));             // its handle
    art_rect(52, 117, 12, 3, C(0x5a3420));                                          // coffee
    ink_disc(48, 126, 3, 0xe8b48a), ink_disc(68, 128, 3, 0xe8b48a);                 // hands
    // steam rising in wisps
    for (int w = 0; w < 3; w++)
        for (int k = 0; k < 14; k++) {
            float y = 112 - k * 2 - fmodf(s_ts * 8, 6), x = 54 + w * 4 + sinf(k * 0.6f + s_ts * 3 + w) * 2;
            art_tint((int)x, (int)y, C(0xffffff), 10 - k * 2 / 3);
        }
}

static void scene_tuning(void)  // 11-13
{
    static const uint32_t S[] = {0xdfe6ea, 0xd2dae0, 0xc5ced6};
    sky(S, 3, 120);
    art_rect(0, 120, ART_W, ART_H - 120, C(0x7a7f8a));
    halftone(120, ART_H, 0x000000, 3);
    ink_rect(14, 30, 26, 34, 0xf6d08a);                                   // a poster (abstract lightning)
    ink_thick(30, 36, 22, 50, 2, 0x1a1a22), ink_thick(22, 50, 30, 48, 2, 0x1a1a22), ink_thick(30, 48, 24, 60, 2, 0x1a1a22);
    // the stool and the rocker, sitting side-on, frowning
    ink_rect(36, 128, 30, 5, 0x6a4a30), ink_rect(40, 133, 4, 30, 0x6a4a30), ink_rect(58, 133, 4, 30, 0x6a4a30);
    ink_poly((const float[]){34, 128, 40, 92, 64, 92, 68, 128}, 4, 0x1a1a22);
    head(54, 80, 0, false);
    art_rect(49, 76, 4, 1, INK), art_rect(56, 76, 4, 1, INK);             // frowning brows
    // the guitar: body on his lap, neck up to the right, the headstock with a clip-on tuner
    ink_poly((const float[]){44, 112, 70, 104, 80, 118, 60, 132, 42, 126}, 5, 0xb02a2a);
    ink_thick(72, 110, 112, 58, 4, 0x3a2a1e);
    ink_rect(108, 48, 10, 12, 0x2a2a2a);
    ink_thick(64, 100, 74, 108, 3, 0xe8b48a);                             // the hand turning the peg
    ink_rect(114, 34, 18, 12, 0x101418);                                  // the tuner
    float needle = sinf(s_ts * 5) * 4 + sinf(s_ts * 13) * 2;
    art_line(123, 44, 123 + needle, 37, needle > -1.5f && needle < 1.5f ? C(0x6aff8a) : C(0xff5a4a));
    art_rect(116, 36, 2, 2, needle > -1.5f && needle < 1.5f ? C(0x6aff8a) : C(0xff5a4a));
    // a drop of sweat
    float d = fmodf(s_ts * 1.2f, 1.0f);
    ink_disc(63, 74 + d * 6, 1.2f, 0x9ad8ff);
}

static void scene_rehearsal(void)  // 13-16
{
    static const uint32_t S[] = {0x1a3a4a, 0x22505e, 0x2a6470};
    sky(S, 3, 128);
    for (int y = 6; y < 128; y += 6) art_rect(0, y, ART_W, 1, C(0x153040));  // the garage door's slats
    art_rect(0, 128, ART_W, ART_H - 128, C(0x3a3a40));
    halftone(128, ART_H, 0x000000, 4);
    // the amp at the back: grille, knobs, a red light that pulses with the noise
    ink_rect(36, 62, 66, 60, 0x24242a);
    for (int y = 76; y < 118; y += 3)
        for (int x = 40; x < 98; x += 3) art_px(x, y, C(0x3e3e48));
    for (int k = 0; k < 6; k++) art_disc(44 + k * 9, 68, 1.5f, C(0xd8c890));
    art_disc(96, 68, 1.5f, sinf(s_ts * 9) > 0 ? C(0xff3a3a) : C(0x5a1a1a));
    // two pointy electric guitars crossed in front of it: neck with strings, a body with two sharp
    // horns, pickups and a bridge, and an angular headstock
    for (int g = 0; g < 2; g++) {
        float m = g ? -1 : 1, bx = g ? 107 : 30, by = 122;     // where the neck meets the body (clear of
        float hx = bx + m * 74, hy = 38;                        // the corners and the time) and the headstock
        float len = sqrtf((hx - bx) * (hx - bx) + (hy - by) * (hy - by)), ux = (hx - bx) / len, uy = (hy - by) / len;
        float vx = -uy, vy = ux;                                // across the guitar
        uint32_t col = g ? 0xe8e8f0 : 0xd02a2a;
        ink_thick(bx, by, hx, hy, 4, 0x3a2a1e);                 // the neck
        // the body along the neck: a big lower bout, a smaller upper one and two horns toward the neck
        float lx = bx - ux * 14, ly = by - uy * 14;
        art_disc(lx, ly, 15, INK), art_disc(bx - ux * 2, by - uy * 2, 11, INK);
        for (int sd = -1; sd <= 1; sd += 2) {
            float pts[] = {bx - ux * 6 + vx * sd * 9, by - uy * 6 + vy * sd * 9, bx + ux * 10 + vx * sd * 7, by + uy * 10 + vy * sd * 7,
                           bx - ux * 2 + vx * sd * 3, by - uy * 2 + vy * sd * 3};
            ink_poly(pts, 3, col);
        }
        art_disc(lx, ly, 14, C(col)), art_disc(bx - ux * 2, by - uy * 2, 10, C(col));
        for (int k = 0; k < 2; k++) {  // pickups and the bridge, across
            float px = bx - ux * (6 + k * 8), py = by - uy * (6 + k * 8);
            art_thick(px - vx * 5, py - vy * 5, px + vx * 5, py + vy * 5, 2.5f, INK);
        }
        art_thick(lx - ux * 6 - vx * 4, ly - uy * 6 - vy * 4, lx - ux * 6 + vx * 4, ly - uy * 6 + vy * 4, 1.5f, C(0x9a9a9a));
        art_line(lx - ux * 6, ly - uy * 6, hx, hy, C(0xd8d0b0));  // the strings
        float head[] = {hx - ux * 2 - vx * 3, hy - uy * 2 - vy * 3, hx + ux * 12 - vx * 6, hy + uy * 12 - vy * 6,
                        hx + ux * 14 + vx * 2, hy + uy * 14 + vy * 2, hx - ux * 2 + vx * 3, hy - uy * 2 + vy * 3};
        ink_poly(head, 4, 0x1a1a1a);
    }
    // thunder out of the amp, in beats
    float beat = fmodf(s_ts * 2, 1.0f);
    if (beat < 0.5f) {
        int r = 12 + (int)(beat * 50);
        for (int k = 0; k < 18; k++) {
            float a = k * 6.28318f / 18;
            art_tint((int)(69 + cosf(a) * r), (int)(96 + sinf(a) * r * 0.7f), C(0xfff07a), 14 - (int)(beat * 26));
        }
        ink_thick(14, 70, 22, 80, 2, 0xfff07a), ink_thick(22, 80, 16, 84, 2, 0xfff07a), ink_thick(16, 84, 26, 96, 2, 0xfff07a);
        ink_thick(124, 70, 116, 80, 2, 0xfff07a), ink_thick(116, 80, 122, 84, 2, 0xfff07a), ink_thick(122, 84, 112, 96, 2, 0xfff07a);
    }
}

static void scene_bus(void)  // 16-19
{
    static const uint32_t S[] = {0x2a1a4a, 0x5a2a6a, 0xa03a6a, 0xe0645a, 0xf6a04a, 0xfacf6a};
    sky(S, 6, 120);
    art_disc(96, 112, 14, C(0xfff0b0));                                    // the sun on the horizon
    for (int x = 0; x < ART_W; x++) {  // far hills, slowly passing
        int h = (int)(10 + sinf((x + s_ts * 4) * 0.05f) * 5 + sinf((x + s_ts * 4) * 0.13f) * 2);
        art_rect(x, 120 - h, 1, h, C(0x4a2a4a));
    }
    art_rect(0, 120, ART_W, 26, C(0x2a2030));                              // the road
    for (int i = 0; i < 6; i++) art_rect((int)fmodf(i * 28 - s_ts * 60 + 600, ART_W + 28) - 14, 132, 12, 2, C(0xf6d06a));
    art_rect(0, 146, ART_W, ART_H - 146, C(0x6a4a3a));
    halftone(146, ART_H, 0x000000, 4);
    // the tour bus, side on, rocking a little on the road
    int by = 100 + (int)(sinf(s_ts * 9) > 0.7f);
    ink_rect(22, by, 92, 26, 0x1a1a22);
    ink_rect(100, by + 4, 14, 12, 0x5a7aa0);                               // windscreen
    for (int w = 0; w < 6; w++) ink_rect(26 + w * 12, by + 5, 9, 8, 0xffd27a);   // lit windows
    art_rect(22, by + 18, 92, 2, C(0xb02a2a));                             // a red stripe
    for (int k = 0; k < 2; k++) {                                          // wheels, turning
        float wx = k ? 98 : 38;
        ink_disc(wx, by + 27, 6, 0x2a2a2a);
        float a = -s_ts * 8;
        art_line(wx, by + 27, wx + cosf(a) * 4, by + 27 + sinf(a) * 4, C(0x8a8a8a));
    }
    for (int i = 0; i < 5; i++) art_tint(16 - i * 3 - (int)fmodf(s_ts * 20, 3), by + 22 - i, C(0xd8b090), 9 - i);  // dust
}

static void scene_show(void)  // 19-23: the time is the star
{
    art_rect(0, 0, ART_W, ART_H, C(0x060406));
    // crossing spotlights from the rig, sweeping
    for (int k = 0; k < 4; k++) {
        float ox = 10 + k * 39, a = 1.57f + sinf(s_ts * (0.8f + k * 0.2f) + k * 1.7f) * 0.55f;
        uint16_t c = k & 1 ? C(0xff2a2a) : C(0xfff4e8);
        for (int r = 4; r < 170; r += 1) {
            int w = r / 9;
            for (int q = -w; q <= w; q++) art_tint((int)(ox + cosf(a) * r - sinf(a) * q), (int)(4 + sinf(a) * r + cosf(a) * q), c, 2);
        }
        ink_rect((int)ox - 3, 0, 6, 5, 0x3a3a3a);
    }
    art_rect(0, 118, ART_W, 6, C(0x5a0a0a));                                // the stage's red glow
    halftone(0, 118, 0xff2a2a, 1);
    // the crowd: heads and raised hands making horns, bobbing to the beat
    for (int i = 0; i < 14; i++) {
        float x = 4 + i * 10, bob = sinf(s_ts * 8 + i * 1.3f) > 0 ? 2 : 0;
        art_disc(x, 140 + bob, 5, INK);
        if (i % 2 == 0) {
            art_thick(x + 3, 138 + bob, x + 5, 124 + bob, 2, INK);
            art_rect((int)x + 3, 120 + (int)bob, 1, 4, INK), art_rect((int)x + 6, 120 + (int)bob, 1, 4, INK);  // the horns
        }
    }
    art_rect(0, 144, ART_W, ART_H - 144, INK);
}

static void scene_pizza(void)  // 23-24
{
    static const uint32_t S[] = {0x14162a, 0x1a1e34, 0x20243c};
    sky(S, 3, 124);
    art_rect(0, 124, ART_W, ART_H - 124, C(0x2a2228));
    halftone(0, ART_H, 0x000000, 4);
    // the TV's blue light flickering on everything
    float tv = 0.6f + 0.4f * sinf(s_ts * 17) * sinf(s_ts * 5.3f);
    ink_rect(98, 70, 30, 24, 0x0a0a14);
    art_rect(100, 72, 26, 20, gfx_blend(C(0x203060), C(0x9ac0ff), (int)(tv * 12)));
    // the sofa and the rocker slumped in it, hair over the face, arms hanging
    ink_rect(10, 100, 80, 28, 0x5a2a3a);
    ink_rect(8, 92, 12, 36, 0x6a3242), ink_rect(80, 92, 12, 36, 0x6a3242);
    ink_poly((const float[]){30, 128, 36, 96, 62, 96, 68, 128}, 4, 0x1a1a22);
    ink_disc(49, 88, 9, 0x2a1d1a);                                          // head bowed: all hair
    art_rect(42, 90, 14, 8, C(0x2a1d1a));
    ink_thick(36, 104, 30, 124, 3, 0x1a1a22), ink_thick(62, 104, 70, 124, 3, 0x1a1a22);
    ink_disc(30, 125, 2.5f, 0xe8b48a), ink_disc(70, 125, 2.5f, 0xe8b48a);
    ink_thick(40, 128, 38, 150, 4, 0x24242e), ink_thick(58, 128, 60, 150, 4, 0x24242e);  // legs
    // a lonely slice of pizza on the floor, one bite gone (on the right, clear of the time below)
    ink_poly((const float[]){92, 120, 124, 114, 112, 136}, 3, 0xf2c45a);
    art_rect(92, 119, 32, 2, C(0xc88a3a));
    art_disc(106, 122, 1.5f, C(0xc0302a)), art_disc(112, 127, 1.5f, C(0xc0302a)), art_disc(102, 125, 1.2f, C(0xc0302a));
    art_disc(121, 115, 2.5f, C(0x2a2228));                                  // the bite
    // tinted by the TV
    for (int y = 60; y < ART_H; y++)
        for (int x = 60; x < ART_W; x++) art_tint(x, y, C(0x6a90ff), (int)(tv * 2));
}

void metal_render(uint16_t *fb, uint32_t t, uint32_t dt)
{
    int dx_, dy_;
    app_take_drag(&dx_, &dy_);
    s_ts = t / 1000.0f;
    int h = g_now.valid ? g_now.h : 12;
    static const char *const CAP_ES[7] = {"ZZZ... CON ROSITA", "DESPERTANDO...", "MALDITA CUERDA!", "ENSAYO EN EL GARAJE",
                                          "DE GIRA", "EN DIRECTO!", "Y AHORA... PIZZA"};
    static const char *const CAP_EN[7] = {"ZZZ... WITH ROSIE", "WAKING UP...", "STUPID STRING!", "GARAGE REHEARSAL",
                                          "ON THE ROAD", "LIVE!", "AND NOW... PIZZA"};
    int sc = h < 8 ? 0 : h < 11 ? 1 : h < 13 ? 2 : h < 16 ? 3 : h < 19 ? 4 : h < 23 ? 5 : 6;
    switch (sc) {
    case 0: scene_sleep(); break;
    case 1: scene_coffee(); break;
    case 2: scene_tuning(); break;
    case 3: scene_rehearsal(); break;
    case 4: scene_bus(); break;
    case 5: scene_show(); break;
    default: scene_pizza(); break;
    }
    art_blit(fb);

    // The narration box, comic style: yellow, inked, at the top (clear of the rounded corners)
    const char *cap = (g_app.lang ? CAP_EN : CAP_ES)[sc];
    int cw = gfx_text_width(cap, 2) + 16, cx = (WW - cw) / 2, cy = 34;
    gfx_fill_rect(fb, cx - 2, cy - 2, cw + 4, 24, rgb565(18, 12, 20));
    gfx_fill_rect(fb, cx, cy, cw, 20, rgb565(250, 214, 80));
    gfx_text(fb, cx + 8, cy + 3, cap, rgb565(18, 12, 20), 2);
    // Zzz rising out of the bunk at night (full resolution letters)
    if (sc == 0)
        for (int k = 0; k < 3; k++) {
            float f = fmodf(s_ts * 0.5f + k / 3.0f, 1.0f);
            int zs = 1 + (int)(f * 2.5f);
            gfx_text_ol(fb, 66 + (int)(f * 40), 150 - (int)(f * 70), "Z", rgb565(200, 210, 255), rgb565(18, 12, 20), zs, 1);
        }
    // The time: big in the middle of the show; elsewhere at the bottom, inked
    static uint16_t cream[4], red[4];
    if (!cream[0]) {
        cream[0] = rgb565(255, 252, 240), cream[1] = rgb565(250, 240, 220), cream[2] = rgb565(230, 214, 190), cream[3] = rgb565(200, 180, 150);
        red[0] = rgb565(255, 240, 240), red[1] = rgb565(255, 200, 200), red[2] = rgb565(255, 90, 80), red[3] = rgb565(200, 20, 20);
    }
    char hhmm[8];
    clock_hhmm(hhmm, !g_now.valid || g_now.ms < 500);
    if (sc == 5) pix_digits_draw(fb, (WW - pix_digits_width("00:00", 6)) / 2, 112, hhmm, 6, red, rgb565(18, 12, 20));
    else pix_digits_draw(fb, (WW - pix_digits_width("00:00", 3)) / 2, 300, hhmm, 3, cream, rgb565(18, 12, 20));
}
