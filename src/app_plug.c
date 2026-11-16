// What a USB cable brings, shown for a few seconds when it is plugged in, as an animated scene with no
// words (only the charge figure): a charger or a computer, charging or full. The screen lights up for
// it. It follows the status loop live: the computer is told apart from a charger by its USB frames
// (main.c), which takes a second or two after plugging in, and the scene then changes on its own.
#include "app_plug.h"
#include <math.h>
#include <stdio.h>
#include "alarm.h"
#include "apps.h"
#include "esp_timer.h"
#include "find.h"
#include "sys.h"

#define SHOW_MS PLUG_SCENE_MS

static uint32_t s_t0;
static int s_force;  // plug_test: 1 charger charging, 2 charger full, 3 computer charging, 4 computer full
static uint32_t s_pc_at;  // when the computer was told apart (0: not yet)
static const app_t app_plug;

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

void plug_notice(void)
{
    s_force = 0;
    s_pc_at = 0;
    // An alarm, a call or the find screen matter more
    if (alarm_ringing() || find_active() || sys_keep_awake()) return;
    s_t0 = now_ms();
    app_wake();
    sys_alert(&app_plug);
}

static void p_open(void) {}

void plug_test(int mode)
{
    plug_notice();
    s_force = mode;
}

// ---------------------------------------------------------------- the charger scene (no text)
// The watch's USB port is in the middle of its right side, so the scene's cable comes in right there:
// the plug slides in from the right edge, at the height of the real one, and clicks into a battery
// lying on its side, with a spark. Golden bolts then leave the edge (a glow where the real cable is)
// and run into the battery, which fills like the sea: its level is the charge, with a tick every
// 10 % and the figure below. Full: the sea is calm and stars twinkle. Drawn in 2x2 "art pixels"
// like the Marea Solsticial background, clear of the rounded corners.

#define AP 2                // one art pixel, in screen pixels
#define ART_W (UI_W / AP)   // 205
#define CY 125              // the port's height (art pixels): the middle of the right side
#define BAT_X 24            // battery body (outline included)
#define BAT_W 104
#define BAT_H 72
#define BAT_Y (CY - BAT_H / 2)
#define IN_X (BAT_X + 3)    // its inside
#define IN_Y (BAT_Y + 3)
#define IN_W (BAT_W - 6)
#define IN_H (BAT_H - 6)
#define CAP_X (BAT_X + BAT_W)  // the terminal, facing the port
#define CAP_W 8
#define PLUG_X (CAP_X + CAP_W)  // where the plug's metal tip ends once in
#define PLUG_IN_MS 600          // the plug reaches the battery

static void ap(int x, int y, int w, int h, uint16_t c) { ui_rect(x * AP, y * AP, w * AP, h * AP, c); }

// A light laid over what is already drawn (a glow, a halo): c mixed in by a (0..256), clipped
static void glow(int x, int y, int w, int h, uint16_t c, int a)
{
    uint16_t *fb = ui_fb();
    int x0 = x * AP, y0 = y * AP, x1 = (x + w) * AP, y1 = (y + h) * AP;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > UI_W) x1 = UI_W;
    if (y1 > UI_H) y1 = UI_H;
    for (int j = y0; j < y1; j++)
        for (int i = x0; i < x1; i++) fb[j * UI_W + i] = ui_mix(c, fb[j * UI_W + i], a);
}

// The glow where the real cable comes in, at the middle of the right edge
static void edge_light(uint16_t c, int a)
{
    for (int r = 4; r >= 1; r--) glow(ART_W - r * 3, CY - r * 4, r * 3, r * 8 + 1, c, a / (r + 1));
}

static float ease_out(float k) { return k >= 1 ? 1 : k <= 0 ? 0 : 1 - (1 - k) * (1 - k) * (1 - k); }

// A zigzag bolt, 5x7 cells of 2x2 art pixels, centred on (x, y), with a golden halo
static void bolt(int x, int y, int alpha)
{
    static const char *const B[7] = {"...##", "..##.", ".##..", "#####", "..##.", ".##..", "##..."};
    uint16_t halo = ui_mix(MS_HALO_ORO, MS_PANEL2, alpha), core = ui_mix(MS_ORO2, MS_PANEL2, alpha);
    uint16_t hot = ui_mix(MS_CREMA, MS_PANEL2, alpha);
    int x0 = x - 5, y0 = y - 7;
    for (int r = 0; r < 7; r++)
        for (int k = 0; k < 5; k++)
            if (B[r][k] == '#') ap(x0 + k * 2 - 1, y0 + r * 2 - 1, 4, 4, halo);
    for (int r = 0; r < 7; r++)
        for (int k = 0; k < 5; k++)
            if (B[r][k] == '#') ap(x0 + k * 2, y0 + r * 2, 2, 2, core);
    ap(x0 + 2, y0 + 6, 6, 1, hot);  // the bright middle stroke
}

// A four-pointed twinkle of radius r
static void star(int x, int y, int r, uint16_t c)
{
    if (r <= 0) return;
    ap(x - r, y, 2 * r + 1, 1, c);
    ap(x, y - r, 1, 2 * r + 1, c);
    if (r > 2) ap(x - 1, y - 1, 3, 3, c);
    for (int d = 1; d <= r / 3; d++) {  // short diagonals
        ap(x - d - 1, y - d - 1, 1, 1, c), ap(x + d + 1, y - d - 1, 1, 1, c);
        ap(x - d - 1, y + d + 1, 1, 1, c), ap(x + d + 1, y + d + 1, 1, 1, c);
    }
}

// The USB plug with its metal tip starting at x (tip, body, sleeve) and the cable on to the right edge
static void plug(int x, bool live)
{
    ap(x, CY - 4, 6, 9, MS_CREMA);                    // the metal tip, against the battery's terminal
    ap(x, CY - 2, 6, 1, MS_GRIS);
    ap(x, CY + 2, 6, 1, MS_GRIS);
    x += 6;
    ap(x, CY - 9, 18, 19, MS_GRIS);                   // the body
    ap(x + 1, CY - 10, 16, 1, MS_GRIS);
    ap(x + 1, CY + 10, 16, 1, MS_LINEA);
    ap(x + 1, CY - 8, 16, 2, MS_CREMA);               // light on top
    ap(x, CY + 6, 18, 4, MS_LINEA);                   // shade below
    ap(x + 7, CY - 2, 4, 4, live ? MS_ORO : MS_LINEA);  // a little lamp: on while it charges
    ap(x + 18, CY - 5, 7, 11, MS_LINEA);              // the sleeve
    int c0 = x + 25;                                  // the cable, off the right edge like the real one
    if (c0 < ART_W) {
        ap(c0, CY - 2, ART_W - c0, 5, MS_PANEL2);
        ap(c0, CY - 1, ART_W - c0, 2, live ? MS_HALO_ORO : MS_LINEA);
    }
}

static void battery(uint32_t t, float level, bool charging, int cap_glow)
{
    uint16_t glass = ui_rgb(10, 14, 32), deep = ui_rgb(16, 48, 92), light = ui_rgb(38, 118, 158);
    uint16_t foam = ui_rgb(176, 222, 230), rim = MS_CREMA;
    // the glass first: the night behind shows through it, darkened and tinted
    uint16_t *fb = ui_fb();
    for (int y = IN_Y * AP; y < (IN_Y + IN_H) * AP; y++)
        for (int x = IN_X * AP; x < (IN_X + IN_W) * AP; x++) fb[y * UI_W + x] = ui_mix(glass, fb[y * UI_W + x], 170);
    if (charging) {  // a soft green glow around it, breathing
        uint16_t c = ui_mix(MS_HALO_VERDE, MS_NOCHE, 120 + (int)lroundf(100 * sinf(t * 0.004f)));
        ap(BAT_X, BAT_Y - 2, BAT_W, 2, c);
        ap(BAT_X, BAT_Y + BAT_H, BAT_W, 2, c);
        ap(BAT_X - 2, BAT_Y + 2, 2, BAT_H - 4, c);
    }
    // the terminal (it glows when a bolt arrives) and the outline with rounded corners
    uint16_t cap = cap_glow ? ui_mix(MS_ORO2, MS_CREMA, cap_glow) : MS_CREMA;
    ap(CAP_X, CY - 13, CAP_W - 1, 27, cap);
    ap(CAP_X + CAP_W - 1, CY - 12, 1, 25, cap);
    ap(CAP_X, CY + 10, CAP_W - 1, 4, MS_GRIS);
    ap(BAT_X + 1, BAT_Y, BAT_W - 2, 3, rim);
    ap(BAT_X + 1, BAT_Y + BAT_H - 3, BAT_W - 2, 3, rim);
    ap(BAT_X, BAT_Y + 1, 3, BAT_H - 2, rim);
    ap(BAT_X + BAT_W - 3, BAT_Y + 1, 3, BAT_H - 2, rim);
    // a "+" pressed into the glass by the terminal
    ap(IN_X + IN_W - 10, IN_Y + 5, 5, 1, MS_GRIS);
    ap(IN_X + IN_W - 8, IN_Y + 3, 1, 5, MS_GRIS);
    // the sea inside: a surface of two slow waves, lighter towards the top
    float amp = charging ? 1.6f : 0.6f, ph = t * (charging ? 0.0042f : 0.0018f);
    int lvl = IN_Y + IN_H - (int)lroundf(level * IN_H / 100.f);
    int sy[IN_W], smin = 9999, smax = -1;
    for (int x = 0; x < IN_W; x++) {
        int v = lvl + (int)lroundf(amp * sinf(ph + x * 0.21f) + amp * 0.6f * sinf(ph * 1.7f - x * 0.13f));
        if (v < IN_Y) v = IN_Y;
        if (v > IN_Y + IN_H) v = IN_Y + IN_H;
        sy[x] = v;
        if (v < smin) smin = v;
        if (v > smax) smax = v;
    }
    for (int y = smin; y < IN_Y + IN_H; y++) {
        uint16_t c = ui_mix(deep, light, 256 * (y - IN_Y) / IN_H);
        if (y > smax) {
            ap(IN_X, y, IN_W, 1, c);
            continue;
        }
        for (int x = 0; x < IN_W; x++)
            if (y > sy[x]) ap(IN_X + x, y, 1, 1, c);
            else if (y == sy[x]) ap(IN_X + x, y, 1, 1, foam);
    }
    // bubbles rising while it charges
    if (charging)
        for (int i = 0; i < 12; i++) {
            uint32_t period = 1300 + i * 170;
            int x = 3 + (i * 37) % (IN_W - 6);
            int y = IN_Y + IN_H - 2 - (int)(((t + i * 377) % period) * (IN_H - 4) / period);
            if (y > sy[x] + 2 && y > sy[x + 1] + 2) {
                ap(IN_X + x, y, 2, 2, ui_mix(foam, light, 150));
                ap(IN_X + x, y, 1, 1, foam);
            }
        }
    // the glass: a light streak along the top
    ap(IN_X + 6, IN_Y + 3, IN_W - 14, 2, ui_mix(MS_CREMA, glass, 40));
    // a tick every 10 % on the left, longer at 50 %, lit up to the charge
    for (int k = 1; k < 10; k++) {
        int y = IN_Y + IN_H - k * IN_H / 10, w = k == 5 ? 7 : 4;
        ap(BAT_X - 3 - w, y, w, 1, k * 10 <= (int)level + 2 ? MS_CREMA : MS_GRIS);
    }
}

// ---------------------------------------------------------------- on a computer
// The computer is told apart a second or two after plugging in: then a little monitor drops in at the
// top (its screen shows a microSD card with a blinking light: the card can be used as a disk) and
// cyan data squares run both ways beside the cable, in to the watch above it and out to the real
// computer, past the right edge, below it.

#define MON_W 64
#define MON_H 40
#define MON_X (102 - MON_W / 2)
#define MON_Y 22
#define MON_DROP_MS 500
#define DATA_IN_Y (CY - 6)   // lane of the squares coming in
#define DATA_OUT_Y (CY + 5)  // lane of the squares going out

static uint16_t cyan(int a) { return ui_mix(ui_rgb(110, 230, 236), MS_NOCHE, a); }

static void monitor(int dy, uint32_t t, int blink)
{
    int x = MON_X, y = MON_Y + dy;
    uint16_t scr = ui_rgb(12, 58, 72), scr2 = ui_rgb(18, 76, 92);
    // stand
    ap(x + MON_W / 2 - 4, y + MON_H, 8, 6, MS_LINEA);
    ap(x + MON_W / 2 - 14, y + MON_H + 6, 28, 3, MS_GRIS);
    // case with rounded corners, a light top and a shaded bottom
    ap(x + 1, y, MON_W - 2, MON_H, MS_GRIS);
    ap(x, y + 1, MON_W, MON_H - 2, MS_GRIS);
    ap(x + 1, y, MON_W - 2, 1, MS_CREMA);
    ap(x + 1, y + MON_H - 4, MON_W - 2, 4, MS_LINEA);
    ap(x + MON_W - 8, y + MON_H - 3, 3, 2, blink > 128 ? MS_VERDE : MS_PILOTO_OFF);  // power light
    // the screen: scanlines drifting down
    int sx = x + 4, sy = y + 4, sw = MON_W - 8, sh = MON_H - 11;
    ap(sx, sy, sw, sh, scr);
    for (int r = (int)(t / 120) % 3; r < sh; r += 3) ap(sx, sy + r, sw, 1, scr2);
    // a microSD card on it: notched corner, golden contacts, and its activity light
    int cx = sx + sw / 2 - 7, cy = sy + 4;
    ap(cx, cy + 3, 14, 17, MS_CREMA);
    ap(cx + 4, cy, 10, 3, MS_CREMA);
    ap(cx, cy + 2, 3, 1, MS_CREMA);
    for (int k = 0; k < 4; k++) ap(cx + 5 + k * 2, cy + 1, 1, 4, MS_ORO);
    ap(cx + 2, cy + 9, 10, 1, MS_GRIS);
    ap(cx + 17, cy + 15, 3, 3, blink > 128 ? cyan(256) : scr2);
}

static void data_square(int x, int y, int a)
{
    glow(x - 2, y - 2, 5, 5, cyan(256), a / 3);
    glow(x - 1, y - 1, 3, 3, cyan(256), a);
}

static void draw_power(uint32_t t, bool charging, int pc_ms)
{
    ui_background(false);
    ui_veil(0, UI_H, 1);
    // the plug slides in from the right edge, where the real one is, and clicks in
    float k = ease_out(t / (float)PLUG_IN_MS);
    int px = PLUG_X + (int)lroundf((1 - k) * (ART_W + 8 - PLUG_X));
    bool in = t >= PLUG_IN_MS;
    // bolts leave the edge and run along the cable into the plug; the terminal glows as each arrives
    int cap_glow = 0, edge_glow = 0;
    const int b_from = ART_W + 6, b_to = PLUG_X + 32;
    const uint32_t gap = 380, run = 520;  // a new bolt every gap ms, each takes run ms
    uint32_t tt = in ? t - PLUG_IN_MS : 0;
    if (in && charging)
        for (uint32_t b = 0; b * gap <= tt; b++) {
            uint32_t age = tt - b * gap;
            if (age < 200 && 256 - (int)age * 256 / 200 > edge_glow) edge_glow = 256 - (int)age * 256 / 200;
            if (age >= run && age < run + 220 && 256 - (int)(age - run) * 256 / 220 > cap_glow)
                cap_glow = 256 - (int)(age - run) * 256 / 220;
        }
    // the level: a little below the charge, rising to it while it charges
    int pct = s_force == 2 || s_force == 4 ? 100 : g_app.bat_pct < 0 ? 0 : g_app.bat_pct;
    float level = pct;
    if (charging) {
        float from = pct > 8 ? pct - 8 : 0;
        level = from + (pct - from) * ease_out((t > 700 ? t - 700 : 0) / 3000.f);
    }
    battery(t, level, charging, cap_glow);
    plug(px, in && charging);
    // where the real cable comes in: a glow at the edge each time a bolt leaves
    if (edge_glow) edge_light(MS_ORO2, edge_glow);
    if (in && charging)
        for (uint32_t b = 0; b * gap <= tt; b++) {
            uint32_t age = tt - b * gap;
            if (age < run) bolt(b_from - (int)(age * (b_from - b_to) / run), CY, age < 80 ? (int)(age * 256 / 80) : 256);
        }
    // on a computer: the monitor drops in, then the data runs both ways (and lights the edge in cyan)
    if (pc_ms >= 0) {
        int dy = -(int)lroundf((1 - ease_out(pc_ms / (float)MON_DROP_MS)) * (MON_Y + MON_H + 12));
        int blink = (t / 180) % 2 ? 256 : 0;
        monitor(dy, t, blink);
        if (pc_ms >= MON_DROP_MS) {
            uint32_t dt = pc_ms - MON_DROP_MS;
            const uint32_t dgap = 150, drun = 460;
            int dglow = 0;
            for (uint32_t b = 0; b * dgap <= dt; b++) {
                uint32_t age = dt - b * dgap;
                if (age >= drun) continue;
                int a = age < 60 ? (int)(age * 256 / 60) : 256, span = b_from - b_to;
                if (b % 2 == 0) data_square(b_from - (int)(age * span / drun), DATA_IN_Y, a);  // in
                else data_square(b_to + (int)(age * span / drun), DATA_OUT_Y, a);               // out
                if (b % 2 == 0 && age < 120) dglow = 256 - (int)age * 256 / 120;
                if (b % 2 == 1 && age > drun - 120) dglow = (int)(age - (drun - 120)) * 256 / 120;
            }
            if (dglow > 0 && !edge_glow) edge_light(cyan(256), dglow);
        }
    }
    // the click: sparks around the tip
    if (in && t < PLUG_IN_MS + 400) {
        int a = 256 - (int)(t - PLUG_IN_MS) * 256 / 400, r = 6 + (int)(t - PLUG_IN_MS) / 30;
        uint16_t c = ui_mix(MS_ORO2, MS_NOCHE, a);
        for (int i = 0; i < 10; i++) {
            float ang = i * 0.628f;
            ap(PLUG_X - 3 + (int)lroundf(cosf(ang) * r * 0.7f), CY + (int)lroundf(sinf(ang) * r), 2, 2, c);
        }
    }
    // full: stars twinkling around the battery
    if (!charging && in) {
        uint32_t c = tt % 1400;
        star(BAT_X + BAT_W - 6, BAT_Y - 12, (c < 700 ? (int)c : (int)(1400 - c)) * 8 / 700, MS_ORO2);
        uint32_t c2 = (t + 300) % 1800;
        star(BAT_X + 14, BAT_Y - 8, (c2 < 900 ? (int)c2 : (int)(1800 - c2)) * 5 / 900, MS_CREMA);
        uint32_t c3 = (t + 900) % 1600;
        star(BAT_X + 50, BAT_Y + BAT_H + 10, (c3 < 800 ? (int)c3 : (int)(1600 - c3)) * 4 / 800, MS_ORO);
    }
    // the figure below: the only thing written
    if (g_app.bat_pct >= 0 || s_force) {
        char b[8];
        snprintf(b, sizeof(b), "%d%%", pct);
        ui_print_c(&ui_jersey, 4, (BAT_X + BAT_W / 2) * AP, (BAT_Y + BAT_H) * AP + 30, b, charging ? MS_VERDE : MS_CREMA, true);
    }
}

void plug_scene(uint32_t t, bool charging) { draw_power(t, charging, -1); }

static bool p_draw(uint32_t now, bool force)
{
    if ((int32_t)(now - s_t0) > SHOW_MS) {  // signed: "now" may be a moment older than s_t0
        sys_alert_done();
        return false;
    }
    bool charging = g_app.bat_chg && !g_app.bat_full, pc = g_app.usb_data;
    if (s_force) {  // the tests: the computer is "told apart" 1.5 s in, as it really happens
        charging = s_force == 1 || s_force == 3;
        pc = s_force >= 3 && now - s_t0 > 1500;
    }
    if (pc && !s_pc_at) s_pc_at = now | 1;
    draw_power(now - s_t0, charging, pc ? (int)(now - s_pc_at) : -1);
    return true;
}

static void p_event(const ui_event_t *e)
{
    if (e->type == EV_TAP) sys_alert_done();
}

static const app_t app_plug = {"USB", "USB", NULL, p_open, p_draw, p_event, NULL, NULL, NULL};
