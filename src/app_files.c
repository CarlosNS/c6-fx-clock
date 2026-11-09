// Files: the microSD card (SDMMC, 1 bit, mounted at /sd while the app is open; FAT or exFAT, see
// components/fatfs). Folders first, then files with their size, in alphabetical order; long names are
// cut with "…" and a scroll bar shows where the list is. Tap a folder to enter it, ".." to go up; tap
// a file for a dialog with its whole name, size and Delete; long press deletes (files, empty folders).
// USB restarts the watch as a USB drive for the computer (usbdisk.c) and Format wipes the card as
// exFAT, each after a dialog. sd_check() tells the status bar whether there is a card.
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "apps.h"
#include "board.h"
#include "bootmode.h"
#include "driver/sdmmc_host.h"
#include "esp_attr.h"
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
#define TOP 134
#define ROW 62
#define X0 40
#define WID 330
#define BAR 120  // bottom bar: free space, USB and format buttons

static const char *TAG = "files";
static sdmmc_card_t *s_card;
static char s_path[160] = MOUNT;
EXT_RAM_BSS_ATTR static struct {  // PSRAM: 64 long names
    char name[128];
    bool dir;
    uint32_t size;
} s_ent[MAX_ENT];
static int s_n;
static ui_scroll_t s_scroll;
enum { M_NONE, M_FILE, M_DELETE, M_USB, M_FORMAT };
static int s_modal = M_NONE;  // dialog on screen
static int s_sel = -1;        // entry the dialog is about
static char s_msg[48];

static int by_name(const void *a, const void *b)
{
    const typeof(s_ent[0]) *x = a, *y = b;
    if (x->dir != y->dir) return x->dir ? -1 : 1;
    return strcasecmp(x->name, y->name);
}

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
    int first = strcmp(s_path, MOUNT) ? 1 : 0;
    qsort(&s_ent[first], s_n - first, sizeof(s_ent[0]), by_name);
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

// ---------------------------------------------------------------- the phone's file viewer

static size_t json_str(char *buf, size_t n, size_t size, const char *s)
{
    for (; *s && n + 8 < size; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') buf[n++] = '\\', buf[n++] = c;
        else if (c < 32) n += snprintf(buf + n, size - n, "\\u%04x", c);
        else buf[n++] = c;
    }
    return n;
}

size_t sd_list_json(const char *path, char *buf, size_t size)
{
    if (!path || !*path) path = "/";
    // Only plain paths from the card's root: nothing that climbs out of it
    if (path[0] != '/' || strstr(path, "/..") || strlen(path) > 200) return snprintf(buf, size, "{\"st\":\"nodir\",\"path\":\"/\",\"list\":[]}");
    if (!s_lock) sd_check();
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool mine = false;  // mounted here just for this listing (the Files app was not open)
    int st = SD_OK;
    if (!s_card) {
        st = mount(false);
        mine = st == SD_OK;
        if (st != g_app.sd_state) g_app.sd_state = st, sys_dirty();
    }
    size_t n = snprintf(buf, size, "{\"st\":\"%s\",\"path\":\"", st == SD_OK ? "ok" : st == SD_BAD ? "bad" : "none");
    n = json_str(buf, n, size, path);
    n += snprintf(buf + n, size - n, "\"");
    if (st == SD_OK) {
        uint64_t total = 0, free_b = 0;
        esp_vfs_fat_info(MOUNT, &total, &free_b);
        n += snprintf(buf + n, size - n, ",\"free\":%llu,\"total\":%llu,\"list\":[", (unsigned long long)free_b, (unsigned long long)total);
        static char full[240];  // static: under s_lock, and the Bluetooth worker's stack is small
        snprintf(full, sizeof(full), MOUNT "%s", strcmp(path, "/") ? path : "");
        DIR *d = opendir(full);
        if (!d) {
            n = snprintf(buf, size, "{\"st\":\"nodir\",\"path\":\"/\",\"list\":[]");  // closed below
        } else {
            struct dirent *de;
            bool first = true;
            while ((de = readdir(d)) && n + 400 < size) {
                static char f[sizeof(full) + 260];
                static struct stat s;
                memset(&s, 0, sizeof(s));
                snprintf(f, sizeof(f), "%s/%s", full, de->d_name);
                stat(f, &s);
                bool dir = de->d_type == DT_DIR;
                n += snprintf(buf + n, size - n, "%s{\"n\":\"", first ? "" : ",");
                n = json_str(buf, n, size, de->d_name);
                n += snprintf(buf + n, size - n, "\",\"d\":%d,\"s\":%llu,\"m\":%lld}", dir, dir ? 0ULL : (unsigned long long)s.st_size,
                              (long long)s.st_mtime);
                first = false;
            }
            closedir(d);
            n += snprintf(buf + n, size - n, "]");
        }
    } else {
        n += snprintf(buf + n, size - n, ",\"list\":[]");
    }
    if (n + 2 < size) buf[n++] = '}', buf[n] = 0;
    if (mine) unmount();
    xSemaphoreGive(s_lock);
    return n < size ? n : size - 1;
}

static void files_open(void)
{
    s_modal = M_NONE;
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

// Bottom bar buttons, and the scroll bar on the right edge (straight between the rounded corners)
#define BTN_Y (UI_H - 84)
#define BTN_W 150
#define BTN_H 54
#define SB_X (X0 + WID + 10)

static void draw_modal(void)
{
    char body[200], size[24];
    const char *name = s_sel >= 0 && s_sel < s_n ? s_ent[s_sel].name : "";
    switch (s_modal) {
    case M_FILE:
        size_text(size, sizeof(size), s_ent[s_sel].size);
        snprintf(body, sizeof(body), "%s\n%s", name, size);
        ui_dialog(TR("Archivo", "File"), body, TR("Cerrar", "Close"), TR("Borrar", "Delete"), UI_BTN_DANGER);
        break;
    case M_DELETE:
        snprintf(body, sizeof(body), "%s%s", name, s_ent[s_sel].dir ? TR("\n(solo si está vacía)", "\n(only if empty)") : "");
        ui_dialog(s_ent[s_sel].dir ? TR("¿Borrar la carpeta?", "Delete the folder?") : TR("¿Borrar el archivo?", "Delete the file?"), body,
                  TR("Cancelar", "Cancel"), TR("Borrar", "Delete"), UI_BTN_DANGER);
        break;
    case M_USB:
        ui_dialog(TR("Modo USB", "USB mode"),
                  TR("El reloj se reinicia como disco USB del ordenador. Vuelve con BOOT, al expulsar el disco o al "
                     "quitar el cable.",
                     "The watch restarts as a USB drive for the computer. It comes back with BOOT, on eject or when "
                     "unplugged."),
                  TR("Cancelar", "Cancel"), TR("Reiniciar", "Restart"), UI_BTN_MAIN);
        break;
    case M_FORMAT:
        ui_dialog(TR("¿Formatear?", "Format?"),
                  TR("Se borra todo lo que hay en la tarjeta. Queda en exFAT, que leen ordenadores y móviles.",
                     "Everything on the card is erased. It ends up as exFAT, for computers and phones."),
                  TR("Cancelar", "Cancel"), TR("Formatear", "Format"), UI_BTN_DANGER);
        break;
    default:
        break;
    }
}

static void draw_list(void)
{
    char buf[48], name[sizeof(s_ent[0].name) + 4];
    s_scroll.view_h = UI_H - TOP - BAR;
    s_scroll.content_h = s_n * ROW;
    for (int i = 0; i < s_n; i++) {
        int y = TOP + i * ROW - s_scroll.offset;
        if (y > UI_H - BAR || y + ROW < TOP - 20) continue;
        ui_panel(X0, y, WID, ROW - 8, MS_PANEL, MS_LINEA, 4, false);
        // Name in Jersey (the narrow font); the size beside it when both fit, otherwise the name gets the
        // whole row, cut with "…" if it must be (the dialog a tap opens has the whole name and the size)
        char shown[sizeof(name)];
        strlcpy(name, s_ent[i].name, sizeof(name));
        if (s_ent[i].dir && strcmp(s_ent[i].name, "..")) strlcat(name, "/", sizeof(name));
        int room = WID - 32, name_w = ui_print_w(&ui_jersey, 2, name);
        if (!s_ent[i].dir) {
            size_text(buf, sizeof(buf), s_ent[i].size);
            int size_w = ui_print_w(&ui_vt, 1, buf) + 14;
            if (name_w <= room - size_w) {
                ui_print_r(&ui_vt, 1, X0 + WID - 14, y + 18, buf, MS_GRIS);
                room -= size_w;
            }
        }
        ui_ellipsize(&ui_jersey, 2, room, name, shown, sizeof(shown));
        ui_print(&ui_jersey, 2, X0 + 16, y + 14, shown, s_ent[i].dir ? MS_ORO2 : MS_CREMA);
    }
    if (!s_n) ui_print_c(&ui_vt, 2, UI_W / 2, TOP + 40, TR("vacía", "empty"), MS_GRIS, false);
    // scroll bar: where the visible part sits in the whole list (only when it does not all fit)
    int track_y = TOP, track_h = UI_H - BAR - TOP - 10;
    if (s_scroll.content_h > s_scroll.view_h) {
        int thumb_h = track_h * s_scroll.view_h / s_scroll.content_h;
        if (thumb_h < 24) thumb_h = 24;
        int max = s_scroll.content_h - s_scroll.view_h;
        int thumb_y = track_y + (track_h - thumb_h) * s_scroll.offset / (max > 0 ? max : 1);
        ui_rect(SB_X, track_y, 6, track_h, MS_PANEL);
        ui_rect(SB_X, thumb_y, 6, thumb_h, MS_ORO2);
    }
    // bottom: free space, the USB and format buttons
    uint64_t total = 0, free_b = 0;
    esp_vfs_fat_info(MOUNT, &total, &free_b);
    if (total >= 10ULL << 30)
        snprintf(buf, sizeof(buf), TR("libre %u de %u GB", "free %u of %u GB"), (unsigned)(free_b >> 30), (unsigned)(total >> 30));
    else
        snprintf(buf, sizeof(buf), TR("libre %u MB de %u", "free %u MB of %u"), (unsigned)(free_b >> 20), (unsigned)(total >> 20));
    ui_rect(0, UI_H - BAR, UI_W, BAR, MS_NOCHE);
    ui_print_c(&ui_vt, 1, UI_W / 2, UI_H - BAR + 6, s_msg[0] ? s_msg : buf, s_msg[0] ? MS_ORO2 : MS_GRIS, false);
    ui_button(UI_W / 2 - BTN_W - 6, BTN_Y, BTN_W, BTN_H, "USB", UI_BTN_SECONDARY, false);
    ui_button(UI_W / 2 + 6, BTN_Y, BTN_W, BTN_H, TR("Formatear", "Format"), UI_BTN_SECONDARY, false);
}

static bool draw(uint32_t now, bool force)
{
    if (!force) return false;
    ui_background(false);
    ui_veil(60, UI_H, 3);
    if (!s_card && g_app.sd_state == SD_BAD) {  // the card answers but cannot be read: offer to format it
        ui_panel(48, 150, UI_W - 96, 170, MS_PANEL, MS_CORAL, 4, true);
        ui_print_c(&ui_jersey, 3, UI_W / 2, 176, TR("No se puede leer", "Cannot read it"), MS_CREMA, false);
        ui_print_c(&ui_vt, 1, UI_W / 2, 236, TR("la tarjeta no tiene un formato", "the card has no format"), MS_GRIS, false);
        ui_print_c(&ui_vt, 1, UI_W / 2, 262, TR("que el reloj entienda", "the watch understands"), MS_GRIS, false);
        if (s_msg[0]) ui_print_c(&ui_vt, 1, UI_W / 2, UI_H - 128, s_msg, MS_ORO2, false);
        ui_button(UI_W / 2 - 120, UI_H - 96, 240, 56, TR("Formatear (exFAT)", "Format (exFAT)"), UI_BTN_SECONDARY, false);
    } else if (!s_card) {
        ui_panel(48, 150, UI_W - 96, 170, MS_PANEL, MS_CORAL, 4, true);
        ui_print_c(&ui_jersey, 3, UI_W / 2, 176, TR("Sin tarjeta", "No card"), MS_CREMA, false);
        ui_print_c(&ui_vt, 1, UI_W / 2, 236, TR("mete una microSD", "insert a microSD"), MS_GRIS, false);
        ui_print_c(&ui_vt, 1, UI_W / 2, 266, TR("y vuelve a entrar", "and open again"), MS_GRIS, false);
    } else {
        draw_list();
    }
    ui_background_band(false, 0, TOP - 16);
    ui_shade(0, 0, UI_W, TOP - 16, 1);
    ui_status_bar();
    const char *title = strcmp(s_path, MOUNT) ? strrchr(s_path, '/') + 1 : TR("Archivos", "Files");
    char t[48];
    ui_ellipsize(&ui_jersey, 2, UI_W - 120, title, t, sizeof(t));
    int sc = ui_fit(&ui_jersey, 4, 2, UI_W - 120, t);
    ui_print_c(&ui_jersey, sc, UI_W / 2, 64 + (4 - sc) * 8, t, MS_ORO, true);
    if (s_modal) draw_modal();
    return true;
}

static int hit(int y)
{
    int i = (y - TOP + s_scroll.offset) / ROW;
    return y >= TOP && y < UI_H - BAR && i >= 0 && i < s_n ? i : -1;
}

static void format_card(void)
{
    strcpy(s_msg, TR("formateando...", "formatting..."));
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool ok;
    if (s_card) {
        ok = esp_vfs_fat_sdcard_format(MOUNT, s_card) == ESP_OK;
    } else {
        g_app.sd_state = mount(true);  // an unreadable card: format_if_mount_failed makes an exFAT volume
        ok = s_card != NULL;
    }
    xSemaphoreGive(s_lock);
    strcpy(s_msg, ok ? TR("tarjeta formateada", "card formatted") : TR("no se pudo formatear", "format failed"));
    strcpy(s_path, MOUNT);
    if (s_card) load();
}

static void modal_event(const ui_event_t *e)
{
    if (e->type != EV_TAP) return;
    int b = ui_dialog_hit(e->x, e->y, true);
    if (!b) return;
    int m = s_modal;
    s_modal = M_NONE;
    if (b == 2) {
        if (m == M_FILE) {
            s_modal = M_DELETE;  // a second, explicit step before deleting
        } else if (m == M_DELETE) {
            char full[sizeof(s_path) + sizeof(s_ent[0].name) + 2];
            snprintf(full, sizeof(full), "%s/%s", s_path, s_ent[s_sel].name);
            strcpy(s_msg, (s_ent[s_sel].dir ? rmdir(full) : unlink(full)) == 0 ? TR("borrado", "deleted")
                          : s_ent[s_sel].dir ? TR("la carpeta no está vacía", "the folder is not empty")
                                             : TR("no se pudo borrar", "could not delete"));
            load();
        } else if (m == M_USB) {
            files_close();
            boot_mode_switch(BOOT_USB);  // does not return
        } else if (m == M_FORMAT) {
            format_card();
        }
    }
    sys_dirty();
}

static void event(const ui_event_t *e)
{
    if (s_modal) {
        modal_event(e);
        return;
    }
    if (!s_card && g_app.sd_state == SD_BAD) {  // an unreadable card: the format button
        if (e->type == EV_TAP && e->y >= UI_H - 110) s_modal = M_FORMAT, sys_dirty();
        return;
    }
    if (!s_card) return;
    ui_scroll_event(&s_scroll, e);
    if (e->type == EV_TAP && e->y >= UI_H - BAR) {
        if (e->y >= BTN_Y - 10) s_modal = e->x < UI_W / 2 ? M_USB : M_FORMAT;
    } else if (e->type == EV_TAP) {
        int i = hit(e->y);
        if (i < 0) return;
        if (s_ent[i].dir) {
            if (!strcmp(s_ent[i].name, "..")) *strrchr(s_path, '/') = 0;
            else if (strlen(s_path) + strlen(s_ent[i].name) + 2 < sizeof(s_path)) {
                strcat(s_path, "/");
                strcat(s_path, s_ent[i].name);
            }
            s_msg[0] = 0;
            load();
        } else {
            s_sel = i;
            s_modal = M_FILE;  // the whole name, its size, and delete
        }
    } else if (e->type == EV_LONG) {
        int i = hit(e->y);
        if (i >= 0 && strcmp(s_ent[i].name, "..")) s_sel = i, s_modal = M_DELETE;
    }
    sys_dirty();
}

const app_t app_files = {"Archivos", "Files", icon_files, files_open, draw, event, files_close, NULL};
