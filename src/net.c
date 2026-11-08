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
#include "nvs.h"
#include "wifi_config.h"

static const char *TAG = "net";
static esp_netif_t *s_sta_netif;

// Our own copy of the home network ("clock" namespace): the Wi-Fi driver keeps its own, but it can
// only be read or written with Wi-Fi running, and in Bluetooth mode the network must still be
// settable (for updates) and known.
static void saved_put(const char *ssid, const char *pass)
{
    nvs_handle_t h;
    if (nvs_open("clock", NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_str(h, "sta_ssid", ssid);
    nvs_set_str(h, "sta_pass", pass);
    nvs_commit(h);
    nvs_close(h);
}

static bool saved_get(char *ssid, size_t ssid_size, char *pass, size_t pass_size)
{
    nvs_handle_t h;
    if (nvs_open("clock", NVS_READONLY, &h) != ESP_OK) return false;
    size_t a = ssid_size, b = pass_size;
    bool ok = nvs_get_str(h, "sta_ssid", ssid, &a) == ESP_OK && nvs_get_str(h, "sta_pass", pass, &b) == ESP_OK && ssid[0];
    nvs_close(h);
    return ok;
}

bool net_has_sta_config(char *ssid, size_t size)
{
    char pass[65];
    if (strlen(STA_SSID) > 0) {
        strlcpy(ssid, STA_SSID, size);
        return true;
    }
    return saved_get(ssid, size, pass, sizeof(pass));
}

void net_load_saved_ssid(void)
{
    net_has_sta_config(g_app.sta_ssid, sizeof(g_app.sta_ssid));
}

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

// The watch's own access point ("Marea") is only for when it cannot reach a home network: with an AP
// up the radio can never sleep, which emptied the battery in a few hours. It comes up when no network
// is saved, or when the saved one gives no address for AP_FALLBACK_US, and goes away once the home
// network answers and nobody is connected to the AP.
#define AP_FALLBACK_US (60 * 1000000LL)
static bool s_has_sta, s_ap_on;
static esp_timer_handle_t s_ap_timer;
// The Wi-Fi radio only runs when the owner switches it on ("wifi" setting): the phone brings the time,
// the weather and the updates, and a radio looking for a missing network all day was what emptied
// the battery away from home. Lost networks are retried with growing pauses, never in a tight loop.
static bool s_started;
static volatile int64_t s_assoc_since;  // 0 = not associated, or already has an IP (DHCP watchdog)
static esp_timer_handle_t s_retry_timer;
static int s_retry_ms;

static void ap_enable(bool on)
{
    if (on == s_ap_on) return;
    s_ap_on = on;
    esp_wifi_set_mode(on ? (s_has_sta ? WIFI_MODE_APSTA : WIFI_MODE_AP) : WIFI_MODE_STA);
    if (on) {
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
        esp_wifi_set_config(WIFI_IF_AP, &ap);
        strlcpy(g_app.ap_ip, "192.168.4.1", sizeof(g_app.ap_ip));
    } else {
        g_app.ap_ip[0] = 0;
    }
    ESP_LOGI(TAG, "Own AP '%s' %s", AP_SSID, on ? "up at http://192.168.4.1" : "off");
}

static void ap_fallback(void *arg)
{
    if (s_started && !g_app.sta_ip[0]) ap_enable(true);
}

static void retry_now(void *arg)
{
    if (s_started) esp_wifi_connect();
}

// 2 s, 4 s, 8 s... up to a minute between attempts while the network is missing
static void retry_later(void)
{
    s_retry_ms = s_retry_ms ? s_retry_ms * 2 : 2000;
    if (s_retry_ms > 60000) s_retry_ms = 60000;
    esp_timer_stop(s_retry_timer);
    esp_timer_start_once(s_retry_timer, s_retry_ms * 1000LL);
}

// Drop the AP once the home network works, unless someone is using it
static void ap_maybe_off(void)
{
    if (!s_ap_on || !s_has_sta || !g_app.sta_ip[0]) return;
    wifi_sta_list_t list;
    if (esp_wifi_ap_get_sta_list(&list) == ESP_OK && list.num > 0) return;
    ap_enable(false);
}

bool net_ap_on(void) { return s_ap_on; }

void net_power(bool screen_on)
{
    // Screen on: light modem sleep (the page and the phone stay quick). Off: the radio sleeps and
    // wakes for every third beacon (listen interval 3)
    if (s_started) esp_wifi_set_ps(screen_on ? WIFI_PS_MIN_MODEM : WIFI_PS_MAX_MODEM);
}

bool net_enabled(void) { return s_started; }

void net_enable(bool on)
{
    if (!s_sta_netif || on == s_started) return;
    s_started = on;
    if (on) {
        if (s_has_sta) esp_wifi_set_mode(WIFI_MODE_STA);
        else ap_enable(true);  // nothing to join: the own AP is the only way in
        s_retry_ms = 0;
        esp_wifi_start();
        esp_wifi_set_ps(WIFI_PS_MIN_MODEM);  // net_power() switches to the deeper sleep with the screen off
        if (s_has_sta) esp_timer_start_once(s_ap_timer, AP_FALLBACK_US);
    } else {
        esp_timer_stop(s_ap_timer);
        esp_timer_stop(s_retry_timer);
        esp_wifi_stop();
        s_ap_on = false;
        s_assoc_since = 0;
        g_app.ap_ip[0] = 0;
        g_app.sta_ip[0] = 0;
    }
    ESP_LOGI(TAG, "Wi-Fi %s%s%s", on ? "on" : "off", on && s_has_sta ? ", joining " : "", on && s_has_sta ? g_app.sta_ssid : "");
}

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
    sta->sta.listen_interval = 3;  // used by WIFI_PS_MAX_MODEM (screen off): a beacon every 0.3 s
}

// DHCP watchdog: some mesh nodes associate but never hand out an address. If that lasts too long,
// drop the link; the disconnect handler reconnects (possibly through another node).
#define DHCP_TIMEOUT_US (20 * 1000000LL)

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
            retry_later();
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        s_retry_ms = 0;
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
        ap_maybe_off();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_AP_STADISCONNECTED) {
        ap_maybe_off();  // whoever set the network up through the AP has left
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
        esp_wifi_get_config(WIFI_IF_STA, &sta);  // network remembered by the Wi-Fi driver, if any
        char ssid[33], pass[65];
        if (saved_get(ssid, sizeof(ssid), pass, sizeof(pass))) {  // set later, maybe in Bluetooth mode
            strlcpy((char *)sta.sta.ssid, ssid, sizeof(sta.sta.ssid));
            strlcpy((char *)sta.sta.password, pass, sizeof(sta.sta.password));
        } else if (sta.sta.ssid[0]) {
            saved_put((char *)sta.sta.ssid, (char *)sta.sta.password);  // keep a copy for the other modes
        }
    }
    bool use_sta = sta.sta.ssid[0] != 0;
    s_has_sta = use_sta;
    sta_prefs(&sta);
    strlcpy(g_app.sta_ssid, (char *)sta.sta.ssid, sizeof(g_app.sta_ssid));

    if (use_sta) {
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta));
    }
    static esp_timer_handle_t wd;
    const esp_timer_create_args_t wd_args = {.callback = dhcp_watchdog, .name = "dhcp_wd"};
    if (esp_timer_create(&wd_args, &wd) == ESP_OK) esp_timer_start_periodic(wd, 5 * 1000000);
    const esp_timer_create_args_t ap_args = {.callback = ap_fallback, .name = "ap_fallback"};
    esp_timer_create(&ap_args, &s_ap_timer);
    const esp_timer_create_args_t retry_args = {.callback = retry_now, .name = "wifi_retry"};
    esp_timer_create(&retry_args, &s_retry_timer);
    if (g_app.wifi_on) net_enable(true);
    else ESP_LOGI(TAG, "Wi-Fi off (switch it on in the quick settings or the app)");
}

void net_set_sta(const char *ssid, const char *pass)
{
    saved_put(ssid, pass);
    if (!s_sta_netif) {  // Wi-Fi is off in this mode: stored for the next Wi-Fi or update boot
        strlcpy(g_app.sta_ssid, ssid, sizeof(g_app.sta_ssid));
        ESP_LOGI(TAG, "Network %s stored", ssid);
        return;
    }
    wifi_config_t sta = {0};
    strlcpy((char *)sta.sta.ssid, ssid, sizeof(sta.sta.ssid));
    strlcpy((char *)sta.sta.password, pass, sizeof(sta.sta.password));
    sta_prefs(&sta);
    strlcpy(g_app.sta_ssid, ssid, sizeof(g_app.sta_ssid));
    g_app.sta_ip[0] = 0;
    s_pinned = false;
    s_has_sta = true;
    esp_wifi_set_mode(s_ap_on ? WIFI_MODE_APSTA : WIFI_MODE_STA);  // keep the AP if the network is set through it
    esp_wifi_disconnect();
    esp_wifi_set_config(WIFI_IF_STA, &sta);
    s_retry_ms = 0;
    if (!s_started) {  // choosing a network means wanting the Wi-Fi
        g_app.wifi_on = 1;
        g_app.dirty = true;
        net_enable(true);
    } else {
        esp_wifi_connect();
    }
    ESP_LOGI(TAG, "Joining %s", ssid);
}
