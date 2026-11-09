#include "phone.h"
#include <stdio.h>
#include <string.h>
#include "app.h"
#include "apps.h"
#include "audio.h"
#include "ble.h"
#include "esp_log.h"
#include "sys.h"

static const char *TAG = "phone";
phone_music_t g_music;
phone_call_t g_call;

static void copy_field(char *dst, size_t n, const char *src, const char *end)
{
    size_t len = end ? (size_t)(end - src) : strlen(src);
    if (len >= n) len = n - 1;
    memcpy(dst, src, len);
    dst[len] = 0;
}

bool phone_command(const char *cmd)
{
    if (strncmp(cmd, "music ", 6) == 0) {
        const char *p = cmd + 6;
        if (strcmp(p, "off") == 0) {
            g_music.valid = false;
        } else {
            int playing = 0, vol = 0;
            if (sscanf(p, "%d %d", &playing, &vol) < 1) return true;
            const char *t = strchr(p, '|');
            const char *a = t ? strchr(t + 1, '|') : NULL;
            g_music.valid = true;
            g_music.playing = playing;
            g_music.volume = vol;
            if (t) copy_field(g_music.title, sizeof(g_music.title), t + 1, a);
            if (a) copy_field(g_music.artist, sizeof(g_music.artist), a + 1, NULL);
        }
        g_music.version++;
        sys_dirty();
        return true;
    }
    if (strncmp(cmd, "call ", 5) == 0) {
        const char *p = cmd + 5;
        if (strncmp(p, "ring", 4) == 0) {
            const char *who = strchr(p, '|');
            copy_field(g_call.who, sizeof(g_call.who), who ? who + 1 : "", NULL);
            g_call.ringing = true;
            ESP_LOGI(TAG, "incoming call");
            if (!g_app.dnd) audio_ring_start();
            sys_alert(&app_call);
        } else if (g_call.ringing) {
            ESP_LOGI(TAG, "call over (%s)", p);
            g_call.ringing = false;
            audio_stop();
            sys_alert_done();
        }
        return true;
    }
    return false;
}

bool phone_send(const char *type, const char *cmd)
{
    char buf[64];
    snprintf(buf, sizeof(buf), "{\"t\":\"%s\",\"cmd\":\"%s\"}", type, cmd);
    return ble_send_json(buf);
}
