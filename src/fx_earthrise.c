// "Earthrise" (full screen, wide canvas, portrait): after the Apollo 8 photograph (William Anders, 24 Dec 1968, NASA,
// public domain). The half-lit Earth rises slowly over the cratered lunar horizon; continents
// and clouds rotate at different speeds over a lit sphere with a thin blue atmosphere.
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "app.h"
#include "control.h"
#include "fx.h"
#include "gfx.h"

#define W WW
#define H WH
#define HZ 238                // lunar horizon at the centre
#define ER 37                // Earth radius in pixels
#define ED (ER * 2 + 1)
#define TW 64                // texture size (longitude x latitude)
#define TH 32

// Lookup tables live in the shared scratch memory (rebuilt if another face used it meanwhile)
#define s_lon (fx_scratch)
#define s_lat (fx_scratch + ED * ED)
#define s_light (fx_scratch + 2 * ED * ED)  // 0 = outside the disc
#define s_land ((uint8_t(*)[TW])(fx_scratch + 3 * ED * ED))
#define s_cloud ((uint8_t(*)[TW])(fx_scratch + 3 * ED * ED + TH * TW))
_Static_assert(3 * ED * ED + 2 * TH * TW <= FX_SCRATCH_SIZE, "earthrise tables exceed the scratch");

static int value_noise(int x, int y, int scale, uint32_t seed)
{
    int x0 = x / scale, y0 = y / scale, fx = (x % scale) * 256 / scale, fy = (y % scale) * 256 / scale;
    int a = fx_hash((x0) % (TW / scale), y0 + seed) & 255, b = fx_hash((x0 + 1) % (TW / scale), y0 + seed) & 255;
    int c = fx_hash((x0) % (TW / scale), y0 + 1 + seed) & 255, d = fx_hash((x0 + 1) % (TW / scale), y0 + 1 + seed) & 255;
    int top = a + ((b - a) * fx >> 8), bot = c + ((d - c) * fx >> 8);
    return top + ((bot - top) * fy >> 8);
}

void earthrise_enter(void)
{
    if (fx_scratch_claim(11)) return;
    const float lx = -0.75f, ly = -0.45f, lz = 0.48f;  // sunlight from the upper left, as in the photo
    for (int y = -ER; y <= ER; y++)
        for (int x = -ER; x <= ER; x++) {
            int i = (y + ER) * ED + (x + ER);
            float d2 = (float)(x * x + y * y) / (ER * ER);
            if (d2 >= 1) { s_light[i] = 0; continue; }
            float nz = sqrtf(1 - d2), nx = x / (float)ER, ny = y / (float)ER;
            float l = nx * lx + ny * ly + nz * lz;
            s_light[i] = l <= 0 ? 1 : (uint8_t)(1 + l * 254);
            s_lon[i] = (uint8_t)((atan2f(nx, nz) / 3.14159f + 1) * 128);
            s_lat[i] = (uint8_t)((asinf(ny) / 3.14159f + 0.5f) * 255);
        }
    for (int y = 0; y < TH; y++)
        for (int x = 0; x < TW; x++) {
            // Averaged octaves cluster around 128: stretch the contrast so land and cloud actually appear
            int n = value_noise(x, y, 16, 1) / 2 + value_noise(x, y, 8, 7) / 3 + value_noise(x, y, 4, 13) / 6;
            n = 128 + (n - 128) * 5 / 2;
            int polar = abs(y - TH / 2) * 2 > TH - 6;
            s_land[y][x] = polar ? 255 : (uint8_t)(n < 0 ? 0 : n > 235 ? 235 : n);
            int c = value_noise(x, y, 8, 21) / 2 + value_noise(x, y, 4, 33) / 2;
            c = 128 + (c - 128) * 2 + (abs(y - TH / 2) < 4 ? 30 : 0);
            s_cloud[y][x] = (uint8_t)(c < 0 ? 0 : c > 255 ? 255 : c);
        }
}

static uint16_t shade(int r, int g, int b, int l)
{
    return rgb565(r * l >> 8, g * l >> 8, b * l >> 8);
}

void earthrise_render(uint16_t *fb, uint32_t t, uint32_t dt)
{
    int dx_, dy_;
    app_take_drag(&dx_, &dy_);
    gfx_fill(fb, rgb565(0, 0, 2));
    for (int i = 0; i < 70; i++) {  // faint stars
        uint32_t h = fx_hash(i, 3);
        int x = h % W, y = (h >> 9) % 230;
        fb[y * W + x] = (h >> 20) & 1 ? rgb565(70, 70, 80) : rgb565(40, 40, 50);
    }

    // Earth slowly rising and setting again over two minutes
    int ex = 170, ey = 200 + (sin_q14[(t / 120) & 1023] * 20 >> 14);
    int rot = t / 70, crot = t / 45;
    gfx_glow(fb, ex, ey, ER + 8, rgb565(60, 120, 255), 5);
    for (int y = -ER; y <= ER; y++) {
        int py = ey + y;
        if ((unsigned)py >= H) continue;
        for (int x = -ER; x <= ER; x++) {
            int i = (y + ER) * ED + (x + ER);
            int l = s_light[i];
            if (!l) continue;
            int px = ex + x;
            if ((unsigned)px >= W) continue;
            int u = (s_lon[i] + rot) & 255, v = s_lat[i];
            int land = s_land[v * TH / 256][u * TW / 256];
            int cl = s_cloud[v * TH / 256][((s_lon[i] + crot) & 255) * TW / 256];
            int lit = l < 30 ? 6 : l;  // night side stays faintly visible
            uint16_t c;
            if (land > 236) c = shade(235, 240, 250, lit);       // ice caps
            else if (land > 150) c = shade(land > 180 ? 150 : 96, land > 180 ? 120 : 130, 70, lit);  // desert / green
            else c = shade(26, 70, 150 + (land >> 2), lit);       // ocean
            if (cl > 160) c = gfx_blend(c, shade(250, 250, 255, lit), cl > 220 ? 14 : (cl - 160) / 4);
            fb[py * W + px] = c;
        }
    }
    // Atmosphere rim on the lit side
    for (int a = 0; a < 1024; a += 3) {
        int rx = ex + (sin_q14[a] * (ER + 1) >> 14), ry = ey - (sin_q14[(a + 256) & 1023] * (ER + 1) >> 14);
        if (a > 512 || a < 64)
            if ((unsigned)rx < W && (unsigned)ry < H) fb[ry * W + rx] = gfx_blend(fb[ry * W + rx], rgb565(120, 180, 255), 10);
    }

    // Lunar horizon: grey, curved, low sun from the left casting long crater shadows
    int scroll = t / 90;
    for (int x = 0; x < W; x++) {
        int hy = HZ + (x - 120) * (x - 120) / 1800 + (sin_q14[(x * 7 + scroll * 3) & 1023] * 2 >> 14);
        for (int y = hy; y < H; y++) {
            int k = (y - hy) * 4;
            int g = 150 - (k > 90 ? 90 : k) + (int)(fx_hash((x + scroll) >> 1, y >> 1) & 15);
            fb[y * W + x] = rgb565(g, g - 4, g - 10);
        }
        fb[hy * W + x] = rgb565(210, 206, 196);
    }
    for (int i = 0; i < 30; i++) {  // craters drifting past
        uint32_t h = fx_hash(i, 17);
        int cx = (int)((h % 600) + 600 - scroll % 600) % 600 - 100, cy = HZ + 18 + (int)((h >> 10) % 70);
        int r = 3 + (int)((h >> 20) % 9) + (cy - HZ) / 12;  // nearer craters look bigger
        int hy = HZ + (cx - 120) * (cx - 120) / 1800;
        if (cx < -20 || cx > W + 20 || cy - r / 2 < hy + 2) continue;
        for (int yy = -r / 2; yy <= r / 2; yy++) {
            int w = r * (r / 2 * r / 2 - yy * yy + 1) / (r / 2 * r / 2 + 1);
            gfx_darken_rect(fb, cx - w + 2, cy + yy, w * 2, 1, 0, 1);  // shadowed bowl
        }
        gfx_fill_rect(fb, cx + r - 2, cy - 1, 2, 3, rgb565(200, 196, 186));  // sunlit far rim
    }

    // Mission-style clock
    char buf[8];
    clock_hhmm(buf, !g_now.valid || g_now.ms < 500);
    int cx = (W - font_width(&font_seg_m, "88:88", -2)) / 2;
    font_draw(fb, &font_seg_m, cx, 28, "88:88", -2, rgb565(22, 22, 26), 0, FONT_FILL);
    font_draw(fb, &font_seg_m, cx, 28, buf, -2, rgb565(220, 222, 226), rgb565(0, 0, 0), FONT_FILL | FONT_OUTLINE);
    char date[32];
    clock_date_str(date);
    ui_text(fb, &ui_s, (W - ui_width(&ui_s, date)) / 2, 94, date, rgb565(170, 172, 180), 0, FONT_FILL);
    const char *cap = "EARTHRISE - APOLO 8, 1968";
    ui_text(fb, &ui_s, (W - ui_width(&ui_s, cap)) / 2, H - 40, cap, rgb565(110, 108, 104), 0, FONT_FILL);
}
