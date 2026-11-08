#include "clock.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>
#include "app.h"
#include "board.h"
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "rtc.h"

static const char *TAG = "clock";
clock_now_t g_now;

static void on_sync(struct timeval *tv)
{
    ESP_LOGI(TAG, "Time synchronized via NTP");
    rtc_write(tv->tv_sec, (int)(tv->tv_usec / 1000));
}

void clock_init(void)
{
    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);  // Europe/Madrid with DST
    tzset();
    // The battery-backed clock kept the time while the board was off: valid from the first frame,
    // so the Bluetooth quiet boot (ble.c, waiting for the phone to bring the time) is skipped too
    int64_t epoch;
    if (rtc_read(&epoch)) {
        struct timeval tv = {.tv_sec = (time_t)epoch, .tv_usec = 0};
        settimeofday(&tv, NULL);
        ESP_LOGI(TAG, "Time from the RTC");
    }
    clock_update();
}

void clock_start_sntp(void)
{
    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    cfg.sync_cb = on_sync;
    esp_err_t err = esp_netif_sntp_init(&cfg);
    if (err != ESP_OK) ESP_LOGW(TAG, "SNTP init failed: %s", esp_err_to_name(err));
}

void clock_update(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    struct tm tm;
    localtime_r(&tv.tv_sec, &tm);
    g_now.valid = tm.tm_year + 1900 >= 2024;
    g_now.h = tm.tm_hour;
    g_now.m = tm.tm_min;
    g_now.s = tm.tm_sec;
    g_now.ms = tv.tv_usec / 1000;
    g_now.day = tm.tm_mday;
    g_now.mon = tm.tm_mon + 1;
    g_now.year = tm.tm_year + 1900;
    g_now.wday = tm.tm_wday;
    if (g_app.dbg_h >= 0) {  // screenshot preview of another time of day
        g_now.valid = true;
        g_now.h = g_app.dbg_h;
        g_now.m = g_app.dbg_m;
    }
    g_now.minute_ms = g_now.s * 1000 + g_now.ms;
}

void clock_set_epoch(int64_t sec, int ms)
{
    struct timeval tv = {.tv_sec = (time_t)sec, .tv_usec = ms * 1000};
    settimeofday(&tv, NULL);
    ESP_LOGI(TAG, "Time set from phone");
    rtc_write(sec, ms);
}

int clock_display_hour(void)
{
    if (g_app.h24) return g_now.h;
    int h = g_now.h % 12;
    return h ? h : 12;
}

void clock_hhmm(char *buf, bool colon)
{
    if (!g_now.valid) {
        sprintf(buf, colon ? "--:--" : "-- --");
        return;
    }
    sprintf(buf, "%02d%c%02d", clock_display_hour(), colon ? ':' : ' ', g_now.m);
}

const char *clock_wday_name(int wday)
{
    static const char *n[] = {"DOM", "LUN", "MAR", "MIE", "JUE", "VIE", "SAB"};
    return n[wday % 7];
}

const char *clock_month_name(int mon)
{
    static const char *n[] = {"ENE", "FEB", "MAR", "ABR", "MAY", "JUN", "JUL", "AGO", "SEP", "OCT", "NOV", "DIC"};
    return n[(mon + 11) % 12];
}
