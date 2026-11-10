// Power-on animation (only when the watch starts from off, not after a restart): the Marea Solsticial
// night scene appears in steps, the stars twinkle in, the moon rises out of the sea with its
// reflection, and the letters of "Marea" drop into place. Its last frame is the boot screen's
// background and title, so the boot screen follows without a jump. Pixel art: everything moves on
// the scene's 2x2 grid and fades in steps, no smooth gradients.
#include "intro.h"
#include <math.h>
#include <string.h>
#include "esp_attr.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lcd.h"
#include "ui.h"

#define INTRO_MS 4000
#define FADE_MS 1200      // the scene comes up from black in FADE_STEPS steps
#define FADE_STEPS 6
#define RISE_T0 500       // the moon rises between these
#define RISE_T1 2400
#define HALO_T 2400       // its halo of dots, one ring after landing
#define FINAL_T 3000      // from here on: the plain scene and the title
#define TITLE_T 2300      // first letter; one more every LETTER_MS
#define LETTER_MS 150
#define HORIZON 300       // native px (the scene's horizon is at 150 in its 2x art)
#define MOON_X 344        // where the scene draws the moon (art 172, 92)
#define MOON_Y 184
#define MOON_R 24

// Set by the console ("intro") before a restart, so the animation can be seen without cutting power
static RTC_NOINIT_ATTR uint32_t s_intro_req;
#define INTRO_MAGIC 0x1A7E0B07u

void intro_request(void) { s_intro_req = INTRO_MAGIC; }

bool intro_wanted(void)
{
    bool asked = s_intro_req == INTRO_MAGIC;
    s_intro_req = 0;
    return asked || esp_reset_reason() == ESP_RST_POWERON;
}

static uint16_t *s_fb;

static void px2(int x, int y, uint16_t c)  // one art pixel = 2x2 native, on even coordinates
{
    x &= ~1, y &= ~1;
    if (x < 0 || y < 0 || x > UI_W - 2 || y > UI_H - 2) return;
    uint16_t *p = s_fb + y * UI_W + x;
    p[0] = p[1] = p[UI_W] = p[UI_W + 1] = c;
}

// Where the sea (or the island and its lighthouse, which stand in front of the moon) starts in a
// column: the same shapes the scene draws (ui.c, build_scene), in native px
static int sea_top(int x)
{
    int xa = x / 2, h = 0;
    if (xa > 110 && xa < 185) h = 7 - (xa - 147) * (xa - 147) / 200;
    if (xa == 160 || xa == 161) h = 22;
    return HORIZON - (h > 0 ? h : 0) * 2;
}

static void disc2(int cx, int cy, int r, uint16_t c)
{
    for (int y = -r; y <= r; y += 2)
        for (int x = -r; x <= r; x += 2)
            if (x * x + y * y <= r * r + r && cy + y < sea_top(cx + x)) px2(cx + x, cy + y, c);
}

static uint32_t s_seed;
static uint32_t rnd(void) { return s_seed = s_seed * 1103515245u + 12345u, s_seed >> 8; }

static void frame(uint32_t t)
{
    s_fb = lcd_native_fb();
    ui_begin(s_fb);
    ui_background(false);  // the final scene, moon included
    // The last second is the scene itself, as the boot screen will show it
    if (t >= FINAL_T) goto title;

    // Take the scene's own moon, halo and reflection out: copy the same rows from 80 px to the left
    // (an even shift keeps the sky's checkerboard dither in step)
    for (int y = MOON_Y - 44; y < UI_H; y++) {
        if (y >= MOON_Y + 44 && y < HORIZON) continue;
        int w = y < HORIZON ? 46 : 30;
        for (int x = MOON_X - w; x < MOON_X + w; x++) s_fb[y * UI_W + x] = s_fb[y * UI_W + x - 80];
    }

    // The rising moon (eased in and out, on the art grid), hidden behind the horizon and the island
    float k = t <= RISE_T0 ? 0 : t >= RISE_T1 ? 1 : (float)(t - RISE_T0) / (RISE_T1 - RISE_T0);
    k = k * k * (3 - 2 * k);
    int my = (int)(HORIZON + MOON_R + 2 - k * (HORIZON + MOON_R + 2 - MOON_Y)) & ~1;
    disc2(MOON_X, my, MOON_R, MS_CREMA);
    disc2(MOON_X - 8, my - 6, 4, MS_GRIS);
    disc2(MOON_X + 6, my + 8, 6, MS_GRIS);
    disc2(MOON_X + 10, my - 10, 2, MS_GRIS);
    if (t > HALO_T) {  // its halo appears dot by dot around the circle
        int n = (int)((t - HALO_T) * 64 / 300);
        for (int i = 0; i < 64 && i < n; i++) {
            float a = i * 6.2831853f / 64;
            int r = 36 + (i & 1) * 4;
            px2(MOON_X + (int)(r * cosf(a)), MOON_Y + (int)(r * sinf(a)), MS_GRIS);
        }
    }
    // Its reflection: broken strokes under it that grow as it comes up and shimmer
    int shown = HORIZON - (my - MOON_R);  // how much of the moon is above the sea
    if (shown > 0) {
        int len = shown > 2 * MOON_R ? 2 * MOON_R : shown;
        for (int y = HORIZON + 4; y < UI_H - 40; y += 6) {  // as wide as the scene's, in cream
            int w = 10 - (y - HORIZON) / 28;
            if (w < 3) w = 3;
            w = (w * len / (2 * MOON_R)) & ~1;
            if (w < 2) continue;
            int wob = (int)(4 * sinf(t * 0.006f + y * 0.11f)) & ~1;
            for (int x = -w; x <= w; x += 2)
                if (((x + y / 6 + (int)(t / 200)) % 5) != 0) px2(MOON_X + x + wob, y, MS_CREMA);
        }
    }

    // Up from black in steps
    if (t < FADE_MS) {
        int step = (int)(t * FADE_STEPS / FADE_MS);
        int a = step * 256 / FADE_STEPS;
        for (int i = 0; i < UI_W * UI_H; i++) s_fb[i] = ui_mix(s_fb[i], MS_NEGRO, a);
    }
    // A few stars twinkle in as the sky comes up (always the same ones)
    s_seed = 0x5751u;
    for (int i = 0; i < 14; i++) {
        int x = 20 + rnd() % (UI_W - 40), y = 40 + rnd() % (HORIZON - 120);
        uint32_t on = rnd() % 1600;
        if (t < on || t > on + 900) continue;
        int ph = (int)((t - on) / 150) % 3;  // grows to a cross, then back to a dot
        px2(x, y, MS_CREMA);
        if (ph == 1)
            for (int d = 2; d <= 4; d += 2) px2(x - d, y, MS_GRIS), px2(x + d, y, MS_GRIS), px2(x, y - d, MS_GRIS), px2(x, y + d, MS_GRIS);
    }

    // The title, letter by letter, each dropping into place in two steps; the boot screen's own spot
title:
    if (t >= TITLE_T) {
        static const char *const L[] = {"M", "a", "r", "e", "a"};
        int x = UI_W / 2 - ui_print_w(&ui_jersey, 7, "Marea") / 2;
        char pre[6] = "";
        for (int i = 0; i < 5; i++) {
            int dt = (int)t - TITLE_T - i * LETTER_MS;
            int lx = x + ui_print_w(&ui_jersey, 7, pre);
            strcat(pre, L[i]);
            if (dt < 0) continue;
            int drop = dt < 60 ? 24 : dt < 120 ? 10 : 0;
            ui_print_sh(&ui_jersey, 7, lx, 70 - drop, L[i], MS_ORO);
        }
    }
    lcd_flush_native();
}

void intro_frame(uint32_t t) { frame(t); }

void intro_play(int brightness)
{
    int64_t t0 = esp_timer_get_time();
    bool lit = false;
    for (;;) {
        uint32_t t = (uint32_t)((esp_timer_get_time() - t0) / 1000);
        if (t > INTRO_MS) t = INTRO_MS;
        frame(t);
        if (!lit) {  // the panel lights up once the first (black) frame is on it
            lcd_set_brightness(brightness);
            lit = true;
        }
        if (t >= INTRO_MS) break;
        vTaskDelay(pdMS_TO_TICKS(15));
    }
}
