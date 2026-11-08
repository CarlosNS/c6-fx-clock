#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Wi-Fi: the board's own access point plus, optionally, the home network (station)
void net_start(void);
// Wi-Fi power save follows the screen: light with it on, deep (one beacon a second) with it off
void net_power(bool screen_on);
// The watch's own access point is up (only without a reachable home network)
bool net_ap_on(void);
// Joins a new home network; the Wi-Fi driver stores it in NVS so it survives reboots
void net_set_sta(const char *ssid, const char *pass);
// Whether a home network is stored (any mode, Wi-Fi need not be running); copies its name
bool net_has_sta_config(char *ssid, size_t size);
// Puts the stored network's name in g_app.sta_ssid (modes without Wi-Fi, for the status)
void net_load_saved_ssid(void);

// Background scan of the networks around (used by the "WiFi" face)
typedef struct {
    char ssid[33];
    int8_t rssi;
    uint8_t ch;
    uint8_t mine;  // the network the board is connected to
} wifi_net_t;
void net_wifi_scan_request(void);  // starts a non-blocking scan if none is running
// Copies the latest results; returns how many. `stamp` increases with every completed scan.
int net_wifi_scan_get(wifi_net_t *out, int max, uint32_t *stamp);
