// "WiFi" (full screen, wide canvas, portrait): the time, big and clear (hours over minutes), over a faint live spectrum of the Wi-Fi networks
// around. Each network is a translucent bell on its channel, as tall as its signal is strong and
// tinted by a hash of its name; bells grow, fade and breathe as scans come in.
#include <stdio.h>
#include <string.h>
#include "app.h"
#include "control.h"
#include "esp_timer.h"
#include "fx.h"
#include "gfx.h"
#include "net.h"

#define W WW
#define H WH
#define BASE (H - 62)  // above the rounded bottom corners
#define CHX(ch) (22 + ((ch) - 1) * (W - 44) / 12)
#define MAXT 20
#define SCAN_EVERY_MS 12000

typedef struct {
    uint32_t key;      // hash of the SSID
    char ssid[12];     // shortened label
    int ch;
    float cur, target; // bell height in px
    uint8_t hue;
    bool mine;
} track_t;

static track_t s_tr[MAXT];
static uint32_t s_stamp, s_last_scan;
static int s_count;

void wifimon_enter(void)
{
    s_last_scan = 0;
}

static uint32_t str_hash(const char *s)
{
    uint32_t h = 2166136261u;
    while (*s) h = (h ^ (uint8_t)*s++) * 16777619u;
    return h;
}

static void merge_scan(void)
{
    wifi_net_t nets[20];
    uint32_t stamp;
    int n = net_wifi_scan_get(nets, 20, &stamp);
    if (stamp == s_stamp) return;
    s_stamp = stamp;
    s_count = n;
    for (int i = 0; i < MAXT; i++) s_tr[i].target = 0;  // anything not seen again fades out
    for (int k = 0; k < n; k++) {
        const char *name = nets[k].ssid[0] ? nets[k].ssid : "(oculta)";
        uint32_t key = str_hash(name) ^ nets[k].ch;
        int slot = -1, free_slot = -1;
        for (int i = 0; i < MAXT; i++) {
            if (s_tr[i].key == key && (s_tr[i].cur > 0.5f || s_tr[i].target > 0)) slot = i;
            if (free_slot < 0 && s_tr[i].cur < 0.5f && s_tr[i].target == 0) free_slot = i;
        }
        if (slot < 0) slot = free_slot;
        if (slot < 0) continue;
        track_t *tr = &s_tr[slot];
        if (tr->key != key) {
            tr->key = key;
            tr->cur = 0;
            strlcpy(tr->ssid, name, sizeof(tr->ssid));
        }
        tr->ch = nets[k].ch < 1 ? 1 : nets[k].ch > 13 ? 13 : nets[k].ch;
        int rssi = nets[k].rssi < -95 ? -95 : nets[k].rssi > -30 ? -30 : nets[k].rssi;
        tr->target = (rssi + 95) * 170 / 65.0f;
        tr->hue = key >> 24;
        tr->mine = nets[k].mine;
    }
}

void wifimon_render(uint16_t *fb, uint32_t t, uint32_t dt)
{
    int dx_, dy_;
    app_take_drag(&dx_, &dy_);
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    if (s_last_scan == 0 || now - s_last_scan > SCAN_EVERY_MS) {
        net_wifi_scan_request();
        s_last_scan = now;
    }
    merge_scan();

    for (int y = 0; y < H; y++) gfx_fill_rect(fb, 0, y, W, 1, rgb565(4, 7 + y / 24, 20 + y / 10));
    uint16_t dim = rgb565(40, 56, 90);
    for (int ch = 1; ch <= 13; ch++) {  // channel axis
        int x = CHX(ch);
        for (int y = 60; y < BASE; y += 6) fb[y * W + x] = rgb565(14, 22, 44);
        char lab[4];
        snprintf(lab, sizeof(lab), "%d", ch);
        ui_text(fb, &ui_s, x - ui_width(&ui_s, lab) / 2, BASE + 1, lab, dim, 0, FONT_FILL);
    }
    gfx_fill_rect(fb, 0, BASE, W, 1, dim);

    // Bells: translucent fill, brighter rim; heights ease towards the latest scan and breathe
    float k = dt / 500.0f;
    for (int i = 0; i < MAXT; i++) {
        track_t *tr = &s_tr[i];
        tr->cur += (tr->target - tr->cur) * (k > 1 ? 1 : k);
        if (tr->cur < 0.5f) continue;
        int cx = CHX(tr->ch), hw = 36;
        int hh = (int)(tr->cur * (0.94f + 0.06f * sin_q14[(t / 4 + i * 97) & 1023] / 16384.0f));
        uint16_t col = hsv565(tr->hue, tr->mine ? 120 : 170, tr->mine ? 255 : 210);
        for (int dx = -hw; dx <= hw; dx++) {
            int x = cx + dx;
            if ((unsigned)x >= W) continue;
            int h = hh * (hw * hw - dx * dx) / (hw * hw);
            h = h * (hw * hw - dx * dx) / (hw * hw) / 2 + h / 2;  // softer shoulders
            if (h <= 0) continue;
            for (int y = BASE - h; y < BASE; y++) fb[y * W + x] = gfx_blend(fb[y * W + x], col, 3);
            fb[(BASE - h) * W + x] = gfx_blend(fb[(BASE - h) * W + x], col, tr->mine ? 14 : 10);
            fb[(BASE - h + 1) * W + x] = gfx_blend(fb[(BASE - h + 1) * W + x], col, 5);
        }
    }
    // Labels on the four strongest bells, kept faint
    bool labelled[MAXT] = {false};
    for (int n = 0; n < 4; n++) {
        int best = -1;
        for (int i = 0; i < MAXT; i++)
            if (s_tr[i].cur >= 8 && !labelled[i] && (best < 0 || s_tr[i].cur > s_tr[best].cur)) best = i;
        if (best < 0) break;
        labelled[best] = true;
        track_t *tr = &s_tr[best];
        int cx = CHX(tr->ch);
        uint16_t c = gfx_blend(rgb565(10, 16, 36), hsv565(tr->hue, 120, 255), tr->mine ? 12 : 7);
        ui_text(fb, &ui_s, cx - ui_width(&ui_s, tr->ssid) / 2, BASE - (int)tr->cur - 13, tr->ssid, c, 0, FONT_FILL);
    }

    // The time, front and center
    clock_draw_stacked(fb, &font_round_xl, 22, 6, rgb565(240, 246, 255), rgb565(4, 8, 24), FONT_FILL | FONT_OUTLINE);
    char info[40], date[24];
    clock_date_str(date);
    snprintf(info, sizeof(info), "%d redes", s_count);
    ui_text(fb, &ui_m, (W - ui_width(&ui_m, date)) / 2, BASE + 20, date, rgb565(170, 190, 230), rgb565(4, 8, 24),
            FONT_FILL | FONT_OUTLINE);
    ui_text(fb, &ui_s, (W - ui_width(&ui_s, info)) / 2, BASE + 40, info, rgb565(170, 190, 230), rgb565(4, 8, 24),
            FONT_FILL | FONT_OUTLINE);
}
