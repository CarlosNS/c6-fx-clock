// What a USB cable brings, shown for a few seconds when it is plugged in: charging, charging and a
// computer, a computer only (battery full) or just power. The screen lights up for it. It follows
// the status loop live: the computer is told apart from a charger by its USB frames (main.c), which
// takes a second or two after plugging in.
#include "app_plug.h"
#include <stdio.h>
#include "alarm.h"
#include "apps.h"
#include "esp_timer.h"
#include "find.h"
#include "sys.h"

#define SHOW_MS 5000

static uint32_t s_t0;
static int s_shown = -1;
static const app_t app_plug;

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

void plug_notice(void)
{
    // An alarm, a call or the find screen matter more
    if (alarm_ringing() || find_active() || sys_keep_awake()) return;
    s_t0 = now_ms();
    s_shown = -1;
    app_wake();
    sys_alert(&app_plug);
}

static void p_open(void) { s_shown = -1; }

static bool p_draw(uint32_t now, bool force)
{
    if ((int32_t)(now - s_t0) > SHOW_MS) {  // signed: "now" may be a moment older than s_t0
        sys_alert_done();
        return false;
    }
    bool charging = g_app.bat_chg && !g_app.bat_full, pc = g_app.usb_data;
    int state = (charging ? 1 : 0) | (pc ? 2 : 0) | (g_app.bat_pct & 0xFF) << 2;
    if (!force && state == s_shown) return false;
    s_shown = state;
    ui_background(false);
    ui_veil(40, UI_H, 2);
    const char *title, *line;
    if (charging && pc) title = TR("Cargando", "Charging"), line = TR("y con el ordenador", "and on a computer");
    else if (charging) title = TR("Cargando", "Charging"), line = TR("con el cargador", "on the charger");
    else if (pc) title = TR("Ordenador", "Computer"), line = g_app.bat_pct >= 0 ? TR("batería llena", "battery full") : TR("conectado por USB", "connected over USB");
    else title = TR("Cargada", "Full"), line = TR("batería llena", "battery full");
    ui_print_c(&ui_jersey, 5, UI_W / 2, 92, title, MS_ORO, true);
    ui_print_c(&ui_vt, ui_fit(&ui_vt, 2, 1, UI_W - 30, line), UI_W / 2, 168, line, MS_CREMA, false);
    if (g_app.bat_pct >= 0) {
        ui_battery(UI_W / 2 - 80, 226, 160, 76, g_app.bat_pct, charging);
        char b[8];
        snprintf(b, sizeof(b), "%d %%", g_app.bat_pct);
        ui_print_c(&ui_jersey, 4, UI_W / 2, 326, b, charging ? MS_VERDE : MS_CREMA, true);
    }
    if (pc) ui_print_c(&ui_vt, 1, UI_W / 2, 402, TR("microSD como disco: app Archivos", "microSD as a disk: Files app"), MS_GRIS, false);
    return true;
}

static void p_event(const ui_event_t *e)
{
    if (e->type == EV_TAP) sys_alert_done();
}

static const app_t app_plug = {"USB", "USB", NULL, p_open, p_draw, p_event, NULL, NULL};
