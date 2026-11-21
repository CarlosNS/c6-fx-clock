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
    volatile int fx_off;       // bit i set: face i is skipped by the automatic rotation (faces 0..30)
    volatile int fx_off2;      // the same for faces 31..61 (bit i: face 31 + i)
    volatile int classic_hue;  // "Clasico" face: dial colour, hue in degrees (0..359)
    volatile int onix_hue;     // "Onix" face: accent colour, hue in degrees (0..359)
    // "Dunas" face: palette (0 ice .. 5 silver), animation (0 crests, 1 soft, 2 contour lines), brightness
    // (30..100 %), dot size (0 fine, 1 normal, 2 coarse), time (0 big, 1 small, 2 only after a touch). Its
    // speed is the per-face speed every face has.
    volatile int dune_col, dune_anim, dune_bri, dune_dots, dune_time;
    // "Mariposa" face: attractor (0 Lorenz, 1 Aizawa, 2 Thomas, 3 Halvorsen), palette (0 ice .. 4 silver),
    // time (0 top, 1 bottom, 2 only after a touch), trail (6..40 hundred points), brightness (30..100 %)
    volatile int lz_form, lz_col, lz_time, lz_trail, lz_bri;
    // "Corriente" face: style (0 silk, 1 smoke, 2 river), palette (0 ice .. 4 silver), time (0 woven into the
    // flow, 1 on top, 2 only after a touch), particles (4..30 hundred), turbulence (5..25 tenths), brightness
    volatile int cr_style, cr_col, cr_time, cr_num, cr_turb, cr_bri;
    // "Globos" face: balloon colours (0 mixed, 1 pastel, 2 red, 3 metallic), how many (3..25 tenths), sky
    // (0 by the time of day, 1 always day), pop sound (0/1)
    volatile int bal_col, bal_num, bal_sky, bal_sound;
    volatile int raise_wake;   // raising the wrist turns the screen on
    volatile int setup;        // the first-run guide was seen (or skipped)
    volatile int tips;         // how many times the edge hints were shown on the face (they stop at 8)
    volatile int cust;         // the custom face "Mis esferas" shows (its folder /esferas/<n>), 0 = none
    volatile int voice;        // holding BOOT records a voice command for the phone app (voice.c)
    volatile int tap_wake;     // touching the dark screen turns it on: 0 never, 1 one tap, 2 a double tap
    volatile int lang;         // 0 = Spanish, 1 = English
    volatile int dnd;          // do not disturb: alarms and calls show silently
    volatile int wifi_on;      // the watch's own Wi-Fi (off by default: the phone brings everything)
    volatile int sd_state;     // microSD for the status bar: SD_NONE, SD_OK, SD_BAD (app_files.c sd_check)
    volatile bool usb_data;    // the USB cable goes to a computer (data), not just a charger
    volatile bool screen_off;  // the panel is asleep (main.c): Bluetooth slows its link then
    volatile int volume;       // speaker volume 0..100 (alarms, sounds)
    volatile int steps_on;     // count steps (the accelerometer is read all the time for it)
    volatile int step_goal;    // daily steps goal

    // ---- Live state ----
    volatile bool touch;       // finger currently on the phone pad
    volatile int tx, ty;       // finger position on the pad, 0..1000
    volatile int bri_override; // brightness forced by an app (flashlight), -1 = none
    // Where gravity pulls, smoothed, in mg along the screen: x to the right, y down (faces that
    // react to how the watch is held, like Lluvia). Updated by the main loop from the accelerometer.
    volatile int grav_x, grav_y;
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
// Writes a whole file on the microSD (path from the card's root, its folder made if needed); false on error
bool sd_write_file(const char *path, const void *data, size_t len);
// Keeps the card mounted from now (true) until the matching sd_hold(false): a transfer mounts it before
// it takes memory, while there is still internal RAM for it; false if there is no usable card
bool sd_hold(bool on);
bool sd_append_file(const char *path, const void *data, size_t len);
// Prints a file of the microSD on the console (calibration logs)
bool sd_print_file(const char *path);
// The whole file in PSRAM (one spare byte at the end; free() it), NULL if missing or over max bytes
void *sd_read_file(const char *path, size_t max, size_t *len);
// Names in a folder of the card (files and folders, no dot-names); returns how many
int sd_list_names(const char *path, char names[][16], int max);
// Deletes a folder and the files in it (one level)
bool sd_remove_dir(const char *path);
// Battery empty (main.c): warning screen, settings saved, power cut. really = false: screen only
void power_off_now(bool really);
void pwr_off_test(void);  // the power-off scene (hold and switch-off), without switching off
// Console "sleeptest": light sleep allowed on USB until this time (ms), to measure it
extern volatile uint32_t g_sleep_test_until;
