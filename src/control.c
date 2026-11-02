#include "control.h"
#include <stdio.h>
#include <string.h>
#include "app.h"
#include "ble.h"
#include "bootmode.h"
#include "clock.h"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "fx.h"
#include "net.h"
#include "ota.h"
#include "settings.h"
#include "weather.h"

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static int s_drag_dx, s_drag_dy;

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

void app_command(const char *cmd)
{
    int a, b, c, d, e, f, g;
    long long epoch;
    // Using the web page keeps the screen on (but not its automatic time / weather pushes)
    if (strncmp(cmd, "time ", 5) != 0 && strncmp(cmd, "wx ", 3) != 0) app_wake();

    if (sscanf(cmd, "drag %d %d %d %d", &a, &b, &c, &d) == 4) {
        taskENTER_CRITICAL(&s_mux);
        g_app.tx = clampi(a, 0, 999);
        g_app.ty = clampi(b, 0, 999);
        s_drag_dx += c;
        s_drag_dy += d;
        g_app.touch = true;
        taskEXIT_CRITICAL(&s_mux);
        return;
    }
    if (strcmp(cmd, "up") == 0) {
        g_app.touch = false;
        return;
    }
    if (strncmp(cmd, "wifi\n", 5) == 0) {  // "wifi\n<ssid>\n<password>"
        char buf[100];
        strlcpy(buf, cmd + 5, sizeof(buf));
        char *pass = strchr(buf, '\n');
        if (pass) {
            *pass++ = 0;
            if (buf[0]) net_set_sta(buf, pass);
        }
        return;
    }
    if (sscanf(cmd, "dbgtime %d %d", &a, &b) == 2) {
        g_app.dbg_m = clampi(b, 0, 59);
        g_app.dbg_h = clampi(a, -1, 23);
        return;
    }
    if (sscanf(cmd, "time %lld %d", &epoch, &a) == 2) {
        clock_set_epoch(epoch, clampi(a, 0, 999));
        return;
    }
    float lat, lon;
    char name[32];
    if (sscanf(cmd, "loc %f %f %31[^\n]", &lat, &lon, name) == 3) {
        weather_set_location(lat, lon, name);
        return;
    }
    if (sscanf(cmd, "wx %d %d %d %d %d %d %d", &a, &b, &c, &d, &e, &f, &g) == 7) {
        weather_set_from_web(a, b, clampi(c, 0, 100), clampi(d, 0, 400), e, f, g != 0);
        return;
    }
    if (sscanf(cmd, "night %d %d %d %d", &a, &b, &c, &d) == 4) {
        settings_set("non", a != 0);
        settings_set("nst", b);
        settings_set("nen", c);
        settings_set("nbr", d);
        return;
    }
    if (strcmp(cmd, "unpair") == 0) {  // forget every paired phone
        ble_forget_all();
        return;
    }
    char key[16], word[16];
    if (sscanf(cmd, "mode %15s", word) == 1) {  // reboots into another mode
        int m = boot_mode_from_name(word);
        if (m >= 0) boot_mode_switch((boot_mode_t)m);
        return;
    }
    if (sscanf(cmd, "%15s %d", key, &a) == 2) settings_set(key, a);
}

void app_take_drag(int *dx, int *dy)
{
    taskENTER_CRITICAL(&s_mux);
    *dx = s_drag_dx;
    *dy = s_drag_dy;
    s_drag_dx = s_drag_dy = 0;
    taskEXIT_CRITICAL(&s_mux);
}

void json_escape(char *out, size_t size, const char *s)
{
    size_t n = 0;
    for (; *s && n + 2 < size; s++) {
        unsigned char ch = (unsigned char)*s;
        if (ch < 32) continue;
        if (ch == '"' || ch == '\\') out[n++] = '\\';
        out[n++] = ch;
    }
    out[n] = 0;
}

size_t app_status_json(char *buf, size_t size)
{
    char ssid[70], loc[70];
    json_escape(ssid, sizeof(ssid), g_app.sta_ssid);
    json_escape(loc, sizeof(loc), g_wx_loc.name);
    size_t len = 1;
    buf[0] = '{';
    len = settings_json(buf, size, len);
    int n = snprintf(buf + len, size - len,
                     ",\"fps\":%d,\"rus\":%d,\"rmax\":%d,\"heap\":%u,\"ver\":\"%s\","
                     "\"ssid\":\"%s\",\"sip\":\"%s\",\"tv\":%d,\"time\":\"%02d:%02d:%02d\","
                     "\"wloc\":\"%s\",\"wlat\":%.4f,\"wlon\":%.4f,\"wv\":%d,\"wt\":%d,\"wc\":%d,\"wage\":%d,\"wweb\":%d,"
                     "\"ota\":%d,\"otamax\":%u,\"part\":\"%s\",\"mode\":\"%s\"}",
                     g_app.fps, g_app.render_us, g_app.render_max_us, (unsigned)heap_caps_get_free_size(MALLOC_CAP_DEFAULT),
                     esp_app_get_description()->version, ssid, g_app.sta_ip, g_now.valid, g_now.h, g_now.m, g_now.s, loc,
                     g_wx_loc.lat, g_wx_loc.lon, g_wx.valid, g_wx.temp10, g_wx.code, weather_age_s(), g_wx.from_web,
                     ota_progress(), (unsigned)ota_max_size(), ota_running_label(), boot_mode_name(boot_mode()));
    if (n > 0) len += n;
    return len < size ? len : size - 1;
}

void app_faces_json(void (*emit)(const char *piece, void *ctx), void *ctx)
{
    char buf[800], a[96], b[400], hint[96];
    emit("{\"groups\":[", ctx);
    for (int g = 0; g < FX_G_COUNT; g++) {
        json_escape(a, sizeof(a), fx_group_names[g]);
        snprintf(buf, sizeof(buf), "%s\"%s\"", g ? "," : "", a);
        emit(buf, ctx);
    }
    emit("],\"faces\":[", ctx);
    for (int i = 0; i < FX_COUNT; i++) {
        const fx_t *f = &fx_list[i];
        json_escape(a, sizeof(a), f->label);
        json_escape(b, sizeof(b), f->desc);
        json_escape(hint, sizeof(hint), f->hint ? f->hint : "");
        snprintf(buf, sizeof(buf), "%s{\"label\":\"%s\",\"o\":\"%s\",\"g\":%d,\"hint\":\"%s\",\"desc\":\"%s\",\"opts\":\"%s\"}",
                 i ? "," : "", a, f->landscape ? "h" : "v", f->group, hint, b, f->opts ? f->opts : "");
        emit(buf, ctx);
    }
    emit("]}", ctx);
}
