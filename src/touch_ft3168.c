// touch.h for the FT3168 on the Waveshare ESP32-S3-Touch-AMOLED-2.06 (I2C 0x38, FT5x06 register set).
// Polled from the main loop: one 5-byte read per frame. board_init() has already taken it out of reset.
// Now and then it does not acknowledge a read (it drops into a low-power scan mode when idle); a failed
// read keeps the last state, so a swipe is not cut in two, and failures are reported once a minute.
#include "touch.h"
#include <stdint.h>
#include "board.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "i2c_bus.h"

#define TP_ADDR 0x38
#define REG_STATUS 0x02    // number of touch points (low nibble), then point 1: XH, XL, YH, YL
#define REG_CHIP_ID 0xA3
#define REG_FW_VER 0xA6

static const char *TAG = "touch";
static bool s_ok;
static bool s_down;
static int s_x, s_y, s_fails;
static int64_t s_fails_since;

bool touch_init(void)
{
    uint8_t id = 0, fw = 0;
    s_ok = i2c_reg_read(TP_ADDR, REG_CHIP_ID, &id, 1);
    if (!s_ok) {
        ESP_LOGW(TAG, "FT3168 not answering");
        return false;
    }
    i2c_reg_read(TP_ADDR, REG_FW_VER, &fw, 1);
    ESP_LOGI(TAG, "FT3168 ready (chip id 0x%02X, firmware 0x%02X)", id, fw);
    return true;
}

bool touch_read(int *x, int *y)
{
    uint8_t r[5];
    if (!s_ok) return false;
    if (!i2c_reg_read(TP_ADDR, REG_STATUS, r, sizeof(r))) {
        int64_t now = esp_timer_get_time();
        if (!s_fails++) s_fails_since = now;
        if (now - s_fails_since > 60000000) {
            ESP_LOGW(TAG, "%d failed reads in the last minute", s_fails);
            s_fails = 0;
        }
        *x = s_x;
        *y = s_y;
        return s_down;  // keep the last state
    }
    int points = r[0] & 0x0F;
    int px = ((r[1] & 0x0F) << 8) | r[2], py = ((r[3] & 0x0F) << 8) | r[4];
    s_down = points >= 1 && points <= 5 && px < BOARD_PANEL_W && py < BOARD_PANEL_H;
    if (s_down) {
        s_x = px;
        s_y = py;
    }
    *x = s_x;
    *y = s_y;
    return s_down;
}
