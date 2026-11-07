// touch.h for the FT3168 on the Waveshare ESP32-S3-Touch-AMOLED-2.06 (I2C 0x38, FT5x06 register set).
// Polled from the main loop: one 5-byte read per frame. board_init() has already taken it out of reset.
#include "touch.h"
#include <stdint.h>
#include "board.h"
#include "esp_log.h"
#include "i2c_bus.h"

#define TP_ADDR 0x38
#define REG_STATUS 0x02    // number of touch points (low nibble), then point 1: XH, XL, YH, YL
#define REG_CHIP_ID 0xA3
#define REG_FW_VER 0xA6

static const char *TAG = "touch";
static bool s_ok;

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
    if (!s_ok || !i2c_reg_read(TP_ADDR, REG_STATUS, r, sizeof(r))) return false;
    int points = r[0] & 0x0F;
    if (points < 1 || points > 5) return false;
    int px = ((r[1] & 0x0F) << 8) | r[2], py = ((r[3] & 0x0F) << 8) | r[4];
    if (px >= BOARD_PANEL_W || py >= BOARD_PANEL_H) return false;
    *x = px;
    *y = py;
    return true;
}
