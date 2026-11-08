// Files: the microSD card (SDMMC, 1 bit, mounted at /sd while the app is open; FAT or exFAT, see
// components/fatfs). Folders and files with their size; tap a folder to enter it, ".." to go up; long
// press a file to delete it (a second tap confirms). The format button wipes the card (exFAT) after two
// confirmations, also offered for a card that cannot be read; the USB button restarts the watch as a
// USB drive for the computer (usbdisk.c) after one. sd_check() tells the status bar whether there is
// a card.
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "apps.h"
#include "board.h"
#include "bootmode.h"
#include "driver/sdmmc_host.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdmmc_cmd.h"
#include "sys.h"
#include "ui_icons.h"

#define MOUNT "/sd"
#define MAX_ENT 64
#define TOP 122
#define ROW 62
#define X0 40
#define WID 330
#define BAR 120  // bottom bar: free space, USB and format buttons

static const char *TAG = "files";
static sdmmc_card_t *s_card;
static char s_path[160] = MOUNT;
static struct {
    char name[40];
    bool dir;
    uint32_t size;
} s_ent[MAX_ENT];
static int s_n;
static ui_scroll_t s_scroll;
static int s_confirm = -1;      // entry waiting for the delete confirmation; -2 = format, -3 = format (2nd)
static char s_msg[48];

static void load(void)
{
    s_n = 0;
    s_scroll.offset = 0;
    if (strcmp(s_path, MOUNT)) {
        strcpy(s_ent[0].name, "..");
        s_ent[0].dir = true;
        s_n = 1;
    }
    DIR *d = opendir(s_path);
    if (!d) return;
    struct dirent *de;
    while ((de = readdir(d)) && s_n < MAX_ENT) {
        strlcpy(s_ent[s_n].name, de->d_name, sizeof(s_ent[s_n].name));
        s_ent[s_n].dir = de->d_type == DT_DIR;
        char full[sizeof(s_path) + 260];
        struct stat st;
        snprintf(full, sizeof(full), "%s/%s", s_path, de->d_name);
        s_ent[s_n].size = !s_ent[s_n].dir && stat(full, &st) == 0 ? (uint32_t)st.st_size : 0;
        s_n++;
    }
    closedir(d);
}

// ---------------------------------------------------------------- the card (also for the status bar)

// Mounting is shared by the app and the background check (sd_check), one at a time. The result goes to
// g_app.sd_state for the status bar icon: SD_NONE, SD_OK, or SD_BAD (the card answers but its file
// system cannot be read: unformatted, or a format FatFs does not know).
static SemaphoreHandle_t s_lock;
static TaskHandle_t s_check_task;

static int mount(bool format)
{
    esp_vfs_fat_sdmmc_mount_config_t mc = {.format_if_mount_failed = format, .max_files = 4,
                                           .allocation_unit_size = 128 * 1024};  // exFAT: big clusters suit cards
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 1;
    slot.clk = BOARD_SD_CLK;
    slot.cmd = BOARD_SD_CMD;
    slot.d0 = BOARD_SD_D0;
    slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
    esp_err_t err = esp_vfs_fat_sdmmc_mount(MOUNT, &host, &slot, &mc, &s_card);
    if (err == ESP_OK) return SD_OK;
    s_card = NULL;
    ESP_LOGW(TAG, "card: %s", esp_err_to_name(err));
    return err == ESP_FAIL ? SD_BAD : SD_NONE;  // ESP_FAIL: the card answered, its file system did not
}

static void unmount(void)
{
    if (s_card) esp_vfs_fat_sdcard_unmount(MOUNT, s_card);
    s_card = NULL;
}

static void check_task(void *arg)
{
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        xSemaphoreTake(s_lock, portMAX_DELAY);
        if (!s_card) {  // the app has it mounted: its state is already known
            int st = mount(false);
            unmount();
            if (st != g_app.sd_state) sys_dirty();
            g_app.sd_state = st;
        }
        xSemaphoreGive(s_lock);
    }
}

void sd_check(void)
{
    if (!s_lock) {
        s_lock = xSemaphoreCreateMutex();
        xTaskCreate(check_task, "sd_check", 4096, NULL, 2, &s_check_task);
    }
    xTaskNotifyGive(s_check_task);
}

// Console "sdinfo": what is on the card (size, partition table, file system), to tell why it does not mount
void sd_info(void)
{
    if (!s_lock) sd_check();
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_card) {
        printf("sdinfo: mounted\n");
        xSemaphoreGive(s_lock);
        return;
    }
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 1, slot.clk = BOARD_SD_CLK, slot.cmd = BOARD_SD_CMD, slot.d0 = BOARD_SD_D0;
    slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
    static sdmmc_card_t card;
    uint8_t *sec = heap_caps_malloc(512, MALLOC_CAP_DMA);
    if (sec && sdmmc_host_init() == ESP_OK && sdmmc_host_init_slot(SDMMC_HOST_SLOT_1, &slot) == ESP_OK &&
        sdmmc_card_init(&host, &card) == ESP_OK) {
        printf("sdinfo: %s, %.1f GB\n", card.cid.name, (double)card.csd.capacity * card.csd.sector_size / 1e9);
        for (uint32_t lba = 0, i = 0; i < 2; i++) {
            if (sdmmc_read_sectors(&card, sec, lba, 1) != ESP_OK) break;
            printf("sdinfo: sector %lu sig %02X%02X oem '%.8s' fs '%.8s' / '%.8s'\n", (unsigned long)lba, sec[510], sec[511],
                   sec + 3, sec + 54, sec + 82);
            if (i == 0) {
                for (int p = 0; p < 4; p++) {
                    const uint8_t *e = sec + 446 + 16 * p;
                    uint32_t start = e[8] | e[9] << 8 | e[10] << 16 | (uint32_t)e[11] << 24;
                    if (e[4]) printf("sdinfo: partition %d type 0x%02X start %lu\n", p, e[4], (unsigned long)start);
                    if (p == 0) lba = start;
                }
                if (!lba) break;
            }
        }
    } else {
        printf("sdinfo: no card answers\n");
    }
    sdmmc_host_deinit();
    free(sec);
    xSemaphoreGive(s_lock);
}

static void files_open(void)
{
    s_confirm = -1;
    s_msg[0] = 0;
    strcpy(s_path, MOUNT);
    if (!s_lock) sd_check();
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (!s_card) g_app.sd_state = mount(false);
    xSemaphoreGive(s_lock);
    if (s_card) load();
}

static void files_close(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    unmount();
    xSemaphoreGive(s_lock);
}

static void size_text(char *b, size_t n, uint32_t v)
{
    if (v >= 1024 * 1024) snprintf(b, n, "%u,%u MB", (unsigned)(v >> 20), (unsigned)((v % (1024 * 1024)) * 10 >> 20));
    else if (v >= 1024) snprintf(b, n, "%u KB", (unsigned)(v >> 10));
    else snprintf(b, n, "%u B", (unsigned)v);
}

static bool draw(uint32_t now, bool force)
{
    if (!force) return false;
    char buf[48];
    ui_background(false);
    ui_veil(60, UI_H, 3);
    if (!s_card && g_app.sd_state == SD_BAD) {  // the card answers but cannot be read: offer to format it
        ui_panel(48, 150, UI_W - 96, 170, MS_PANEL, MS_CORAL, 4, true);
        ui_print_c(&ui_jersey, 3, UI_W / 2, 176, TR("No se puede leer", "Cannot read it"), MS_CREMA, false);
        ui_print_c(&ui_vt, 1, UI_W / 2, 236, TR("la tarjeta no tiene un formato", "the card has no format"), MS_GRIS, false);
        ui_print_c(&ui_vt, 1, UI_W / 2, 262, TR("que el reloj entienda", "the watch understands"), MS_GRIS, false);
        if (s_msg[0]) ui_print_c(&ui_vt, 1, UI_W / 2, UI_H - 128, s_msg, MS_ORO2, false);
        const char *fl = s_confirm == -2 ? TR("Â¿Seguro?", "Sure?") : s_confirm == -3 ? TR("Â¡Se borra todo!", "Erase all!")
                                                                                     : TR("Formatear (exFAT)", "Format (exFAT)");
        ui_button(UI_W / 2 - 120, UI_H - 96, 240, 56, fl, s_confirm <= -2 ? UI_BTN_DANGER : UI_BTN_SECONDARY, false);
    } else if (!s_card) {
        ui_panel(48, 150, UI_W - 96, 170, MS_PANEL, MS_CORAL, 4, true);
        ui_print_c(&ui_jersey, 3, UI_W / 2, 176, TR("Sin tarjeta", "No card"), MS_CREMA, false);
        ui_print_c(&ui_vt, 1, UI_W / 2, 236, TR("mete una microSD", "insert a microSD"), MS_GRIS, false);
        ui_print_c(&ui_vt, 1, UI_W / 2, 266, TR("y vuelve a entrar", "and open again"), MS_GRIS, false);
    } else {
        s_scroll.view_h = UI_H - TOP - BAR;
        s_scroll.content_h = s_n * ROW;
        for (int i = 0; i < s_n; i++) {
            int y = TOP + i * ROW - s_scroll.offset;
            if (y > UI_H - BAR || y + ROW < TOP - 20) continue;
            bool conf = i == s_confirm;
            ui_panel(X0, y, WID, ROW - 8, conf ? MS_PANEL2 : MS_PANEL, conf ? MS_CORAL : MS_LINEA, 4, false);
            ui_print(&ui_vt, 2, X0 + 16, y + 4, s_ent[i].name, s_ent[i].dir ? MS_ORO2 : MS_CREMA);
            if (conf) ui_print_r(&ui_vt, 1, X0 + WID - 14, y + 18, TR("Â¿borrar?", "delete?"), MS_CORAL);
            else if (!s_ent[i].dir) {
                size_text(buf, sizeof(buf), s_ent[i].size);
                ui_print_r(&ui_vt, 1, X0 + WID - 14, y + 18, buf, MS_GRIS);
            }
        }
        if (!s_n) ui_print_c(&ui_vt, 2, UI_W / 2, TOP + 40, TR("vacÃ­a", "empty"), MS_GRIS, false);
        // bottom: free space, the USB and format buttons
        uint64_t total = 0, free_b = 0;
        esp_vfs_fat_info(MOUNT, &total, &free_b);
        snprintf(buf, sizeof(buf), TR("libre %u MB de %u", "free %u MB of %u"), (unsigned)(free_b >> 20), (unsigned)(total >> 20));
        ui_rect(0, UI_H - BAR, UI_W, BAR, MS_NOCHE);
        ui_print_c(&ui_vt, 1, UI_W / 2, UI_H - BAR + 6, s_msg[0] ? s_msg : buf, s_msg[0] ? MS_ORO2 : MS_GRIS, false);
        ui_button(UI_W / 2 - 150, UI_H - 82, 140, 50, s_confirm == -4 ? TR("Â¿Reiniciar?", "Restart?") : "USB",
                  s_confirm == -4 ? UI_BTN_MAIN : UI_BTN_SECONDARY, false);
        const char *fl = s_confirm == -2 ? TR("Â¿Seguro?", "Sure?") : s_confirm == -3 ? TR("Â¡Se borra todo!", "Erase all!") : TR("Formatear", "Format");
        ui_button(UI_W / 2 + 10, UI_H - 82, 140, 50, fl, s_confirm <= -2 ? UI_BTN_DANGER : UI_BTN_SECONDARY, false);
    }
    ui_background_band(false, 0, TOP - 16);
    ui_shade(0, 0, UI_W, TOP - 16, 1);
    ui_status_bar();
    const char *title = strcmp(s_path, MOUNT) ? strrchr(s_path, '/') + 1 : TR("Archivos", "Files");
    int sc = ui_fit(&ui_jersey, 4, 2, UI_W - 120, title);
    ui_print_c(&ui_jersey, sc, UI_W / 2, 64 + (4 - sc) * 8, title, MS_ORO, true);
    return true;
}

static int hit(int y)
{
    int i = (y - TOP + s_scroll.offset) / ROW;
    return y >= TOP && y < UI_H - BAR && i >= 0 && i < s_n ? i : -1;
}

static void event(const ui_event_t *e)
{
    if (!s_card && g_app.sd_state == SD_BAD) {  // format an unreadable card: two confirmations
        if (e->type != EV_TAP || e->y < UI_H - 110) return;
        if (s_confirm == -3) {
            strcpy(s_msg, TR("formateando...", "formatting..."));
            xSemaphoreTake(s_lock, portMAX_DELAY);
            g_app.sd_state = mount(true);  // format_if_mount_failed: FatFs makes an exFAT volume
            xSemaphoreGive(s_lock);
            strcpy(s_msg, s_card ? TR("tarjeta formateada", "card formatted") : TR("no se pudo formatear", "format failed"));
            s_confirm = -1;
            if (s_card) load();
        } else {
            s_confirm = s_confirm == -2 ? -3 : -2;
        }
        sys_dirty();
        return;
    }
    if (!s_card) return;
    ui_scroll_event(&s_scroll, e);
    if (e->type == EV_TAP && e->y >= UI_H - BAR) {
        if (e->x < UI_W / 2) {  // USB drive mode: a restart, so one confirmation
            if (s_confirm == -4) {
                files_close();
                boot_mode_switch(BOOT_USB);  // does not return
            }
            s_confirm = -4;
            strcpy(s_msg, TR("el reloj se reinicia como disco USB", "the watch restarts as a USB drive"));
            sys_dirty();
            return;
        }
        if (s_confirm == -3) {
            strcpy(s_msg, esp_vfs_fat_sdcard_format(MOUNT, s_card) == ESP_OK ? TR("tarjeta formateada", "card formatted")
                                                                            : TR("no se pudo formatear", "format failed"));
            strcpy(s_path, MOUNT);
            load();
            s_confirm = -1;
        } else {
            s_confirm = s_confirm == -2 ? -3 : -2;
        }
    } else if (e->type == EV_TAP) {
        int i = hit(e->y);
        if (i < 0) return;
        if (i == s_confirm) {  // delete confirmed
            char full[sizeof(s_path) + sizeof(s_ent[0].name) + 2];
            snprintf(full, sizeof(full), "%s/%s", s_path, s_ent[i].name);
            strcpy(s_msg, (s_ent[i].dir ? rmdir(full) : unlink(full)) == 0 ? TR("borrado", "deleted") : TR("no se pudo borrar", "could not delete"));
            s_confirm = -1;
            load();
        } else if (s_ent[i].dir) {
            if (!strcmp(s_ent[i].name, "..")) *strrchr(s_path, '/') = 0;
            else if (strlen(s_path) + strlen(s_ent[i].name) + 2 < sizeof(s_path)) {
                strcat(s_path, "/");
                strcat(s_path, s_ent[i].name);
            }
            s_confirm = -1;
            s_msg[0] = 0;
            load();
        } else {
            s_confirm = -1;
        }
    } else if (e->type == EV_LONG) {
        int i = hit(e->y);
        if (i >= 0 && strcmp(s_ent[i].name, "..")) s_confirm = i;
    }
    sys_dirty();
}

const app_t app_files = {"Archivos", "Files", icon_files, files_open, draw, event, files_close, NULL};
