#include "update.h"
#include "bootmode.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gfx.h"
#include "lcd.h"

static const char *TAG = "update";

static void screen(const char *line1, const char *line2)
{
    uint16_t *fb = lcd_fb();
    gfx_fill(fb, rgb565(10, 12, 20));
    gfx_text(fb, (g_lcd_w - gfx_text_width(line1, 2)) / 2, 130, line1, rgb565(255, 255, 255), 2);
    gfx_text(fb, (g_lcd_w - gfx_text_width(line2, 1)) / 2, 160, line2, rgb565(170, 180, 210), 1);
    lcd_flush();
}

void update_run(void)
{
    ESP_LOGI(TAG, "Update mode, free heap %u", (unsigned)heap_caps_get_free_size(MALLOC_CAP_DEFAULT));
    screen("ACTUALIZAR", "todavia no disponible");
    // Phase 6: Wi-Fi client, manifest, esp_https_ota, result in NVS
    ESP_LOGW(TAG, "Not implemented yet, back to %s mode", boot_mode_name(boot_mode_return()));
    vTaskDelay(pdMS_TO_TICKS(3000));
    boot_mode_switch(boot_mode_return());
}
