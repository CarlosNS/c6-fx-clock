// Battery log: every 5 minutes, the charge, the voltage and what the watch was doing (on USB, screen on
// for how long, Wi-Fi state), kept in RTC memory so it survives restarts (not a power cut). Every 15
// minutes the sample also goes to flash (NVS, 128 of them: 32 hours, with the date and time), which
// survives an empty battery: the night that drained it can be read the next morning. Read both as
// CSV at GET /batlog or with the "batlog" console command, to see what drains the battery.
#include "batlog.h"
#include <stdio.h>
#include <string.h>
#include "esp_attr.h"
#include "esp_timer.h"
#include "pmu_axp2101.h"
#include <sys/time.h>
#include "nvs.h"

#define N 192           // 16 hours
#define MAGIC 0xBA77106u

typedef struct {
    uint32_t minute;    // minutes since boot... of the boot that wrote it, see `boot`
    uint16_t mv;
    uint8_t pct;
    uint8_t flags;      // bit 0 USB, bit 1 Wi-Fi joined, bit 2 own AP on, bits 3-7 screen-on minutes (0..31)
    uint8_t boot;       // which boot (wraps), to tell restarts apart
    uint8_t pad[3];
} entry_t;

static RTC_NOINIT_ATTR struct {
    uint32_t magic;
    uint16_t head, count;
    uint8_t boot;
    entry_t e[N];
} s_log;

static uint32_t s_screen_ms, s_last_ms, s_next_min = 5;

// ---- in flash: one NVS key per slot ("e000".."e127", 8 bytes) and the next slot in "head"
#define NF 128
typedef struct {
    uint32_t epoch;     // Unix time (0: the watch had no time)
    uint16_t mv;
    uint8_t pct;
    uint8_t flags;      // as above, screen-on minutes over the last 15 (0..31)
} fentry_t;
static uint32_t s_f_screen_min;
static int s_f_n;

static void flash_add(const entry_t *e)
{
    s_f_screen_min += e->flags >> 3;
    if (++s_f_n < 3) return;  // every third sample: 15 minutes
    struct timeval tv;
    gettimeofday(&tv, NULL);
    uint32_t sm = s_f_screen_min > 31 ? 31 : s_f_screen_min;
    fentry_t f = {.epoch = tv.tv_sec > 1700000000 ? (uint32_t)tv.tv_sec : 0, .mv = e->mv, .pct = e->pct,
                  .flags = (uint8_t)((e->flags & 7) | (sm << 3))};
    s_f_n = 0;
    s_f_screen_min = 0;
    nvs_handle_t h;
    if (nvs_open("batlog", NVS_READWRITE, &h) != ESP_OK) return;
    uint16_t head = 0;
    nvs_get_u16(h, "head", &head);
    char key[8];
    snprintf(key, sizeof(key), "e%03u", head % NF);
    nvs_set_blob(h, key, &f, sizeof(f));
    nvs_set_u16(h, "head", (uint16_t)((head + 1) % NF));
    nvs_commit(h);
    nvs_close(h);
}

void batlog_init(void)
{
    if (s_log.magic != MAGIC || s_log.head >= N || s_log.count > N) {
        memset(&s_log, 0, sizeof(s_log));
        s_log.magic = MAGIC;
    }
    s_log.boot++;
}

void batlog_tick(uint32_t now_ms, bool screen_on, bool wifi_joined, bool ap_on)
{
    if (s_last_ms && screen_on) s_screen_ms += now_ms - s_last_ms;
    s_last_ms = now_ms;
    uint32_t minute = now_ms / 60000;
    if (minute < s_next_min) return;
    s_next_min = minute + 5;
    pmu_battery_t b;
    if (!pmu_battery(&b) || !b.present) return;
    uint32_t on_min = s_screen_ms / 60000;
    s_screen_ms = 0;
    entry_t *e = &s_log.e[s_log.head];
    *e = (entry_t){.minute = minute, .mv = (uint16_t)(b.mv > 0 ? b.mv : 0), .pct = (uint8_t)(b.percent < 0 ? 255 : b.percent),
                   .flags = (uint8_t)((b.usb ? 1 : 0) | (wifi_joined ? 2 : 0) | (ap_on ? 4 : 0) | ((on_min > 31 ? 31 : on_min) << 3)),
                   .boot = s_log.boot};
    s_log.head = (s_log.head + 1) % N;
    if (s_log.count < N) s_log.count++;
    flash_add(e);
}

size_t batlog_csv(char *buf, size_t size)
{
    size_t len = snprintf(buf, size, "boot,minute,pct,mv,usb,wifi,ap,screen_min\n");
    for (int i = 0; i < s_log.count && len < size; i++) {
        const entry_t *e = &s_log.e[(s_log.head + N - s_log.count + i) % N];
        int n = snprintf(buf + len, size - len, "%u,%lu,%u,%u,%u,%u,%u,%u\n", e->boot, (unsigned long)e->minute, e->pct, e->mv,
                         e->flags & 1, (e->flags >> 1) & 1, (e->flags >> 2) & 1, e->flags >> 3);
        if (n < 0) break;
        len += n;
    }
    // The flash part, oldest first, with the time of day (UTC+offset as the watch keeps it)
    nvs_handle_t h;
    if (len < size && nvs_open("batlog", NVS_READONLY, &h) == ESP_OK) {
        uint16_t head = 0;
        nvs_get_u16(h, "head", &head);
        int n = snprintf(buf + len, size - len, "flash: time,pct,mv,usb,wifi,ap,screen_min\n");
        if (n > 0) len += n;
        for (int i = 0; i < NF && len < size; i++) {
            char key[8];
            fentry_t f;
            size_t sz = sizeof(f);
            snprintf(key, sizeof(key), "e%03u", (head + i) % NF);
            if (nvs_get_blob(h, key, &f, &sz) != ESP_OK || sz != sizeof(f)) continue;
            char when[24] = "-";
            if (f.epoch) {
                time_t t = f.epoch;
                struct tm tm;
                localtime_r(&t, &tm);
                strftime(when, sizeof(when), "%d/%m %H:%M", &tm);
            }
            n = snprintf(buf + len, size - len, "%s,%u,%u,%u,%u,%u,%u\n", when, f.pct, f.mv, f.flags & 1, (f.flags >> 1) & 1,
                         (f.flags >> 2) & 1, f.flags >> 3);
            if (n < 0) break;
            len += n;
        }
        nvs_close(h);
    }
    return len < size ? len : size - 1;
}
