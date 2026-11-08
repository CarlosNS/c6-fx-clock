#include "alarm.h"
#include <string.h>
#include "app.h"
#include "apps.h"
#include "audio.h"
#include "clock.h"
#include "esp_log.h"
#include "nvs.h"
#include "sys.h"

static const char *TAG = "alarm";
alarm_t g_alarms[ALARMS_MAX];
int g_nalarms;
static bool s_ringing;
static int s_cur = -1;
static int s_snooze_at = -1;  // minute of the day the snoozed alarm rings again

void alarms_init(void)
{
    nvs_handle_t h;
    size_t n = sizeof(g_alarms);
    g_nalarms = 0;
    if (nvs_open("alarms", NVS_READONLY, &h) != ESP_OK) return;
    if (nvs_get_blob(h, "list", g_alarms, &n) == ESP_OK) g_nalarms = (int)(n / sizeof(alarm_t));
    nvs_close(h);
    if (g_nalarms > ALARMS_MAX) g_nalarms = ALARMS_MAX;
}

void alarms_save(void)
{
    nvs_handle_t h;
    if (nvs_open("alarms", NVS_READWRITE, &h) != ESP_OK) return;
    if (g_nalarms) nvs_set_blob(h, "list", g_alarms, g_nalarms * sizeof(alarm_t));
    else nvs_erase_key(h, "list");
    nvs_commit(h);
    nvs_close(h);
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
