// "Trueno y peluches" (full screen, wide canvas, pixel art on art.c): a comic strip of a day in the life
// of a thrash metal band (a generic band in leather and long hair: no real band's name, logo or members).
// The scene follows the hour, each with a little animation, no titles. The scenes painted by
// tools/metal_art.py (16-bit style: shading, dithering, coloured outlines) are in fx_metal_art.h:
//   00-08 asleep hugging Rosita, the pink plush (painted)             08-11 half asleep with a coffee (painted)
//   11-13 glaring at the clip-on tuner of his V (painted)              13-16 the garage, amp and guitars (painted)
//   16-19 the tour van at sunset (painted)                              19-23 the show: spotlights, crowd,
//   23-24 slumped on the sofa, a lonely slice of pizza on the floor          and the time big in the middle
#include <math.h>
#include <stdio.h>
#include "app.h"
#include "art.h"
#include "fx_metal_art.h"
#include "control.h"
#include "fx.h"
#include "gfx.h"

static uint16_t C(uint32_t v) { return rgb565(v >> 16, (v >> 8) & 255, v & 255); }
#define INK C(0x120c14)

static float s_ts;
bool g_metal_bare;

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

// A picture painted by tools/metal_art.py (fx_metal_art.h): runs of <count, palette index>
static void art_rle(const uint16_t *pal, const uint8_t *rle, int len)
{
    uint16_t *d = art, *end = art + ART_W * ART_H;
    for (int i = 0; i + 1 < len && d < end; i += 2)
        for (int n = rle[i]; n-- && d < end;) *d++ = pal[rle[i + 1]];
}

// The same for a sprite: pixels of colour `key` are left as they are; `dy` moves it down
static void art_rle_key(const uint16_t *pal, const uint8_t *rle, int len, int key, int dy)
{
    int p = 0;
    for (int i = 0; i + 1 < len; i += 2) {
        int n = rle[i], c = rle[i + 1];
        if (c != key) {
            for (int q = p; q < p + n; q++) {
                int y = q / ART_W + dy;
                if (y >= 0 && y < ART_H) art[y * ART_W + q % ART_W] = pal[c];
            }
        }
        p += n;
    }
}

// ---- the seven scenes

// 00-08, painted: the rocker asleep hugging Rosita, the moonbeam from the window across the bed. Here
// it breathes (two frames: chest, arm and plush one pixel up) and dust floats in the beam.
static void scene_sleep(uint32_t t)
{
    if (t % 4000 < 1600) art_rle(metal_sleep_pal, metal_sleep_1, sizeof metal_sleep_1);
    else art_rle(metal_sleep_pal, metal_sleep_0, sizeof metal_sleep_0);
    // the beam, as in metal_art.py: across  s = -0.62 x + 0.78 y  in 14..50, along  a = 0.78 x + 0.62 y > 36
    for (int i = 0; i < 22; i++) {
        float sp = 0.6f + (fx_hash(i, 601) % 100) / 120.0f;               // art px per second
        float a = 40 + fmodf(s_ts * sp + (fx_hash(i, 602) % 1000) / 7.0f, 130);
        float c = 16 + (fx_hash(i, 603) % 320) / 10.0f + 2.5f * sinf(s_ts * 0.7f + i);
        int x = (int)(a * 0.78f - c * 0.62f), y = (int)(a * 0.62f + c * 0.78f);
        int tw = 8 + (int)(6 * sinf(s_ts * (1.3f + (i % 5) * 0.3f) + i * 2.1f));
        art_tint(x, y, C(0xe8eeff), tw);
    }
}

// 08-11, painted: waist up in the kitchen, half asleep, both hands round a black mug; dawn through the
// blinds. Here he blinks now and then and the steam curls up from the mug in a see-through spiral.
static void scene_coffee(uint32_t t)
{
    if (t % 5200 < 260) art_rle(metal_coffee_pal, metal_coffee_1, sizeof metal_coffee_1);
    else art_rle(metal_coffee_pal, metal_coffee_0, sizeof metal_coffee_0);
    for (int w = 0; w < 2; w++)
        for (int h = 0; h < 54; h++) {
            float ph = h * 0.19f - s_ts * 1.7f + w * 3.1f, amp = 1.5f + h * 0.13f;
            float x = 73.5f + (w ? 3.0f : -3.0f) + sinf(ph) * amp + h * 0.12f;
            int y = 104 - h, xi = (int)x;
            float a = 14.0f * (1 - h / 54.0f) * (h < 5 ? h / 5.0f : 1) * (0.45f + 0.55f * cosf(ph));  // the back of each turn fades
            uint16_t c = C(0xfff4ea);
            art_tint(xi, y, c, (int)a);
            art_tint(xi - 1, y, c, (int)(a * 0.55f));
            art_tint(xi + 1, y, c, (int)(a * 0.55f));
        }
}

// 11-13, painted: on the combo amp, leaning over a red V, glaring sideways at the clip-on tuner while
// his right hand turns a peg. Here the sweat drop slides and the tuner works (tuner_overlay).
static void scene_tuning(uint32_t t)
{
    if (t % 3000 >= 2000) art_rle(metal_tuning_pal, metal_tuning_1, sizeof metal_tuning_1);
    else art_rle(metal_tuning_pal, metal_tuning_0, sizeof metal_tuning_0);
}

// The tuner's little screen, at full wide resolution over the picture: a needle that will not settle
// and a red LED, then for a moment centred and green
static void tuner_overlay(uint16_t *fb, uint32_t t)
{
    int x0 = METAL_TUNER_X * 2, y0 = METAL_TUNER_Y * 2, w = METAL_TUNER_W * 2, h = METAL_TUNER_H * 2;
    bool ok = t % 6000 > 4300;
    float a = ok ? 0.04f * sinf(s_ts * 9) : 0.45f + 0.5f * sinf(s_ts * 2.3f) + 0.3f * sinf(s_ts * 5.1f);
    uint16_t lit = ok ? rgb565(90, 255, 120) : rgb565(255, 70, 60), dim = rgb565(70, 110, 120);
    int px = x0 + w / 2 - 2, py = y0 + h - 2;
    for (int k = -2; k <= 2; k++) {  // the scale
        int tx = px + k * 4, ty = y0 + 2 + (k * k) / 2;
        if (tx >= 0 && tx < WW) fb[ty * WW + tx] = k ? dim : rgb565(200, 230, 230);
    }
    for (int r = 0; r < h - 4; r++) {  // the needle
        int x = px + (int)lroundf(r * sinf(a)), y = py - (int)lroundf(r * cosf(a));
        if (x >= 0 && x < WW && y >= 0 && y < WH) fb[y * WW + x] = ok ? lit : rgb565(230, 240, 250);
    }
    int lx = x0 + w - 4, ly = y0 + 3;  // the LED, with a glow
    for (int dy = -2; dy <= 2; dy++)
        for (int dx = -2; dx <= 2; dx++) {
            int d = dx * dx + dy * dy, x = lx + dx, y = ly + dy;
            if (x < 0 || x >= WW || y < 0 || y >= WH) continue;
            if (d <= 1) fb[y * WW + x] = lit;
            else if (d <= 5) fb[y * WW + x] = gfx_blend(fb[y * WW + x], lit, 7);
        }
}

// 13-16, painted: the garage, egg cartons on the wall, a half stack with its four speakers, two pointed
// guitars on a stand, a mic stand, cables and a pedal. Here the speakers pump and pixel sound waves
// ripple out from them, past the cabinet (as metal_art.py: cones at these centres, radius 12)
static void scene_rehearsal(uint32_t t)
{
    static const uint8_t cones[4][2] = {{19, 100}, {49, 100}, {19, 134}, {49, 134}};
    art_rle(metal_garage_pal, metal_garage_0, sizeof metal_garage_0);
    float beat = fmodf(s_ts * 2.2f, 1.0f);                      // a riff at about 130 bpm
    for (int c = 0; c < 4; c++) {
        int cx = cones[c][0], cy = cones[c][1];
        // the cone pumps on the beat: a flash on the dust cap
        int fl = beat < 0.25f ? (int)(10 * (1 - beat * 4)) : 0;
        for (int dy = -3; dy <= 3; dy++)
            for (int dx = -3; dx <= 3; dx++)
                if (dx * dx + dy * dy <= 10) art_tint(cx + dx, cy + dy, C(0xffe0b0), fl);
        for (int k = 0; k < 3; k++) {                            // three rings travelling out
            float f = fmodf(beat + k / 3.0f, 1.0f), r = 14 + f * 40;
            int al = (int)(16 * (1 - f) + 3);
            for (int i = 0; i < 160; i++) {                      // in dashes, wobbling
                if ((i / 3) % 2) continue;
                float an = i * 6.2832f / 160, rr = r + 1.5f * sinf(an * 6 + s_ts * 9);
                for (int th = 0; th < 2; th++) {
                    int x = cx + (int)lroundf((rr + th) * cosf(an)), y = cy + (int)lroundf((rr + th) * sinf(an));
                    if (x >= 3 && x <= 65 && y >= 53) continue;  // only outside the amp
                    art_tint(x, y, C(0xffd27a), al);
                }
            }
        }
    }
    if ((t / 400) % 2) art_px(42, 159, C(0xff3a2a));            // the pedal is on
    else art_px(42, 159, C(0x5a1010));
}

// 16-19, painted: the tour van with flames on its side heading into the sunset. The background is one
// picture, the van another drawn on top; between them, at their own speeds (parallax): hills, power
// poles with their wires and the dashes on the road. The van bounces a little and its wheels turn.
static void scene_bus(uint32_t t)
{
    art_rle(metal_bus_pal, metal_bus_0, sizeof metal_bus_0);
    // hills in the middle distance, scrolling slowly (the van drives left, so the world goes right)
    float hs = s_ts * 5;
    uint16_t hill = C(0x1e1430), hill2 = C(0x2a1c3c);
    for (int x = 0; x < ART_W; x++) {
        float u = x - hs;
        int top = 97 - (int)(3 + 2.5f * sinf(u * 0.07f) + 1.8f * sinf(u * 0.19f + 1) + 1.2f * sinf(u * 0.41f));
        for (int y = top; y < 105; y++) art_px(x, y, y == top ? hill2 : hill);
    }
    // power poles rushing past, wires sagging between them
    uint16_t pole = C(0x140c1c);
    float ps = fmodf(s_ts * 34, 70);
    for (int i = -1; i < 3; i++) {
        int x = (int)(i * 70 + ps);
        for (int y = 64; y < 106; y++) art_px(x, y, pole), art_px(x + 1, y, pole);
        for (int dx = -5; dx <= 6; dx++) art_px(x + dx, 68, pole);
        art_px(x - 5, 67, pole), art_px(x + 6, 67, pole);
        for (int dx = 0; dx < 70; dx++) {  // the wire to the next pole
            float f = dx / 70.0f;
            int y = 67 + (int)(6 * 4 * f * (1 - f));
            art_px(x + 6 + dx, y, pole);
        }
    }
    // dashes down the middle of the road, fastest of all
    uint16_t dash = C(0xe8dcb0);
    float ds = fmodf(s_ts * 60, 22);
    for (int i = -1; i < 8; i++)
        for (int dx = 0; dx < 11; dx++)
            for (int y = 154; y < 157; y++) art_px((int)(i * 22 + ds) + dx + (y - 154), y, dash);
    // the van on top, bouncing on the bumps; the hubcaps turning
    int dy = sinf(s_ts * 8.3f) > 0.75f ? -1 : 0;
    art_rle_key(metal_bus_pal, metal_bus_1, sizeof metal_bus_1, METAL_BUS_KEY, dy);
    static const uint8_t wheels[2][2] = {{56, 137}, {116, 137}};
    for (int w = 0; w < 2; w++)
        for (int k = 0; k < 3; k++) {
            float an = -s_ts * 9 + k * 2.094f;  // rolling left: anticlockwise
            art_px(wheels[w][0] + (int)lroundf(3 * cosf(an)), wheels[w][1] + dy + (int)lroundf(3 * sinf(an)), C(0x3a3440));
        }
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
    int sc = h < 8 ? 0 : h < 11 ? 1 : h < 13 ? 2 : h < 16 ? 3 : h < 19 ? 4 : h < 23 ? 5 : 6;
    switch (sc) {
    case 0: scene_sleep(t); break;
    case 1: scene_coffee(t); break;
    case 2: scene_tuning(t); break;
    case 3: scene_rehearsal(t); break;
    case 4: scene_bus(t); break;
    case 5: scene_show(); break;
    default: scene_pizza(); break;
    }
    art_blit(fb);
    if (sc == 2) tuner_overlay(fb, t);
    if (g_metal_bare) return;  // console "metalbare 1": the drawing alone, to review it

    // Zzz rising from his head towards the poster, below the time (full resolution letters)
    if (sc == 0)
        for (int k = 0; k < 3; k++) {
            float f = fmodf(s_ts * 0.4f + k / 3.0f, 1.0f);
            int zs = 1 + (int)(f * 2.5f);
            gfx_text_ol(fb, 92 + (int)(f * 96), 124 - (int)(f * 18), "Z", rgb565(200, 210, 255), rgb565(16, 18, 44), zs, 1);
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
    else if (sc <= 4) pix_digits_draw(fb, (WW - pix_digits_width("00:00", 4)) / 2, 42, hhmm, 4, cream, rgb565(16, 18, 44));
    else pix_digits_draw(fb, (WW - pix_digits_width("00:00", 3)) / 2, 300, hhmm, 3, cream, rgb565(18, 12, 20));
}
