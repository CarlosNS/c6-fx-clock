// Settings: a scrolling list of rows, on the dusk background (the style keeps dusk for configuration).
// Rows are switches (tap), sliders (drag sideways), choices (tap cycles) and information. Every
// change goes through settings_set(), like the phone app and the web page, so it is saved and they
// all see it.
// The row table leaves the fields a kind does not use at zero
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "apps.h"
#include "ble.h"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "ota.h"
#include "settings.h"
#include "sys.h"
#include "ui_icons.h"

#define TOP 120
#define ROW 82
#define X0 40
#define WID 330

typedef enum { R_HEAD, R_SWITCH, R_SLIDER, R_CHOICE, R_INFO, R_ACTION } row_kind_t;

typedef struct {
    row_kind_t kind;
    const char *es, *en;
    const char *key;            // setting for switch / slider / choice
    volatile int *val;
    int lo, hi;                 // slider range; choice: index into the choice list
    const int *choices;         // choice values (ends with -1)
    void (*info)(char *buf, size_t n);  // info / action detail
    void (*action)(void);
} row_t;

static const int WKS[] = {5, 10, 15, 30, 60, -1};
static const int LANGS[] = {0, 1, -1};
static const int GOALS[] = {4000, 6000, 8000, 10000, 12000, 15000, -1};
static bool s_forget_armed;

static void info_version(char *b, size_t n) { snprintf(b, n, "%s · %s", esp_app_get_description()->version, ota_running_label()); }
static void info_memory(char *b, size_t n)
{
    snprintf(b, n, "RAM %u KB · PSRAM %u KB", (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) >> 10),
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) >> 10));
}
static void info_battery(char *b, size_t n)
{
    if (g_app.bat_pct < 0) snprintf(b, n, "%s", TR("sin batería", "no battery"));
    else snprintf(b, n, "%d %%%s", g_app.bat_pct, g_app.bat_chg ? TR(" · cargando", " · charging") : "");
}
static void info_uptime(char *b, size_t n)
{
    int64_t s = esp_timer_get_time() / 1000000;
    snprintf(b, n, "%lld h %lld min", s / 3600, s / 60 % 60);
}
static void info_bt(char *b, size_t n)
{
    snprintf(b, n, "%s · %s", ble_name(), ble_connected() ? TR("conectado", "connected") : TR("sin móvil", "no phone"));
}
static void info_wifi(char *b, size_t n)
{
    if (g_app.sta_ip[0]) snprintf(b, n, "%s · %s", g_app.sta_ssid, g_app.sta_ip);
    else snprintf(b, n, "%s", g_app.sta_ssid[0] ? TR("conectando…", "connecting…") : TR("sin red", "no network"));
}
static void info_forget(char *b, size_t n)
{
    snprintf(b, n, "%s", s_forget_armed ? TR("toca otra vez para confirmar", "tap again to confirm") : TR("borra los emparejados", "removes paired phones"));
}
static void act_forget(void)
{
    if (!s_forget_armed) {
        s_forget_armed = true;
        return;
    }
    s_forget_armed = false;
    ble_forget_all();
}

static const row_t ROWS[] = {
    {R_HEAD, "PANTALLA", "SCREEN"},
    {R_SLIDER, "Brillo", "Brightness", "bri", &g_app.brightness, 5, 100},
    {R_SWITCH, "Modo noche", "Night mode", "non", &g_app.night_on},
    {R_CHOICE, "Se apaga a los", "Turns off after", "wks", &g_app.wake_secs, 0, 0, WKS},
    {R_SWITCH, "Siempre encendida", "Always on", "aon", &g_app.always_on},
    {R_SWITCH, "Levantar la muñeca", "Raise to wake", "raise", &g_app.raise_wake},
    {R_HEAD, "SONIDO", "SOUND"},
    {R_SLIDER, "Volumen", "Volume", "vol", &g_app.volume, 0, 100},
    {R_SWITCH, "No molestar", "Do not disturb", "dnd", &g_app.dnd},
    {R_HEAD, "ESFERAS", "FACES"},
    {R_SWITCH, "Formato 24 h", "24-hour time", "h24", &g_app.h24},
    {R_SWITCH, "Segundos", "Seconds", "secs", &g_app.show_secs},
    {R_SWITCH, "Cambio automático", "Auto change", "auto", &g_app.auto_cycle},
    {R_HEAD, "GENERAL", "GENERAL"},
    {R_CHOICE, "Idioma", "Language", "lang", &g_app.lang, 0, 0, LANGS},
    {R_CHOICE, "Objetivo de pasos", "Steps goal", "goal", &g_app.step_goal, 0, 0, GOALS},
    {R_HEAD, "CONEXIONES", "CONNECTIONS"},
    {R_INFO, "Bluetooth", "Bluetooth", NULL, NULL, 0, 0, NULL, info_bt},
    {R_ACTION, "Olvidar móviles", "Forget phones", NULL, NULL, 0, 0, NULL, info_forget, act_forget},
    {R_INFO, "WiFi", "Wi-Fi", NULL, NULL, 0, 0, NULL, info_wifi},
    {R_HEAD, "SISTEMA", "SYSTEM"},
    {R_INFO, "Versión", "Version", NULL, NULL, 0, 0, NULL, info_version},
    {R_INFO, "Memoria libre", "Free memory", NULL, NULL, 0, 0, NULL, info_memory},
    {R_INFO, "Batería", "Battery", NULL, NULL, 0, 0, NULL, info_battery},
    {R_INFO, "Encendido", "Uptime", NULL, NULL, 0, 0, NULL, info_uptime},
};
#define NROWS ((int)(sizeof(ROWS) / sizeof(ROWS[0])))
#define HEAD_H 46

static ui_scroll_t s_scroll;
static int s_slider = -1, s_pressed = -1;
static bool s_axis_known, s_sideways;

static int row_y(int i)
{
    int y = 0;
    for (int k = 0; k < i; k++) y += ROWS[k].kind == R_HEAD ? HEAD_H : ROW;
    return y;
}

static int row_at(int y)
{
    int yy = y - TOP + s_scroll.offset;
    if (y < TOP || yy < 0) return -1;
    for (int k = 0, acc = 0; k < NROWS; k++) {
        int h = ROWS[k].kind == R_HEAD ? HEAD_H : ROW;
        if (yy < acc + h) return ROWS[k].kind == R_HEAD ? -1 : k;
        acc += h;
    }
    return -1;
}

static void open(void)
{
    s_scroll.offset = 0;
    s_forget_armed = false;
}

static void value_text(const row_t *r, char *b, size_t n)
{
    int v = *r->val;
    if (!strcmp(r->key, "wks")) snprintf(b, n, "%d s", v);
    else if (!strcmp(r->key, "lang")) snprintf(b, n, "%s", v ? "English" : "Español");
    else if (!strcmp(r->key, "goal")) snprintf(b, n, "%d.%03d", v / 1000, v % 1000);
    else snprintf(b, n, "%d %%", v);
}

static bool draw(uint32_t now, bool force)
{
    if (!force) return false;
    char buf[64];
    s_scroll.view_h = UI_H - TOP - 30;
    s_scroll.content_h = row_y(NROWS);
    ui_background(true);
    ui_veil(50, UI_H, 3);
    for (int i = 0; i < NROWS; i++) {
        const row_t *r = &ROWS[i];
        int y = TOP + row_y(i) - s_scroll.offset;
        if (y > UI_H || y + ROW < TOP - 30) continue;
        if (r->kind == R_HEAD) {
            ui_print(&ui_vt, 1, X0 + 6, y + 18, TR(r->es, r->en), MS_GRIS);
            continue;
        }
        bool p = i == s_pressed;
        ui_panel(X0, y, WID, ROW - 10, p ? MS_PANEL2 : MS_PANEL, MS_LINEA, 4, false);
        const char *label = TR(r->es, r->en);
        // Labels shrink to leave room for the value or the switch on the right
        int room = r->kind == R_SWITCH ? WID - 120 : r->kind == R_CHOICE ? WID - 140 : WID - 110;
        int ls = ui_fit(&ui_vt, 2, 1, room, label), ly = ls == 2 ? 12 : 22;
        switch (r->kind) {
        case R_SWITCH:
            ui_print(&ui_vt, ls, X0 + 16, y + ly, label, MS_CREMA);
            ui_switch(X0 + WID - 82, y + 20, *r->val);
            break;
        case R_SLIDER:
            ui_print(&ui_vt, 2, X0 + 16, y + 2, label, MS_CREMA);
            value_text(r, buf, sizeof(buf));
            ui_print_r(&ui_jersey, 2, X0 + WID - 16, y + 12, buf, MS_ORO2);
            ui_bar(X0 + 16, y + 48, WID - 32, 14, (*r->val - r->lo) * 1000 / (r->hi - r->lo), MS_ORO);
            break;
        case R_CHOICE:
            ui_print(&ui_vt, ls, X0 + 16, y + ly, label, MS_CREMA);
            value_text(r, buf, sizeof(buf));
            ui_print_r(&ui_jersey, 2, X0 + WID - 16, y + 22, buf, MS_ORO2);
            break;
        case R_INFO:
        case R_ACTION:
            ui_print(&ui_vt, 2, X0 + 16, y + 2, label, r->kind == R_ACTION ? (s_forget_armed ? MS_CORAL : MS_ORO2) : MS_CREMA);
            r->info(buf, sizeof(buf));
            ui_print(&ui_vt, 1, X0 + 16, y + 40, buf, MS_GRIS);
            break;
        default:
            break;
        }
    }
    ui_background_band(true, 0, TOP - 16);
    ui_shade(0, 0, UI_W, TOP - 16, 1);
    ui_status_bar();
    ui_print_c(&ui_jersey, 4, UI_W / 2, 64, TR("Ajustes", "Settings"), MS_ORO, true);
    return true;
}

static void slide_to(const row_t *r, int x)
{
    int v = r->lo + (x - X0 - 16) * (r->hi - r->lo) / (WID - 32);
    v = v < r->lo ? r->lo : v > r->hi ? r->hi : v;
    if (v != *r->val) settings_set(r->key, v);
}

static void event(const ui_event_t *e)
{
    switch (e->type) {
    case EV_DOWN:
        s_axis_known = s_sideways = false;
        s_pressed = row_at(e->y);
        s_slider = s_pressed >= 0 && ROWS[s_pressed].kind == R_SLIDER ? s_pressed : -1;
        ui_scroll_event(&s_scroll, e);
        break;
    case EV_MOVE:
        // The first movement decides: sideways on a slider row adjusts it, anything else scrolls
        if (!s_axis_known && (e->x - e->x0) * (e->x - e->x0) + (e->y - e->y0) * (e->y - e->y0) > 100) {
            s_axis_known = true;
            s_sideways = s_slider >= 0 && abs(e->x - e->x0) > abs(e->y - e->y0);
        }
        if (s_sideways) slide_to(&ROWS[s_slider], e->x);
        else if (s_axis_known) {
            s_pressed = -1;
            ui_scroll_event(&s_scroll, e);
        }
        break;
    case EV_TAP: {
        int i = row_at(e->y);
        if (i < 0) break;
        const row_t *r = &ROWS[i];
        if (r->kind == R_SWITCH) settings_set(r->key, !*r->val);
        else if (r->kind == R_SLIDER) slide_to(r, e->x);
        else if (r->kind == R_CHOICE) {
            int k = 0;
            while (r->choices[k] >= 0 && r->choices[k] != *r->val) k++;
            int next = r->choices[k] >= 0 && r->choices[k + 1] >= 0 ? r->choices[k + 1] : r->choices[0];
            settings_set(r->key, next);
        } else if (r->kind == R_ACTION) r->action();
        if (r->kind != R_ACTION) s_forget_armed = false;
        break;
    }
    case EV_UP:
        s_pressed = s_slider = -1;
        ui_scroll_event(&s_scroll, e);
        break;
    default:
        break;
    }
    sys_dirty();
}

const app_t app_settings = {"Ajustes", "Settings", icon_settings, open, draw, event, NULL, NULL};
