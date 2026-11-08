// Battery log: every 5 minutes, the charge, the voltage and what the watch was doing (on USB, screen on
// for how long, Wi-Fi state), kept in RTC memory so it survives restarts (not a power cut). Read it as
// CSV at GET /batlog or with the "batlog" console command, to see what drains the battery.
#include "batlog.h"
#include <stdio.h>
#include <string.h>
#include "esp_attr.h"
#include "esp_timer.h"
#include "pmu_axp2101.h"

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
    return len < size ? len : size - 1;
}
