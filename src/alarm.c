#include "alarm.h"
#include <stdio.h>
#include <string.h>
#include "app.h"
#include "apps.h"
#include "audio.h"
#include "clock.h"
#include "esp_log.h"
#include "nvs.h"
#include "sys.h"

static const char *TAG = "alarm";
static void ring(int i);
alarm_t g_alarms[ALARMS_MAX];
int g_nalarms;
static bool s_ringing;
static int s_cur = -1;
static int s_snooze_at = -1;  // minute of the day the snoozed alarm rings again

// Stored as "list2" (with label and animation). Watches from before kept 4-byte alarms under "list":
// read once and carried over, with no label and the sunrise.
void alarms_init(void)
{
    nvs_handle_t h;
    size_t n = sizeof(g_alarms);
    g_nalarms = 0;
    memset(g_alarms, 0, sizeof(g_alarms));
    if (nvs_open("alarms", NVS_READONLY, &h) != ESP_OK) return;
    if (nvs_get_blob(h, "list2", g_alarms, &n) == ESP_OK) {
        g_nalarms = (int)(n / sizeof(alarm_t));
    } else {
        uint8_t old[ALARMS_MAX][4];
        n = sizeof(old);
        if (nvs_get_blob(h, "list", old, &n) == ESP_OK) {
            g_nalarms = (int)(n / 4);
            for (int i = 0; i < g_nalarms && i < ALARMS_MAX; i++)
                g_alarms[i] = (alarm_t){.h = old[i][0], .m = old[i][1], .days = old[i][2], .on = old[i][3]};
        }
    }
    nvs_close(h);
    if (g_nalarms > ALARMS_MAX) g_nalarms = ALARMS_MAX;
    for (int i = 0; i < g_nalarms; i++) {
        if (g_alarms[i].anim >= ALARM_ANIMS) g_alarms[i].anim = 0;
        g_alarms[i].label[ALARM_LABEL_MAX - 1] = 0;
    }
}

void alarms_save(void)
{
    nvs_handle_t h;
    if (nvs_open("alarms", NVS_READWRITE, &h) != ESP_OK) return;
    if (g_nalarms) nvs_set_blob(h, "list2", g_alarms, g_nalarms * sizeof(alarm_t));
    else nvs_erase_key(h, "list2");
    nvs_erase_key(h, "list");  // the old format, once carried over
    nvs_commit(h);
    nvs_close(h);
}

const char *alarm_anim_name(int anim)
{
    switch (anim) {
    case ALARM_ANIM_BOMB: return TR("Bomba", "Bomb");
    default: return TR("Amanecer", "Sunrise");
    }
}

size_t alarms_json(char *buf, size_t size)
{
    size_t n = snprintf(buf, size, "{\"list\":[");
    for (int i = 0; i < g_nalarms && n < size; i++) {
        const alarm_t *a = &g_alarms[i];
        n += snprintf(buf + n, size - n, "%s{\"h\":%d,\"m\":%d,\"d\":%d,\"on\":%d,\"a\":%d,\"l\":\"", i ? "," : "", a->h, a->m,
                      a->days, a->on, a->anim);
        for (const char *p = a->label; *p && n + 3 < size; p++) {  // JSON string: escape quotes and backslashes
            if (*p == '"' || *p == '\\') buf[n++] = '\\';
            buf[n++] = *p;
        }
        if (n < size) n += snprintf(buf + n, size - n, "\"}");
    }
    if (n < size) n += snprintf(buf + n, size - n, "],\"anims\":[");
    for (int k = 0; k < ALARM_ANIMS && n < size; k++) n += snprintf(buf + n, size - n, "%s\"%s\"", k ? "," : "", alarm_anim_name(k));
    if (n < size) n += snprintf(buf + n, size - n, "]}");
    return n < size ? n : size - 1;
}

bool alarm_command(const char *cmd)
{
    int i, v, h, m, off = 0;
    // Created, deleted and retimed from the phone too
    if (sscanf(cmd, "alarm add %d %d %d", &h, &m, &v) == 3) {
        if (g_nalarms < ALARMS_MAX && h >= 0 && h < 24 && m >= 0 && m < 60) {
            g_alarms[g_nalarms++] = (alarm_t){.h = h, .m = m, .days = v & 0x7F, .on = 1};
            alarms_save();
            sys_dirty();
        }
        return true;
    }
    if (sscanf(cmd, "alarm %d del%n", &i, &off) == 1 && off > 0) {
        if (i >= 0 && i < g_nalarms && !s_ringing) {  // not while one rings: its screen points at it
            for (int k = i + 1; k < g_nalarms; k++) g_alarms[k - 1] = g_alarms[k];
            g_nalarms--;
            if (s_snooze_at >= 0 && s_cur == i) s_snooze_at = -1;
            else if (s_cur > i) s_cur--;
            alarms_save();
            sys_dirty();
        }
        return true;
    }
    if (sscanf(cmd, "alarm %d time %d %d", &i, &h, &m) == 3) {
        if (i >= 0 && i < g_nalarms && h >= 0 && h < 24 && m >= 0 && m < 60)
            g_alarms[i].h = h, g_alarms[i].m = m, alarms_save(), sys_dirty();
        return true;
    }
    if (sscanf(cmd, "alarm %d days %d", &i, &v) == 2) {
        if (i >= 0 && i < g_nalarms) g_alarms[i].days = v & 0x7F, alarms_save(), sys_dirty();
        return true;
    }
    if (sscanf(cmd, "alarm %d test%n", &i, &off) == 1 && off > 0) {  // ring it now (trying its animation)
        if (i >= 0 && i < g_nalarms && !s_ringing) ring(i);
        return true;
    }
    if (sscanf(cmd, "alarm %d on %d", &i, &v) == 2) {  // switch it on or off (the app's alarm list)
        if (i >= 0 && i < g_nalarms) g_alarms[i].on = v ? 1 : 0, alarms_save(), sys_dirty();
        return true;
    }
    if (sscanf(cmd, "alarm %d anim %d", &i, &v) == 2) {
        if (i >= 0 && i < g_nalarms && v >= 0 && v < ALARM_ANIMS) g_alarms[i].anim = (uint8_t)v, alarms_save(), sys_dirty();
        return true;
    }
    if (sscanf(cmd, "alarm %d label %n", &i, &off) == 1 && off > 0) {
        if (i < 0 || i >= g_nalarms) return true;
        // Keep whole UTF-8 characters and nothing that breaks the command or the JSON line
        char *dst = g_alarms[i].label;
        size_t n = 0;
        for (const char *p = cmd + off; *p && n < ALARM_LABEL_MAX - 1; p++)
            if ((uint8_t)*p >= 32 && *p != '|') dst[n++] = *p;
        while (n > 0 && ((uint8_t)dst[n - 1] & 0xC0) == 0xC0) n--;  // a lead byte cut off from its tail
        dst[n] = 0;
        alarms_save();
        sys_dirty();
        return true;
    }
    return false;
}

static void ring(int i)
{
    s_cur = i;
    s_ringing = true;
    ESP_LOGI(TAG, "ringing %02d:%02d", g_now.h, g_now.m);
    if (!g_app.dnd) audio_alarm_start();
    sys_alert(&app_alarm_ring);
}

void alarms_minute(void)
{
    if (!g_now.valid || s_ringing) return;
    int now_min = g_now.h * 60 + g_now.m;
    if (s_snooze_at == now_min) {
        s_snooze_at = -1;
        ring(s_cur);
        return;
    }
    int wday = (g_now.wday + 6) % 7;  // 0 = Monday
    for (int i = 0; i < g_nalarms; i++) {
        alarm_t *a = &g_alarms[i];
        if (!a->on || a->h != g_now.h || a->m != g_now.m) continue;
        if (a->days && !(a->days >> wday & 1)) continue;
        if (!a->days) {  // once: off after ringing
            a->on = 0;
            alarms_save();
        }
        ring(i);
        return;
    }
}

bool alarm_ringing(void) { return s_ringing; }

const alarm_t *alarm_current(void)
{
    return s_cur >= 0 && s_cur < g_nalarms ? &g_alarms[s_cur] : NULL;
}

void alarm_snooze(void)
{
    s_ringing = false;
    audio_stop();
    s_snooze_at = (g_now.h * 60 + g_now.m + SNOOZE_MIN) % (24 * 60);
    sys_alert_done();
}

void alarm_stop(void)
{
    s_ringing = false;
    s_snooze_at = -1;
    audio_stop();
    sys_alert_done();
}

const char *alarm_day_letter(int i)
{
    static const char *const es[7] = {"L", "M", "X", "J", "V", "S", "D"};
    static const char *const en[7] = {"M", "T", "W", "T", "F", "S", "S"};
    return (g_app.lang ? en : es)[i % 7];
}
