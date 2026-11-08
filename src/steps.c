// Step counter: peaks of the acceleration magnitude (gravity taken away by a slow average), with a
// minimum and maximum time between steps. A run only counts once it has 4 steps in a row at a walking
// rhythm, so taps on the table and single arm moves do not add up. History: one count per day for 30
// days, in NVS ("steps" namespace), saved every 10 minutes and at midnight.
#include "steps.h"
#include <string.h>
#include "clock.h"
#include "esp_log.h"
#include "nvs.h"

#define MIN_GAP_MS 260     // faster than ~3.8 steps/s is not walking
#define MAX_GAP_MS 2000    // slower breaks the run
#define RUN_START 4        // steps in a row before they count
#define THRESH_MG 110      // peak above the average

static const char *TAG = "steps";

typedef struct {
    int32_t day;                   // days since 1970 of days[0]
    uint16_t days[STEPS_DAYS];     // days[0] = that day, days[1] the one before...
} history_t;

static history_t s_hist;
static int s_today;
static int s_avg = 1000;           // slow average of |a| (gravity), mg
static int s_prev, s_rising;
static uint32_t s_last_step;
static int s_run;                  // steps in the current run (counted once it reaches RUN_START)
static int s_minutes;

static int32_t today_index(void)
{
    if (!g_now.valid) return -1;
    // days since 1970 from the civil date (same algorithm as rtc_pcf85063.c)
    int y = g_now.year, m = g_now.mon, d = g_now.day;
    y -= m <= 2;
    int era = (y >= 0 ? y : y - 399) / 400, yoe = y - era * 400;
    int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    return era * 146097 + yoe * 365 + yoe / 4 - yoe / 100 + doy - 719468;
}

static void save(void)
{
    nvs_handle_t h;
    if (nvs_open("steps", NVS_READWRITE, &h) != ESP_OK) return;
    s_hist.days[0] = s_today > 65535 ? 65535 : s_today;
    nvs_set_blob(h, "hist", &s_hist, sizeof(s_hist));
    nvs_commit(h);
    nvs_close(h);
}

void steps_init(void)
{
    nvs_handle_t h;
    size_t n = sizeof(s_hist);
    memset(&s_hist, 0, sizeof(s_hist));
    s_hist.day = -1;
    if (nvs_open("steps", NVS_READONLY, &h) == ESP_OK) {
        if (nvs_get_blob(h, "hist", &s_hist, &n) != ESP_OK || n != sizeof(s_hist)) {
            memset(&s_hist, 0, sizeof(s_hist));
            s_hist.day = -1;
        }
        nvs_close(h);
    }
    s_today = s_hist.days[0];
    steps_minute();  // a different day: roll over
}

static void roll_to(int32_t day)
{
    if (s_hist.day < 0) {
        s_hist.day = day;
        return;
    }
    int shift = day - s_hist.day;
    if (shift <= 0) return;
    s_hist.days[0] = s_today > 65535 ? 65535 : s_today;
    if (shift >= STEPS_DAYS) memset(s_hist.days, 0, sizeof(s_hist.days));
    else {
        memmove(s_hist.days + shift, s_hist.days, (STEPS_DAYS - shift) * sizeof(uint16_t));
        memset(s_hist.days, 0, shift * sizeof(uint16_t));
    }
    s_hist.day = day;
    s_today = 0;
    ESP_LOGI(TAG, "new day, yesterday %u steps", s_hist.days[1]);
    save();
}

void steps_minute(void)
{
    int32_t day = today_index();
    if (day >= 0) roll_to(day);
    if (++s_minutes >= 10) {
        s_minutes = 0;
        save();
    }
}

void steps_feed(int x, int y, int z, uint32_t now)
{
    // |a| without a square root: the largest component plus a share of the others is close enough
    int ax = x < 0 ? -x : x, ay = y < 0 ? -y : y, az = z < 0 ? -z : z;
    int mx = ax > ay ? (ax > az ? ax : az) : (ay > az ? ay : az);
    int mag = mx + (ax + ay + az - mx) * 3 / 8;
    s_avg += (mag - s_avg) / 16;
    int v = mag - s_avg;
    // A step is a peak above the threshold: rising, then falling
    if (v > s_prev) s_rising = 1;
    else if (s_rising && s_prev > THRESH_MG) {
        s_rising = 0;
        uint32_t gap = now - s_last_step;
        if (gap >= MIN_GAP_MS) {
            if (gap > MAX_GAP_MS) s_run = 0;
            s_last_step = now;
            if (++s_run == RUN_START) s_today += RUN_START;  // the run is real: count it whole
            else if (s_run > RUN_START) s_today++;
        }
    }
    s_prev = v;
}

int steps_today(void) { return s_today; }

int steps_day(int days_ago)
{
    if (days_ago == 0) return s_today;
    if (days_ago < 0 || days_ago >= STEPS_DAYS || s_hist.day < 0) return -1;
    return s_hist.days[days_ago];
}
