#include "settings.h"
#include "board.h"
#include <stdio.h>
#include <string.h>
#include "app.h"
#include "esp_log.h"
#include "fx.h"
#include "net.h"
#include "nvs.h"

static const char *TAG = "settings";

typedef struct {
    const char *key;  // NVS key (<= 15 chars), command name and JSON field
    volatile int *val;
    int min, max;
    bool persist;
    void (*changed)(void);  // optional side effect after a change
} setting_t;

static void on_fx(void) { g_app.auto_cycle = 0; }  // choosing a face stops the auto rotation
static void on_wake(void) { app_wake(); }          // show the new timeout straight away
// Animation speed per face: "spd" sets the face on screen, and showing a face brings its own back
// (settings_face_speed). Stored as one NVS blob; a watch without it starts every face at "spd".
static uint16_t s_fx_speed[FX_COUNT];
static void on_spd(void) { s_fx_speed[g_app.fx] = (uint16_t)g_app.speed; }

void settings_face_speed(int fx)
{
    if (fx >= 0 && fx < FX_COUNT && s_fx_speed[fx]) g_app.speed = s_fx_speed[fx];
}

static void on_wifi(void) { net_enable(g_app.wifi_on); }

static const setting_t SETTINGS[] = {
    {"fx", &g_app.fx, 0, FX_COUNT - 1, true, on_fx},
    {"auto", &g_app.auto_cycle, 0, 1, true, NULL},
    {"fxoff", &g_app.fx_off, 0, 0x7FFFFFFF, true, NULL},  // faces left out of the auto rotation (bitmask)
    {"spd", &g_app.speed, 10, 300, true, on_spd},  // the face on screen (each face keeps its own)
    {"bri", &g_app.brightness, 5, 100, true, NULL},
    {"h24", &g_app.h24, 0, 1, true, NULL},
    {"secs", &g_app.show_secs, 0, 1, true, NULL},
    {"non", &g_app.night_on, 0, 1, true, NULL},
    {"nst", &g_app.night_start, 0, 23, true, NULL},
    {"nen", &g_app.night_end, 0, 23, true, NULL},
    {"nbr", &g_app.night_bri, 1, 100, true, NULL},
    {"aon", &g_app.always_on, 0, 1, true, NULL},
    {"wks", &g_app.wake_secs, 3, 600, true, on_wake},
    {"chue", &g_app.classic_hue, 0, 359, true, NULL},
    {"ohue", &g_app.onix_hue, 0, 359, true, NULL},
    {"raise", &g_app.raise_wake, 0, 1, true, NULL},  // raise the wrist to turn the screen on
    {"lang", &g_app.lang, 0, 1, true, NULL},          // 0 Spanish, 1 English
    {"dnd", &g_app.dnd, 0, 1, true, NULL},            // do not disturb
    {"wifi", &g_app.wifi_on, 0, 1, true, on_wifi},    // the watch's own Wi-Fi radio
    {"vol", &g_app.volume, 0, 100, true, NULL},       // speaker volume
    {"goal", &g_app.step_goal, 1000, 50000, true, NULL},  // daily steps goal
    {"ovl", &g_app.overlay, 0, 1, false, NULL},  // debug only
};
#define NSET (sizeof(SETTINGS) / sizeof(SETTINGS[0]))

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

void settings_load(void)
{
    nvs_handle_t h;
    if (nvs_open("clock", NVS_READONLY, &h) != ESP_OK) return;
    for (size_t i = 0; i < NSET; i++) {
        int32_t v;
        if (SETTINGS[i].persist && nvs_get_i32(h, SETTINGS[i].key, &v) == ESP_OK)
            *SETTINGS[i].val = clampi(v, SETTINGS[i].min, SETTINGS[i].max);
    }
    size_t n = sizeof(s_fx_speed);
    if (nvs_get_blob(h, "fxspd", s_fx_speed, &n) != ESP_OK) n = 0;
    nvs_close(h);
    for (int i = n / sizeof(s_fx_speed[0]); i < FX_COUNT; i++) s_fx_speed[i] = (uint16_t)g_app.speed;  // new faces too
    for (int i = 0; i < FX_COUNT; i++) s_fx_speed[i] = (uint16_t)clampi(s_fx_speed[i], 10, 300);
    settings_face_speed(g_app.fx);
}

void settings_save(void)
{
    nvs_handle_t h;
    if (nvs_open("clock", NVS_READWRITE, &h) != ESP_OK) return;
    for (size_t i = 0; i < NSET; i++)
        if (SETTINGS[i].persist) nvs_set_i32(h, SETTINGS[i].key, *SETTINGS[i].val);
    nvs_set_blob(h, "fxspd", s_fx_speed, sizeof(s_fx_speed));
    nvs_commit(h);
    nvs_close(h);
    ESP_LOGI(TAG, "Saved");
}

bool settings_set(const char *key, int value)
{
    for (size_t i = 0; i < NSET; i++) {
        const setting_t *s = &SETTINGS[i];
        if (strcmp(key, s->key) != 0) continue;
        *s->val = clampi(value, s->min, s->max);
        if (s->changed) s->changed();
        if (s->persist) g_app.dirty = true;
        return true;
    }
    return false;
}

size_t settings_json(char *buf, size_t size, size_t len)
{
    for (size_t i = 0; i < NSET && len < size; i++) {
        int n = snprintf(buf + len, size - len, "%s\"%s\":%d", len && buf[len - 1] != '{' ? "," : "", SETTINGS[i].key,
                         *SETTINGS[i].val);
        if (n > 0) len += n;
    }
    return len < size ? len : size - 1;
}
