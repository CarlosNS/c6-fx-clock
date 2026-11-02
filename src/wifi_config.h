#pragma once

// The board always creates its own access point, so the demo works with no setup.
#define AP_SSID "C6-FX-Demo"
#define AP_PASS "esp32c6fx"

// Optional: also join your home network. Leave empty to reuse the network the board already
// remembers in NVS (e.g. from a previous firmware); fill in to override it.
#define STA_SSID ""
#define STA_PASS ""
