// Files: the microSD card (SDMMC, 1 bit, mounted at /sd while the app is open). Folders and files with
// their size; tap a folder to enter it, ".." to go up; long press a file to delete it (a second tap
// confirms). The format button wipes the card after two confirmations; the USB button restarts the
// watch as a USB drive for the computer (usbdisk.c) after one.
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "apps.h"
#include "board.h"
#include "bootmode.h"
#include "driver/sdmmc_host.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
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

static void files_open(void)
{
    s_confirm = -1;
    s_msg[0] = 0;
    strcpy(s_path, MOUNT);
    if (!s_card) {
        esp_vfs_fat_sdmmc_mount_config_t mc = {.format_if_mount_failed = false, .max_files = 4,
                                               .allocation_unit_size = 16 * 1024};
        sdmmc_host_t host = SDMMC_HOST_DEFAULT();
        sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
        slot.width = 1;
        slot.clk = BOARD_SD_CLK;
        slot.cmd = BOARD_SD_CMD;
        slot.d0 = BOARD_SD_D0;
        slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
        esp_err_t err = esp_vfs_fat_sdmmc_mount(MOUNT, &host, &slot, &mc, &s_card);
        if (err != ESP_OK) {
            s_card = NULL;
            ESP_LOGW(TAG, "no card: %s", esp_err_to_name(err));
        }
    }
    if (s_card) load();
}

static void files_close(void)
{
    if (s_card) esp_vfs_fat_sdcard_unmount(MOUNT, s_card);
    s_card = NULL;
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
    if (!s_card) {
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
            if (conf) ui_print_r(&ui_vt, 1, X0 + WID - 14, y + 18, TR("¿borrar?", "delete?"), MS_CORAL);
            else if (!s_ent[i].dir) {
                size_text(buf, sizeof(buf), s_ent[i].size);
                ui_print_r(&ui_vt, 1, X0 + WID - 14, y + 18, buf, MS_GRIS);
            }
        }
        if (!s_n) ui_print_c(&ui_vt, 2, UI_W / 2, TOP + 40, TR("vacía", "empty"), MS_GRIS, false);
        // bottom: free space, the USB and format buttons
        uint64_t total = 0, free_b = 0;
        esp_vfs_fat_info(MOUNT, &total, &free_b);
        snprintf(buf, sizeof(buf), TR("libre %u MB de %u", "free %u MB of %u"), (unsigned)(free_b >> 20), (unsigned)(total >> 20));
        ui_rect(0, UI_H - BAR, UI_W, BAR, MS_NOCHE);
        ui_print_c(&ui_vt, 1, UI_W / 2, UI_H - BAR + 6, s_msg[0] ? s_msg : buf, s_msg[0] ? MS_ORO2 : MS_GRIS, false);
        ui_button(UI_W / 2 - 150, UI_H - 82, 140, 50, s_confirm == -4 ? TR("¿Reiniciar?", "Restart?") : "USB",
                  s_confirm == -4 ? UI_BTN_MAIN : UI_BTN_SECONDARY, false);
        const char *fl = s_confirm == -2 ? TR("¿Seguro?", "Sure?") : s_confirm == -3 ? TR("¡Se borra todo!", "Erase all!") : TR("Formatear", "Format");
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
