#pragma once
#include <stdbool.h>
#include <stdint.h>

// Shared state between the render loop (main.c), the control layer (control.c) and the
// transports (web.c; later Bluetooth). Plain ints written by one task and read by another.
typedef struct {
    // ---- User settings (described in settings.c; ints so one table handles them all) ----
    volatile int fx;           // current face index
    volatile int auto_cycle;   // rotate faces every AUTO_CYCLE_MS
    volatile int speed;        // animation speed, percent (10..300)
    volatile int brightness;   // backlight, percent (5..100)
    volatile int overlay;      // debug: draw face name / fps / IP on screen (not saved)
    volatile int h24;          // 24 h clock (0: 12 h)
    volatile int show_secs;    // show seconds on the faces
    volatile int night_on;     // dim the backlight between night_start and night_end
    volatile int night_start, night_end, night_bri;  // hours 0..23, percent
    volatile int always_on;    // 0: screen sleeps until the BOOT button (or the web) wakes it
    volatile int wake_secs;    // how long the screen stays on after the button / web (3..600 s)
    volatile int fx_off;       // bit i set: face i is skipped by the automatic rotation (max 31 faces)
    volatile int classic_hue;  // "Clasico" face: dial colour, hue in degrees (0..359)
    volatile int raise_wake;   // boards with a motion sensor: raising the wrist turns the screen on

    // ---- Live state ----
    volatile bool touch;       // finger currently on the phone pad
    volatile int tx, ty;       // finger position on the pad, 0..1000
    volatile int fps;
    volatile int render_us;    // average render time of the last second (excluding the LCD wait)
    volatile int render_max_us;
    volatile uint32_t wake_until;  // ms timestamp until which the screen stays on
    volatile bool dirty;       // settings changed, save to NVS soon
    volatile bool freeze_req;  // screenshot: ask the render loop to pause...
    volatile bool frozen;      // ...and it confirms here
    volatile int dbg_h, dbg_m; // screenshot: fake time of day (-1 = real time)
    volatile int pairing_code; // Bluetooth passkey being shown on the screen, -1 = none
    volatile uint32_t pairing_since; // ms timestamp when it appeared (the phone has 30 s to type it)

    // ---- Network ----
    char ap_ip[16];
    char sta_ip[16];
    char sta_ssid[33];
} app_state_t;

extern app_state_t g_app;

// Turns the screen on (or keeps it on) for wake_secs
void app_wake(void);
