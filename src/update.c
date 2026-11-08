#include "update.h"
#include "apps.h"
#include "ui.h"
#include "board.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "app.h"
#include "bootmode.h"
#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gfx.h"
#include "lcd.h"
#include "mbedtls/sha256.h"
#include "net.h"
#include "nvs.h"
#include "ota.h"

static const char *TAG = "update";

#define WIFI_LIMIT_MS 30000            // joining the network (a mesh node without DHCP costs 20 s)
#define DOWNLOAD_LIMIT_US (300 * 1000000LL)  // the whole image; beyond that the old firmware stays
#define URL_MAX 200
#define CHUNK 4096

// ---- result in NVS ("clock" namespace) ----

static void put_str(const char *key, const char *v)
{
    nvs_handle_t h;
    if (nvs_open("clock", NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_str(h, key, v);
    nvs_commit(h);
    nvs_close(h);
}

static void get_str(const char *key, char *out, size_t size)
{
    nvs_handle_t h;
    out[0] = 0;
    if (nvs_open("clock", NVS_READONLY, &h) != ESP_OK) return;
    size_t n = size;
    if (nvs_get_str(h, key, out, &n) != ESP_OK) out[0] = 0;
    nvs_close(h);
}

static int32_t next_attempt(void)
{
    nvs_handle_t h;
    int32_t at = 0;
    if (nvs_open("clock", NVS_READWRITE, &h) != ESP_OK) return 0;
    nvs_get_i32(h, "upd_at", &at);
    nvs_set_i32(h, "upd_at", ++at);
    nvs_commit(h);
    nvs_close(h);
    return at;
}

size_t update_result_json(char *buf, size_t size)
{
    char state[16], from[32], to[32];
    int32_t at = 0;
    get_str("upd_st", state, sizeof(state));
    get_str("upd_from", from, sizeof(from));
    get_str("upd_to", to, sizeof(to));
    nvs_handle_t h;
    if (nvs_open("clock", NVS_READONLY, &h) == ESP_OK) {
        nvs_get_i32(h, "upd_at", &at);
        nvs_close(h);
    }
    int n = snprintf(buf, size, "{\"state\":\"%s\",\"at\":%ld,\"from\":\"%s\",\"to\":\"%s\"}", state[0] ? state : "none",
                     (long)at, from, to);
    return n > 0 && (size_t)n < size ? (size_t)n : 0;
}

void update_record(const char *state, const char *from, const char *to, bool new_attempt)
{
    if (new_attempt) next_attempt();
    if (from) put_str("upd_from", from);
    if (to) put_str("upd_to", to);
    put_str("upd_st", state);
}

void update_boot_check(void)
{
    char state[16];
    get_str("upd_st", state, sizeof(state));
    if (strcmp(state, "running") == 0) {
        ESP_LOGW(TAG, "The last update was cut short (reset or power loss); the firmware did not change");
        put_str("upd_st", "interrupted");
    }
}

// ---- request ----

static bool url_allowed(const char *u)
{
    if (strncmp(u, "https://", 8) == 0) return true;
    if (strncmp(u, "http://", 7) != 0) return false;
    unsigned a, b;  // plain http only inside the home network (the test server on the PC)
    if (sscanf(u + 7, "%u.%u.", &a, &b) != 2) return false;
    return a == 10 || (a == 192 && b == 168) || (a == 172 && b >= 16 && b <= 31);
}

bool update_request(const char *url, const char **msg)
{
    if (strlen(url) >= URL_MAX) return *msg = "La dirección es demasiado larga", false;
    if (!url_allowed(url)) return *msg = "Solo se admiten direcciones https://", false;
    char ssid[33];
    if (!net_has_sta_config(ssid, sizeof(ssid))) return *msg = "El reloj no tiene ninguna red WiFi guardada", false;
    put_str("upd_url", url);
    return true;
}

// ---- UPDATE mode ----

// Full screen in the Marea Solsticial style: night sea background, gold title, a panel with the step
// and, while downloading, a progress bar. line1 is the title, line2 the step.
static void screen(const char *line1, const char *line2, int percent)
{
    ui_begin(lcd_native_fb());
    ui_background(false);
    ui_veil(90, UI_H, 2);
    ui_print_c(&ui_jersey, 4, UI_W / 2, 112, line1, MS_ORO, true);
    ui_panel(48, 196, UI_W - 96, percent >= 0 ? 150 : 110, MS_PANEL, MS_LINEA, 3, true);
    int sc = ui_fit(&ui_vt, 2, 1, UI_W - 130, line2);
    ui_print_c(&ui_vt, sc, UI_W / 2, 220 + (2 - sc) * 8, line2, MS_CREMA, false);
    if (percent >= 0) {
        char pct[16];
        snprintf(pct, sizeof(pct), "%d %%", percent);
        ui_bar(78, 282, UI_W - 156, 18, percent * 10, MS_ORO);
        ui_print_c(&ui_jersey, 2, UI_W / 2, 306, pct, MS_ORO2, false);
    }
    ui_print_c(&ui_vt, 1, UI_W / 2, 400, TR("no apagues el reloj", "do not switch the watch off"), MS_GRIS, false);
    lcd_flush_native();
}

static void finish(const char *state, const char *to, const char *line2)
{
    put_str("upd_st", state);
    if (to) put_str("upd_to", to);
    ESP_LOGI(TAG, "Result: %s%s%s", state, to ? " -> " : "", to ? to : "");
    screen(strcmp(state, "updated") == 0 ? TR("Actualizado", "Updated") : strcmp(state, "up_to_date") == 0 ? TR("Al día", "Up to date")
                                                                                                         : TR("Sin cambios", "No change"),
           line2, -1);
    vTaskDelay(pdMS_TO_TICKS(2500));
    boot_mode_switch(boot_mode_return());  // a new image boots in "pending verify" (rollback protected)
}

static esp_http_client_handle_t open_url(const char *url, int *status, int64_t *length)
{
    esp_http_client_config_t cfg = {
        .url = url,
        .timeout_ms = 15000,
        .buffer_size = 2048,      // GitHub's response headers are long
        .buffer_size_tx = 1024,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .keep_alive_enable = false,
    };
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) return NULL;
    if (esp_http_client_open(c, 0) != ESP_OK) {
        esp_http_client_cleanup(c);
        return NULL;
    }
    *length = esp_http_client_fetch_headers(c);
    *status = esp_http_client_get_status_code(c);
    return c;
}

static void close_url(esp_http_client_handle_t c)
{
    esp_http_client_close(c);
    esp_http_client_cleanup(c);
}

void update_run(void)
{
    // Buffers from the heap, not static: static RAM would be lost in every other mode too
    char *url = malloc(URL_MAX), *body = malloc(4096), *img_url = malloc(URL_MAX);
    uint8_t *chunk = malloc(CHUNK);
    char version[32], sha_hex[65];
    if (!url || !body || !img_url || !chunk) finish("no_internet", NULL, "sin memoria");
    const char *running = esp_app_get_description()->version;
    ESP_LOGI(TAG, "Update mode, free heap %u", (unsigned)heap_caps_get_free_size(MALLOC_CAP_DEFAULT));
    get_str("upd_url", url, URL_MAX);
    put_str("upd_url", "");  // one attempt per request
    next_attempt();
    put_str("upd_from", running);
    put_str("upd_to", "");
    if (!url[0]) finish("bad_manifest", NULL, "no hay manifiesto");
    put_str("upd_st", "running");

    screen(TR("Actualizar", "Update"), TR("conectando al WiFi", "connecting to Wi-Fi"), -1);
    net_start();
    for (int t = 0; !g_app.sta_ip[0]; t += 250) {
        if (t >= WIFI_LIMIT_MS) finish("no_wifi", NULL, "sin WiFi");
        vTaskDelay(pdMS_TO_TICKS(250));
    }

    // Manifest
    screen(TR("Actualizar", "Update"), TR("buscando versión nueva", "looking for a new version"), -1);
    ESP_LOGI(TAG, "Manifest %s", url);
    int status = 0;
    int64_t len = 0;
    esp_http_client_handle_t c = open_url(url, &status, &len);
    if (!c) finish("no_internet", NULL, "sin internet");
    // Clocks up to 3.0.0 read at most 1023 bytes: tools/release.py keeps the manifest under that
    int n = status == 200 ? esp_http_client_read_response(c, body, 4095) : -1;
    close_url(c);
    if (n <= 0) finish("bad_manifest", NULL, "manifiesto no valido");
    body[n] = 0;
    cJSON *m = cJSON_Parse(body);
    // This watch's entry is boards.<id>; the top-level keys are the old ESP32-C6 clocks' image. A
    // manifest without our entry has nothing for us: never try another board's image.
    const cJSON *e = cJSON_GetObjectItem(cJSON_GetObjectItem(m, "boards"), BOARD_ID);
    if (m && !e) {
        cJSON_Delete(m);
        finish("up_to_date", running, "no hay version para esta placa");
    }
    const cJSON *jv = cJSON_GetObjectItem(e, "version"), *ju = cJSON_GetObjectItem(e, "url");
    const cJSON *js = cJSON_GetObjectItem(e, "size"), *jh = cJSON_GetObjectItem(e, "sha256");
    bool ok = cJSON_IsString(jv) && cJSON_IsString(ju) && cJSON_IsNumber(js) && cJSON_IsString(jh) &&
              strlen(jh->valuestring) == 64 && strlen(ju->valuestring) < URL_MAX && strlen(jv->valuestring) < sizeof(version);
    size_t size = 0;
    if (ok) {
        strlcpy(version, jv->valuestring, sizeof(version));
        strlcpy(img_url, ju->valuestring, URL_MAX);
        strlcpy(sha_hex, jh->valuestring, sizeof(sha_hex));
        size = (size_t)js->valuedouble;
    }
    cJSON_Delete(m);
    if (!ok || !url_allowed(img_url)) finish("bad_manifest", NULL, "manifiesto no valido");
    ESP_LOGI(TAG, "Offered %s (%u bytes), running %s", version, (unsigned)size, running);
    if (strcmp(version, running) == 0) finish("up_to_date", version, running);
    if (size < 1024 || size > ota_max_size()) finish("bad_image", version, "tamano no valido");

    // Image: streamed into the other slot, hashed on the way
    char line[72];
    snprintf(line, sizeof(line), "%s -> %s", running, version);
    screen(TR("Actualizando", "Updating"), line, 0);
    c = open_url(img_url, &status, &len);
    if (!c) finish("no_internet", version, "sin internet");
    if (status != 200) {
        close_url(c);
        finish("bad_image", version, "no se pudo descargar");
    }
    if (ota_begin(size) != ESP_OK) {
        close_url(c);
        finish("bad_image", version, "no se pudo preparar");
    }
    mbedtls_sha256_context sha;
    mbedtls_sha256_init(&sha);
    mbedtls_sha256_starts(&sha, 0);
    size_t got = 0;
    int last_pct = -1;
    const char *msg = NULL, *state = NULL;
    int64_t deadline = esp_timer_get_time() + DOWNLOAD_LIMIT_US;
    while (!state) {
        if (esp_timer_get_time() > deadline) {
            state = "timeout";
            break;
        }
        int r = esp_http_client_read(c, (char *)chunk, CHUNK);
        if (r < 0) state = "no_internet";
        if (r <= 0) break;
        if (got + r > size) {
            state = "bad_image";  // longer than the manifest says
            break;
        }
        mbedtls_sha256_update(&sha, chunk, r);
        if (ota_write(chunk, r, &msg) != ESP_OK) {
            state = "bad_image";  // e.g. an image of another project (ota.c checks before writing)
            break;
        }
        got += r;
        int pct = (int)(got * 100 / size);
        if (pct != last_pct) {
            last_pct = pct;
            screen(TR("Actualizando", "Updating"), line, pct);
        }
    }
    close_url(c);
    uint8_t digest[32];
    mbedtls_sha256_finish(&sha, digest);
    mbedtls_sha256_free(&sha);
    if (!state && got != size) state = "bad_image";  // cut short
    if (!state) {
        char hex[65];
        for (int i = 0; i < 32; i++) sprintf(hex + 2 * i, "%02x", digest[i]);
        if (strcasecmp(hex, sha_hex) != 0) state = "bad_image";
    }
    if (state) {
        if (msg) ESP_LOGW(TAG, "%s", msg);
        ESP_LOGW(TAG, "Rejected after %u of %u bytes: %s", (unsigned)got, (unsigned)size, state);
        ota_abort();
        finish(state, version, "se queda la version actual");
    }
    if (ota_finish(&msg) != ESP_OK) {
        ESP_LOGW(TAG, "%s", msg ? msg : "ota_finish failed");
        finish("bad_image", version, "se queda la version actual");
    }
    finish("updated", version, line);
}
