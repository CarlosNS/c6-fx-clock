// rtc.h for the PCF85063A on the Waveshare ESP32-S3-Touch-AMOLED-2.06 (I2C 0x51). It runs from the
// battery while the board is off. It holds UTC in BCD registers with 1 s resolution; the time zone
// is applied by clock.c as for the system time.
#include "rtc.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "i2c_bus.h"

#define RTC_ADDR 0x51
#define REG_CTRL1 0x00     // bit5 STOP, bit1 12/24 h (0 = 24 h), bit0 crystal load: left alone
#define REG_SECONDS 0x04   // seconds..years, 7 registers; bit7 of seconds = oscillator stopped (OS)
#define CTRL1_STOP 0x20
#define CTRL1_12H 0x02

static const char *TAG = "rtc";
static esp_timer_handle_t s_write_timer;
static volatile int64_t s_write_sec;

static int from_bcd(uint8_t v) { return (v >> 4) * 10 + (v & 0x0F); }
static uint8_t to_bcd(int v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }

// Days since 1970-01-01 for a proleptic Gregorian date, and back (H. Hinnant's algorithms)
static int64_t days_from_civil(int y, int m, int d)
{
    y -= m <= 2;
    int era = (y >= 0 ? y : y - 399) / 400;
    int yoe = y - era * 400;
    int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return (int64_t)era * 146097 + doe - 719468;
}

static void civil_from_days(int64_t z, int *y, int *m, int *d)
{
    z += 719468;
    int era = (int)((z >= 0 ? z : z - 146096) / 146097);
    int doe = (int)(z - (int64_t)era * 146097);
    int yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    int mp = (5 * doy + 2) / 153;
    *d = doy - (153 * mp + 2) / 5 + 1;
    *m = mp + (mp < 10 ? 3 : -9);
    *y = yoe + era * 400 + (*m <= 2);
}

bool rtc_read(int64_t *epoch)
{
    uint8_t first;
    if (!i2c_reg_read(RTC_ADDR, REG_SECONDS, &first, 1)) {
        ESP_LOGW(TAG, "PCF85063 not answering");
        return false;
    }
    if (first & 0x80) {
        ESP_LOGW(TAG, "PCF85063 lost the time (its oscillator stopped)");
        return false;
    }
    // Wait for the next second tick, so the time is exact to the polling step
    uint8_t r[7];
    int64_t until = esp_timer_get_time() + 1100000;
    do {
        vTaskDelay(pdMS_TO_TICKS(2));
        if (!i2c_reg_read(RTC_ADDR, REG_SECONDS, r, 7)) return false;
    } while ((r[0] & 0x7F) == (first & 0x7F) && esp_timer_get_time() < until);

    int y = 2000 + from_bcd(r[6]), mo = from_bcd(r[5] & 0x1F), d = from_bcd(r[3] & 0x3F);
    int h = from_bcd(r[2] & 0x3F), mi = from_bcd(r[1] & 0x7F), s = from_bcd(r[0] & 0x7F);
    if (y < 2024 || mo < 1 || mo > 12 || d < 1 || d > 31 || h > 23 || mi > 59 || s > 59) {
        ESP_LOGW(TAG, "PCF85063 holds no valid time (%04d-%02d-%02d %02d:%02d:%02d)", y, mo, d, h, mi, s);
        return false;
    }
    *epoch = days_from_civil(y, mo, d) * 86400 + h * 3600 + mi * 60 + s;
    ESP_LOGI(TAG, "PCF85063: %04d-%02d-%02d %02d:%02d:%02d UTC", y, mo, d, h, mi, s);
    return true;
}

// Setting the time exactly: the clock is stopped (STOP bit) and the time written right at a full
// second; STOP is released 492 ms later, and the datasheet puts the first second tick 0.508 s after
// the release, so it falls on the next full second.
#define STOP_RELEASE_US 492000

static uint8_t s_ctrl;  // control register to restore when restarting the clock

static void restart_clock(void *arg)
{
    i2c_reg_write(RTC_ADDR, REG_CTRL1, s_ctrl);
}

// Timer callback, right at a full second
static void write_now(void *arg)
{
    static esp_timer_handle_t restart;
    if (!restart) {
        const esp_timer_create_args_t a = {.callback = restart_clock, .name = "rtc_start"};
        if (esp_timer_create(&a, &restart) != ESP_OK) return;
    }
    int64_t t = s_write_sec;
    int64_t days = t / 86400;
    int sod = (int)(t % 86400), y, mo, d;
    civil_from_days(days, &y, &mo, &d);
    if (y < 2000 || y > 2099) return;
    uint8_t regs[7] = {
        to_bcd(sod % 60),                 // seconds; writing it also clears the OS flag
        to_bcd(sod / 60 % 60),
        to_bcd(sod / 3600),
        to_bcd(d),
        (uint8_t)((days + 4) % 7),        // weekday, 0 = Sunday (1970-01-01 was a Thursday)
        to_bcd(mo),
        to_bcd(y - 2000),
    };
    uint8_t ctrl;
    if (!i2c_reg_read(RTC_ADDR, REG_CTRL1, &ctrl, 1)) {
        ESP_LOGW(TAG, "PCF85063 not answering");
        return;
    }
    s_ctrl = ctrl & ~(CTRL1_STOP | CTRL1_12H);  // running, 24 h
    if (!i2c_reg_write(RTC_ADDR, REG_CTRL1, s_ctrl | CTRL1_STOP) ||
        !i2c_regs_write(RTC_ADDR, REG_SECONDS, regs, sizeof(regs))) {
        ESP_LOGW(TAG, "PCF85063 write failed");
        restart_clock(NULL);
        return;
    }
    esp_timer_start_once(restart, STOP_RELEASE_US);
    ESP_LOGI(TAG, "PCF85063 set to %04d-%02d-%02d %02d:%02d:%02d UTC", y, mo, d, sod / 3600, sod / 60 % 60, sod % 60);
}

void rtc_write(int64_t sec, int ms)
{
    if (!s_write_timer) {
        const esp_timer_create_args_t a = {.callback = write_now, .name = "rtc_write"};
        if (esp_timer_create(&a, &s_write_timer) != ESP_OK) return;
    }
    esp_timer_stop(s_write_timer);  // a newer time replaces one still waiting
    s_write_sec = sec + 1;
    esp_timer_start_once(s_write_timer, (uint64_t)(1000 - ms) * 1000);
}
