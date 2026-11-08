// Music: what the phone is playing (Spotify or any media app, sent by the phone app over Bluetooth) and
// buttons for previous / play-pause / next and the phone's volume.
#include <stdio.h>
#include "apps.h"
#include "phone.h"
#include "sys.h"
#include "ui_icons.h"

#define BTN_Y 300
#define BIG 96
#define SMALL 76
#define VOL_Y 412

static unsigned s_shown = ~0u;
static int s_pressed = -1;  // 0 prev, 1 play, 2 next, 3 vol-, 4 vol+

static int hit(int x, int y)
{
    if (y >= BTN_Y && y < BTN_Y + BIG) {
        if (x >= UI_W / 2 - BIG / 2 && x < UI_W / 2 + BIG / 2) return 1;
        if (x >= UI_W / 2 - BIG / 2 - 16 - SMALL && x < UI_W / 2 - BIG / 2 - 16) return 0;
        if (x >= UI_W / 2 + BIG / 2 + 16 && x < UI_W / 2 + BIG / 2 + 16 + SMALL) return 2;
    }
    if (y >= VOL_Y - 10 && y < VOL_Y + 50) return x < UI_W / 2 ? 3 : 4;
    return -1;
}

// Pixel glyphs for the transport buttons, drawn in blocks of 4 px
static void glyph(int kind, int cx, int cy, uint16_t c)
{
    const int b = 4;
    if (kind == 1 && g_music.playing) {  // pause: two bars
        ui_rect(cx - 5 * b, cy - 6 * b, 3 * b, 12 * b, c);
        ui_rect(cx + 2 * b, cy - 6 * b, 3 * b, 12 * b, c);
    } else if (kind == 1) {  // play: a triangle pointing right
        for (int i = 0; i < 12; i++) ui_rect(cx - 4 * b, cy - 6 * b + i * b, (i < 6 ? i + 1 : 12 - i) * b, b, c);
    } else {  // previous / next: double triangle
        int dir = kind == 2 ? 1 : -1;
        for (int k = 0; k < 2; k++)
            for (int i = 0; i < 8; i++) {
                int w = (i < 4 ? i + 1 : 8 - i) * b;
                int x0 = cx + dir * (k * 4 * b) - (dir > 0 ? 4 * b : 0);
                ui_rect(dir > 0 ? x0 : x0 - w + 4 * b, cy - 4 * b + i * b, w, b, c);
            }
    }
}

static bool draw(uint32_t now, bool force)
{
    if (!force && s_shown == g_music.version) return false;
    s_shown = g_music.version;
    char buf[24];
    app_frame(TR("Música", "Music"), false);
    if (!g_music.valid) {
        ui_panel(48, 150, UI_W - 96, 170, MS_PANEL, MS_LINEA, 4, true);
        ui_print_c(&ui_jersey, 3, UI_W / 2, 176, g_app.phone_linked ? TR("Nada sonando", "Nothing playing") : TR("Sin móvil", "No phone"), MS_CREMA, false);
        ui_print_c(&ui_vt, 1, UI_W / 2, 236, g_app.phone_linked ? TR("pon música en el móvil", "play something on the phone")
                                                               : TR("abre la app Marea en el móvil", "open the Marea app on the phone"),
                  MS_GRIS, false);
        return true;
    }
    ui_panel(40, 128, UI_W - 80, 150, MS_PANEL, MS_ORO, 4, true);
    int sc = ui_fit(&ui_jersey, 4, 2, UI_W - 120, g_music.title);
    ui_print_c(&ui_jersey, sc, UI_W / 2, 150, g_music.title, MS_CREMA, false);
    ui_print_c(&ui_vt, 2, UI_W / 2, 214, g_music.artist, MS_GRIS, false);
    // transport
    int x0 = UI_W / 2 - BIG / 2 - 16 - SMALL, x2 = UI_W / 2 + BIG / 2 + 16;
    ui_panel(x0, BTN_Y + (BIG - SMALL) / 2, SMALL, SMALL, s_pressed == 0 ? MS_PANEL2 : MS_PANEL, MS_LINEA, 4, false);
    glyph(0, x0 + SMALL / 2, BTN_Y + BIG / 2, MS_CREMA);
    ui_rect(UI_W / 2 - BIG / 2 + (s_pressed == 1 ? 2 : 0), BTN_Y + (s_pressed == 1 ? 2 : 0), BIG, BIG, MS_ORO);
    glyph(1, UI_W / 2 + (s_pressed == 1 ? 2 : 0), BTN_Y + BIG / 2 + (s_pressed == 1 ? 2 : 0), MS_NOCHE);
    ui_panel(x2, BTN_Y + (BIG - SMALL) / 2, SMALL, SMALL, s_pressed == 2 ? MS_PANEL2 : MS_PANEL, MS_LINEA, 4, false);
    glyph(2, x2 + SMALL / 2, BTN_Y + BIG / 2, MS_CREMA);
    // volume
    ui_print(&ui_jersey, 3, 66, VOL_Y, "-", MS_CREMA);
    ui_bar(100, VOL_Y + 14, UI_W - 200, 16, g_music.volume * 10, MS_ORO);
    ui_print_r(&ui_jersey, 3, UI_W - 66, VOL_Y, "+", MS_CREMA);
    snprintf(buf, sizeof(buf), "%d %%", g_music.volume);
    ui_print_c(&ui_vt, 1, UI_W / 2, VOL_Y + 36, buf, MS_GRIS, false);
    return true;
}

static void event(const ui_event_t *e)
{
    static const char *const CMDS[] = {"prev", "play", "next", "vol-", "vol+"};
    if (e->type == EV_DOWN) {
        s_pressed = hit(e->x, e->y);
    } else if (e->type == EV_UP) {
        s_pressed = -1;
    } else if (e->type == EV_TAP) {
        int b = hit(e->x, e->y);
        if (b >= 0 && g_music.valid) {
            const char *cmd = b == 1 ? (g_music.playing ? "pause" : "play") : CMDS[b];
            phone_send("media", cmd);
            if (b == 1) g_music.playing = !g_music.playing;  // instant feedback; the phone confirms
            if (b >= 3) g_music.volume += b == 4 ? 6 : -6;
            if (g_music.volume < 0) g_music.volume = 0;
            if (g_music.volume > 100) g_music.volume = 100;
        }
    }
    g_music.version++;
}

const app_t app_music = {"Música", "Music", icon_music, NULL, draw, event, NULL, NULL};

// ---------------------------------------------------------------- incoming call

static int s_call_phase = -1;
static uint32_t s_call_t0;

static void call_open(void)
{
    s_call_phase = -1;
    s_call_t0 = 0;
}

static bool call_draw(uint32_t now, bool force)
{
    if (!s_call_t0) s_call_t0 = now ? now : 1;
    int phase = (int)((now - s_call_t0) / 600) & 1;
    if (!force && phase == s_call_phase) return false;
    s_call_phase = phase;
    ui_background(true);
    ui_veil(40, UI_H, 2);
    if (phase) ui_frame(8, 8, UI_W - 16, UI_H - 16, 8, MS_VERDE);
    ui_print_c(&ui_jersey, 3, UI_W / 2, 74, TR("Llamada entrante", "Incoming call"), MS_ORO, true);
    const char *who = g_call.who[0] ? g_call.who : TR("Número oculto", "Unknown number");
    int sc = ui_fit(&ui_jersey, 5, 2, UI_W - 80, who);
    ui_print_c(&ui_jersey, sc, UI_W / 2, 170, who, MS_CREMA, true);
    ui_button(UI_W / 2 - 160, 340, 150, 76, TR("Silenciar", "Silence"), UI_BTN_SECONDARY, false);
    ui_button(UI_W / 2 + 10, 340, 150, 76, TR("Rechazar", "Reject"), UI_BTN_DANGER, false);
    return true;
}

static void call_event(const ui_event_t *e)
{
    if (e->type != EV_TAP || e->y < 330 || e->y > 430) return;
    bool reject = e->x >= UI_W / 2;
    phone_send("call", reject ? "reject" : "silence");
    phone_command("call end");
}

static bool call_keep_on(void) { return true; }

const app_t app_call = {"Llamada", "Call", icon_music, call_open, call_draw, call_event, NULL, call_keep_on};
