#include "board.h"
#include "driver/gpio.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_log.h"
#include "esp_psram.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "i2c_bus.h"
#include "pmu_axp2101.h"
#include "imu.h"
#include "touch.h"

static const char *TAG = "board";

// Waveshare ESP32-S3-Touch-AMOLED-2.06: the shared I2C bus and a report of what is on it. The
// AXP2101's power-on defaults already feed the screen and every chip, so it is only read for now.
void board_init(void)
{
    esp_chip_info_t chip;
    esp_chip_info(&chip);
    uint32_t flash = 0;
    esp_flash_get_physical_size(NULL, &flash);
    ESP_LOGI(TAG, "%s: ESP32-S3 rev %d.%d, flash %lu MB, PSRAM %u MB", BOARD_NAME, chip.revision / 100,
             chip.revision % 100, (unsigned long)(flash >> 20), (unsigned)(esp_psram_get_size() >> 20));

    // The touch controller only answers on I2C once out of reset
    gpio_config_t rst = {.pin_bit_mask = 1ULL << BOARD_TOUCH_RST, .mode = GPIO_MODE_OUTPUT};
    gpio_config(&rst);
    gpio_set_level(BOARD_TOUCH_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(BOARD_TOUCH_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(300));

    // The touch controller sometimes skips an acknowledge (touch_ft3168.c copes with it); ESP-IDF
    // would print three error lines each time. Our drivers report their own failures.
    esp_log_level_set("i2c.master", ESP_LOG_NONE);
    i2c_bus_init();
    ESP_LOGI(TAG, "I2C bus:");
    i2c_bus_scan();
    pmu_log_status();
    touch_init();
    if (imu_init()) {
        int x, y, z;
        vTaskDelay(pdMS_TO_TICKS(100));  // first sample
        if (imu_read(&x, &y, &z)) ESP_LOGI(TAG, "accelerometer: x %d y %d z %d mg", x, y, z);
    }
}
