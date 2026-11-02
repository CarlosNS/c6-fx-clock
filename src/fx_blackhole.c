// "Agujero negro" (landscape): a black hole seen almost edge-on.
// - Accretion disk: hot plasma streaks orbiting faster near the center (Keplerian), much brighter
//   on the side moving towards us (relativistic Doppler beaming).
// - Gravitational lensing: the far side of the disk appears bent over the top and under the
//   bottom of the shadow, with a thin bright photon ring at the edge.
// - Background stars are lensed with the point-lens equation: each star has a primary image
//   pushed outwards and a faint secondary image on the opposite side; as the view slowly turns
//   they stream around the hole.
#include <math.h>
#include <stdio.h>
#include "app.h"
#include "control.h"
#include "fx.h"
#include "gfx.h"

#define W LAND_W
#define H LAND_H
#define CX 160
#define CY 104
#define RS 20            // shadow radius, px
#define RE 34            // Einstein radius for the star lensing, px
#define BW 96            // half-resolution lookup region: 192 x 112 px around the hole
#define BH 56
#define INCL 0.2f        // disk seen almost edge-on: vertical squash
#define RIN 30
#define ROUT 92
#define NSTARS 170

// Lookup tables in the shared scratch memory
#define t_lr (fx_scratch)                // distance to the center, px
#define t_la (fx_scratch + BW * BH)      // screen angle, 0..255
#define t_dr (fx_scratch + 2 * BW * BH)  // radius in the disk plane, px
#define t_da (fx_scratch + 3 * BW * BH)  // angle in the disk plane, 0..255
_Static_assert(4 * BW * BH <= FX_SCRATCH_SIZE, "black hole tables exceed the scratch");

static uint16_t fire[256];
static int16_t star_r[NSTARS], star_a[NSTARS];
static uint8_t star_b[NSTARS];

void blackhole_enter(void)
{
    for (int i = 0; i < 256; i++) {  // black -> red -> orange -> yellow -> white
        int r = i < 90 ? i * 200 / 90 : 200 + (i - 90) * 55 / 166;
        int g = i < 80 ? 0 : i < 200 ? (i - 80) * 200 / 120 : 200 + (i - 200) * 50 / 56;
        int b = i < 170 ? i / 12 : (i - 170) * 230 / 86;
        fire[i] = rgb565(r > 255 ? 255 : r, g > 255 ? 255 : g, b > 255 ? 255 : b);
    }
    for (int i = 0; i < NSTARS; i++) {
        uint32_t h = fx_hash(i, 99);
        star_r[i] = 8 + (int16_t)(h % 230);
        star_a[i] = (int16_t)((h >> 8) & 1023);
        star_b[i] = 60 + (h >> 20) % 190;
    }
    if (fx_scratch_claim(18)) return;
    for (int j = 0; j < BH; j++)
        for (int i = 0; i < BW; i++) {
            float dx = (i * 2 + 1) - BW, dy = (j * 2 + 1) - BH;
            int k = j * BW + i;
            float r = sqrtf(dx * dx + dy * dy);
            t_lr[k] = r > 255 ? 255 : (uint8_t)r;
            t_la[k] = (uint8_t)((int)((atan2f(dy, dx) / 6.28318f + 1) * 256) & 255);
            float yd = dy / INCL, d = sqrtf(dx * dx + yd * yd);
            t_dr[k] = d > 255 ? 255 : (uint8_t)d;
            t_da[k] = (uint8_t)((int)((atan2f(yd, dx) / 6.28318f + 1) * 256) & 255);
        }
}

void blackhole_render(uint16_t *fb, uint32_t t, uint32_t dt)
{
    int dx_, dy_;
    app_take_drag(&dx_, &dy_);

    // Background: deep space with a faint bluish haze, then lensed stars
    gfx_fill(fb, rgb565(0, 0, 3));
    int view = t / 45;   // turn of the viewpoint: stars stream around the hole
    for (int i = 0; i < NSTARS; i++) {
        int a = (star_a[i] + view) & 1023;
        int beta = star_r[i];
        int root = (int)sqrtf((float)(beta * beta + 4 * RE * RE));
        int rp = (beta + root) / 2, rm = (root - beta) / 2;  // primary and secondary image radii
        int b = star_b[i];
        uint16_t c = rgb565(b, b, b * 9 / 8 > 255 ? 255 : b * 9 / 8);
        int x = CX + (sin_q14[(a + 256) & 1023] * rp >> 14), y = CY + (sin_q14[a] * rp >> 14);
        if ((unsigned)x < W && (unsigned)y < H) fb[y * W + x] = c;
        if (rm > RS + 2) {  // faint mirrored image on the far side of the hole
            int x2 = CX - (sin_q14[(a + 256) & 1023] * rm >> 14), y2 = CY - (sin_q14[a] * rm >> 14);
            if ((unsigned)x2 < W && (unsigned)y2 < H) fb[y2 * W + x2] = gfx_blend(fb[y2 * W + x2], c, 6);
        }
    }
    gfx_glow(fb, CX, CY, 90, rgb565(255, 120, 40), 3);

    // Disk, lensed halo, photon ring and shadow, at half resolution
    int spin = t;        // plasma orbiting
    for (int j = 0; j < BH; j++) {
        int py = CY - BH + j * 2;
        uint16_t *r0 = fb + py * W, *r1 = r0 + W;
        bool near_half = j * 2 + 1 >= BH;  // lower half: the disk passes in front of the shadow
        for (int i = 0; i < BW; i++) {
            int k = j * BW + i;
            int lr = t_lr[k], la = t_la[k], dr = t_dr[k], da = t_da[k];
            int inten = 0;
            bool shadow = lr < RS;
            // Lensed image of the far disk: a halo hugging the shadow, strongest over the top and bottom
            if (lr >= RS && lr < RS * 9 / 5) {
                int fall = 255 - (lr - RS) * 255 / (RS * 4 / 5);
                int vert = abs(sin_q14[(la * 4) & 1023]) >> 6;  // 0..255, max at top/bottom
                int tex = sin_u8[(uint8_t)(la * 6 + lr * 9 - spin)];
                int dop = 256 - (sin_q14[(la * 4 + 256) & 1023] >> 7);  // 128..384: left side approaches
                inten = fall * (90 + vert * 2 / 3) / 255 * (150 + tex / 2) / 255 * dop / 256;
                if (lr < RS + 2) inten = 255;  // photon ring
            }
            // Accretion disk in front (and its far half where it is not behind the shadow)
            if (dr >= RIN && dr <= ROUT && (near_half || !shadow)) {
                int fall = 255 - (dr - RIN) * 230 / (ROUT - RIN);
                int rot = (int)((int64_t)spin * 40 / dr);  // inner orbits are faster
                int tex = sin_u8[(uint8_t)(da * 5 + rot + dr * 7)] / 2 + sin_u8[(uint8_t)(da * 2 - rot + dr * 3)] / 2;
                int dop = 256 - (sin_q14[(da * 4 + 256) & 1023] * 3 >> 9);  // strong beaming
                int d = fall * (110 + tex) / 365 * dop / 256;
                if (d > inten) inten = d;
                shadow = false;
            }
            uint16_t c;
            if (shadow) c = 0;
            else if (inten < 8) continue;  // leave the stars
            else c = inten > 255 ? fire[255] : fire[inten];
            int x = CX - BW + i * 2;
            r0[x] = r0[x + 1] = r1[x] = r1[x + 1] = c;
        }
    }

    clock_row_grad(fb, &font_round_m, W / 2, 2, -4, rgb565(255, 255, 255), rgb565(255, 214, 170), rgb565(0, 0, 0), 3);
    char date[24];
    clock_date_str(date);
    ui_text(fb, &ui_s, 8, H - 15, date, rgb565(200, 170, 140), 0, FONT_FILL);
}
