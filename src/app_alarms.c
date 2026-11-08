// Alarms app: the list (time, repeat days, switch) and an editor (big hh:mm with arrows above and
// below each pair of digits, the seven days, delete / done). The ringing screen is app_alarm_ring.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "alarm.h"
#include "apps.h"
#include "audio.h"
#include "clock.h"
#include "esp_timer.h"
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
// A sunrise over the sea in pixel art (2x2 art pixels, like the system backgrounds): while the alarm
// rings the sun climbs out of the sea in about a minute and the sky turns from night to gold, so how
// high it is tells how long it has been ringing. Snoozing sinks it back into the sea; stopping lets
// it finish rising and the sky brighten, then the watch goes back to where it was.

#define RA_W 205        // art grid, shown 2x2
#define RA_H 251
#define RA_HOR 156      // horizon row
#define RISE_MS 60000
#define EXIT_MS 900

static uint32_t s_ring_t0, s_exit_t0;
static int s_exit;      // 0 ringing, 1 snoozing (sun sinks), 2 stopping (sun rises)
static float s_exit_from;

static void ring_open(void)
{
    s_ring_t0 = 0;
    s_exit = 0;
}

static inline int lerp(int a, int b, int f) { return a + (b - a) * f / 256; }

static uint16_t sky_at(int y, int day)  // day 0 (night) .. 256 (dawn)
{
    int f = y * 256 / RA_HOR;  // 0 top .. 256 horizon
    // night: #14102C -> #463A7A at the horizon; dawn: #463A7A -> #F9B25A / coral at the horizon
    int nr = lerp(0x14, 0x46, f), ng = lerp(0x10, 0x3A, f), nb = lerp(0x2C, 0x7A, f);
    int f2 = f * f >> 8;
    int dr = lerp(0x46, 0xF9, f2), dg = lerp(0x3A, 0xB2 - 0x30 * (256 - f) / 256, f2), db = lerp(0x7A, 0x5A, f2);
    return ui_rgb(lerp(nr, dr, day), lerp(ng, dg, day), lerp(nb, db, day));
}

static inline void apx(int x, int y, uint16_t c)  // one art pixel
{
    ui_rect(x * 2, y * 2, 2, 2, c);
}

static void sunrise(uint32_t t, float rise)
{
    int day = (int)(rise * 256);
    if (day > 256) day = 256;
    if (day < 0) day = 0;
    // Sky, one band per art row
    for (int y = 0; y < RA_HOR; y++) ui_rect(0, y * 2, UI_W, 2, sky_at(y, day));
    // Stars fade as the sky lightens
    for (int i = 0; i < 40; i++) {
        uint32_t h = (uint32_t)i * 2654435761u;
        int x = h % RA_W, y = (h >> 12) % (RA_HOR - 40);
        int tw = ((t / 300 + i) % 7) ? 200 : 120;
        int a = (256 - day * 2) * tw / 256;
        if (a > 0) apx(x, y, ui_mix(MS_CREMA, sky_at(y, day), a));
    }
    // Sun: rises from below the horizon to well above it, bands cut its lower part near the sea
    int r = 24, sx = RA_W / 2;
    int sy = RA_HOR + r + 4 - (int)((rise > 1.05f ? 1.05f : rise) * 60);  // ends just under the time
    uint16_t sun_hi = ui_rgb(0xFF, 0xE6, 0xA0), sun = MS_ORO2, sun_lo = MS_ORO;
    for (int dy = -r - 10; dy <= r + 10; dy++) {
        int y = sy + dy;
        if (y < 0 || y >= RA_HOR) continue;
        for (int dx = -r - 10; dx <= r + 10; dx++) {
            int d2 = dx * dx + dy * dy, x = sx + dx;
            if (x < 0 || x >= RA_W) continue;
            if (d2 <= r * r) {
                if (dy > r / 3 && ((y + t / 120) & 3) == 0) continue;  // the retro bands
                apx(x, y, dy < -r / 2 ? sun_hi : dy < r / 3 ? sun : sun_lo);
            } else if (d2 <= (r + 10) * (r + 10) && ((x + y) & 1) == 0) {
                apx(x, y, ui_mix(MS_ORO, sky_at(y, day), 90));  // dithered glow
            }
        }
    }
    // Sea: dark bands with moving glints and the sun's reflection breaking on the waves
    uint16_t deep = ui_mix(ui_rgb(0x46, 0x3A, 0x7A), MS_NOCHE, 70 + day / 3), wave = ui_mix(MS_LINEA, deep, 120);
    for (int y = RA_HOR; y < RA_H; y++) {
        int d = y - RA_HOR;
        ui_rect(0, y * 2, UI_W, 2, d < 2 ? ui_mix(MS_ORO, deep, day) : deep);
        // Wave crests
        for (int k = 0; k < 6; k++) {
            uint32_t h = (uint32_t)(y * 31 + k) * 2246822519u;
            int len = 3 + (h >> 28), x = (int)((h >> 8) % RA_W + t / (40 + d)) % (RA_W + 20) - 10;
            if ((h & 3) == 0)
                for (int q = 0; q < len; q++)
                    if (x + q >= 0 && x + q < RA_W) apx(x + q, y, wave);
        }
        // Reflection column: wider towards the viewer, broken by the waves, only once the sun is up
        if (sy < RA_HOR + r && day > 20) {
            int half = (6 + d / 3) * (day > 128 ? 256 : day * 2) / 256 + 1, sh = (int)((sinf((float)(y * 0.7f) + t * 0.004f)) * 4);
            for (int x = sx - half + sh; x < sx + half + sh; x++)
                if (((x * 7 + y * 3 + t / 90) % 5) < 2 && x >= 0 && x < RA_W)
                    apx(x, y, ui_mix(MS_ORO2, deep, 120 + day / 2));
        }
    }
    // Two gulls crossing the sky
    for (int g = 0; g < 2; g++) {
        int gx = (int)((t / (55 + g * 20) + g * 90) % (RA_W + 40)) - 20, gy = 40 + g * 22 + (int)(sinf(t * 0.002f + g) * 4);
        int flap = ((t / 220 + g) & 1) ? 1 : -1;
        uint16_t c = ui_mix(MS_NOCHE, sky_at(gy, day), 200);
        for (int k = 1; k <= 4; k++) {
            apx(gx - k, gy - (k * flap) / 2, c);
            apx(gx + k, gy - (k * flap) / 2, c);
        }
        apx(gx, gy, c);
    }
}

static bool ring_draw(uint32_t now, bool force)
{
    (void)force;
    if (!s_ring_t0) s_ring_t0 = now ? now : 1;
    uint32_t el = now - s_ring_t0;
    float rise = el / (float)RISE_MS;
    if (rise > 1) rise = 1;
    if (!s_exit) {
        // Shaking the watch snoozes it, like a hand slapping a bedside clock
        int x, y, z;
        bool shake = imu_read(&x, &y, &z) && (abs(x) + abs(y) + abs(z)) > 2600 && el > 1500;
        if (shake || el > 120000) {  // or two minutes unanswered: snooze by itself
            s_exit = 1;
            s_exit_t0 = now;
            s_exit_from = rise;
            audio_stop();
        }
    }
    if (s_exit) {
        if (!s_exit_t0) s_exit_t0 = now ? now : 1;  // the tap only asks: the clock is this loop's
        float f = (now - s_exit_t0) / (float)EXIT_MS;
        if (f >= 1) {
            if (s_exit == 1) alarm_snooze();
            else alarm_stop();
            return false;
        }
        // ease out: the sun sinks back into the sea, or finishes rising
        float e = 1 - (1 - f) * (1 - f);
        rise = s_exit == 1 ? s_exit_from * (1 - e) - 0.25f * e : s_exit_from + (1.25f - s_exit_from) * e;
    }
    sunrise(now, rise);
    // The time over the sky, with a soft beat
    const alarm_t *a = alarm_current();
    char buf[8];
    snprintf(buf, sizeof(buf), "%02d:%02d", a ? a->h : g_now.h, a ? a->m : g_now.m);
    int beat = (int)((sinf(el * 0.0063f) + 1) * 128);  // about once a second
    ui_print_c(&ui_jersey, 3, UI_W / 2, 36, TR("ALARMA", "ALARM"), MS_CREMA, true);
    ui_print_c(&ui_jersey, 8, UI_W / 2, 72, buf, ui_mix(MS_ORO2, MS_CREMA, s_exit ? 0 : beat), true);
    if (!s_exit) {
        ui_button(UI_W / 2 - 160, 350, 150, 76, TR("Posponer", "Snooze"), UI_BTN_SECONDARY, false);
        ui_button(UI_W / 2 + 10, 350, 150, 76, TR("Parar", "Stop"), UI_BTN_MAIN, false);
        ui_print_c(&ui_vt, 1, UI_W / 2, 436, TR("agita el reloj para posponer", "shake the watch to snooze"), MS_CREMA, false);
    }
    return true;
}

static void ring_event(const ui_event_t *e)
{
    if (s_exit || e->type != EV_TAP || e->y < 340 || e->y > 440) return;
    s_exit = e->x < UI_W / 2 ? 1 : 2;
    s_exit_t0 = 0;  // set by the next frame, with the same clock as the animation
    float rise = (uint32_t)(esp_timer_get_time() / 1000 - s_ring_t0) / (float)RISE_MS;
    s_exit_from = rise > 1 ? 1 : rise < 0 ? 0 : rise;
    audio_stop();
}

static bool ring_keep_on(void) { return true; }

const app_t app_alarm_ring = {"Alarma", "Alarm", icon_alarms, ring_open, ring_draw, ring_event, NULL, ring_keep_on};
