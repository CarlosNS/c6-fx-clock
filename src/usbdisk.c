// USB disk mode (boot mode "usb"): the microSD card shows up on the computer as a USB drive.
// The S3 has one USB port for both the console and TinyUSB, so this is a boot of its own: the mode
// is one-shot (bootmode.c): BOOT, ejecting the drive on the computer, unplugging the cable or a reset
// go back to the normal watch with the console.
#include "usbdisk.h"
#include <stdio.h>
#include "board.h"
#include "driver/gpio.h"
#include "driver/sdmmc_host.h"
#include "esp_log.h"
#include "hal/usb_serial_jtag_ll.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lcd.h"
#include "sdmmc_cmd.h"
#include "tinyusb.h"
#include "tusb_msc_storage.h"
#include "ui.h"
#include "ui_icons.h"
#include "apps.h"

static const char *TAG = "usbdisk";
static volatile bool s_exposed, s_ejected;

// Back to the watch: hand the USB PHY back to the serial console first (a software reset keeps it on USB OTG)
static void restart(void)
{
    usb_serial_jtag_ll_phy_enable_external(false);
    esp_restart();
}

static void mount_changed(tinyusb_msc_event_t *e)
{
    if (!e->mount_changed_data.is_mounted) s_exposed = true;
    else if (s_exposed) s_ejected = true;
}

// Ejecting on the computer arrives as SCSI START STOP UNIT (load_eject, !start). esp_tinyusb answers it
// itself and tells nobody (its mount callback only follows mounts on the watch's side), so the call
// is wrapped at link time (src/CMakeLists.txt: --wrap=tud_msc_start_stop_cb) to notice it.
bool __real_tud_msc_start_stop_cb(uint8_t lun, uint8_t power_condition, bool start, bool load_eject);
bool __wrap_tud_msc_start_stop_cb(uint8_t lun, uint8_t power_condition, bool start, bool load_eject)
{
    bool ok = __real_tud_msc_start_stop_cb(lun, power_condition, start, load_eject);
    if (load_eject && !start) s_ejected = true;
    return ok;
}

static void screen(const char *line1, const char *line2, uint16_t accent)
{
    ui_begin(lcd_native_fb());
    ui_background(false);
    ui_veil(120, UI_H, 2);
    ui_icon(icon_files, UI_W / 2 - 48, 92, 6);
    ui_print_c(&ui_jersey, 4, UI_W / 2, 210, TR("MODO USB", "USB MODE"), MS_ORO, true);
    ui_panel(52, 268, UI_W - 104, 116, MS_PANEL, accent, 3, true);
    ui_print_c(&ui_vt, 1, UI_W / 2, 290, line1, MS_CREMA, false);
    ui_print_c(&ui_vt, 1, UI_W / 2, 326, line2, MS_GRIS, false);
    ui_print_c(&ui_vt, 1, UI_W / 2, 412, TR("BOOT: volver al reloj", "BOOT: back to the watch"), MS_GRIS, false);
    lcd_flush_native();
}

void usbdisk_run(void)
{
    lcd_set_brightness(40);
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 1;
    slot.clk = BOARD_SD_CLK;
    slot.cmd = BOARD_SD_CMD;
    slot.d0 = BOARD_SD_D0;
    slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
    static sdmmc_card_t card;
    bool ok = sdmmc_host_init() == ESP_OK && sdmmc_host_init_slot(SDMMC_HOST_SLOT_1, &slot) == ESP_OK &&
              sdmmc_card_init(&host, &card) == ESP_OK;
    if (!ok) {
        ESP_LOGW(TAG, "no card");
        screen(TR("No hay tarjeta microSD", "No microSD card"), TR("Volviendo al reloj...", "Back to the watch..."), MS_CORAL);
        vTaskDelay(pdMS_TO_TICKS(4000));
        esp_restart();
    }
    char size[48];
    snprintf(size, sizeof(size), TR("Tarjeta de %.1f GB", "%.1f GB card"),
             (double)card.csd.capacity * card.csd.sector_size / 1e9);

    // From here the USB port belongs to TinyUSB: the serial console is gone until the next boot
    esp_log_level_set("*", ESP_LOG_NONE);
    const tinyusb_msc_sdmmc_config_t msc = {.card = &card, .callback_mount_changed = mount_changed};
    const tinyusb_config_t usb = {0};
    if (tinyusb_msc_storage_init_sdmmc(&msc) != ESP_OK || tinyusb_driver_install(&usb) != ESP_OK) {
        screen(TR("No se pudo iniciar el USB", "USB failed to start"), TR("Volviendo al reloj...", "Back to the watch..."), MS_CORAL);
        vTaskDelay(pdMS_TO_TICKS(4000));
        esp_restart();
    }

    int shown = -1, released = 0, was_mounted = 0, gone = 0;
    for (;;) {
        int state = tud_mounted() ? 1 : 0;
        if (s_ejected || (was_mounted && !state && ++gone > 20)) {  // ejected, or unplugged for 2 s
            screen(s_ejected ? TR("Disco expulsado", "Drive ejected") : TR("Cable desconectado", "Cable unplugged"),
                   TR("Volviendo al reloj...", "Back to the watch..."), MS_VERDE);
            vTaskDelay(pdMS_TO_TICKS(1500));
            restart();
        }
        if (state) was_mounted = 1, gone = 0;
        if (state != shown) {
            shown = state;
            screen(state ? TR("Conectado al ordenador", "Connected to the computer") : TR("Conecta el cable USB", "Plug in the USB cable"),
                   size, state ? MS_VERDE : MS_ORO);
        }
        // BOOT (active low), once it has been seen released: back to the watch
        if (gpio_get_level(BOARD_PIN_BUTTON)) released = 1;
        else if (released) restart();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
