// Steps: page 1 is today (a pixel ring filling towards the goal, the count, calories and distance);
// page 2 the last 30 days as a bar chart in the style of the guide (dashed guides, a bright crest on
// each column, 0 shown as a thin line). Swipe sideways to change page; tap a bar to read it.
#include <math.h>
#include <stdio.h>
#include "apps.h"
#include "steps.h"
#include "sys.h"
#include "ui_icons.h"

static int s_page, s_sel = -1, s_shown = -1;

static void open(void)
{
    s_page = 0;
    s_sel = -1;
}

static void thousands(char *b, size_t n, int v)
{
    if (v >= 1000) snprintf(b, n, "%d.%03d", v / 1000, v % 1000);
    else snprintf(b, n, "%d", v);
}

// Ring of square "pixels" around (cx, cy): the first `permille` of it in gold, starting at the top
static void ring(int cx, int cy, int r, int permille)
{
    const int dots = 72, sz = 12;
    for (int i = 0; i < dots; i++) {
        float a = -1.5707963f + i * 6.2831853f / dots;
        int x = cx + (int)lroundf(r * cosf(a)) - sz / 2, y = cy + (int)lroundf(r * sinf(a)) - sz / 2;
        bool on = i * 1000 < permille * dots;
        ui_rect(x, y, sz, sz, on ? (permille >= 1000 ? MS_VERDE : MS_ORO) : MS_BARRA_OFF);
    }
}

static void page_today(void)
{
    char buf[32], line[48];
    int st = steps_today(), goal = g_app.step_goal > 0 ? g_app.step_goal : 10000;
    int pm = st >= goal ? 1000 : st * 1000 / goal;
    ring(UI_W / 2, 236, 112, pm);
    thousands(buf, sizeof(buf), st);
    int sc = ui_fit(&ui_jersey, 6, 3, 180, buf);
    ui_print_c(&ui_jersey, sc, UI_W / 2, 236 - sc * 9, buf, MS_ORO, true);
    thousands(buf, sizeof(buf), goal);
    snprintf(line, sizeof(line), TR("de %s", "of %s"), buf);
    ui_print_c(&ui_vt, 2, UI_W / 2, 236 + 30, line, MS_GRIS, false);
    // Rough figures for an adult: 0.04 kcal and 0.75 m per step
    int kcal = st * 4 / 100, m = st * 3 / 4;
    snprintf(buf, sizeof(buf), "%d kcal", kcal);
    ui_print(&ui_vt, 2, 72, 380, buf, MS_CREMA);
    snprintf(buf, sizeof(buf), "%d,%d km", m / 1000, m % 1000 / 100);
    ui_print_r(&ui_vt, 2, UI_W - 72, 380, buf, MS_CREMA);
}

#define G_X 48
#define G_Y 150
#define G_W 314
#define G_H 210

static void page_history(void)
{
    char buf[40];
    int maxv = g_app.step_goal;
    for (int d = 0; d < STEPS_DAYS; d++)
        if (steps_day(d) > maxv) maxv = steps_day(d);
    ui_rect(G_X, G_Y, G_W, G_H, MS_FONDO_GRAF);
    for (int q = 1; q < 4; q++)  // dashed guides at 25, 50 and 75 %
        for (int x = G_X; x < G_X + G_W; x += 18) ui_rect(x, G_Y + G_H - G_H * q / 4, 8, 2, MS_BARRA_OFF);
    // goal line
    int gy = G_Y + G_H - G_H * g_app.step_goal / maxv;
    for (int x = G_X; x < G_X + G_W; x += 12) ui_rect(x, gy, 6, 2, MS_ORO2);
    int cw = G_W / STEPS_DAYS;
    for (int d = STEPS_DAYS - 1; d >= 0; d--) {
        int v = steps_day(d), col = STEPS_DAYS - 1 - d, x = G_X + col * cw + 1;
        if (v <= 0) {
            ui_rect(x, G_Y + G_H - 2, cw - 2, 2, MS_LINEA);
            continue;
        }
        int h = v * (G_H - 6) / maxv;
        uint16_t bright = v >= g_app.step_goal ? MS_VERDE : MS_ORO;
        ui_rect(x, G_Y + G_H - h, cw - 2, h, ui_mix(bright, MS_FONDO_GRAF, 90));
        ui_rect(x, G_Y + G_H - h, cw - 2, 5, bright);
        if (d == s_sel) ui_frame(x - 1, G_Y + G_H - h - 1, cw, h + 2, 1, MS_CREMA);
    }
    ui_print(&ui_vt, 1, G_X, G_Y + G_H + 8, TR("hace 30 días", "30 days ago"), MS_GRIS);
    ui_print_r(&ui_vt, 1, G_X + G_W, G_Y + G_H + 8, TR("hoy", "today"), MS_GRIS);
    if (s_sel >= 0) {
        char n[16];
        thousands(n, sizeof(n), steps_day(s_sel) < 0 ? 0 : steps_day(s_sel));
        if (s_sel == 0) snprintf(buf, sizeof(buf), TR("hoy · %s", "today · %s"), n);
        else snprintf(buf, sizeof(buf), TR("hace %d d · %s", "%d d ago · %s"), s_sel, n);
        ui_print_c(&ui_jersey, 3, UI_W / 2, G_Y + G_H + 44, buf, MS_ORO2, false);
    } else {
        int sum = 0, days = 0;
        for (int d = 1; d < STEPS_DAYS; d++)
            if (steps_day(d) > 0) sum += steps_day(d), days++;
        char n[16];
        thousands(n, sizeof(n), days ? sum / days : 0);
        snprintf(buf, sizeof(buf), TR("media · %s", "average · %s"), n);
        ui_print_c(&ui_jersey, 3, UI_W / 2, G_Y + G_H + 44, buf, MS_CREMA, false);
    }
}

static bool draw(uint32_t now, bool force)
{
    int st = steps_today();
    if (!force && st == s_shown) return false;
    s_shown = st;
    app_frame(TR("Pasos", "Steps"), false);
    if (s_page == 0) page_today();
    else page_history();
    ui_dots(UI_W / 2, UI_H - 46, 2, s_page);
    return true;
}

static void event(const ui_event_t *e)
{
    if (e->type == EV_SWIPE && (e->dir == DIR_LEFT || (e->dir == DIR_RIGHT && !(e->edge & EDGE_LEFT)))) {
        s_page = e->dir == DIR_LEFT ? 1 : 0;
        s_sel = -1;
        sys_dirty();
    } else if (e->type == EV_TAP && s_page == 1) {
        int cw = G_W / STEPS_DAYS, col = (e->x - G_X) / cw;
        s_sel = e->x >= G_X && col >= 0 && col < STEPS_DAYS && e->y > G_Y - 20 && e->y < G_Y + G_H + 20 ? STEPS_DAYS - 1 - col : -1;
        sys_dirty();
    }
}

const app_t app_steps = {"Pasos", "Steps", icon_steps, open, draw, event, NULL, NULL};
