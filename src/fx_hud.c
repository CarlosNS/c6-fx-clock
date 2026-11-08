// Sci-fi HUD (full screen, wide canvas, portrait): time on top, telemetry, then the seconds ring with rotating arcs, 7-segment time over "ghost" segments,
// live telemetry (chip temperature, Wi-Fi signal, uptime), sweep line, scanlines and a glitch
// burst when the minute changes.
#include <stdio.h>
#include <string.h>
#include "app.h"
#include "control.h"
#include "driver/temperature_sensor.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "fx.h"
#include "gfx.h"

#define W WW
#define H WH
#define RCX (W / 2)  // seconds ring at the bottom; time and telemetry above it
#define RCY 252

static temperature_sensor_handle_t s_temp;
static float s_temp_c;
static int s_rssi;
static uint32_t s_last_read, s_glitch_until;
static int s_last_min = -1;

void hud_enter(void)
{
    if (!s_temp) {
        temperature_sensor_config_t cfg = TEMPERATURE_SENSOR_CONFIG_DEFAULT(-10, 80);
        if (temperature_sensor_install(&cfg, &s_temp) == ESP_OK) temperature_sensor_enable(s_temp);
    }
    s_last_read = 0;
}

static void polar(int r, int a, int *x, int *y)  // a: 0..1023, 0 = 12 o'clock, clockwise
{
    *x = RCX + (sin_q14[a & 1023] * r >> 14);
    *y = RCY - (sin_q14[(a + 256) & 1023] * r >> 14);
}

static void arc(uint16_t *fb, int r, int a0, int span, uint16_t c)
{
    for (int a = a0; a < a0 + span; a += 3) {
        int x, y;
        polar(r, a, &x, &y);
        gfx_fill_rect(fb, x, y, 2, 2, c);
    }
}

static void bracket(uint16_t *fb, int x, int y, int sx, int sy, uint16_t c)
{
    gfx_fill_rect(fb, sx > 0 ? x : x - 11, y, 12, 2, c);
    gfx_fill_rect(fb, x, sy > 0 ? y : y - 11, 2, 12, c);
}

void hud_render(uint16_t *fb, uint32_t t, uint32_t dt)
{
    int dx, dy;
    app_take_drag(&dx, &dy);
    const uint16_t bg = rgb565(2, 8, 16), cy = rgb565(0, 230, 255), cyd = rgb565(0, 95, 120),
                   cydd = rgb565(0, 36, 48), white = rgb565(230, 255, 255), orange = rgb565(255, 150, 30);

    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    if (now - s_last_read > 2000 || s_last_read == 0) {
        s_last_read = now;
        if (s_temp) temperature_sensor_get_celsius(s_temp, &s_temp_c);
        wifi_ap_record_t ap;
        s_rssi = esp_wifi_sta_get_ap_info(&ap) == ESP_OK ? ap.rssi : 0;
    }
    if (g_now.valid && g_now.m != s_last_min) {
        if (s_last_min >= 0) s_glitch_until = now + 600;
        s_last_min = g_now.m;
    }

    // Background: dot grid + sweep line
    gfx_fill(fb, bg);
    for (int y = 4; y < H; y += 16)
        for (int x = 4; x < W; x += 16) fb[y * W + x] = cydd;
    int sweep = (t / 12) % (H + 30) - 15;
    for (int k = 0; k < 12; k++) {
        int y = sweep - k;
        if (y >= 0 && y < H)
            for (int x = 0; x < W; x++) fb[y * W + x] = gfx_blend(fb[y * W + x], cyd, 12 - k);
    }

    // Seconds ring
    int sec = g_now.valid ? g_now.s : -1;
    for (int i = 0; i < 60; i++) {
        int a = i * 1024 / 60, x0, y0, x1, y1;
        bool major = i % 5 == 0;
        uint16_t c = i == sec ? white : (i < sec ? cy : cydd);
        polar(i == sec ? 56 : (major ? 60 : 63), a, &x0, &y0);
        polar(i == sec ? 74 : 70, a, &x1, &y1);
        gfx_line(fb, x0, y0, x1, y1, c);
        gfx_line(fb, x0 + 1, y0, x1 + 1, y1, c);
    }
    int rot = t / 6;
    for (int k = 0; k < 3; k++) arc(fb, 50, rot + k * 341, 200, cyd);
    for (int k = 0; k < 16; k++) arc(fb, 42, -rot * 2 + k * 64, 22, k % 4 == 0 ? orange : cydd);

    char buf[32];
    if (g_app.show_secs && g_now.valid) {
        snprintf(buf, sizeof(buf), "%02d", g_now.s);
        font_draw(fb, &font_seg_s, RCX - font_width(&font_seg_s, buf, 2) / 2, RCY - 22, buf, 2, white, bg, FONT_FILL | FONT_OUTLINE);
        gfx_text(fb, RCX - 8, RCY + 17, "SEG", cyd, 1);
    } else {
        snprintf(buf, sizeof(buf), "%02d", g_now.valid ? g_now.day : 0);
        font_draw(fb, &font_seg_s, RCX - font_width(&font_seg_s, buf, 2) / 2, RCY - 22, buf, 2, white, bg, FONT_FILL | FONT_OUTLINE);
        gfx_text(fb, RCX - 8, RCY + 17, clock_wday_name(g_now.wday), cyd, 1);
    }

    // Time panel
    const int pw = 156, px = (W - pw) / 2;
    gfx_text(fb, px + 18, 24, "SYS.TIME // CET", cyd, 1);
    gfx_fill_rect(fb, px + 118, 26, 4, 4, (now / 500) & 1 ? orange : cydd);
    int tw = font_width(&font_seg_m, "88:88", -4);
    int tx = px + (pw - tw) / 2;
    font_draw(fb, &font_seg_m, tx, 38, "88:88", -4, cydd, bg, FONT_FILL);  // unlit segments
    clock_hhmm(buf, !g_now.valid || g_now.ms < 500);
    font_draw(fb, &font_seg_m, tx, 38, buf, -4, cy, bg, FONT_FILL | FONT_OUTLINE);
    if (!g_app.h24 && g_now.valid) gfx_text(fb, px + 128, 24, g_now.h < 12 ? "AM" : "PM", orange, 1);

    gfx_fill_rect(fb, px, 106, pw, 1, cyd);
    gfx_fill_rect(fb, px, 104, 2, 5, cy);
    gfx_fill_rect(fb, px + pw - 2, 104, 2, 5, cy);

    static const char *labels[] = {"FECHA", "TEMP", "WIFI", "UPTIME"};
    char vals[4][24];
    if (g_now.valid) snprintf(vals[0], 24, "%02d.%02d.%04d", g_now.day, g_now.mon, g_now.year);
    else strcpy(vals[0], "SIN SYNC");
    snprintf(vals[1], 24, "%.1f C", s_temp_c);
    if (s_rssi) snprintf(vals[2], 24, "%d dBm", s_rssi);
    else strcpy(vals[2], "AP LOCAL");
    uint32_t up = now / 1000;
    snprintf(vals[3], 24, "%02lu:%02lu:%02lu", (unsigned long)(up / 3600), (unsigned long)(up / 60 % 60), (unsigned long)(up % 60));
    for (int i = 0; i < 4; i++) {
        gfx_text(fb, px + 8, 114 + i * 12, labels[i], cyd, 1);
        gfx_text(fb, px + 62, 114 + i * 12, vals[i], i == 1 && s_temp_c > 60 ? orange : cy, 1);
    }

    // Signal-strength style bars driven by the Wi-Fi RSSI, with a little live jitter
    int level = s_rssi ? (s_rssi + 95) * 16 / 55 : 6;
    for (int i = 0; i < 16; i++) {
        int h = 3 + (sin_u8[(uint8_t)(t / 9 + i * 23)] >> 5);
        uint16_t c = i < level ? (i > 12 ? orange : cy) : cydd;
        gfx_fill_rect(fb, px + 6 + i * 9, 174 - h, 6, h, c);
    }

    // Corner brackets, tucked inside the display's rounded corners
    bracket(fb, 28, 28, 1, 1, cy);
    bracket(fb, W - 30, 28, -1, 1, cy);
    bracket(fb, 28, H - 30, 1, -1, cy);
    bracket(fb, W - 30, H - 30, -1, -1, cy);

    // Glitch burst: shift random slices of the time panel sideways
    if ((int32_t)(s_glitch_until - now) > 0) {
        for (int k = 0; k < 10; k++) {
            int y = 36 + fx_rand() % 66, hgt = 2 + fx_rand() % 6, sh = (int)(fx_rand() % 17) - 8;
            for (int yy = y; yy < y + hgt && yy < H; yy++) {
                uint16_t *row = fb + yy * W + px;
                if (sh > 0) memmove(row + sh, row, (pw - sh) * 2);
                else if (sh < 0) memmove(row, row - sh, (pw + sh) * 2);
            }
        }
    }

    // CRT scanlines
    for (int y = 1; y < H; y += 3) gfx_darken_rect(fb, 0, y, W, 1, 0, 1);
}
