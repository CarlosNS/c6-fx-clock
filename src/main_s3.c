// Bring-up firmware for the Waveshare ESP32-S3-Touch-AMOLED-2.06 (phase 1 of docs/PLAN-s3.md):
// memory, the I2C bus, the power manager and a still test image on the AMOLED. The clock itself
// (main.c and the faces) moves to this board in phases 2 and 3.
#include <string.h>
#include "board.h"
#include "driver/gpio.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_psram.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "i2c_bus.h"
#include "lcd.h"
#include "lcd_co5300.h"
#include "pmu_axp2101.h"

#define W BOARD_LCD_W
#define H BOARD_LCD_H
#define STRIP_LINES 38   // even (CO5300 rule) and 38 * 410 * 2 bytes fits in one 32 KB transfer
#define CORNER 40

static const char *TAG = "s3";

static void log_system(void)
{
    esp_chip_info_t chip;
    esp_chip_info(&chip);
    uint32_t flash = 0;
    esp_flash_get_physical_size(NULL, &flash);
    ESP_LOGI(TAG, "%s: ESP32-S3 rev %d.%d, %d cores, flash %lu MB, PSRAM %u MB", BOARD_NAME,
             chip.revision / 100, chip.revision % 100, chip.cores, (unsigned long)(flash >> 20),
             (unsigned)(esp_psram_get_size() >> 20));
    ESP_LOGI(TAG, "free heap: internal %u KB, PSRAM %u KB",
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) >> 10),
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) >> 10));
}

// The touch controller only answers on I2C once out of reset
static void touch_release_reset(void)
{
    gpio_config_t rst = {.pin_bit_mask = 1ULL << BOARD_TOUCH_RST, .mode = GPIO_MODE_OUTPUT};
    gpio_config(&rst);
    gpio_set_level(BOARD_TOUCH_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(BOARD_TOUCH_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(300));
}

// Diagonal gradient, a white frame on the outermost pixels and a colored square in each corner
// (red top left, green top right, blue bottom left, white bottom right): checks colors, the column
// offset and the orientation at a glance
static void draw_test_image(uint16_t *fb)
{
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            uint16_t c = rgb565(x * 255 / (W - 1), y * 255 / (H - 1), 255 - (x + y) * 255 / (W + H - 2));
            if (x == 0 || y == 0 || x == W - 1 || y == H - 1) c = rgb565(255, 255, 255);
            else if (x < CORNER + 4 && y < CORNER + 4 && x > 3 && y > 3) c = rgb565(255, 0, 0);
            else if (x >= W - CORNER - 4 && y < CORNER + 4 && x < W - 4 && y > 3) c = rgb565(0, 255, 0);
            else if (x < CORNER + 4 && y >= H - CORNER - 4 && x > 3 && y < H - 4) c = rgb565(0, 0, 255);
            else if (x >= W - CORNER - 4 && y >= H - CORNER - 4 && x < W - 4 && y < H - 4) c = rgb565(255, 255, 255);
            fb[y * W + x] = c;
        }
    }
}

// The framebuffer lives in PSRAM; the SPI DMA reads from an internal strip buffer
static int send_frame(const uint16_t *fb, uint16_t *strip)
{
    int64_t t0 = esp_timer_get_time();
    for (int y = 0; y < H; y += STRIP_LINES) {
        int lines = H - y < STRIP_LINES ? H - y : STRIP_LINES;
        memcpy(strip, fb + y * W, (size_t)lines * W * 2);
        co5300_draw(0, y, W, lines, strip);
    }
    return (int)(esp_timer_get_time() - t0);
}

void app_main(void)
{
    log_system();

    touch_release_reset();
    i2c_bus_init();
    ESP_LOGI(TAG, "I2C scan:");
    i2c_bus_scan();
    pmu_log_status();

    co5300_init();
    uint16_t *fb = heap_caps_malloc(W * H * 2, MALLOC_CAP_SPIRAM);
    uint16_t *strip = heap_caps_malloc(STRIP_LINES * W * 2, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    assert(fb && strip);
    draw_test_image(fb);
    int us = send_frame(fb, strip);
    ESP_LOGI(TAG, "test image sent in %d.%d ms", us / 1000, us % 1000 / 100);

    static const int levels[] = {80, 40, 10, 100};
    int level = 0;
    co5300_set_brightness(levels[level]);
    ESP_LOGI(TAG, "brightness %d %% (BOOT button: next level)", levels[level]);

    gpio_config_t btn = {.pin_bit_mask = 1ULL << BOARD_PIN_BUTTON, .mode = GPIO_MODE_INPUT, .pull_up_en = GPIO_PULLUP_ENABLE};
    gpio_config(&btn);
    bool was_down = false;
    for (;;) {
        bool down = gpio_get_level(BOARD_PIN_BUTTON) == 0;
        if (down && !was_down) {
            level = (level + 1) % (int)(sizeof(levels) / sizeof(levels[0]));
            co5300_set_brightness(levels[level]);
            ESP_LOGI(TAG, "brightness %d %%", levels[level]);
        }
        was_down = down;
        vTaskDelay(pdMS_TO_TICKS(30));
    }
}
