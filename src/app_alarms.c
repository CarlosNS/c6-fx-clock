// Alarms app: the list (time, repeat days, switch) and an editor (big hh:mm with arrows above and
// below each pair of digits, the seven days, delete / done). The ringing screen is app_alarm_ring.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "alarm.h"
#include "apps.h"
#include "audio.h"
#include "clock.h"
#include "imu.h"
#include "sys.h"
#include "ui_icons.h"

#define TOP 122
#define ROW 96
#define X0 44
#define WID 322

static int s_edit = -1;        // alarm being edited, -1 = list
static alarm_t s_draft;
static bool s_new;
static ui_scroll_t s_scroll;

static void days_text(const alarm_t *a, int x, int y, int scale)
{
    for (int d = 0; d < 7; d++) {
        bool on = a->days >> d & 1;
        ui_print(&ui_vt, scale, x + d * 14 * scale, y, alarm_day_letter(d), on ? MS_ORO2 : MS_PILOTO_OFF);
    }
}

static void open(void)
{
    s_edit = -1;
    s_scroll.offset = 0;
}

// ---- list

static int list_rows(void) { return g_nalarms + (g_nalarms < ALARMS_MAX ? 1 : 0); }

static void draw_list(void)
{
    char buf[16];
    s_scroll.view_h = UI_H - TOP - 30;
    s_scroll.content_h = list_rows() * ROW;
    ui_background(false);
    ui_veil(60, UI_H, 3);
    for (int i = 0; i < list_rows(); i++) {
        int y = TOP + i * ROW - s_scroll.offset;
        if (y > UI_H || y + ROW < TOP - 20) continue;
        if (i == g_nalarms) {  // "new alarm"
            ui_button(X0 + 40, y + 10, WID - 80, 64, TR("+ Nueva", "+ New"), UI_BTN_SECONDARY, false);
            continue;
        }
        alarm_t *a = &g_alarms[i];
        ui_panel(X0, y, WID, ROW - 12, MS_PANEL, a->on ? MS_ORO : MS_LINEA, 4, false);
        snprintf(buf, sizeof(buf), "%02d:%02d", a->h, a->m);
        ui_print(&ui_jersey, 4, X0 + 16, y + 6, buf, a->on ? MS_CREMA : MS_GRIS);
        if (a->days) days_text(a, X0 + 18, y + 54, 1);
        else ui_print(&ui_vt, 1, X0 + 18, y + 54, TR("una vez", "once"), MS_GRIS);
        ui_switch(X0 + WID - 84, y + 26, a->on);
    }
    if (!g_nalarms)
        ui_print_c(&ui_vt, 2, UI_W / 2, TOP + ROW + 20, TR("sin alarmas", "no alarms"), MS_GRIS, false);
    ui_background_band(false, 0, TOP - 16);
    ui_shade(0, 0, UI_W, TOP - 16, 1);
    ui_status_bar();
    ui_print_c(&ui_jersey, 4, UI_W / 2, 64, TR("Alarmas", "Alarms"), MS_ORO, true);
}

static void list_event(const ui_event_t *e)
{
    ui_scroll_event(&s_scroll, e);
    if (e->type != EV_TAP) return;
    int i = (e->y - TOP + s_scroll.offset) / ROW;
    if (e->y < TOP || i < 0 || i >= list_rows()) return;
    if (i == g_nalarms) {
        s_new = true;
        s_draft = (alarm_t){g_now.valid ? (g_now.h + 1) % 24 : 7, 0, 0x1F, 1};  // weekdays
        s_edit = i;
    } else if (e->x >= X0 + WID - 100) {
        g_alarms[i].on = !g_alarms[i].on;
        alarms_save();
    } else {
        s_new = false;
        s_draft = g_alarms[i];
        s_edit = i;
    }
}

// ---- editor

#define E_DIG_Y 160
#define E_ARROW 54
#define E_DAYS_Y 316
#define E_BTN_Y 380

static void arrow(int cx, int y, bool up)
{
    for (int r = 0; r < 12; r++) {
        int w = up ? r : 11 - r;
        ui_rect(cx - w * 2, y + r * 2, w * 4 + 2, 2, MS_ORO);
    }
}

static void draw_edit(void)
{
    char buf[8];
    app_frame(s_new ? TR("Nueva alarma", "New alarm") : TR("Alarma", "Alarm"), true);
    int cx_h = UI_W / 2 - 78, cx_m = UI_W / 2 + 78;
    arrow(cx_h, E_DIG_Y - 46, true);
    arrow(cx_m, E_DIG_Y - 46, true);
    snprintf(buf, sizeof(buf), "%02d", s_draft.h);
    ui_print_c(&ui_jersey, 8, cx_h, E_DIG_Y - 20, buf, MS_CREMA, true);
    ui_print_c(&ui_jersey, 8, UI_W / 2, E_DIG_Y - 24, ":", MS_ORO, false);
    snprintf(buf, sizeof(buf), "%02d", s_draft.m);
    ui_print_c(&ui_jersey, 8, cx_m, E_DIG_Y - 20, buf, MS_CREMA, true);
    arrow(cx_h, E_DIG_Y + 110, false);
    arrow(cx_m, E_DIG_Y + 110, false);
    for (int d = 0; d < 7; d++) {
        int x = 52 + d * 44;
        bool on = s_draft.days >> d & 1;
        ui_rect(x, E_DAYS_Y, 38, 44, on ? MS_ORO : MS_PANEL);
        ui_print_c(&ui_jersey, 3, x + 19, E_DAYS_Y + 2, alarm_day_letter(d), on ? MS_NOCHE : MS_GRIS, false);
    }
    if (!s_new) ui_button(UI_W / 2 - 158, E_BTN_Y, 148, 64, TR("Borrar", "Delete"), UI_BTN_DANGER, false);
    ui_button(s_new ? UI_W / 2 - 74 : UI_W / 2 + 10, E_BTN_Y, 148, 64, TR("Listo", "Done"), UI_BTN_MAIN, false);
}

static void edit_event(const ui_event_t *e)
{
    if (e->type != EV_TAP && e->type != EV_LONG) return;
    int x = e->x, y = e->y;
    bool left = x < UI_W / 2;
    int step = e->type == EV_LONG ? (left ? 6 : 10) : 1;
    if (y >= E_DIG_Y - 60 && y < E_DIG_Y - 10) {  // up arrows
        if (left) s_draft.h = (s_draft.h + step) % 24;
        else s_draft.m = (s_draft.m + step) % 60;
    } else if (y >= E_DIG_Y + 96 && y < E_DIG_Y + 150) {  // down arrows
        if (left) s_draft.h = (s_draft.h + 24 - step % 24) % 24;
        else s_draft.m = (s_draft.m + 60 - step % 60) % 60;
    } else if (y >= E_DAYS_Y && y < E_DAYS_Y + 44 && x >= 52 && x < 52 + 7 * 44) {
        s_draft.days ^= 1 << ((x - 52) / 44);
    } else if (y >= E_BTN_Y && y < E_BTN_Y + 64) {
        bool done = s_new ? (x >= UI_W / 2 - 74 && x < UI_W / 2 + 74) : x >= UI_W / 2;
        if (done) {
            s_draft.on = 1;
            if (s_new && g_nalarms < ALARMS_MAX) g_alarms[g_nalarms++] = s_draft;
            else if (!s_new) g_alarms[s_edit] = s_draft;
        } else if (!s_new) {
            for (int i = s_edit + 1; i < g_nalarms; i++) g_alarms[i - 1] = g_alarms[i];
            g_nalarms--;
        }
        alarms_save();
        s_edit = -1;
    }
}

static bool draw(uint32_t now, bool force)
{
    if (!force) return false;
    if (s_edit >= 0) draw_edit();
    else draw_list();
    return true;
}

static void event(const ui_event_t *e)
{
    if (s_edit >= 0) edit_event(e);
    else list_event(e);
    sys_dirty();
}

const app_t app_alarms = {"Alarmas", "Alarms", icon_alarms, open, draw, event, NULL, NULL};

// ---------------------------------------------------------------- ringing

static uint32_t s_ring_t0;
static int s_ring_phase = -1;

static void ring_open(void)
{
    s_ring_t0 = 0;
    s_ring_phase = -1;
}

static bool ring_draw(uint32_t now, bool force)
{
    if (!s_ring_t0) s_ring_t0 = now ? now : 1;
    // Shaking the watch snoozes it, like a hand slapping a bedside clock
    int x, y, z;
    if (imu_read(&x, &y, &z) && (abs(x) + abs(y) + abs(z)) > 2600 && now - s_ring_t0 > 1500) {
        alarm_snooze();
        return false;
    }
    if (now - s_ring_t0 > 120000) {  // two minutes unanswered: snooze by itself
        alarm_snooze();
        return false;
    }
    int phase = (int)((now - s_ring_t0) / 500) & 1;
    if (!force && phase == s_ring_phase) return false;
    s_ring_phase = phase;
    char buf[8];
    ui_background(true);
    ui_veil(40, UI_H, 2);
    if (phase) ui_frame(8, 8, UI_W - 16, UI_H - 16, 10, MS_ORO);
    ui_print_c(&ui_jersey, 4, UI_W / 2, 64, TR("ALARMA", "ALARM"), MS_ORO, true);
    const alarm_t *a = alarm_current();
    snprintf(buf, sizeof(buf), "%02d:%02d", a ? a->h : g_now.h, a ? a->m : g_now.m);
    ui_print_c(&ui_jersey, 10, UI_W / 2, 120, buf, MS_CREMA, true);
    ui_print_c(&ui_vt, 1, UI_W / 2, 296, TR("agita el reloj para posponer", "shake the watch to snooze"), MS_GRIS, false);
    ui_button(UI_W / 2 - 160, 350, 150, 76, TR("Posponer", "Snooze"), UI_BTN_SECONDARY, false);
    ui_button(UI_W / 2 + 10, 350, 150, 76, TR("Parar", "Stop"), UI_BTN_MAIN, false);
    return true;
}

static void ring_event(const ui_event_t *e)
{
    if (e->type != EV_TAP || e->y < 340 || e->y > 440) return;
    if (e->x < UI_W / 2) alarm_snooze();
    else alarm_stop();
}

static bool ring_keep_on(void) { return true; }

const app_t app_alarm_ring = {"Alarma", "Alarm", icon_alarms, ring_open, ring_draw, ring_event, NULL, ring_keep_on};
