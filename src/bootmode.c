#include "bootmode.h"
#include <string.h>
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "ota.h"

static const char *TAG = "bootmode";
static const char *const NAMES[] = {"ble", "wifi", "update"};
static boot_mode_t s_mode = BOOT_DEFAULT, s_return = BOOT_DEFAULT;

static int32_t nvs_get(nvs_handle_t h, const char *key, int32_t def)
{
    int32_t v;
    return nvs_get_i32(h, key, &v) == ESP_OK && v >= BOOT_BLE && v <= BOOT_UPDATE ? v : def;
}

static void nvs_put(const char *key, int32_t v)
{
    nvs_handle_t h;
    if (nvs_open("clock", NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_i32(h, key, v);
    nvs_commit(h);
    nvs_close(h);
}

boot_mode_t boot_mode_init(void)
{
    nvs_handle_t h;
    if (nvs_open("clock", NVS_READONLY, &h) == ESP_OK) {
        s_mode = nvs_get(h, "bmode", BOOT_DEFAULT);
        s_return = nvs_get(h, "bret", BOOT_DEFAULT);
        nvs_close(h);
    }
    if (s_return == BOOT_UPDATE) s_return = BOOT_DEFAULT;
    if (s_mode == BOOT_UPDATE) nvs_put("bmode", s_return);  // one-shot
    ESP_LOGI(TAG, "Boot mode: %s", NAMES[s_mode]);
    return s_mode;
}

boot_mode_t boot_mode(void) { return s_mode; }
boot_mode_t boot_mode_return(void) { return s_return; }

const char *boot_mode_name(boot_mode_t m)
{
    return m >= BOOT_BLE && m <= BOOT_UPDATE ? NAMES[m] : "?";
}

boot_mode_t boot_mode_from_name(const char *name)
{
    for (int i = 0; i <= BOOT_UPDATE; i++)
        if (strcmp(name, NAMES[i]) == 0) return (boot_mode_t)i;
    return (boot_mode_t)-1;
}

void boot_mode_switch(boot_mode_t m)
{
    if (m == BOOT_UPDATE) nvs_put("bret", s_mode == BOOT_UPDATE ? s_return : s_mode);
    nvs_put("bmode", m);
    // A switch is asked for by a working firmware: confirm it, or a version installed less than 30 s
    // ago would be rolled back by this very restart
    ota_confirm_app();
    ESP_LOGI(TAG, "Switching to %s mode, restarting", NAMES[m]);
    vTaskDelay(pdMS_TO_TICKS(300));  // let the log and any reply go out
    esp_restart();
}
