// "Onix" (full screen, native): a minimalist analog face made for the AMOLED panel. The background is
// true black, so those pixels are simply off (no glow, no power); only thin strokes light up: a ring
// of minute dots, slim hour bars, two warm hands and a small accent. The seconds are not a hand but a
// short trail of light running round the minute ring. The whole drawing drifts by a couple of pixels
// every minute so nothing stays lit on the same pixels for hours. Accent colour: setting "ohue".
#include <stdio.h>
#include <string.h>
#include "app.h"
#include "esp_timer.h"
#include "control.h"
#include "fx.h"
#include "gfx.h"

#define W FW  // native: 410 x 502 px, the whole screen
#define H FH
#define R 182     // minute ring radius
#define TRAIL 48  // seconds trail: steps of a quarter of a minute dot (12 dots long)

static uint16_t c_warm, c_soft, c_dot, c_date;

// What each of the two framebuffers holds, so the next frame only erases what moves (the face is
// almost all black: clearing the whole native frame alone would cost about 13 ms)
typedef struct {
    uint16_t *fb;
    int step, hue, day, a_h, a_m, head;  // head < 0: no trail
} drawn_t;
static drawn_t s_drawn[2];

void onix_enter(void)
{
    c_warm = rgb565(240, 232, 218);  // warm white: hands and hour bars
    c_soft = rgb565(150, 144, 134);
    c_dot = rgb565(70, 68, 64);      // minute dots, barely there
    c_date = rgb565(120, 116, 108);
    memset(s_drawn, 0, sizeof(s_drawn));
}

// Point at angle a (1/1024 turn, 0 = 12 o'clock, clockwise) and radius r, in 1/16 px
static inline void polar(int cx, int cy, int a, int r, int *x, int *y)
{
    *x = cx * 16 + (sin_q14[a & 1023] * r >> 10);
    *y = cy * 16 - (sin_q14[(a + 256) & 1023] * r >> 10);
}

// A hand: from a little behind the centre to `len`, `w` px wide
static void hand(uint16_t *fb, int cx, int cy, int a, int len, int back, int w16, uint16_t c)
{
    int x0, y0, x1, y1;
    polar(cx, cy, a + 512, back, &x0, &y0);
    polar(cx, cy, a, len, &x1, &y1);
    gfx_aa_capsule(fb, x0, y0, x1, y1, w16, c, 16);
}


static void draw_trail(uint16_t *fb, int cx, int cy, int head, uint16_t c, bool erase)
{
    for (int k = TRAIL; k >= 0; k--) {
        int a = head - k * 1024 / 240, x, y;
        polar(cx, cy, a, R, &x, &y);
        int r = 18 + (TRAIL - k) * 18 / TRAIL;  // thinner towards the tail
        gfx_aa_capsule(fb, x, y, x, y, erase ? r + 16 : r, erase ? 0 : c, erase ? 16 : 16 - k * 15 / TRAIL);
    }
}

void onix_render(uint16_t *fb, uint32_t t, uint32_t dt)
{
    (void)t;
    (void)dt;
    // Drift: a small orbit that moves one step a minute (8 steps, at most 2 px from the centre)
    static const int8_t drift[8][2] = {{0, 0}, {2, 0}, {2, 2}, {0, 2}, {-2, 2}, {-2, 0}, {-2, -2}, {0, -2}};
    int step = g_now.valid ? (g_now.h * 60 + g_now.m) & 7 : 0;
    int cx = W / 2 + drift[step][0], cy = H / 2 + drift[step][1];

    int hue = g_app.onix_hue * 256 / 360;
    uint16_t accent = hsv565(hue, 200, 255);
    int day = g_now.valid ? g_now.day : 0;

    // Erase: the whole frame if this buffer is new or the layout moved, otherwise only the hands and
    // the trail it showed two frames ago (in black, a pixel wider to take their soft edges)
    // Anything else may have drawn on these buffers meanwhile (a system screen while the face was not
    // shown, a toast or the pairing code on top): after a pause both are cleared, and as a safety net
    // a full clear comes round twice every 16 frames (both buffers), so leftovers last half a second
    static int64_t s_last_us;
    static unsigned s_frame;
    int64_t now = esp_timer_get_time();
    if (now - s_last_us > 150000) memset(s_drawn, 0, sizeof(s_drawn));
    s_last_us = now;
    bool sweep = (++s_frame & 15) < 2;
    drawn_t *d = s_drawn[0].fb == fb ? &s_drawn[0] : s_drawn[1].fb == fb ? &s_drawn[1] : NULL;
    if (!d) d = s_drawn[0].fb ? &s_drawn[1] : &s_drawn[0];
    if (sweep || d->fb != fb || d->step != step || d->hue != hue || d->day != day) {
        memset(fb, 0, W * H * 2);  // true black: these pixels stay off
    } else {
        if (d->head >= 0) draw_trail(fb, cx, cy, d->head, 0, true);
        hand(fb, cx, cy, d->a_h, 104, 16, 50 + 16, 0);
        hand(fb, cx, cy, d->a_m, 160, 20, 32 + 16, 0);
    }

    // Minute ring: tiny dots; hour bars, longer at 12, 3, 6 and 9
    for (int i = 0; i < 60; i++) {
        int a = i * 1024 / 60, x0, y0, x1, y1;
        if (i % 5) {
            polar(cx, cy, a, R, &x0, &y0);
            gfx_aa_capsule(fb, x0, y0, x0, y0, 20, c_dot, 16);
        } else {
            polar(cx, cy, a, R + 2, &x0, &y0);
            polar(cx, cy, a, i % 15 ? R - 20 : R - 32, &x1, &y1);
            gfx_aa_capsule(fb, x0, y0, x1, y1, i % 15 ? 18 : 22, i % 15 ? c_soft : c_warm, 16);
        }
    }

    // Seconds: a trail of light on the ring, its head moving smoothly, fading behind it
    int head = -1;
    if (g_app.show_secs && g_now.valid) {
        head = (int)(g_now.minute_ms * 1024 / 60000);
        draw_trail(fb, cx, cy, head, accent, false);
    }

    // Date, small and dim, above 6
    if (g_now.valid) {
        char d[16];
        snprintf(d, sizeof(d), "%s %d", clock_wday_name(g_now.wday), g_now.day);
        ui_text(fb, &ui_s, cx - ui_width(&ui_s, d) / 2, cy + 84, d, c_date, 0, FONT_FILL);
    }

    // Hands (10:10 until the time is known), moving smoothly
    int h = g_now.valid ? g_now.h : 10, m = g_now.valid ? g_now.m : 10;
    uint32_t mms = g_now.valid ? g_now.minute_ms : 0;
    int a_h = (int)(((h % 12) * 60 + m) * 1024 / 720 + mms * 1024 / 43200000);
    int a_m = (int)((m * 60000 + mms) * 1024 / 3600000);
    hand(fb, cx, cy, a_h, 104, 16, 50, c_warm);
    hand(fb, cx, cy, a_m, 160, 20, 32, c_warm);
    // Centre: accent bead with a black core
    gfx_aa_capsule(fb, cx * 16, cy * 16, cx * 16, cy * 16, 112, accent, 16);
    gfx_aa_capsule(fb, cx * 16, cy * 16, cx * 16, cy * 16, 40, 0, 16);
    *d = (drawn_t){.fb = fb, .step = step, .hue = hue, .day = day, .a_h = a_h, .a_m = a_m, .head = head};
}
