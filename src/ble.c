#include "ble.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include "app.h"
#include "bootmode.h"
#include "clock.h"
#include "control.h"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "host/ble_hs.h"
#include "lcd.h"
#include "mbedtls/base64.h"
#include "host/ble_store.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "ota.h"
#include "update.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "nvs.h"
#include "settings.h"

void ble_store_config_init(void);  // NimBLE bond store in NVS (no public header)

static const char *TAG = "ble";

#define CMD_MAX 248             // longest command accepted (MTU 251+)
#define FAST_ADV_MS 30000       // after boot / disconnect: advertise fast so the phone finds it quickly
#define QUIET_BOOT_MS 60000     // paired clock booting without the time: stay silent this long first
#define HEARTBEAT_MS 30000      // status sent unasked this often while connected
#define STATUS_GAP_MS 300       // at most one change notification this often
#define QUEUE_LEN 16
#define FRAME_LAST 0x80
#define FRAME_FIRST 0x40

// 7f1c000X-6c0c-4e0b-a1c6-0000c6c10c00, little-endian
#define UUID_CLOCK(x) BLE_UUID128_DECLARE(0x00, 0x0c, 0xc1, 0xc6, 0x00, 0x00, 0xc6, 0xa1, 0x0b, 0x4e, 0x0c, 0x6c, x, 0x00, 0x1c, 0x7f)
static const ble_uuid128_t SVC_UUID = BLE_UUID128_INIT(0x00, 0x0c, 0xc1, 0xc6, 0x00, 0x00, 0xc6, 0xa1, 0x0b, 0x4e, 0x0c, 0x6c, 0x01,
                                                       0x00, 0x1c, 0x7f);

typedef struct { char text[CMD_MAX + 1]; } cmd_t;

static QueueHandle_t s_queue;
static volatile int s_dropped;  // commands that didn't fit in the queue
static uint8_t s_addr_type;
static volatile uint16_t s_conn = BLE_HS_CONN_HANDLE_NONE;
static volatile bool s_subscribed;
static uint16_t s_evt_handle;
static uint8_t s_msg_id;
static esp_timer_handle_t s_adv_timer, s_quiet_timer;
static char s_name[20];
static uint32_t s_sync_count;
static int64_t s_sync_last;  // epoch seconds

static volatile bool s_running;  // host stack up (BLE boot mode)

bool ble_connected(void) { return s_conn != BLE_HS_CONN_HANDLE_NONE; }

// Paired with the passkey shown on the screen (encrypted and authenticated, protects against a
// man in the middle). Only such links may send commands or receive anything.
static bool link_secure(uint16_t conn)
{
    struct ble_gap_conn_desc d;
    return ble_gap_conn_find(conn, &d) == 0 && d.sec_state.encrypted && d.sec_state.authenticated;
}
const char *ble_name(void) { return s_name; }

// ---------------------------------------------------------------- sending

// Sends one JSON message as a series of notifications (see the frame header in ble.h)
static void send_msg(const char *json, size_t len)
{
    uint16_t conn = s_conn;
    if (conn == BLE_HS_CONN_HANDLE_NONE || !s_subscribed || !link_secure(conn)) return;
    int chunk = ble_att_mtu(conn) - 3 - 1;  // ATT header and our frame header
    if (chunk < 19) chunk = 19;
    uint8_t id = s_msg_id++ & 0x3F;
    size_t off = 0;
    do {
        size_t n = len - off < (size_t)chunk ? len - off : (size_t)chunk;
        uint8_t frame[256];
        frame[0] = id | (off == 0 ? FRAME_FIRST : 0) | (off + n >= len ? FRAME_LAST : 0);
        memcpy(frame + 1, json + off, n);
        // The controller's buffers fill up on long messages: wait and retry
        for (int tries = 0;; tries++) {
            struct os_mbuf *om = ble_hs_mbuf_from_flat(frame, n + 1);
            int rc = om ? ble_gatts_notify_custom(conn, s_evt_handle, om) : BLE_HS_ENOMEM;
            if (rc == 0) break;
            if (s_conn != conn || tries > 200) {
                ESP_LOGW(TAG, "Notify failed (%d), message dropped", rc);
                return;
            }
            vTaskDelay(pdMS_TO_TICKS(5));
        }
        off += n;
    } while (off < len);
}

static void send_str(const char *json) { send_msg(json, strlen(json)); }

static void send_status(void)
{
    char *buf = malloc(1100);
    if (!buf) return;
    // '{"t":"status"' is 13 chars; app_status_json writes "{...}" right after it and its opening
    // brace becomes the separating comma
    memcpy(buf, "{\"t\":\"status\"", 13);
    size_t n = app_status_json(buf + 13, 1100 - 13);
    buf[13] = ',';
    send_msg(buf, 13 + n);
    free(buf);
}

typedef struct { char *buf; size_t len, cap; } collect_t;

static void collect(const char *piece, void *ctx)
{
    collect_t *c = ctx;
    size_t n = strlen(piece);
    if (c->len + n < c->cap) {
        memcpy(c->buf + c->len, piece, n);
        c->len += n;
    }
}

static void send_faces(void)
{
    collect_t c = {.cap = 8192};
    c.buf = malloc(c.cap);
    if (!c.buf) return;
    collect("{\"t\":\"faces\",", &c);
    size_t start = c.len;
    app_faces_json(collect, &c);  // "{...}": drop its opening brace
    memmove(c.buf + start, c.buf + start + 1, c.len - start - 1);
    c.len--;
    send_msg(c.buf, c.len);
    free(c.buf);
}

static void send_info(void)
{
    char buf[400], upd[128];
    if (!update_result_json(upd, sizeof(upd))) strcpy(upd, "{}");
    snprintf(buf, sizeof(buf),
             "{\"t\":\"info\",\"ver\":\"%s\",\"mode\":\"%s\",\"part\":\"%s\",\"name\":\"%s\","
             "\"sync\":{\"last\":%lld,\"count\":%lu},\"heap\":%u,\"upd\":%s}",
             esp_app_get_description()->version, boot_mode_name(boot_mode()), ota_running_label(), s_name,
             (long long)s_sync_last, (unsigned long)s_sync_count, (unsigned)heap_caps_get_free_size(MALLOC_CAP_DEFAULT), upd);
    send_str(buf);
}

static void send_ack(const char *cmd)
{
    char buf[64];
    snprintf(buf, sizeof(buf), "{\"t\":\"ack\",\"cmd\":\"%s\"}", cmd);
    send_str(buf);
}

// Screenshot (debugging without Wi-Fi): {"t":"shot",w,h,fmt} then one {"t":"shotdata","y","d"}
// per row (RGB565 little-endian, base64) and {"t":"shotend"}. Rendering is paused meanwhile so the
// rows all come from the same frame.
static void send_shot(void)
{
    static uint16_t row[LAND_W];
    static char b64[LAND_W * 2 * 4 / 3 + 8], msg[LAND_W * 2 * 4 / 3 + 64];
    app_wake();                       // a sleeping screen renders nothing
    vTaskDelay(pdMS_TO_TICKS(400));   // let a few frames through
    g_app.freeze_req = true;
    for (int i = 0; i < 100 && !g_app.frozen; i++) vTaskDelay(pdMS_TO_TICKS(5));
    int w = g_lcd_w, h = g_lcd_h;
    const uint16_t *fb = lcd_last_frame();
    snprintf(msg, sizeof(msg), "{\"t\":\"shot\",\"w\":%d,\"h\":%d,\"fmt\":\"rgb565le\"}", w, h);
    send_str(msg);
    int64_t t0 = esp_timer_get_time();
    for (int y = 0; y < h && ble_connected(); y++) {
        for (int x = 0; x < w; x++) {
            uint16_t p = fb[y * w + x];
            row[x] = (uint16_t)((p >> 8) | (p << 8));  // the framebuffer is in the panel's byte order
        }
        size_t n = 0;
        mbedtls_base64_encode((unsigned char *)b64, sizeof(b64), &n, (const unsigned char *)row, w * 2);
        b64[n] = 0;
        snprintf(msg, sizeof(msg), "{\"t\":\"shotdata\",\"y\":%d,\"d\":\"%s\"}", y, b64);
        send_str(msg);
    }
    send_str("{\"t\":\"shotend\"}");
    g_app.freeze_req = false;
    ESP_LOGI(TAG, "Screenshot %dx%d sent in %lld ms", w, h, (esp_timer_get_time() - t0) / 1000);
}

// ---------------------------------------------------------------- worker

// Settings fingerprint, to notice changes made from anywhere (button, Bluetooth, console)
static uint32_t settings_hash(void)
{
    char buf[400];
    size_t n = settings_json(buf, sizeof(buf), 0);
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; i++) h = (h ^ (uint8_t)buf[i]) * 16777619u;
    return h;
}

static void worker(void *arg)
{
    static cmd_t cmd;
    uint32_t last_hash = settings_hash();
    int64_t last_status = 0;
    bool pending = false;
    for (;;) {
        if (s_dropped) {
            ESP_LOGW(TAG, "Command queue full: %d commands dropped", s_dropped);
            s_dropped = 0;
        }
        if (xQueueReceive(s_queue, &cmd, pdMS_TO_TICKS(100)) == pdTRUE) {
            const char *c = cmd.text;
            if (strcmp(c, "get info") == 0) send_info();
            else if (strcmp(c, "get status") == 0) send_status(), last_status = esp_timer_get_time();
            else if (strcmp(c, "get faces") == 0) send_faces();
            else if (strcmp(c, "sync done") == 0) {
                struct timeval tv;
                gettimeofday(&tv, NULL);
                s_sync_last = tv.tv_sec;
                s_sync_count++;
                send_ack("sync");
            } else if (strcmp(c, "shot") == 0) {
                send_shot();
            } else if (strcmp(c, "unpair") == 0) {
                send_ack("unpair");
                vTaskDelay(pdMS_TO_TICKS(200));  // let the ack out: forgetting drops the link
                app_command(c);
            } else if (strncmp(c, "update apply ", 13) == 0) {
                const char *msg = "";
                bool ok = update_request(c + 13, &msg);
                char buf[160];
                snprintf(buf, sizeof(buf), "{\"t\":\"ack\",\"cmd\":\"update\",\"ok\":%s,\"msg\":\"%s\"}", ok ? "true" : "false", msg);
                send_str(buf);
                if (ok) {
                    vTaskDelay(pdMS_TO_TICKS(200));  // let the ack go out before the reboot
                    boot_mode_switch(BOOT_UPDATE);
                }
            } else if (strncmp(c, "mode ", 5) == 0) {
                send_ack("mode");
                vTaskDelay(pdMS_TO_TICKS(200));  // let the ack go out before the reboot
                app_command(c);
            } else {
                app_command(c);
            }
        }
        if (!ble_connected() || !s_subscribed) continue;
        // A change is announced once things settle: a slider or a burst of commands produces one
        // status message per STATUS_GAP_MS at most, always with the latest values
        uint32_t h = settings_hash();
        if (h != last_hash) {
            last_hash = h;
            pending = true;
        }
        int64_t now = esp_timer_get_time();
        bool due = pending && now - last_status > STATUS_GAP_MS * 1000LL && !uxQueueMessagesWaiting(s_queue);
        if (due || now - last_status > HEARTBEAT_MS * 1000LL) {
            pending = false;
            last_status = now;
            send_status();
        }
    }
}

// ---------------------------------------------------------------- GATT

static int clock_access(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) return BLE_ATT_ERR_UNLIKELY;
    cmd_t c;
    uint16_t n = 0;
    if (OS_MBUF_PKTLEN(ctxt->om) > CMD_MAX) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    if (ble_hs_mbuf_to_flat(ctxt->om, c.text, CMD_MAX, &n) != 0) return BLE_ATT_ERR_UNLIKELY;
    c.text[n] = 0;
    if (!n) return 0;
    // Never block the Bluetooth host task: if the worker is behind, drop the command (counted and
    // logged by the worker). Bursts are things like a slider, where only the last value matters.
    if (xQueueSend(s_queue, &c, 0) != pdTRUE) s_dropped++;
    return 0;
}

static int fw_rev_access(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    const char *v = esp_app_get_description()->version;
    return os_mbuf_append(ctxt->om, v, strlen(v)) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

static const struct ble_gatt_svc_def SERVICES[] = {
    {.type = BLE_GATT_SVC_TYPE_PRIMARY,
     .uuid = &SVC_UUID.u,
     .characteristics = (struct ble_gatt_chr_def[]){
         {.uuid = UUID_CLOCK(0x02),
          .access_cb = clock_access,
          .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP | BLE_GATT_CHR_F_WRITE_ENC | BLE_GATT_CHR_F_WRITE_AUTHEN},
         {.uuid = UUID_CLOCK(0x03), .access_cb = clock_access, .flags = BLE_GATT_CHR_F_NOTIFY, .val_handle = &s_evt_handle},
         {0}}},
    {.type = BLE_GATT_SVC_TYPE_PRIMARY,
     .uuid = BLE_UUID16_DECLARE(0x180A),  // Device Information
     .characteristics = (struct ble_gatt_chr_def[]){
         {.uuid = BLE_UUID16_DECLARE(0x2A26), .access_cb = fw_rev_access, .flags = BLE_GATT_CHR_F_READ},  // firmware revision
         {0}}},
    {0}};

// ---------------------------------------------------------------- GAP

static int gap_event(struct ble_gap_event *ev, void *arg);

static void advertise(bool fast)
{
    if (ble_connected()) return;
    ble_gap_adv_stop();
    // Advertising packet: flags + the service UUID (so apps can filter); scan response: the name
    struct ble_hs_adv_fields f = {0};
    f.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    f.uuids128 = &SVC_UUID;
    f.num_uuids128 = 1;
    f.uuids128_is_complete = 1;
    // Manufacturer data (test company id 0xFFFF) + a flags byte, bit 0 = the clock has the time.
    // The phone peeks at it when the clock reappears and only connects if the time is missing
    // (e.g. after a quick power cycle that Android never noticed as an absence).
    uint8_t mfg[3] = {0xFF, 0xFF, g_now.valid ? 0x01 : 0x00};
    f.mfg_data = mfg;
    f.mfg_data_len = sizeof(mfg);
    ble_gap_adv_set_fields(&f);
    struct ble_hs_adv_fields r = {0};
    r.name = (uint8_t *)s_name;
    r.name_len = strlen(s_name);
    r.name_is_complete = 1;
    ble_gap_adv_rsp_set_fields(&r);
    struct ble_gap_adv_params p = {.conn_mode = BLE_GAP_CONN_MODE_UND, .disc_mode = BLE_GAP_DISC_MODE_GEN};
    // Slow rate ~420 ms: at 1 s, Android's low-power presence scan (a short window every few
    // seconds) kept missing the clock, reported it lost and found every ~20 s, and each "found"
    // could start a sync.
    p.itvl_min = fast ? BLE_GAP_ADV_ITVL_MS(100) : BLE_GAP_ADV_ITVL_MS(400);
    p.itvl_max = fast ? BLE_GAP_ADV_ITVL_MS(150) : BLE_GAP_ADV_ITVL_MS(440);
    int rc = ble_gap_adv_start(s_addr_type, NULL, BLE_HS_FOREVER, &p, gap_event, NULL);
    if (rc) ESP_LOGW(TAG, "Advertising failed: %d", rc);
    if (fast) esp_timer_start_once(s_adv_timer, FAST_ADV_MS * 1000LL);
}

static void slow_down(void *arg) { advertise(false); }

static int gap_event(struct ble_gap_event *ev, void *arg)
{
    switch (ev->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (ev->connect.status == 0) {
            s_conn = ev->connect.conn_handle;
            esp_timer_stop(s_adv_timer);
            ESP_LOGI(TAG, "Connected, heap %u", (unsigned)heap_caps_get_free_size(MALLOC_CAP_DEFAULT));
            app_wake();
        } else {
            advertise(true);
        }
        break;
    case BLE_GAP_EVENT_PASSKEY_ACTION:
        if (ev->passkey.params.action == BLE_SM_IOACT_DISP) {
            // A random 6-digit code on the screen; the phone types it in
            struct ble_sm_io io = {.action = BLE_SM_IOACT_DISP, .passkey = esp_random() % 1000000};
            g_app.pairing_since = (uint32_t)(esp_timer_get_time() / 1000);
            g_app.pairing_code = (int)io.passkey;
            app_wake();
            ESP_LOGI(TAG, "Pairing, passkey %06lu", (unsigned long)io.passkey);
            ble_sm_inject_io(ev->passkey.conn_handle, &io);
        }
        break;
    case BLE_GAP_EVENT_ENC_CHANGE:
        g_app.pairing_code = -1;
        ESP_LOGI(TAG, "Encryption %s (status %d)", link_secure(ev->enc_change.conn_handle) ? "on, paired" : "failed",
                 ev->enc_change.status);
        break;
    case BLE_GAP_EVENT_REPEAT_PAIRING: {
        // The phone lost its bond but the clock kept it: forget the old one and pair again
        struct ble_gap_conn_desc d;
        if (ble_gap_conn_find(ev->repeat_pairing.conn_handle, &d) == 0) ble_store_util_delete_peer(&d.peer_id_addr);
        return BLE_GAP_REPEAT_PAIRING_RETRY;
    }
    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "Disconnected (reason %d)", ev->disconnect.reason);
        g_app.pairing_code = -1;
        s_conn = BLE_HS_CONN_HANDLE_NONE;
        s_subscribed = false;
        advertise(true);
        break;
    case BLE_GAP_EVENT_SUBSCRIBE:
        if (ev->subscribe.attr_handle == s_evt_handle) s_subscribed = ev->subscribe.cur_notify;
        break;
    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG, "MTU %d", ev->mtu.value);
        break;
    case BLE_GAP_EVENT_ADV_COMPLETE:
        advertise(false);
        break;
    default:
        break;
    }
    return 0;
}

static void on_sync(void)
{
    ble_hs_util_ensure_addr(0);
    ble_hs_id_infer_auto(0, &s_addr_type);
    uint8_t mac[6] = {0};
    ble_hs_id_copy_addr(s_addr_type, mac, NULL);
    snprintf(s_name, sizeof(s_name), "C6 Clock %02X%02X", mac[1], mac[0]);
    ble_svc_gap_device_name_set(s_name);
    // A quick power cycle is too short for the phone to notice the clock was gone, so it would not
    // bring the time until its next periodic sync (Xiaomi phones can't peek at the advertising
    // flag from the background either). A paired clock without the time stays silent a while:
    // Android then reports it lost and found again, and the app syncs at once.
    ble_addr_t peers[1];
    int bonds = 0;
    ble_store_util_bonded_peers(peers, &bonds, 1);
    if (bonds > 0 && !g_now.valid) {
        ESP_LOGI(TAG, "Advertising as %s in %d s (paired, no time yet)", s_name, QUIET_BOOT_MS / 1000);
        esp_timer_start_once(s_quiet_timer, QUIET_BOOT_MS * 1000LL);
        return;
    }
    ESP_LOGI(TAG, "Advertising as %s", s_name);
    advertise(true);
}

static void quiet_over(void *arg) { advertise(true); }

static void host_task(void *arg)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
}

void ble_start(void)
{
    s_queue = xQueueCreate(QUEUE_LEN, sizeof(cmd_t));
    const esp_timer_create_args_t t = {.callback = slow_down, .name = "ble_adv"};
    esp_timer_create(&t, &s_adv_timer);
    const esp_timer_create_args_t q = {.callback = quiet_over, .name = "ble_quiet"};
    esp_timer_create(&q, &s_quiet_timer);
    esp_err_t err = nimble_port_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NimBLE init failed: %s", esp_err_to_name(err));
        return;
    }
    ble_hs_cfg.sync_cb = on_sync;
    // LE Secure Connections with a passkey shown on the screen, bonding (remembered in NVS)
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_DISPLAY_ONLY;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 1;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;  // bond list full: forget the oldest
    ble_store_config_init();
    ble_att_set_preferred_mtu(247);
    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_gatts_count_cfg(SERVICES);
    ble_gatts_add_svcs(SERVICES);
    xTaskCreate(worker, "ble_worker", 4096, NULL, 4, NULL);
    nimble_port_freertos_init(host_task);
    s_running = true;
}

void ble_forget_all(void)
{
    if (s_running) {
        ble_store_clear();
        if (ble_connected()) ble_gap_terminate(s_conn, BLE_ERR_REM_USER_CONN_TERM);
    } else {
        // Bluetooth not running (Wi-Fi mode): wipe the bond store directly
        nvs_handle_t h;
        if (nvs_open("nimble_bond", NVS_READWRITE, &h) == ESP_OK) {
            nvs_erase_all(h);
            nvs_commit(h);
            nvs_close(h);
        }
    }
    ESP_LOGI(TAG, "All paired phones forgotten");
}
