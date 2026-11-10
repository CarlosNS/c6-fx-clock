#pragma once
#include <stdbool.h>
#include <stddef.h>
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
    volatile int onix_hue;     // "Onix" face: accent colour, hue in degrees (0..359)
    volatile int raise_wake;   // raising the wrist turns the screen on
    volatile int tap_wake;     // touching the dark screen turns it on: 0 never, 1 one tap, 2 a double tap
    volatile int lang;         // 0 = Spanish, 1 = English
    volatile int dnd;          // do not disturb: alarms and calls show silently
    volatile int wifi_on;      // the watch's own Wi-Fi (off by default: the phone brings everything)
    volatile int sd_state;     // microSD for the status bar: SD_NONE, SD_OK, SD_BAD (app_files.c sd_check)
    volatile bool usb_data;    // the USB cable goes to a computer (data), not just a charger
    volatile bool screen_off;  // the panel is asleep (main.c): Bluetooth slows its link then
    volatile int volume;       // speaker volume 0..100 (alarms, sounds)
    volatile int step_goal;    // daily steps goal

    // ---- Live state ----
    volatile bool touch;       // finger currently on the phone pad
    volatile int tx, ty;       // finger position on the pad, 0..1000
    volatile int bri_override; // brightness forced by an app (flashlight), -1 = none
    volatile bool phone_linked;  // the phone app is connected over Bluetooth
    volatile int bat_pct;        // battery level 0..100, -1 unknown (refreshed every few seconds)
    volatile bool bat_chg;       // plugged in: charging, or full on USB (the bolt shows)
    volatile bool bat_full;      // plugged in and no longer charging
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
enum { SD_NONE, SD_OK, SD_BAD };  // g_app.sd_state

void app_wake(void);
// Checks the microSD in the background (app_files.c) and updates g_app.sd_state
void sd_check(void);
// One folder of the microSD as JSON, for the app's file viewer (GET /files?p=, "get files <path>"):
// {"st":"ok","path":"/Música","free":B,"total":B,"list":[{"n":"Olas.mp3","d":0,"s":5200000,"m":epoch}]}.
// st is "none" (no card), "bad" (unreadable) or "nodir". path is from the card's root ("/" = root).
size_t sd_list_json(const char *path, char *buf, size_t size);
// Battery empty (main.c): warning screen, settings saved, power cut. really = false: screen only
void power_off_now(bool really);
// Console "sleeptest": light sleep allowed on USB until this time (ms), to measure it
extern volatile uint32_t g_sleep_test_until;
