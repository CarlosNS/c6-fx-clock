// PHASE 0 SPIKE (throwaway, branch spike/ble-medida): minimal NimBLE peripheral to measure what
// Bluetooth costs on the C6. Advertises "C6 Clock XXXX", a write characteristic that feeds
// app_command() and a read characteristic with heap / render numbers.
#include <stdio.h>
#include <string.h>
#include "app.h"
#include "control.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

static const char *TAG = "ble_spike";
static uint8_t s_addr_type;

// 7f1c000X-6c0c-4e0b-a1c6-0000c6c10c00, little-endian
#define UUID_CLOCK(x) BLE_UUID128_DECLARE(0x00, 0x0c, 0xc1, 0xc6, 0x00, 0x00, 0xc6, 0xa1, 0x0b, 0x4e, 0x0c, 0x6c, x, 0x00, 0x1c, 0x7f)

static unsigned heap_now(void) { return (unsigned)heap_caps_get_free_size(MALLOC_CAP_DEFAULT); }
static unsigned heap_min(void) { return (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_DEFAULT); }

static int chr_access(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        char buf[200];
        uint16_t n = 0;
        if (ble_hs_mbuf_to_flat(ctxt->om, buf, sizeof(buf) - 1, &n) != 0) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        buf[n] = 0;
        app_command(buf);
        return 0;
    }
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        char buf[120];
        int n = snprintf(buf, sizeof(buf), "heap=%u min=%u fps=%d rus=%d rmax=%d fx=%d", heap_now(), heap_min(), g_app.fps,
                         g_app.render_us, g_app.render_max_us, g_app.fx);
        return os_mbuf_append(ctxt->om, buf, n) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    return BLE_ATT_ERR_UNLIKELY;
}

static const struct ble_gatt_svc_def SVCS[] = {
    {.type = BLE_GATT_SVC_TYPE_PRIMARY,
     .uuid = UUID_CLOCK(0x01),
     .characteristics = (struct ble_gatt_chr_def[]){
         {.uuid = UUID_CLOCK(0x02), .access_cb = chr_access, .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP},
         {.uuid = UUID_CLOCK(0x04), .access_cb = chr_access, .flags = BLE_GATT_CHR_F_READ},
         {0}}},
    {0}};

static int gap_event(struct ble_gap_event *ev, void *arg);

static void advertise(void)
{
    struct ble_hs_adv_fields f = {0};
    const char *name = ble_svc_gap_device_name();
    f.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    f.name = (uint8_t *)name;
    f.name_len = strlen(name);
    f.name_is_complete = 1;
    ble_gap_adv_set_fields(&f);
    struct ble_gap_adv_params p = {.conn_mode = BLE_GAP_CONN_MODE_UND, .disc_mode = BLE_GAP_DISC_MODE_GEN,
                                   .itvl_min = BLE_GAP_ADV_ITVL_MS(500), .itvl_max = BLE_GAP_ADV_ITVL_MS(1000)};
    int rc = ble_gap_adv_start(s_addr_type, NULL, BLE_HS_FOREVER, &p, gap_event, NULL);
    ESP_LOGI(TAG, "Advertising as %s (rc=%d), heap %u min %u", name, rc, heap_now(), heap_min());
}

static int gap_event(struct ble_gap_event *ev, void *arg)
{
    switch (ev->type) {
    case BLE_GAP_EVENT_CONNECT:
        ESP_LOGI(TAG, "Connected (status %d), heap %u min %u", ev->connect.status, heap_now(), heap_min());
        if (ev->connect.status != 0) advertise();
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "Disconnected, heap %u min %u", heap_now(), heap_min());
        advertise();
        break;
    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG, "MTU %d", ev->mtu.value);
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
    char name[24];
    snprintf(name, sizeof(name), "C6 Clock %02X%02X", mac[1], mac[0]);
    ble_svc_gap_device_name_set(name);
    advertise();
}

static void host_task(void *arg)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
}

void ble_spike_start(void)
{
    unsigned before = heap_now();
    esp_err_t err = nimble_port_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nimble_port_init failed: %s", esp_err_to_name(err));
        return;
    }
    ESP_LOGI(TAG, "NimBLE controller+host init took %u bytes (heap %u)", before - heap_now(), heap_now());
    ble_hs_cfg.sync_cb = on_sync;
    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_gatts_count_cfg(SVCS);
    ble_gatts_add_svcs(SVCS);
    nimble_port_freertos_init(host_task);
    ESP_LOGI(TAG, "Bluetooth total %u bytes (heap %u)", before - heap_now(), heap_now());
}
