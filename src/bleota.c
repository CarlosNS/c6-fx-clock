// Firmware update over Bluetooth (see bleota.h). The host task only checks the offset and copies each
// packet into a stream buffer in PSRAM; a writer task hashes the bytes and writes them to flash in 4 KB
// pieces (ota.c), acknowledging as it goes so the phone never sends more than the buffer holds.
#include "bleota.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "apps.h"
#include "ble.h"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/stream_buffer.h"
#include "freertos/task.h"
#include "host/ble_att.h"
#include "mbedtls/sha256.h"
#include "ota.h"
#include "sys.h"
#include "ui_icons.h"
#include "update.h"

static const char *TAG = "bleota";

#define BUF_BYTES (64 * 1024)   // stream buffer between the host task and the writer
#define WINDOW (48 * 1024)      // what the phone may send past the last ack
#define ACK_EVERY (16 * 1024)
#define STAGE 4096              // flash write size
#define IDLE_LIMIT_US (20 * 1000000LL)  // no data this long: give up

typedef enum { ST_IDLE, ST_RECV, ST_DONE, ST_FAILED } state_t;

static volatile state_t s_state = ST_IDLE;
static volatile int s_cancel;  // 1 asked by the phone, 2 link lost
static StreamBufferHandle_t s_sb;
static size_t s_size;
static volatile size_t s_rx, s_written;  // bytes received from the phone / written to flash
static char s_sha[65], s_version[32], s_from[32], s_fail[64];

static void send_state(const char *st, const char *extra)
{
    char buf[128];
    snprintf(buf, sizeof(buf), "{\"t\":\"ota\",\"st\":\"%s\"%s}", st, extra ? extra : "");
    ble_send_json(buf);
}

static void send_err(const char *msg)
{
    char extra[96];
    snprintf(extra, sizeof(extra), ",\"msg\":\"%s\"", msg);
    send_state("err", extra);
}

// ---------------------------------------------------------------- screen (an alert while it runs)

static int s_shown_pct = -2;
static state_t s_shown_state;

static void ota_open(void) { s_shown_pct = -2; }

static bool ota_draw(uint32_t now, bool force)
{
    state_t st = s_state;
    int pct = s_size ? (int)((uint64_t)s_written * 100 / s_size) : 0;
    if (!force && pct == s_shown_pct && st == s_shown_state) return false;
    s_shown_pct = pct;
    s_shown_state = st;
    ui_background(false);
    ui_veil(90, UI_H, 2);
    const char *title = st == ST_DONE ? TR("Actualizado", "Updated") : st == ST_FAILED ? TR("Sin cambios", "No change") : TR("Actualizando", "Updating");
    ui_print_c(&ui_jersey, 4, UI_W / 2, 112, title, MS_ORO, true);
    ui_panel(48, 196, UI_W - 96, 150, MS_PANEL, MS_LINEA, 3, true);
    char line[72];
    if (st == ST_FAILED) strlcpy(line, s_fail, sizeof(line));
    else snprintf(line, sizeof(line), "%s -> %s", s_from, s_version);
    int sc = ui_fit(&ui_vt, 2, 1, UI_W - 130, line);
    ui_print_c(&ui_vt, sc, UI_W / 2, 220 + (2 - sc) * 8, line, MS_CREMA, false);
    if (st != ST_FAILED) {
        char p[16];
        snprintf(p, sizeof(p), "%d %%", pct);
        ui_bar(78, 282, UI_W - 156, 18, pct * 10, MS_ORO);
        ui_print_c(&ui_jersey, 2, UI_W / 2, 306, p, MS_ORO2, false);
    } else {
        ui_print_c(&ui_vt, 1, UI_W / 2, 296, TR("sigue la versión actual", "the current version stays"), MS_GRIS, false);
    }
    ui_print_c(&ui_vt, 1, UI_W / 2, 380, TR("por Bluetooth desde el móvil", "over Bluetooth from the phone"), MS_GRIS, false);
    if (st == ST_RECV) ui_print_c(&ui_vt, 1, UI_W / 2, 404, TR("no apagues el reloj", "do not switch the watch off"), MS_GRIS, false);
    return true;
}

static void ota_event(const ui_event_t *e) {}
static bool ota_keep_on(void) { return true; }

static const app_t app_ota = {"Actualizar", "Update", icon_settings, ota_open, ota_draw, ota_event, NULL, ota_keep_on};

// ---------------------------------------------------------------- writer

static void fail(const char *state, const char *why)
{
    ESP_LOGW(TAG, "Failed after %u of %u bytes: %s", (unsigned)s_written, (unsigned)s_size, why);
    ota_abort();
    strlcpy(s_fail, why, sizeof(s_fail));
    s_state = ST_FAILED;
    update_record(state, NULL, NULL, false);
    send_err(why);
    sys_dirty();
    vTaskDelay(pdMS_TO_TICKS(2500));
    sys_alert_done();
}

static void writer(void *arg)
{
    uint8_t *stage = heap_caps_malloc(STAGE, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    mbedtls_sha256_context sha;
    mbedtls_sha256_init(&sha);
    mbedtls_sha256_starts(&sha, 0);
    size_t fill = 0, last_ack = 0;
    int64_t last_data = esp_timer_get_time();
    const char *msg = NULL;
    bool ok = stage != NULL;
    if (!ok) fail("bad_image", "sin memoria");
    while (ok && s_written < s_size) {
        if (s_cancel) {
            fail(s_cancel == 2 ? "ble_lost" : "cancelled", s_cancel == 2 ? "se cortó el Bluetooth" : "cancelada en el móvil");
            ok = false;
            break;
        }
        size_t want = STAGE - fill;
        if (want > s_size - s_written - fill) want = s_size - s_written - fill;
        size_t n = xStreamBufferReceive(s_sb, stage + fill, want, pdMS_TO_TICKS(500));
        int64_t now = esp_timer_get_time();
        if (!n) {
            if (now - last_data > IDLE_LIMIT_US) {
                fail("timeout", "el móvil dejó de enviar");
                ok = false;
            }
            continue;
        }
        last_data = now;
        fill += n;
        if (fill < STAGE && s_written + fill < s_size) continue;
        mbedtls_sha256_update(&sha, stage, fill);
        if (ota_write(stage, fill, &msg) != ESP_OK) {  // ota.c aborts on error
            fail("bad_image", msg ? msg : "error al escribir");
            ok = false;
            break;
        }
        s_written += fill;
        fill = 0;
        sys_dirty();
        if (s_written - last_ack >= ACK_EVERY || s_written == s_size) {
            last_ack = s_written;
            char extra[32];
            snprintf(extra, sizeof(extra), ",\"off\":%u", (unsigned)s_written);
            send_state("ack", extra);
        }
    }
    if (ok) {
        uint8_t digest[32];
        char hex[65];
        mbedtls_sha256_finish(&sha, digest);
        for (int i = 0; i < 32; i++) sprintf(hex + 2 * i, "%02x", digest[i]);
        if (strcasecmp(hex, s_sha) != 0) fail("bad_image", "la imagen llegó dañada");
        else if (ota_finish(&msg) != ESP_OK) fail("bad_image", msg ? msg : "no se pudo activar");
        else {
            ESP_LOGI(TAG, "Image verified, rebooting into %s", s_version);
            update_record("updated", NULL, NULL, false);
            s_state = ST_DONE;
            send_state("done", NULL);
            sys_dirty();
            vTaskDelay(pdMS_TO_TICKS(2000));
            esp_restart();
        }
    }
    mbedtls_sha256_free(&sha);
    free(stage);
    vStreamBufferDeleteWithCaps(s_sb);
    s_sb = NULL;
    s_state = ST_IDLE;
    vTaskDelete(NULL);
}

// ---------------------------------------------------------------- entry points

static void begin(const char *args)
{
    unsigned size = 0;
    char sha[65] = "", ver[32] = "";
    if (s_state != ST_IDLE) return send_err("ya hay una actualización en curso");
    if (sscanf(args, "%u %64s %31s", &size, sha, ver) != 3 || strlen(sha) != 64) return send_err("orden no válida");
    if (size < 1024 || size > ota_max_size()) return send_err("tamaño no válido");
    if (strcmp(ver, esp_app_get_description()->version) == 0) return send_err("el reloj ya tiene esa versión");
    s_sb = xStreamBufferCreateWithCaps(BUF_BYTES, 1, MALLOC_CAP_SPIRAM);
    if (!s_sb) return send_err("sin memoria");
    if (ota_begin(size) != ESP_OK) {
        vStreamBufferDeleteWithCaps(s_sb);
        s_sb = NULL;
        return send_err("no se pudo preparar la memoria");
    }
    s_size = size;
    s_rx = s_written = 0;
    s_cancel = 0;
    strlcpy(s_sha, sha, sizeof(s_sha));
    strlcpy(s_version, ver, sizeof(s_version));
    strlcpy(s_from, esp_app_get_description()->version, sizeof(s_from));
    update_record("running", s_from, s_version, true);
    s_state = ST_RECV;
    if (xTaskCreate(writer, "bleota", 4096, NULL, 5, NULL) != pdPASS) {
        ota_abort();
        vStreamBufferDeleteWithCaps(s_sb);
        s_sb = NULL;
        s_state = ST_IDLE;
        update_record("bad_image", NULL, NULL, false);
        return send_err("sin memoria");
    }
    ESP_LOGI(TAG, "Receiving %s (%u bytes)", ver, size);
    ble_fast_link();
    sys_alert(&app_ota);
    char extra[32];
    snprintf(extra, sizeof(extra), ",\"win\":%d", WINDOW);
    send_state("ready", extra);
}

bool bleota_command(const char *cmd)
{
    if (strncmp(cmd, "ota begin ", 10) == 0) {
        begin(cmd + 10);
        return true;
    }
    if (strcmp(cmd, "ota abort") == 0) {
        if (s_state == ST_RECV) s_cancel = 1;
        return true;
    }
    return false;
}

int bleota_data(const uint8_t *p, size_t n)
{
    if (s_state != ST_RECV || !s_sb || n < 4) return BLE_ATT_ERR_UNLIKELY;
    uint32_t off = p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24;
    n -= 4;
    // Out of order, past the end or more than the window: the phone broke the protocol, stop
    if (off != s_rx || s_rx + n > s_size || xStreamBufferSend(s_sb, p + 4, n, 0) != n) {
        ESP_LOGW(TAG, "Bad packet at %u (expected %u, %u bytes)", (unsigned)off, (unsigned)s_rx, (unsigned)n);
        s_cancel = 1;
        return BLE_ATT_ERR_UNLIKELY;
    }
    s_rx += n;
    return 0;
}

void bleota_link_lost(void)
{
    if (s_state == ST_RECV) s_cancel = 2;
}
