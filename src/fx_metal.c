// "Trueno y peluches" (full screen, wide canvas, pixel art on art.c): a day in the life of a thrash metal
// guitarist (a big, strong rocker with long hair; a generic band: no real band's name, logo or members),
// one scene for each part of the day, no titles. Every scene is painted by tools/metal_art.py in a 16-bit
// style (shading, dithering, coloured outlines) and kept in fx_metal_art.h; this file puts the pictures
// up and animates on top of them:
//   00-08 asleep hugging Rosita, the pink plush, a moonbeam    08-11 half asleep over a coffee at dawn
//   11-13 glaring at the clip-on tuner of his red V            13-16 the garage: amp, guitars, sound waves
//   16-19 the black tour van at sunset, with parallax          19-23 the show: beams, fire, horns up; the
//   23-24 out cold on the old sofa by the TV, pizza                  time big in the middle
#include <math.h>
#include <stdio.h>
#include "app.h"
#include "art.h"
#include "fx_metal_art.h"
#include "control.h"
#include "fx.h"
#include "gfx.h"

static uint16_t C(uint32_t v) { return rgb565(v >> 16, (v >> 8) & 255, v & 255); }

static float s_ts;
bool g_metal_bare;

void metal_enter(void) { art_begin(); }

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

// 19-23, painted: the show. The stage (truss, the band backlit, amp walls) is one picture and the crowd
// another, horns up, in two poses. Between them the watch sweeps the beams down from the truss (red,
// amber, white, crossing towards the middle) and fires the pyro columns at the sides; the time is big
// in the middle, between the columns.
static void scene_show(uint32_t t)
{
    static const uint8_t lamps[5][2] = {{18, 24}, {42, 22}, {68, 21}, {94, 22}, {119, 24}};
    static const uint32_t cols[5] = {0xff2a2a, 0xffb040, 0xffffff, 0xffb040, 0xff2a2a};
    art_rle(metal_show_pal, metal_show_0, sizeof metal_show_0);
    // the beams: cones from each lamp, swinging; brighter in the middle of the cone
    for (int b = 0; b < 5; b++) {
        float base = (2 - b) * 0.32f;                                   // aimed across, towards the middle
        float an = base + 0.28f * sinf(s_ts * (0.7f + b * 0.13f) + b * 1.7f);
        float tn = tanf(an);
        uint16_t c = C(cols[b]);
        for (int y = lamps[b][1] + 2; y < 126; y++) {
            float d = y - lamps[b][1], cx = lamps[b][0] + d * tn, hw = 1.5f + d * 0.13f;
            for (int x = (int)(cx - hw); x <= (int)(cx + hw); x++) {
                float e = fabsf(x - cx) / hw;
                int al = (int)((1 - e * e) * (7 - d * 0.03f));
                if (al > 0) art_tint(x, y, c, al);
            }
        }
    }
    // pyro: columns of fire at the sides of the stage, roaring up in pulses
    for (int side = 0; side < 2; side++) {
        int cx = side ? 123 : 14;
        float pulse = 0.5f + 0.5f * sinf(s_ts * 1.6f + side * 1.3f);
        int hgt = 34 + (int)(30 * pulse * pulse);
        for (int h = 0; h < hgt; h++) {
            float f = h / (float)hgt, w = (5.5f - 3.5f * f) * (0.85f + 0.15f * sinf(s_ts * 23 + h * 0.7f));
            float wob = 1.5f * sinf(s_ts * 11 + h * 0.31f + side) * f;
            uint32_t col = f < 0.3f ? 0xfff2b0 : f < 0.6f ? 0xffb040 : f < 0.85f ? 0xff6a20 : 0xc02a18;
            int y = 117 - h;
            for (int x = (int)(cx + wob - w - 3); x <= (int)(cx + wob + w + 3); x++) {
                float e = fabsf(x - cx - wob);
                if (e <= w) art_px(x, y, C(col));
                else art_tint(x, y, C(0xff8030), (int)(8 * (1 - (e - w) / 3)));  // its glow
            }
        }
    }
    // the crowd in front, arms pumping
    if ((t / 450) % 2) art_rle_key(metal_show_pal, metal_show_1, sizeof metal_show_1, METAL_SHOW_KEY, 0);
    else art_rle_key(metal_show_pal, metal_show_2, sizeof metal_show_2, METAL_SHOW_KEY, 0);
}

// 23-24, painted: slumped back on the old leather sofa, out cold, his head lolling, an open pizza box in
// front, a can on the floor. The only light is a TV off to the right: here it flickers bluish over
// the room, stronger on his side, and now and then the picture cuts. He breathes.
static void scene_pizza(uint32_t t)
{
    if (t % 4200 < 1800) art_rle(metal_pizza_pal, metal_pizza_1, sizeof metal_pizza_1);
    else art_rle(metal_pizza_pal, metal_pizza_0, sizeof metal_pizza_0);
    uint32_t shot = t / 2600;                                       // the TV changes shot every few seconds
    int base = 1 + fx_hash(shot, 701) % 3;
    int fl = base + (int)(1.2f * sinf(s_ts * 13) + 0.8f * sinf(s_ts * 31));
    uint16_t tv = (fx_hash(shot, 702) & 1) ? C(0x5a7aff) : C(0x8ab0ff);
    if (fl < 0) fl = 0;
    for (int y = 0; y < ART_H; y++)
        for (int x = 0; x < ART_W; x++) {  // stronger towards the TV, a dithered ramp (no bands)
            int a16 = fl * 16 + x * 40 / ART_W, al = a16 >> 4;
            if ((a16 & 15) > 7 && ((x + y) & 1)) al++;
            art_tint(x, y, tv, al);
        }
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
    case 5: scene_show(t); break;
    default: scene_pizza(t); break;
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
    // The time: big in the middle of the show; in the other scenes in the top third, clear of the picture
    static uint16_t cream[4], red[4];
    if (!cream[0]) {
        cream[0] = rgb565(255, 252, 240), cream[1] = rgb565(250, 240, 220), cream[2] = rgb565(230, 214, 190), cream[3] = rgb565(200, 180, 150);
        red[0] = rgb565(255, 240, 240), red[1] = rgb565(255, 200, 200), red[2] = rgb565(255, 90, 80), red[3] = rgb565(200, 20, 20);
    }
    char hhmm[8];
    clock_hhmm(hhmm, !g_now.valid || g_now.ms < 500);
    if (sc == 5) pix_digits_draw(fb, (WW - pix_digits_width("00:00", 5)) / 2, 100, hhmm, 5, red, rgb565(18, 12, 20));
    else pix_digits_draw(fb, (WW - pix_digits_width("00:00", 4)) / 2, 42, hhmm, 4, cream, rgb565(16, 18, 44));
}
