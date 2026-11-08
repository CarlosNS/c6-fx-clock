// Stopwatch: start, pause, reset and laps (the last ones listed below the time). It keeps running
// while other screens are shown.
#include <stdio.h>
#include "apps.h"
#include "esp_timer.h"
#include "sys.h"
#include "ui_icons.h"

#define MAX_LAPS 30
#define BTN_Y 262
#define BTN_H 70
#define BTN_W 150

static bool s_running;
static int64_t s_start, s_acc;  // us
static int64_t s_laps[MAX_LAPS];
static int s_nlaps;
static int s_pressed = -1;
static uint32_t s_last_cs = UINT32_MAX;

static int64_t elapsed(void)
{
    return s_acc + (s_running ? esp_timer_get_time() - s_start : 0);
}

static void fmt(char *buf, size_t n, int64_t us)
{
    int64_t t = us / 10000;  // centiseconds
    int c = t % 100, s = t / 100 % 60, m = t / 6000 % 60, h = t / 360000;
    if (h) snprintf(buf, n, "%d:%02d:%02d,%02d", h, m, s, c);
    else snprintf(buf, n, "%02d:%02d,%02d", m, s, c);
}

static int hit(int x, int y)
{
    if (y < BTN_Y || y >= BTN_Y + BTN_H) return -1;
    if (x >= UI_W / 2 - 8 - BTN_W && x < UI_W / 2 - 8) return 0;
    if (x >= UI_W / 2 + 8 && x < UI_W / 2 + 8 + BTN_W) return 1;
    return -1;
}

static bool draw(uint32_t now, bool force)
{
    int64_t e = elapsed();
    uint32_t cs = (uint32_t)(e / 10000);
    if (!force && cs == s_last_cs) return false;
    s_last_cs = cs;
    char buf[24], main_t[16];
    app_frame(TR("Cronómetro", "Stopwatch"), false);
    // mm:ss big, the hundredths smaller beside them, sharing the baseline
    fmt(buf, sizeof(buf), e);
    int comma = 0;
    while (buf[comma] && buf[comma] != ',') comma++;
    snprintf(main_t, sizeof(main_t), "%.*s", comma, buf);
    const char *frac = buf + comma;
    int big = comma > 5 ? 5 : 7;
    int wm = ui_print_w(&ui_jersey, big, main_t), wf = ui_print_w(&ui_jersey, 4, frac);
    int x = (UI_W - wm - wf) / 2;
    ui_print_sh(&ui_jersey, big, x, 116, main_t, s_running ? MS_ORO : MS_CREMA);
    ui_print_sh(&ui_jersey, 4, x + wm, 116 + (big - 4) * 15, frac, MS_CREMA);
    // Buttons: left lap / reset, right start / pause / resume
    const char *left = s_running ? TR("Vuelta", "Lap") : TR("Reiniciar", "Reset");
    const char *right = s_running ? TR("Pausa", "Pause") : e ? TR("Seguir", "Resume") : TR("Iniciar", "Start");
    ui_button(UI_W / 2 - 8 - BTN_W, BTN_Y, BTN_W, BTN_H, left, UI_BTN_SECONDARY, s_pressed == 0);
    ui_button(UI_W / 2 + 8, BTN_Y, BTN_W, BTN_H, right, UI_BTN_MAIN, s_pressed == 1);
    // Last laps, newest first: lap time and total
    for (int i = 0; i < 3 && i < s_nlaps; i++) {
        int k = s_nlaps - 1 - i;
        int64_t lap = s_laps[k] - (k ? s_laps[k - 1] : 0);
        char a[24], b[24], line[16];
        fmt(a, sizeof(a), lap);
        fmt(b, sizeof(b), s_laps[k]);
        int y = 356 + i * 36;
        snprintf(line, sizeof(line), "%s%d", TR("V", "L"), k + 1);
        ui_print(&ui_vt, 2, 66, y - 8, line, MS_GRIS);
        ui_print(&ui_vt, 2, 140, y - 8, a, i ? MS_CREMA : MS_ORO2);
        ui_print_r(&ui_vt, 1, 348, y, b, MS_GRIS);
    }
    return true;
}

static void event(const ui_event_t *e)
{
    if (e->type == EV_DOWN) {
        s_pressed = hit(e->x, e->y);
    } else if (e->type == EV_UP) {
        s_pressed = -1;
    } else if (e->type == EV_TAP) {
        int b = hit(e->x, e->y);
        int64_t now = esp_timer_get_time();
        if (b == 1) {
            if (s_running) {
                s_acc += now - s_start;
                s_running = false;
            } else {
                s_start = now;
                s_running = true;
            }
        } else if (b == 0) {
            if (s_running) {
                if (s_nlaps == MAX_LAPS) {
                    for (int i = 1; i < MAX_LAPS; i++) s_laps[i - 1] = s_laps[i];
                    s_nlaps--;
                }
                s_laps[s_nlaps++] = elapsed();
            } else {
                s_acc = 0;
                s_nlaps = 0;
            }
        }
    }
    sys_dirty();
}

const app_t app_stopwatch = {"Cronómetro", "Stopwatch", icon_stopwatch, NULL, draw, event, NULL, NULL};
