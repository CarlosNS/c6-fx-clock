// HTTP transport: the control page, its WebSocket, the status / catalogue JSON, screenshots and
// OTA uploads. All decisions live in control.c / ota.c so other transports can reuse them.
#include "web.h"
#include "alarm.h"
#include "batlog.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "app.h"
#include "control.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "fx.h"
#include "lcd.h"
#include "net.h"
#include "ota.h"
#include "web_assets.h"
#include "esp_heap_caps.h"

static const char *TAG = "web";
extern const char index_html_start[] asm("_binary_index_html_start");
extern const char transport_js_start[] asm("_binary_transport_js_start");  // shared with the Android app

static esp_err_t index_get(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, index_html_start, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t transport_get(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/javascript; charset=utf-8");
    return httpd_resp_send(req, transport_js_start, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t send_json(httpd_req_t *req, const char *json)
{
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t status_get(httpd_req_t *req)
{
    char buf[1024];
    app_status_json(buf, sizeof(buf));
    return send_json(req, buf);
}

static void emit_chunk(const char *piece, void *ctx) { httpd_resp_send_chunk((httpd_req_t *)ctx, piece, HTTPD_RESP_USE_STRLEN); }

// GET /alarms -> the alarms with their labels and animations (alarm.c)
static esp_err_t alarms_get(httpd_req_t *req)
{
    char buf[1100];
    size_t n = alarms_json(buf, sizeof(buf));
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, buf, n);
}

// web.pack (tools/web_pack.py): "MPK1", u32 count, {u8 name length, name, u32 offset, u32 size}..., data
extern const uint8_t web_pack_start[] asm("_binary_web_pack_start");
extern const uint8_t web_pack_end[] asm("_binary_web_pack_end");

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

const uint8_t *web_asset_find(const char *name, size_t len, size_t *size)
{
    const uint8_t *p = web_pack_start, *end = web_pack_end;
    if (end - p < 8 || memcmp(p, "MPK1", 4)) return NULL;
    uint32_t count = rd32(p + 4);
    p += 8;
    for (uint32_t i = 0; i < count && p < end; i++) {
        size_t n = *p++;
        if (p + n + 8 > end) return NULL;
        uint32_t off = rd32(p + n), sz = rd32(p + n + 4);
        if (n == len && !memcmp(p, name, n) && off + sz <= (size_t)(end - web_pack_start)) {
            *size = sz;
            return web_pack_start + off;
        }
        p += n + 8;
    }
    return NULL;
}

// GET /a/<name> -> a picture or font of the page (src/web/)
static esp_err_t asset_get(httpd_req_t *req)
{
    const char *name = req->uri + 3;
    size_t len = strcspn(name, "?#"), size = 0;
    const uint8_t *data = web_asset_find(name, len, &size);
    if (!data) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "no such file");
        return ESP_FAIL;
    }
    const char *ext = memrchr(name, '.', len);
    size_t el = ext ? len - (ext - name) : 0;
    httpd_resp_set_type(req, el == 5 && !memcmp(ext, ".webp", 5) ? "image/webp"
                             : el == 4 && !memcmp(ext, ".png", 4) ? "image/png"
                             : el == 4 && !memcmp(ext, ".ttf", 4) ? "font/ttf" : "application/octet-stream");
    httpd_resp_set_hdr(req, "Cache-Control", "max-age=86400");
    return httpd_resp_send(req, (const char *)data, size);
}

// %XX and + in a query value (httpd_query_key_value leaves them as they came)
static void url_decode(char *s)
{
    char *o = s;
    for (; *s; s++) {
        if (*s == '%' && s[1] && s[2]) {
            char hex[3] = {s[1], s[2], 0};
            *o++ = (char)strtol(hex, NULL, 16);
            s += 2;
        } else {
            *o++ = *s == '+' ? ' ' : *s;
        }
    }
    *o = 0;
}

// GET /files?p=/Música -> one folder of the microSD (app_files.c, sd_list_json)
static esp_err_t files_get(httpd_req_t *req)
{
    char q[320] = "", p[256] = "/";
    httpd_req_get_url_query_str(req, q, sizeof(q));
    if (httpd_query_key_value(q, "p", p, sizeof(p)) == ESP_OK) url_decode(p);
    const size_t cap = 32 * 1024;
    char *buf = heap_caps_malloc(cap, MALLOC_CAP_SPIRAM);
    if (!buf) return httpd_resp_send_500(req);
    size_t n = sd_list_json(p, buf, cap);
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t err = httpd_resp_send(req, buf, n);
    free(buf);
    return err;
}

// GET /faces -> the face catalogue (control.c, app_faces_json), streamed in pieces
// Battery log (batlog.c) as CSV
static esp_err_t batlog_get(httpd_req_t *req)
{
    static char buf[192 * 32 + 64];
    size_t n = batlog_csv(buf, sizeof(buf));
    httpd_resp_set_type(req, "text/csv");
    return httpd_resp_send(req, buf, n);
}

static esp_err_t faces_get(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    app_faces_json(emit_chunk, req);
    return httpd_resp_send_chunk(req, NULL, 0);
}

static int query_int(const char *q, const char *key, int def)
{
    char v[12];
    return httpd_query_key_value(q, key, v, sizeof(v)) == ESP_OK ? atoi(v) : def;
}

// GET /screenshot[?fx=N&h=H&m=M&wait=MS] -> BMP of the screen (optionally switching face / faking the time first)
static esp_err_t screenshot_get(httpd_req_t *req)
{
    char q[64] = "";
    httpd_req_get_url_query_str(req, q, sizeof(q));
    int fx = query_int(q, "fx", -1), h = query_int(q, "h", -1), m = query_int(q, "m", 0);
    int wait = query_int(q, "wait", fx >= 0 || h >= 0 ? 1500 : 0);
    if (h >= 0) {
        g_app.dbg_m = m;
        g_app.dbg_h = h;
    }
    if (fx >= 0 && fx < FX_COUNT) g_app.fx = fx;
    app_wake();  // rendering is paused while the screen sleeps
    if (wait < 300) wait = 300;
    vTaskDelay(pdMS_TO_TICKS(wait));

    g_app.freeze_req = true;
    for (int i = 0; i < 100 && !g_app.frozen; i++) vTaskDelay(pdMS_TO_TICKS(5));
    int w, hh;
    const uint16_t *fb = lcd_last_frame(&w, &hh);

    uint8_t hdr[66] = {'B', 'M'};
    uint32_t data = w * hh * 2;
#define PUT32(off, v) do { uint32_t _v = (v); memcpy(hdr + (off), &_v, 4); } while (0)
    PUT32(2, 66 + data);
    PUT32(10, 66);
    PUT32(14, 40);
    PUT32(18, w);
    PUT32(22, (uint32_t)-hh);  // top-down
    hdr[26] = 1;
    hdr[28] = 16;
    PUT32(30, 3);  // BI_BITFIELDS
    PUT32(34, data);
    PUT32(54, 0xF800);
    PUT32(58, 0x07E0);
    PUT32(62, 0x001F);
#undef PUT32
    httpd_resp_set_type(req, "image/bmp");
    esp_err_t err = httpd_resp_send_chunk(req, (const char *)hdr, sizeof(hdr));
    static uint16_t row[LCD_PW];  // static: keeps the server task stack small
    for (int y = 0; y < hh && err == ESP_OK; y++) {
        for (int x = 0; x < w; x++) {
            uint16_t p = fb[y * w + x];
            row[x] = (uint16_t)((p >> 8) | (p << 8));
        }
        err = httpd_resp_send_chunk(req, (const char *)row, w * 2);
    }
    g_app.freeze_req = false;
    g_app.dbg_h = -1;
    httpd_resp_send_chunk(req, NULL, 0);
    return err;
}

static void restart_cb(void *arg) { esp_restart(); }

static esp_err_t ota_fail(httpd_req_t *req, const char *msg)
{
    httpd_resp_set_status(req, "400 Bad Request");
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    return httpd_resp_send(req, msg, HTTPD_RESP_USE_STRLEN);
}

// POST /ota with the raw app image (firmware.bin) as the body
static esp_err_t ota_post(httpd_req_t *req)
{
    static char buf[1024];  // static: one update at a time, keeps the server task stack small
    size_t left = req->content_len;
    esp_err_t err = ota_begin(left);
    if (err == ESP_ERR_INVALID_STATE) return ota_fail(req, "Ya hay una actualización en curso");
    if (err == ESP_ERR_INVALID_SIZE) return ota_fail(req, "El archivo no cabe en la memoria del reloj");
    if (err != ESP_OK) return ota_fail(req, "No se pudo empezar la actualización");
    const char *msg = NULL;
    int timeouts = 0;
    while (left > 0) {
        // Fill the whole buffer so the first chunk always holds the image header to check
        size_t want = left < sizeof(buf) ? left : sizeof(buf), got = 0;
        while (got < want) {
            int n = httpd_req_recv(req, buf + got, want - got);
            if (n == HTTPD_SOCK_ERR_TIMEOUT && ++timeouts < 5) continue;
            if (n <= 0) {
                ota_abort();
                ESP_LOGW(TAG, "OTA upload interrupted");
                return ESP_FAIL;
            }
            got += n;
        }
        if (ota_write(buf, got, &msg) != ESP_OK) return ota_fail(req, msg);
        left -= got;
        app_wake();  // keep the progress visible on the screen
    }
    if (ota_finish(&msg) != ESP_OK) return ota_fail(req, msg);
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    httpd_resp_send(req, "ok", HTTPD_RESP_USE_STRLEN);
    // Restart once the response has gone out
    static esp_timer_handle_t t;
    const esp_timer_create_args_t args = {.callback = restart_cb, .name = "ota_restart"};
    if (!t) esp_timer_create(&args, &t);
    esp_timer_start_once(t, 1500 * 1000);
    return ESP_OK;
}

static esp_err_t ws_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) return ESP_OK;  // handshake
    uint8_t buf[128];
    httpd_ws_frame_t f = {.type = HTTPD_WS_TYPE_TEXT};
    esp_err_t err = httpd_ws_recv_frame(req, &f, 0);
    if (err != ESP_OK) return err;
    if (f.len >= sizeof(buf)) return ESP_FAIL;
    f.payload = buf;
    if (f.len > 0) {
        err = httpd_ws_recv_frame(req, &f, f.len);
        if (err != ESP_OK) return err;
    }
    buf[f.len] = 0;
    if (f.type == HTTPD_WS_TYPE_TEXT) app_command((char *)buf);
    return ESP_OK;
}

void web_start(void)
{
    net_start();

    httpd_handle_t server;
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.lru_purge_enable = true;
    cfg.max_uri_handlers = 16;  // the default 8 silently dropped the last ones (the /ws commands) once /alarms came
    cfg.uri_match_fn = httpd_uri_match_wildcard;  // /a/* (the page's pictures and fonts)
    cfg.stack_size = 6144;
    cfg.recv_wait_timeout = 10;  // seconds; OTA uploads over a weak link
    ESP_ERROR_CHECK(httpd_start(&server, &cfg));

    const httpd_uri_t uris[] = {
        {.uri = "/", .method = HTTP_GET, .handler = index_get},
        {.uri = "/transport.js", .method = HTTP_GET, .handler = transport_get},
        {.uri = "/status", .method = HTTP_GET, .handler = status_get},
        {.uri = "/faces", .method = HTTP_GET, .handler = faces_get},
        {.uri = "/alarms", .method = HTTP_GET, .handler = alarms_get},
        {.uri = "/files", .method = HTTP_GET, .handler = files_get},
        {.uri = "/a/*", .method = HTTP_GET, .handler = asset_get},
        {.uri = "/batlog", .method = HTTP_GET, .handler = batlog_get},
        {.uri = "/screenshot", .method = HTTP_GET, .handler = screenshot_get},
        {.uri = "/ota", .method = HTTP_POST, .handler = ota_post},
        {.uri = "/ws", .method = HTTP_GET, .handler = ws_handler, .is_websocket = true},
    };
    for (int i = 0; i < sizeof(uris) / sizeof(uris[0]); i++)
        if (httpd_register_uri_handler(server, &uris[i]) != ESP_OK) ESP_LOGE("web", "could not register %s", uris[i].uri);
}
