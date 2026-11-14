// Settings: a menu of sections (Display, Sound, Time and faces, General, Connections, System, the phone
// app), each a scrolling list of rows, on the dusk background (the style keeps dusk for configuration).
// Rows are switches (tap), sliders (drag sideways), choices (tap cycles), information and, in "the
// phone app", a QR code with the link to the latest Android app (it is not in the Play Store: a
// phone that lost it finds it again here). Back goes from a section to the menu. Every change goes
// through settings_set(), like the phone app and the web page, so it is saved and they all see it.
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
#include "qrcode.h"
#include "settings.h"
#include "sys.h"
#include "ui_icons.h"

#define TOP 120
#define ROW 82
#define X0 40
#define WID 330

typedef enum { R_HEAD, R_SWITCH, R_SLIDER, R_CHOICE, R_INFO, R_ACTION, R_QR } row_kind_t;

typedef struct {
    row_kind_t kind;
    const char *es, *en;
    const char *key;            // setting for switch / slider / choice
    volatile int *val;
    int lo, hi;                 // slider range; choice: index into the choice list
    const int *choices;         // choice values (ends with -1)
    void (*info)(char *buf, size_t n);  // info / action detail
    void (*action)(void);
    const char *des, *den;      // a section (R_HEAD): what it holds, for the menu
} row_t;

#define APP_URL "https://carlosns.github.io/c6-fx-clock/c6-clock-app.apk"  // tools/release.py: always the latest

static const int WKS[] = {5, 10, 15, 30, 60, -1};
static const int LANGS[] = {0, 1, -1};
static const int TAPS[] = {1, 2, 0, -1};
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
    else snprintf(b, n, "%d %%%s", g_app.bat_pct, g_app.bat_full ? TR(" · cargada", " · full") : g_app.bat_chg ? TR(" · cargando", " · charging") : "");
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
    if (!g_app.wifi_on) snprintf(b, n, "%s", TR("apagado", "off"));
    else if (g_app.sta_ip[0]) snprintf(b, n, "%s · %s", g_app.sta_ssid, g_app.sta_ip);
    else snprintf(b, n, "%s", g_app.sta_ssid[0] ? TR("conectando…", "connecting…") : TR("sin red", "no network"));
}
static void info_forget(char *b, size_t n)
{
    snprintf(b, n, "%s", s_forget_armed ? TR("toca otra vez para confirmar", "tap again to confirm") : TR("borra los emparejados", "removes paired phones"));
}
static bool s_reset_dlg;
static void info_reset(char *b, size_t n) { snprintf(b, n, "%s", TR("vuelve a los valores de fábrica", "back to the factory values")); }
static void act_reset(void) { s_reset_dlg = true; }
static void act_forget(void)
{
    if (!s_forget_armed) {
        s_forget_armed = true;
        return;
    }
    s_forget_armed = false;
    ble_forget_all();
}

#define S(es, en, des, den) {R_HEAD, es, en, NULL, NULL, 0, 0, NULL, NULL, NULL, des, den}
static const row_t ROWS[] = {
    S("Pantalla", "Display", "Brillo, noche, apagado", "Brightness, night, sleep"),
    {R_SLIDER, "Brillo", "Brightness", "bri", &g_app.brightness, 5, 100},
    {R_SWITCH, "Modo noche", "Night mode", "non", &g_app.night_on},
    {R_CHOICE, "Se apaga a los", "Turns off after", "wks", &g_app.wake_secs, 0, 0, WKS},
    {R_SWITCH, "Siempre encendida", "Always on", "aon", &g_app.always_on},
    {R_SWITCH, "Levantar la muñeca", "Raise to wake", "raise", &g_app.raise_wake},
    {R_CHOICE, "Tocar para encender", "Tap to wake", "tap", &g_app.tap_wake, 0, 0, TAPS},
    S("Sonido", "Sound", "Volumen, no molestar", "Volume, do not disturb"),
    {R_SLIDER, "Volumen", "Volume", "vol", &g_app.volume, 0, 100},
    {R_SWITCH, "No molestar", "Do not disturb", "dnd", &g_app.dnd},
    S("Hora y esferas", "Time and faces", "24 h, segundos, cambio", "24 h, seconds, cycle"),
    {R_SWITCH, "Formato 24 h", "24-hour time", "h24", &g_app.h24},
    {R_SWITCH, "Segundos", "Seconds", "secs", &g_app.show_secs},
    {R_SWITCH, "Cambio automático", "Auto change", "auto", &g_app.auto_cycle},
    S("General", "General", "Idioma, pasos, voz", "Language, steps, voice"),
    {R_CHOICE, "Idioma", "Language", "lang", &g_app.lang, 0, 0, LANGS},
    {R_CHOICE, "Objetivo de pasos", "Steps goal", "goal", &g_app.step_goal, 0, 0, GOALS},
    {R_SWITCH, "Voz (mantén BOOT)", "Voice (hold BOOT)", "voice", &g_app.voice},
    S("Conexiones", "Connections", "Bluetooth, móviles, WiFi", "Bluetooth, phones, Wi-Fi"),
    {R_INFO, "Bluetooth", "Bluetooth", NULL, NULL, 0, 0, NULL, info_bt},
    {R_ACTION, "Olvidar móviles", "Forget phones", NULL, NULL, 0, 0, NULL, info_forget, act_forget},
    {R_INFO, "WiFi", "Wi-Fi", NULL, NULL, 0, 0, NULL, info_wifi},
    S("Sistema", "System", "Versión, batería, reset", "Version, battery, reset"),
    {R_INFO, "Versión", "Version", NULL, NULL, 0, 0, NULL, info_version},
    {R_INFO, "Memoria libre", "Free memory", NULL, NULL, 0, 0, NULL, info_memory},
    {R_INFO, "Batería", "Battery", NULL, NULL, 0, 0, NULL, info_battery},
    {R_INFO, "Encendido", "Uptime", NULL, NULL, 0, 0, NULL, info_uptime},
    {R_ACTION, "Restablecer ajustes", "Reset settings", NULL, NULL, 0, 0, NULL, info_reset, act_reset},
    S("App del móvil", "Phone app", "QR para descargarla", "QR to download it"),
    {R_QR, "App Marea para Android", "Marea app for Android"},
};
#define NROWS ((int)(sizeof(ROWS) / sizeof(ROWS[0])))
#define HEAD_H 46

#define QR_H 340  // fits the view: the page does not scroll
static ui_scroll_t s_scroll;
static int s_slider = -1, s_pressed = -1;
static bool s_axis_known, s_sideways;
static int s_sec = -1;  // the section open (index of its R_HEAD row), -1 = the menu of sections

// The rows on screen: the menu lists the sections; a section, its own rows (up to the next one)
static int first_row(void) { return s_sec < 0 ? 0 : s_sec + 1; }
static int end_row(void)
{
    if (s_sec < 0) return NROWS;
    int k = s_sec + 1;
    while (k < NROWS && ROWS[k].kind != R_HEAD) k++;
    return k;
}
static bool shown(int k) { return s_sec < 0 ? ROWS[k].kind == R_HEAD : true; }
static int row_h(int k) { return s_sec < 0 ? ROW : ROWS[k].kind == R_QR ? QR_H : ROW; }

static int row_y(int i)
{
    int y = 0;
    for (int k = first_row(); k < i; k++)
        if (shown(k)) y += row_h(k);
    return y;
}

static int row_at(int y)
{
    int yy = y - TOP + s_scroll.offset;
    if (y < TOP || yy < 0) return -1;
    for (int k = first_row(), acc = 0; k < end_row(); k++) {
        if (!shown(k)) continue;
        if (yy < acc + row_h(k)) return k;
        acc += row_h(k);
    }
    return -1;
}

static void open(void)
{
    s_scroll.offset = 0;
    s_forget_armed = false;
    s_sec = -1;
}

// The QR code of the app's link, made once (qrcodegen, through espressif/qrcode)
static uint8_t s_qr[45 * 45];
static int s_qr_n;
static void qr_got(esp_qrcode_handle_t q)
{
    int n = esp_qrcode_get_size(q);
    if (n > 45) return;
    for (int y = 0; y < n; y++)
        for (int x = 0; x < n; x++) s_qr[y * n + x] = esp_qrcode_get_module(q, x, y);
    s_qr_n = n;
}
static void draw_qr(int y)
{
    if (!s_qr_n) {
        esp_qrcode_config_t cfg = ESP_QRCODE_CONFIG_DEFAULT();
        cfg.display_func = qr_got;
        cfg.max_qrcode_version = 6;
        cfg.qrcode_ecc_level = ESP_QRCODE_ECC_MED;
        esp_qrcode_generate(&cfg, APP_URL);
    }
    if (!s_qr_n) return;
    int m = 216 / (s_qr_n + 4), side = m * (s_qr_n + 4), x0 = (UI_W - side) / 2;  // a quiet zone of 2 modules
    ui_rect(x0, y, side, side, 0xFFFF);
    for (int r = 0; r < s_qr_n; r++)
        for (int c = 0; c < s_qr_n; c++)
            if (s_qr[r * s_qr_n + c]) ui_rect(x0 + (c + 2) * m, y + (r + 2) * m, m, m, 0x0000);
}

static void value_text(const row_t *r, char *b, size_t n)
{
    int v = *r->val;
    if (!strcmp(r->key, "wks")) snprintf(b, n, "%d s", v);
    else if (!strcmp(r->key, "lang")) snprintf(b, n, "%s", v ? "English" : "Español");
    else if (!strcmp(r->key, "tap")) snprintf(b, n, "%s", v == 2 ? TR("doble", "double") : v ? TR("un toque", "one tap") : "no");
    else if (!strcmp(r->key, "goal")) snprintf(b, n, "%d.%03d", v / 1000, v % 1000);
    else snprintf(b, n, "%d %%", v);
}

static bool draw(uint32_t now, bool force)
{
    if (!force) return false;
    char buf[64];
    s_scroll.view_h = UI_H - TOP - 30;
    s_scroll.content_h = row_y(end_row());
    ui_background(true);
    ui_veil(50, UI_H, 3);
    for (int i = first_row(); i < end_row(); i++) {
        if (!shown(i)) continue;
        const row_t *r = &ROWS[i];
        int y = TOP + row_y(i) - s_scroll.offset;
        if (y > UI_H || y + row_h(i) < TOP - 30) continue;
        bool p = i == s_pressed;
        if (r->kind == R_HEAD) {  // the menu: a section, what it holds, and the way in
            ui_panel(X0, y, WID, ROW - 10, p ? MS_PANEL2 : MS_PANEL, MS_LINEA, 4, false);
            ui_print(&ui_jersey, 2, X0 + 16, y + 2, TR(r->es, r->en), MS_CREMA);
            ui_print(&ui_vt, 1, X0 + 16, y + 42, TR(r->des, r->den), MS_GRIS);
            ui_print_r(&ui_jersey, 3, X0 + WID - 16, y + 6, ">", MS_ORO2);
            continue;
        }
        if (r->kind == R_QR) {  // the link to the app, to scan with the phone
            draw_qr(y + 10);
            ui_print_c(&ui_vt, 1, UI_W / 2, y + 238, TR("Escanéalo con el móvil para descargar", "Scan it with the phone to download"), MS_CREMA, false);
            ui_print_c(&ui_vt, 1, UI_W / 2, y + 260, TR("la última versión de la app Marea", "the latest Marea app"), MS_CREMA, false);
            ui_print_c(&ui_vt, 1, UI_W / 2, y + 288, TR("(no está en Play Store: Android pedirá", "(not in the Play Store: Android will ask"), MS_GRIS, false);
            ui_print_c(&ui_vt, 1, UI_W / 2, y + 310, TR("permiso para instalarla)", "to allow installing it)"), MS_GRIS, false);
            continue;
        }
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
    const char *title = s_sec < 0 ? TR("Ajustes", "Settings") : TR(ROWS[s_sec].es, ROWS[s_sec].en);
    ui_print_c(&ui_jersey, ui_fit(&ui_jersey, 4, 3, UI_W - 60, title), UI_W / 2, 64, title, MS_ORO, true);
    if (s_reset_dlg)
        ui_dialog(TR("¿Restablecer?", "Reset?"),
                  TR("Los ajustes vuelven a los de fábrica. Se quedan el idioma, los móviles, la red WiFi, las alarmas y los pasos.",
                     "Settings go back to the factory values. The language, phones, Wi-Fi network, alarms and steps stay."),
                  TR("Cancelar", "Cancel"), TR("Restablecer", "Reset"), UI_BTN_DANGER);
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
    if (s_reset_dlg) {  // the dialog takes every touch until it is answered
        if (e->type == EV_TAP) {
            int hit = ui_dialog_hit(e->x, e->y, true);
            if (hit == 2) settings_reset();
            if (hit) s_reset_dlg = false;
            sys_dirty();
        }
        return;
    }
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
        if (r->kind == R_HEAD) {  // into the section
            s_sec = i;
            s_scroll.offset = 0;
            s_pressed = -1;
            break;
        }
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

static bool back(void)
{
    if (s_reset_dlg) return s_reset_dlg = false, true;
    if (s_sec >= 0) {  // from a section back to the menu
        s_sec = -1;
        s_scroll.offset = 0;
        s_forget_armed = false;
        return true;
    }
    return false;
}

const app_t app_settings = {"Ajustes", "Settings", icon_settings, open, draw, event, NULL, NULL, back};
