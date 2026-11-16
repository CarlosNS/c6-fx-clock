// First-run guide: what a new watch shows after the splash, until it is finished or skipped (the
// "setup" setting; Settings → System → See the introduction brings it back). Four short steps on the
// dusk background:
//   0  language (Spanish / English)
//   1  the gestures, practised: swipe down from the top, up from the bottom, sideways, hold the finger
//      still (the face picker, on any face) and back (BOOT or the left edge); a little screen shows
//      each one, and doing it shows what it opens
//   2  the phone app: the QR code of the latest Android app (it is not in the Play Store), the name
//      the watch has in Bluetooth, and "waiting for the phone" until it connects (then it moves on)
//   3  all set, with the time if the phone already sent it
// It is an alert, so BOOT and the left edge never leave it by mistake: they go one step back (or are
// the gesture being practised). "Skip" ends it at any step.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "apps.h"
#include "ble.h"
#include "clock.h"
#include "esp_timer.h"
#include "settings.h"
#include "sys.h"
#include "trans.h"

#define NSTEPS 4
#define BTN_Y 396
#define BTN_H 52
#define DOTS_Y 462
// The little screen of the gestures step
#define BX 105
#define BY 156
#define BW 200
#define BH 170

enum { G_TOP, G_BOTTOM, G_SIDE, G_HOLD, G_BACK, NGEST };
enum { HIT_NONE, HIT_SKIP, HIT_NEXT, HIT_ES, HIT_EN, HIT_START };

static int s_step, s_gest, s_pressed;
static bool s_ok;                 // the gesture being practised was done
static uint32_t s_ok_t, s_anim0, s_last, s_conn_t;
static bool s_was_conn;

static const char *const CHECK[16] = {
    "................", "................", "..............kk", ".............kvk",
    "............kvvk", "...........kvvk.", "..kk......kvvk..", ".kvvk....kvvk...",
    ".kvvvk..kvvk....", "..kvvvkkvvk.....", "...kvvvvvk......", "....kvvvk.......",
    ".....kvk........", "......k.........", "................", "................",
};

typedef struct {
    const char *es, *en;                 // what to do
    const char *h1es, *h1en, *h2es, *h2en;  // how
    const char *o1es, *o1en, *o2es, *o2en;  // what it does (once done)
} gesture_t;

static const gesture_t GEST[NGEST] = {
    {"Baja desde arriba", "Swipe down",
     "Pon el dedo en el borde de arriba", "Start right at the top edge", "y bájalo", "and pull down",
     "Ajustes rápidos: brillo,", "Quick settings: brightness,", "Bluetooth, linterna, batería", "Bluetooth, torch, battery"},
    {"Sube desde abajo", "Swipe up",
     "Pon el dedo en el borde de abajo", "Start right at the bottom edge", "y súbelo", "and push up",
     "Todas las apps: alarmas, pasos,", "All the apps: alarms, steps,", "música, cronómetro, ajustes", "music, stopwatch, settings"},
    {"Desliza a un lado", "Swipe sideways",
     "Un trazo decidido, en medio", "One firm stroke across", "de la pantalla", "the middle of the screen",
     "Cambia a la esfera siguiente", "Changes to the next face", "o a la anterior", "or the one before"},
    {"Mantén el dedo quieto", "Hold your finger still",
     "En medio, sin moverlo; vale", "In the middle, not moving;", "en todas las esferas", "it works on every face",
     "Todas las esferas: desliza y", "Every face: swipe, then tap", "toca la del centro para ponerla", "the middle one to wear it"},
    {"Vuelve atrás", "Go back",
     "Pulsa el botón BOOT o desliza", "Press BOOT, or swipe in", "desde el borde izquierdo", "from the left edge",
     "Así sales de cualquier", "That leaves any screen,", "pantalla, paso a paso", "one step at a time"},
};

// The main loop reads the time before the gestures come in: times set here can be a little after
// the `now` draw() gets, so compare them signed
static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }
static int since(uint32_t now, uint32_t t) { int d = (int)(int32_t)(now - t); return d < 0 ? 0 : d; }

static void go(int step)
{
    s_step = step < 0 ? 0 : step;
    s_gest = 0;
    s_ok = false;
    s_anim0 = now_ms();
    s_was_conn = ble_connected();
    s_conn_t = 0;
}

static void open(void)
{
    s_pressed = HIT_NONE;
    s_last = now_ms();
    go(0);
}

static void finish(void)
{
    settings_set("setup", 1);
    settings_save();
    trans_start(T_UNCOVER_RIGHT);
    sys_alert_done();
}

// The gesture being practised was done: show what it does, then the next one
static void gesture_done(void)
{
    if (s_ok) return;
    s_ok = true;
    s_ok_t = now_ms();
}

static void next_gesture(void)
{
    if (s_gest + 1 < NGEST) {
        s_gest++;
        s_ok = false;
        s_anim0 = now_ms();
    } else go(2);
}

// ---------------------------------------------------------------- drawing

static void title_c(const char *t, uint16_t c)
{
    ui_print_c(&ui_jersey, ui_fit(&ui_jersey, 4, 2, UI_W - 60, t), UI_W / 2, 64, t, c, true);
}
static void title(const char *t) { title_c(t, MS_ORO); }

static void line_c(int y, const char *s, uint16_t c) { ui_print_c(&ui_vt, 1, UI_W / 2, y, s, c, false); }

static void footer(const char *next)
{
    if (next) {
        ui_button(52, BTN_Y, 140, BTN_H, TR("Saltar", "Skip"), UI_BTN_SECONDARY, s_pressed == HIT_SKIP);
        ui_button(218, BTN_Y, 140, BTN_H, next, UI_BTN_MAIN, s_pressed == HIT_NEXT);
    } else ui_button(85, BTN_Y, 240, BTN_H, TR("Empezar", "Start"), UI_BTN_MAIN, s_pressed == HIT_START);
    ui_dots(UI_W / 2, DOTS_Y, NSTEPS, s_step);
}

static void finger(int x, int y)
{
    ui_rect(x - 10, y - 10, 20, 20, MS_NOCHE);
    ui_rect(x - 8, y - 8, 16, 16, MS_CREMA);
}

// The little screen: a face with the time, and what the gesture brings in, `f` of the way (0..1000)
static void demo(int g, int f, bool show_finger)
{
    ui_panel(BX - 4, BY - 4, BW + 8, BH + 8, MS_NOCHE, MS_LINEA, 3, true);
    int x0 = BX, y0 = BY, cx = BX + BW / 2, cy = BY + BH / 2;
    if (g == G_SIDE) {  // the face slides out, the next one in
        int off = f * BW / 1000;
        if (off < 30) ui_print_c(&ui_jersey, 3, cx - off, cy - 30, "10:08", MS_GRIS, false);
        if (off) ui_rect(x0 + BW - off, y0, off, BH, MS_HALO_ORO);
        if (off > 152) ui_print_c(&ui_jersey, 3, x0 + BW - off + BW / 2, cy - 30, "10:08", MS_ORO2, false);
        if (show_finger) finger(BX + BW - 30 - f * (BW - 60) / 1000, cy + 30);
        return;
    }
    if (g == G_HOLD) {  // the finger stays put, then the face shrinks into a card between its neighbours
        if (f > 0) {
            int w = BW - f * (BW - 70) / 1000, h = BH - f * (BH - 86) / 1000;
            if (f > 500) {
                ui_rect(cx - 35 - 62, cy - 30, 50, 60, MS_PANEL);
                ui_rect(cx + 35 + 12, cy - 30, 50, 60, MS_PANEL);
            }
            ui_rect(cx - w / 2 - 3, cy - h / 2 - 3, w + 6, h + 6, MS_ORO);
            ui_rect(cx - w / 2, cy - h / 2, w, h, MS_PANEL2);
            int sc = f < 600 ? 3 : 2;
            ui_print_c(&ui_jersey, sc, cx, cy - sc * 10, "10:08", MS_CREMA, false);
        } else {
            ui_print_c(&ui_jersey, 3, cx, cy - 30, "10:08", MS_GRIS, false);
        }
        if (show_finger && f == 0) finger(cx, cy + 30);
        return;
    }
    ui_print_c(&ui_jersey, 3, cx, cy - 30, "10:08", MS_GRIS, false);
    if (g == G_TOP) {  // quick settings come down
        int h = f * 118 / 1000;
        ui_rect(x0, y0, BW, h, MS_PANEL2);
        for (int r = 0; r < 2; r++)
            for (int c = 0; c < 3; c++) {
                int tx = x0 + 14 + c * 60, ty = y0 + 12 + r * 52;
                if (ty + 40 <= y0 + h) ui_rect(tx, ty, 52, 40, (r + c) % 2 ? MS_ORO : MS_LINEA);
            }
        if (show_finger) finger(cx, y0 + 12 + h);
    } else if (g == G_BOTTOM) {  // the launcher comes up
        int h = f * 130 / 1000;
        ui_rect(x0, y0 + BH - h, BW, h, MS_PANEL2);
        for (int r = 0; r < 2; r++)
            for (int c = 0; c < 3; c++) {
                int tx = x0 + 22 + c * 58, ty = y0 + BH - 118 + r * 56;
                if (ty >= y0 + BH - h) ui_rect(tx, ty, 40, 40, c == 1 ? MS_ORO2 : MS_HALO_ORO);
            }
        if (show_finger) finger(cx, y0 + BH - 12 - h);
    } else {  // an app slides away to the right
        int off = f * BW / 1000;
        if (off < BW) {
            ui_rect(x0 + off, y0, BW - off, BH, MS_PANEL);
            if (BW - off > 12) ui_rect(x0 + off, y0, BW - off, 30, MS_PANEL2);
        }
        if (show_finger) finger(BX + 14 + f * (BW - 60) / 1000, cy + 30);
    }
}

// Progress of the demo loop: still, then the stroke, then still at the end
static int demo_f(uint32_t now)
{
    int p = since(now, s_anim0) % 2000;
    if (p < 300) return 0;
    if (p > 1300) return 1000;
    int t = p - 300;  // 0..1000 over one second
    return t * (2000 - t) / 1000;  // ease out
}

static bool demo_finger(uint32_t now)
{
    int p = since(now, s_anim0) % 2000;
    return p >= 200 && p <= 1500;
}

static void draw_lang(void)
{
    title("Marea");
    ui_print_c(&ui_jersey, 3, UI_W / 2, 136, "Hola · Hello", MS_CREMA, true);
    line_c(194, "Elige tu idioma · Choose a language", MS_GRIS);
    ui_button(85, 236, 240, 56, "Español", g_app.lang ? UI_BTN_SECONDARY : UI_BTN_MAIN, s_pressed == HIT_ES);
    ui_button(85, 308, 240, 56, "English", g_app.lang ? UI_BTN_MAIN : UI_BTN_SECONDARY, s_pressed == HIT_EN);
    footer(TR("Siguiente", "Next"));
}

static void draw_gestures(uint32_t now)
{
    const gesture_t *g = &GEST[s_gest];
    char n[24];
    snprintf(n, sizeof(n), "%d/%d", s_gest + 1, NGEST);
    if (s_ok) {
        title_c(TR("¡Bien!", "Well done!"), MS_VERDE);
        demo(s_gest, 1000, false);
        ui_icon(CHECK, BX + BW - 40, BY + 6, 2);
        line_c(346, TR(g->o1es, g->o1en), MS_CREMA);
        line_c(368, TR(g->o2es, g->o2en), MS_CREMA);
    } else {
        title(TR(g->es, g->en));  // what to do is the title
        if (s_gest == G_HOLD) {  // the finger stays put most of the loop, then the face shrinks
            int p = since(now, s_anim0) % 2400;
            demo(s_gest, p < 1300 ? 0 : p > 1800 ? 1000 : (p - 1300) * 2, p >= 200 && p < 1300);
        } else {
            demo(s_gest, demo_f(now), demo_finger(now));
        }
        line_c(346, TR(g->h1es, g->h1en), MS_GRIS);
        line_c(368, TR(g->h2es, g->h2en), MS_GRIS);
    }
    ui_print_r(&ui_vt, 1, BX - 14, BY + 2, n, MS_GRIS);  // which gesture of them all
    footer(TR("Siguiente", "Next"));
}

static void draw_app(uint32_t now)
{
    title(TR("La app del móvil", "The phone app"));
    ui_qr(UI_W / 2, 116, 190, APP_URL);
    char b[64];
    line_c(306, TR("Escanéalo, instala Marea,", "Scan it, install Marea,"), MS_CREMA);
    snprintf(b, sizeof(b), TR("ábrela y elige «%s»", "open it and pick \"%s\""), ble_name()[0] ? ble_name() : "Marea");
    line_c(328, b, MS_CREMA);
    bool conn = ble_connected();
    const char *st = conn ? TR("Móvil conectado", "Phone connected") : TR("Esperando al móvil…", "Waiting for the phone…");
    int w = ui_print_w(&ui_vt, 1, st), x = UI_W / 2 - (w + 18) / 2;
    bool on = conn || (now / 500) % 2;
    ui_rect(x, 362, 10, 10, on ? (conn ? MS_VERDE : MS_ORO) : MS_PILOTO_OFF);
    ui_print(&ui_vt, 1, x + 18, 354, st, conn ? MS_VERDE : MS_ORO2);
    footer(conn ? TR("Siguiente", "Next") : TR("Más tarde", "Later"));
}

static void draw_done(void)
{
    title(TR("¡Listo!", "All set!"));
    bool conn = ble_connected();
    if (g_now.valid) {
        char t[8];
        clock_hhmm(t, true);
        ui_print_c(&ui_jersey, 6, UI_W / 2, 126, t, MS_CREMA, true);
    }
    int y = g_now.valid ? 262 : 170;
    if (conn) {
        line_c(y, TR("Conectado con tu móvil: la hora", "Linked to your phone: the time"), MS_CREMA);
        line_c(y + 22, TR("y el tiempo llegan solos", "and the weather come by themselves"), MS_CREMA);
    } else {
        line_c(y, TR("Sin móvil por ahora. El QR de la app", "No phone for now. The app's QR code"), MS_CREMA);
        line_c(y + 22, TR("sigue en Ajustes → App del móvil", "is in Settings → Phone app"), MS_CREMA);
    }
    line_c(y + 58, TR("Mantén pulsada la esfera", "Hold the face down"), MS_GRIS);
    line_c(y + 80, TR("para elegir otra", "to pick another one"), MS_GRIS);
    footer(NULL);
}

static bool draw(uint32_t now, bool force)
{
    static uint32_t last_tick;
    // the phone connecting on the app step moves on by itself, once it has been seen
    bool conn = ble_connected();
    if (s_step == 2 && conn && !s_was_conn && !s_conn_t) s_conn_t = now ? now : 1;
    s_was_conn = conn;
    if (s_step == 2 && s_conn_t && (int32_t)(now - s_conn_t) > 1500) {
        go(3);
        force = true;
    }
    if (s_step == 1 && s_ok && (int32_t)(now - s_ok_t) > 1800) {
        next_gesture();
        force = true;
    }
    bool tick = now / 100 != last_tick;  // the animations: 10 frames a second is plenty
    last_tick = now / 100;
    if (!force && !(tick && (s_step == 1 || s_step == 2))) return false;
    app_frame(NULL, true);
    switch (s_step) {
    case 0: draw_lang(); break;
    case 1: draw_gestures(now); break;
    case 2: draw_app(now); break;
    default: draw_done(); break;
    }
    return true;
}

// ---------------------------------------------------------------- touch

static bool in(int x, int y, int bx, int by, int bw, int bh) { return x >= bx && x < bx + bw && y >= by && y < by + bh; }

static int hit(int x, int y)
{
    if (s_step == 3) return in(x, y, 85, BTN_Y, 240, BTN_H) ? HIT_START : HIT_NONE;
    if (in(x, y, 52, BTN_Y, 140, BTN_H)) return HIT_SKIP;
    if (in(x, y, 218, BTN_Y, 140, BTN_H)) return HIT_NEXT;
    if (s_step == 0 && in(x, y, 85, 236, 240, 56)) return HIT_ES;
    if (s_step == 0 && in(x, y, 85, 308, 240, 56)) return HIT_EN;
    return HIT_NONE;
}

static void event(const ui_event_t *e)
{
    s_last = now_ms();
    switch (e->type) {
    case EV_DOWN: s_pressed = hit(e->x, e->y); break;
    case EV_UP: s_pressed = HIT_NONE; break;
    case EV_TAP:
        switch (hit(e->x, e->y)) {
        case HIT_SKIP: finish(); return;
        case HIT_START: finish(); return;
        case HIT_ES: settings_set("lang", 0); break;
        case HIT_EN: settings_set("lang", 1); break;
        case HIT_NEXT:
            if (s_step == 1) next_gesture();
            else go(s_step + 1);
            break;
        default: break;
        }
        break;
    case EV_SWIPE:
        if (s_step != 1 || s_ok) break;
        if (s_gest == G_TOP && e->dir == DIR_DOWN && (e->edge & EDGE_TOP)) gesture_done();
        else if (s_gest == G_BOTTOM && e->dir == DIR_UP && (e->edge & EDGE_BOTTOM)) gesture_done();
        else if (s_gest == G_SIDE && (e->dir == DIR_LEFT || e->dir == DIR_RIGHT) && abs(e->x - e->x0) >= 100)
            gesture_done();
        break;
    case EV_LONG:
        if (s_step == 1 && !s_ok && s_gest == G_HOLD && e->y < BTN_Y) gesture_done();
        break;
    default: break;
    }
    sys_force();
}

// BOOT or the left edge: the gesture being practised, or one step back (never out of the guide)
static bool back(void)
{
    s_last = now_ms();
    if (s_step == 1 && !s_ok && (s_gest == G_BACK || s_gest == G_SIDE)) {
        gesture_done();  // a swipe right from the left edge is a sideways stroke too
        return true;
    }
    if (s_step == 1 && s_gest > 0) {
        s_gest--;
        s_ok = false;
        s_anim0 = now_ms();
        return true;
    }
    if (s_step > 0) go(s_step - 1);
    return true;
}

// The screen stays on while someone is going through it (but not for ever if it is left alone)
static bool keep_on(void) { return since(now_ms(), s_last) < 5 * 60 * 1000; }

const app_t app_setup = {"Bienvenida", "Welcome", NULL, open, draw, event, NULL, keep_on, back};
