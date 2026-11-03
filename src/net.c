#include "net.h"
#include <stdio.h>
#include <string.h>
#include "app.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "wifi_config.h"

static const char *TAG = "net";
static esp_netif_t *s_sta_netif;

#define MAX_NETS 20
static wifi_net_t s_nets[MAX_NETS];
static int s_nnets;
static uint32_t s_scan_stamp;
static volatile bool s_scanning;
static portMUX_TYPE s_scan_mux = portMUX_INITIALIZER_UNLOCKED;

void net_wifi_scan_request(void)
{
    if (s_scanning) return;
    wifi_scan_config_t cfg = {
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time.active = {.min = 30, .max = 60},  // short dwell: keeps the link to the router smooth
    };
    if (esp_wifi_scan_start(&cfg, false) == ESP_OK) s_scanning = true;
}

int net_wifi_scan_get(wifi_net_t *out, int max, uint32_t *stamp)
{
    taskENTER_CRITICAL(&s_scan_mux);
    int n = s_nnets < max ? s_nnets : max;
    memcpy(out, s_nets, n * sizeof(wifi_net_t));
    *stamp = s_scan_stamp;
    taskEXIT_CRITICAL(&s_scan_mux);
    return n;
}

// Mesh networks: a node can associate and never hand out an address. The DHCP watchdog notes that
// node and a scan picks the strongest other node of the same network; the link is pinned to it.
static uint8_t s_bad_bssid[6];
static volatile bool s_avoid;  // the next scan chooses the node to join
static bool s_pinned;          // the STA config names one node (bssid_set)

static void connect_to(const uint8_t *bssid)
{
    wifi_config_t sta;
    esp_wifi_get_config(WIFI_IF_STA, &sta);
    sta.sta.bssid_set = bssid != NULL;
    if (bssid) memcpy(sta.sta.bssid, bssid, 6);
    s_pinned = bssid != NULL;
    esp_wifi_set_config(WIFI_IF_STA, &sta);
    esp_wifi_connect();
}

static void scan_done(void)
{
    static wifi_ap_record_t recs[MAX_NETS];
    uint16_t n = MAX_NETS;
    if (esp_wifi_scan_get_ap_records(&n, recs) != ESP_OK) n = 0;
    taskENTER_CRITICAL(&s_scan_mux);
    for (int i = 0; i < n; i++) {
        strlcpy(s_nets[i].ssid, (const char *)recs[i].ssid, sizeof(s_nets[i].ssid));
        s_nets[i].rssi = recs[i].rssi;
        s_nets[i].ch = recs[i].primary;
        s_nets[i].mine = g_app.sta_ip[0] && strcmp(s_nets[i].ssid, g_app.sta_ssid) == 0;
    }
    s_nnets = n;
    s_scan_stamp++;
    taskEXIT_CRITICAL(&s_scan_mux);
    s_scanning = false;
    if (!s_avoid) return;
    s_avoid = false;
    const wifi_ap_record_t *best = NULL;
    for (int i = 0; i < n; i++)
        if (strcmp((const char *)recs[i].ssid, g_app.sta_ssid) == 0 && memcmp(recs[i].bssid, s_bad_bssid, 6) != 0 &&
            (!best || recs[i].rssi > best->rssi))
            best = &recs[i];
    if (best) ESP_LOGI(TAG, "Trying another node: " MACSTR " (ch %d, %d dBm)", MAC2STR(best->bssid), best->primary, best->rssi);
    else ESP_LOGW(TAG, "No other node of %s in range", g_app.sta_ssid);
    connect_to(best ? best->bssid : NULL);
}

// Mesh networks: pick the strongest node of the network, not the first one the scan finds
static void sta_prefs(wifi_config_t *sta)
{
    sta->sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    sta->sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
}

// DHCP watchdog: some mesh nodes associate but never hand out an address. If that lasts too long,
// drop the link; the disconnect handler reconnects (possibly through another node).
#define DHCP_TIMEOUT_US (20 * 1000000LL)
static volatile int64_t s_assoc_since;  // 0 = not associated, or already has an IP

static void dhcp_watchdog(void *arg)
{
    int64_t since = s_assoc_since;
    if (since && esp_timer_get_time() - since > DHCP_TIMEOUT_US) {
        ESP_LOGW(TAG, "No IP from the router after 20 s, reconnecting");
        s_assoc_since = 0;
        wifi_ap_record_t ap;
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) memcpy(s_bad_bssid, ap.bssid, 6);
        s_avoid = true;
        esp_wifi_disconnect();
    }
}

static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_SCAN_DONE) {
        scan_done();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_CONNECTED) {
        s_assoc_since = esp_timer_get_time();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_assoc_since = 0;
        g_app.sta_ip[0] = 0;
        if (s_avoid) {  // the watchdog dropped a node without DHCP: look for another one
            if (!s_scanning && esp_wifi_scan_start(NULL, false) == ESP_OK) s_scanning = true;
            if (!s_scanning) s_avoid = false, esp_wifi_connect();
        } else if (s_pinned) {
            connect_to(NULL);  // the chosen node went away: any node again
        } else {
            esp_wifi_connect();
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = data;
        // Route internet traffic (DNS, NTP) through the home network, not the board's own AP
        esp_netif_set_default_netif(s_sta_netif);
        // Use public DNS: some routers (like this one) refuse DNS queries from LAN clients
        esp_netif_dns_info_t dns = {.ip = {.type = ESP_IPADDR_TYPE_V4, .u_addr.ip4.addr = ESP_IP4TOADDR(1, 1, 1, 1)}};
        esp_netif_set_dns_info(s_sta_netif, ESP_NETIF_DNS_MAIN, &dns);
        dns.ip.u_addr.ip4.addr = ESP_IP4TOADDR(8, 8, 8, 8);
        esp_netif_set_dns_info(s_sta_netif, ESP_NETIF_DNS_BACKUP, &dns);
        s_assoc_since = 0;
        snprintf(g_app.sta_ip, sizeof(g_app.sta_ip), IPSTR, IP2STR(&e->ip_info.ip));
        ESP_LOGI(TAG, "Connected to %s, open http://%s", g_app.sta_ssid, g_app.sta_ip);
    }
}

void net_start(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();
    s_sta_netif = esp_netif_create_default_wifi_sta();
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_wifi_event, NULL));

    wifi_config_t sta = {0};
    if (strlen(STA_SSID) > 0) {
        strlcpy((char *)sta.sta.ssid, STA_SSID, sizeof(sta.sta.ssid));
        strlcpy((char *)sta.sta.password, STA_PASS, sizeof(sta.sta.password));
    } else {
        esp_wifi_get_config(WIFI_IF_STA, &sta);  // network remembered in NVS, if any
    }
    bool use_sta = sta.sta.ssid[0] != 0;
    sta_prefs(&sta);
    strlcpy(g_app.sta_ssid, (char *)sta.sta.ssid, sizeof(g_app.sta_ssid));

    wifi_config_t ap = {
        .ap = {
            .ssid = AP_SSID,
            .ssid_len = strlen(AP_SSID),
            .password = AP_PASS,
            .channel = 1,
            .max_connection = 4,
            .authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(use_sta ? WIFI_MODE_APSTA : WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap));
    if (use_sta) ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta));
    ESP_ERROR_CHECK(esp_wifi_start());
    esp_wifi_set_ps(WIFI_PS_NONE);  // modem sleep adds hundreds of ms of latency to the touch control
    strlcpy(g_app.ap_ip, "192.168.4.1", sizeof(g_app.ap_ip));
    static esp_timer_handle_t wd;
    const esp_timer_create_args_t wd_args = {.callback = dhcp_watchdog, .name = "dhcp_wd"};
    if (esp_timer_create(&wd_args, &wd) == ESP_OK) esp_timer_start_periodic(wd, 5 * 1000000);
    ESP_LOGI(TAG, "AP '%s' up at http://%s%s%s", AP_SSID, g_app.ap_ip, use_sta ? ", joining " : "",
             use_sta ? g_app.sta_ssid : "");
}

void net_set_sta(const char *ssid, const char *pass)
{
    wifi_config_t sta = {0};
    strlcpy((char *)sta.sta.ssid, ssid, sizeof(sta.sta.ssid));
    strlcpy((char *)sta.sta.password, pass, sizeof(sta.sta.password));
    sta_prefs(&sta);
    strlcpy(g_app.sta_ssid, ssid, sizeof(g_app.sta_ssid));
    g_app.sta_ip[0] = 0;
    s_pinned = false;
    esp_wifi_set_mode(WIFI_MODE_APSTA);
    esp_wifi_disconnect();
    esp_wifi_set_config(WIFI_IF_STA, &sta);
    esp_wifi_connect();
    ESP_LOGI(TAG, "Joining %s", ssid);
}
