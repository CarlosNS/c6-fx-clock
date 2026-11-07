// imu.h for the QMI8658 on the Waveshare ESP32-S3-Touch-AMOLED-2.06 (I2C 0x6B). Accelerometer only,
// ±4 g at 21 Hz in its low-power mode; the gyroscope stays off.
#include "imu.h"
#include <stdint.h>
#include "board.h"
#include "esp_log.h"
#include "i2c_bus.h"

#define IMU_ADDR 0x6B
#define REG_WHO_AM_I 0x00   // 0x05
#define REG_CTRL1 0x02      // bit6 address auto-increment
#define REG_CTRL2 0x03      // accelerometer full scale (bits 6:4) and data rate (bits 3:0)
#define REG_CTRL7 0x08      // bit0 accelerometer on, bit1 gyroscope on
#define REG_AX_L 0x35       // AX, AY, AZ: 16-bit little endian

#define CTRL2_4G_21HZ ((1 << 4) | 0x0D)   // ±4 g, low-power 21 Hz
#define LSB_PER_G 8192                     // at ±4 g

static const char *TAG = "imu";
static bool s_ok;

bool imu_init(void)
{
    uint8_t id = 0;
    if (!i2c_reg_read(IMU_ADDR, REG_WHO_AM_I, &id, 1) || id != 0x05) {
        ESP_LOGW(TAG, "QMI8658 not answering (id 0x%02X)", id);
        return false;
    }
    s_ok = i2c_reg_write(IMU_ADDR, REG_CTRL1, 0x40) && i2c_reg_write(IMU_ADDR, REG_CTRL2, CTRL2_4G_21HZ) &&
           i2c_reg_write(IMU_ADDR, REG_CTRL7, 0x01);
    ESP_LOGI(TAG, "QMI8658 %s", s_ok ? "ready (accelerometer)" : "setup failed");
    return s_ok;
}

bool imu_read(int *x, int *y, int *z)
{
    uint8_t r[6];
    if (!s_ok || !i2c_reg_read(IMU_ADDR, REG_AX_L, r, sizeof(r))) return false;
    int ax = (int16_t)(r[0] | r[1] << 8), ay = (int16_t)(r[2] | r[3] << 8), az = (int16_t)(r[4] | r[5] << 8);
    // Chip axes -> board axes (BOARD_IMU_MAP in the board header)
    int c[3] = {ax * 1000 / LSB_PER_G, ay * 1000 / LSB_PER_G, az * 1000 / LSB_PER_G};
    static const int8_t map[3][2] = BOARD_IMU_MAP;
    *x = c[map[0][0]] * map[0][1];
    *y = c[map[1][0]] * map[1][1];
    *z = c[map[2][0]] * map[2][1];
    return true;
}
