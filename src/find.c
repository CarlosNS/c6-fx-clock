// Find the watch (it rings and flashes) and find the phone (the watch makes it ring). See find.h.
#include "find.h"
#include <stdio.h>
#include <string.h>
#include "app.h"
#include "apps.h"
#include "audio.h"
#include "ble.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "sys.h"

#define FIND_MAX_MS (5 * 60 * 1000)  // nobody found it: stop, for the battery's sake
#define FLASH_MS 500                 // the screen alternates bright and dark

static const char *TAG = "find";
static volatile bool s_finding, s_phone;
static uint32_t s_t0;
static const app_t app_find;

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

bool find_active(void) { return s_finding; }
bool find_phone_active(void) { return s_phone && ble_connected(); }

static void find_start(void)
{
    if (s_finding) return;
    ESP_LOGI(TAG, "the phone is looking for the watch");
    s_t0 = now_ms();
    s_finding = true;
    g_app.bri_override = 100;  // over the night mode and the brightness setting
    audio_find_start();        // full volume, even in do not disturb
    app_wake();
    sys_alert(&app_find);
}

void find_stop(bool tell)
{
    if (!s_finding) return;
    ESP_LOGI(TAG, "stopped%s", tell ? " on the watch" : "");
    s_finding = false;
    audio_stop();
    g_app.bri_override = -1;
    if (tell) ble_send_json("{\"t\":\"find\",\"st\":\"off\"}");
    sys_alert_done();
}

void find_phone_toggle(void)
{
    if (!s_phone && !ble_connected()) return;  // the tile says "no phone" then
    s_phone = !s_phone;
    ble_send_json(s_phone ? "{\"t\":\"findphone\",\"st\":\"on\"}" : "{\"t\":\"findphone\",\"st\":\"off\"}");
    sys_dirty();
}

bool find_command(const char *cmd)
{
    if (strcmp(cmd, "find 1") == 0) find_start();
    else if (strcmp(cmd, "find 0") == 0) find_stop(false);
    else if (strcmp(cmd, "findphone 0") == 0) s_phone = false, sys_dirty();  // stopped on the phone
    else return false;
    return true;
}

// ---------------------------------------------------------------- the screen

static int s_phase = -1;

static void f_open(void) { s_phase = -1; }

static bool f_draw(uint32_t now, bool force)
{
    // The loop's "now" can be a moment older than s_t0 (set from another task): signed
    int32_t age = (int32_t)(now - s_t0);
    if (age < 0) age = 0;
    if (s_finding && age > FIND_MAX_MS) {
        find_stop(true);
        return false;
    }
    int phase = (age / FLASH_MS) & 1;
    if (!force && phase == s_phase) return false;
    s_phase = phase;
    // Bright phase: a cream screen that lights the room; dark phase: the night scene. Both say where
    // to tap, so it stops at the first touch whatever the phase
    if (phase == 0) {
        ui_fill(MS_CREMA);
        ui_print_c(&ui_jersey, 5, UI_W / 2, 150, TR("¡Estoy", "I'm"), MS_NEGRO, false);
        ui_print_c(&ui_jersey, 5, UI_W / 2, 220, TR("aquí!", "here!"), MS_NEGRO, false);
        ui_print_c(&ui_vt, 2, UI_W / 2, 330, TR("toca para parar", "tap to stop"), MS_NEGRO, false);
    } else {
        ui_background(false);
        ui_veil(40, UI_H, 2);
        ui_print_c(&ui_jersey, 5, UI_W / 2, 150, TR("¡Estoy", "I'm"), MS_ORO, true);
        ui_print_c(&ui_jersey, 5, UI_W / 2, 220, TR("aquí!", "here!"), MS_ORO, true);
        ui_print_c(&ui_vt, 2, UI_W / 2, 330, TR("toca para parar", "tap to stop"), MS_CREMA, false);
    }
    return true;
}

static void f_event(const ui_event_t *e)
{
    if (e->type == EV_TAP || e->type == EV_LONG) find_stop(true);
}

static void f_close(void)
{
    // Closed by BOOT or a swipe: that also stops it
    if (s_finding) {
        s_finding = false;
        audio_stop();
        g_app.bri_override = -1;
        ble_send_json("{\"t\":\"find\",\"st\":\"off\"}");
    }
}

static bool f_keep_on(void) { return s_finding; }

static const app_t app_find = {"Buscar", "Find", NULL, f_open, f_draw, f_event, f_close, f_keep_on};
