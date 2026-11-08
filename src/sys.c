#include "sys.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "app.h"
#include "apps.h"
#include "control.h"
#include "fx.h"
#include "lcd.h"
#include "lcd_co5300.h"
#include "settings.h"
#include "ui.h"
#include "ui_icons.h"

static const app_t *const s_apps[] = {&app_faces, &app_alarms, &app_steps, &app_stopwatch,
                                      &app_music, &app_torch, &app_files, &app_settings};
#define NAPPS ((int)(sizeof(s_apps) / sizeof(s_apps[0])))

static view_t s_view = VIEW_FACE;
static const app_t *s_cur;      // app on screen (VIEW_APP): from the launcher or an alert
static view_t s_back = VIEW_FACE;  // where an alert returns to
static bool s_force = true;     // the next native frame must be drawn whole
static bool s_redraw;           // something on the current system view changed

int sys_app_count(void) { return NAPPS; }
view_t sys_view(void) { return s_view; }
void sys_dirty(void) { s_redraw = true; }

static void set_view(view_t v)
{
    if (s_view == VIEW_APP && v != VIEW_APP && s_cur && s_cur->close) s_cur->close();
    s_view = v;
    s_force = true;
    app_wake();
}

void sys_home(void) { set_view(VIEW_FACE); }
void sys_open_quick(void) { set_view(VIEW_QUICK); }

static ui_scroll_t s_launch_scroll;
void sys_open_launcher(void)
{
    set_view(VIEW_LAUNCHER);
}

void sys_open(const app_t *a)
{
    if (s_view == VIEW_APP && s_cur && s_cur->close) s_cur->close();
    s_cur = a;
    s_view = VIEW_APP;
    s_force = true;
    app_wake();
    if (a->open) a->open();
}

void sys_open_app(int i)
{
    if (i >= 0 && i < NAPPS) sys_open(s_apps[i]);
}

void sys_alert(const app_t *a)
{
    if (s_cur == a && s_view == VIEW_APP) return;
    s_back = s_view == VIEW_APP ? VIEW_LAUNCHER : s_view;
    sys_open(a);
}

void sys_alert_done(void)
{
    set_view(s_back == VIEW_APP ? VIEW_FACE : s_back);
}

bool sys_keep_awake(void)
{
    return s_view == VIEW_APP && s_cur && s_cur->keep_on && s_cur->keep_on();
}

// ---------------------------------------------------------------- shared app helpers

void app_frame(const char *title, bool dusk)
{
    ui_background(dusk);
    ui_veil(60, UI_H, 3);
    ui_status_bar();
    if (title) ui_print_c(&ui_jersey, 4, UI_W / 2, 66, title, MS_ORO, true);
}

bool ui_scroll_event(ui_scroll_t *s, const ui_event_t *e)
{
    int max = s->content_h - s->view_h;
    if (max < 0) max = 0;
    switch (e->type) {
    case EV_DOWN:
        s->dragging = true;
        s->moved = 0;
        return false;
    case EV_MOVE:
        if (!s->dragging) return false;
        s->moved += abs(e->dy);
        s->offset -= e->dy;
        if (s->offset < 0) s->offset = 0;
        if (s->offset > max) s->offset = max;
        return e->dy != 0;
    case EV_UP:
        s->dragging = false;
        return false;
    default:
        return false;
    }
}

// ---------------------------------------------------------------- launcher

#define L_TOP 120
#define L_ROW 150
#define L_TILE_W 156
#define L_TILE_H 136
static int s_pressed = -1;

static int launcher_hit(int x, int y)
{
    int yy = y - L_TOP + s_launch_scroll.offset;
    if (y < L_TOP - 10 || yy < 0) return -1;
    int row = yy / L_ROW, col = x < UI_W / 2 ? 0 : 1, i = row * 2 + col;
    int tx = col ? UI_W / 2 + 6 : UI_W / 2 - 6 - L_TILE_W;
    if (x < tx || x >= tx + L_TILE_W || yy % L_ROW >= L_TILE_H) return -1;
    return i < NAPPS ? i : -1;
}

static bool launcher_draw(void)
{
    s_launch_scroll.view_h = UI_H - L_TOP - 30;
    s_launch_scroll.content_h = ((NAPPS + 1) / 2) * L_ROW;
    ui_background(false);
    ui_veil(60, UI_H, 3);
    for (int i = 0; i < NAPPS; i++) {
        int row = i / 2, col = i % 2;
        int x = col ? UI_W / 2 + 6 : UI_W / 2 - 6 - L_TILE_W;
        int y = L_TOP + row * L_ROW - s_launch_scroll.offset;
        if (y + L_TILE_H < 60 || y > UI_H) continue;
        bool p = i == s_pressed;
        ui_panel(x, y, L_TILE_W, L_TILE_H, p ? MS_PANEL2 : MS_PANEL, p ? MS_ORO : MS_LINEA, 4, true);
        ui_icon(s_apps[i]->icon, x + (L_TILE_W - 48) / 2, y + 18, 3);
        const char *name = TR(s_apps[i]->name_es, s_apps[i]->name_en);
        ui_print_c(&ui_jersey, 2, x + L_TILE_W / 2, y + 86, name, p ? MS_ORO2 : MS_CREMA, false);
    }
    // Clean band under the status bar, so tiles scroll away beneath it
    ui_background_band(false, 0, L_TOP - 14);
    ui_shade(0, 0, UI_W, L_TOP - 14, 1);
    ui_status_bar();
    ui_print_c(&ui_jersey, 3, UI_W / 2, 66, "Marea", MS_ORO, true);
    return true;
}

static void launcher_event(const ui_event_t *e)
{
    if (ui_scroll_event(&s_launch_scroll, e)) s_redraw = true;
    if (e->type == EV_DOWN) {
        s_pressed = launcher_hit(e->x, e->y);
        s_redraw = true;
    } else if (e->type == EV_MOVE && s_pressed >= 0 && s_launch_scroll.moved > 12) {
        s_pressed = -1;
        s_redraw = true;
    } else if (e->type == EV_TAP) {
        int i = launcher_hit(e->x, e->y);
        s_pressed = -1;
        if (i >= 0) sys_open_app(i);
    } else if (e->type == EV_UP) {
        if (s_pressed >= 0) s_redraw = true;
        s_pressed = -1;
    } else if (e->type == EV_SWIPE && e->dir == DIR_RIGHT && (e->edge & EDGE_LEFT)) {
        sys_home();
    } else if (e->type == EV_SWIPE && e->dir == DIR_DOWN && (e->edge & EDGE_TOP) && s_launch_scroll.offset == 0) {
        sys_home();
    }
}

// ---------------------------------------------------------------- quick settings

#define Q_BRI_Y 104
#define Q_BAR_X 60
#define Q_BAR_W 290
#define Q_TILE_Y 196
#define Q_TILE_W 154
#define Q_TILE_H 112
static int s_quick_pressed = -1;
static bool s_bri_drag;

static int quick_tile(int x, int y)
{
    for (int i = 0; i < 4; i++) {
        int tx = i % 2 ? UI_W / 2 + 6 : UI_W / 2 - 6 - Q_TILE_W, ty = Q_TILE_Y + (i / 2) * (Q_TILE_H + 12);
        if (x >= tx && x < tx + Q_TILE_W && y >= ty && y < ty + Q_TILE_H) return i;
    }
    return -1;
}

static void quick_tile_draw(int i, const char *label, const char *state, bool on, bool good)
{
    int x = i % 2 ? UI_W / 2 + 6 : UI_W / 2 - 6 - Q_TILE_W, y = Q_TILE_Y + (i / 2) * (Q_TILE_H + 12);
    bool p = s_quick_pressed == i;
    ui_panel(x, y, Q_TILE_W, Q_TILE_H, p ? MS_PANEL2 : MS_PANEL, on ? MS_ORO : MS_LINEA, 4, true);
    int sc = ui_fit(&ui_jersey, 3, 2, Q_TILE_W - 16, label);
    ui_print_c(&ui_jersey, sc, x + Q_TILE_W / 2, y + 18, label, on ? MS_ORO2 : MS_CREMA, false);
    ui_print_c(&ui_vt, 1, x + Q_TILE_W / 2, y + 66, state, good ? MS_VERDE : on ? MS_CREMA : MS_GRIS, false);
}

static bool quick_draw(void)
{
    char buf[48];
    ui_background(false);
    ui_veil(40, UI_H, 3);
    ui_status_bar();
    // Brightness
    ui_print(&ui_vt, 2, Q_BAR_X, Q_BRI_Y - 6, TR("BRILLO", "BRIGHTNESS"), MS_GRIS);
    snprintf(buf, sizeof(buf), "%d %%", g_app.brightness);
    ui_print_r(&ui_jersey, 3, Q_BAR_X + Q_BAR_W, Q_BRI_Y - 4, buf, MS_CREMA);
    ui_bar(Q_BAR_X, Q_BRI_Y + 46, Q_BAR_W, 22, g_app.brightness * 10, MS_ORO);
    // Tiles
    quick_tile_draw(0, "Bluetooth", g_app.phone_linked ? TR("conectado", "connected") : TR("sin móvil", "no phone"),
                    g_app.phone_linked, g_app.phone_linked);
    quick_tile_draw(1, TR("Silencio", "Silent"), g_app.dnd ? TR("activo", "on") : TR("apagado", "off"), g_app.dnd, false);
    quick_tile_draw(2, TR("Siempre", "Always on"), g_app.always_on ? TR("activo", "on") : TR("apagado", "off"), g_app.always_on, false);
    quick_tile_draw(3, TR("Linterna", "Flashlight"), TR("abrir", "open"), false, false);
    // Battery
    int by = Q_TILE_Y + 2 * (Q_TILE_H + 12) + 6;
    if (g_app.bat_pct >= 0)
        snprintf(buf, sizeof(buf), "%d %%%s", g_app.bat_pct,
                 g_app.bat_full ? TR(" · cargada", " · full") : g_app.bat_chg ? TR(" · cargando", " · charging") : "");
    else
        snprintf(buf, sizeof(buf), "%s", TR("sin batería", "no battery"));
    // Icon and text centred together; the bottom edge is straight only between the rounded corners
    int sc = 60 + 14 + ui_print_w(&ui_vt, 2, buf) <= 290 ? 2 : 1;
    int tw = ui_print_w(&ui_vt, sc, buf), bx = (UI_W - (60 + 14 + tw)) / 2;
    ui_battery(bx, by + 4, 60, 30, g_app.bat_pct, g_app.bat_chg);
    ui_print(&ui_vt, sc, bx + 74, by + (sc == 2 ? 0 : 8), buf, g_app.bat_chg ? MS_VERDE : MS_CREMA);
    return true;
}

static void set_brightness_at(int x)
{
    int v = (x - Q_BAR_X) * 100 / Q_BAR_W;
    v = v < 5 ? 5 : v > 100 ? 100 : v;
    if (v != g_app.brightness) {
        settings_set("bri", v);
        s_redraw = true;
    }
}

static void quick_event(const ui_event_t *e)
{
    bool in_bar = e->y0 >= Q_BRI_Y && e->y0 < Q_BRI_Y + 90 && e->x0 >= Q_BAR_X - 20 && e->x0 < Q_BAR_X + Q_BAR_W + 20;
    switch (e->type) {
    case EV_DOWN:
        s_bri_drag = in_bar;
        if (s_bri_drag) set_brightness_at(e->x);
        s_quick_pressed = quick_tile(e->x, e->y);
        if (s_quick_pressed >= 0) s_redraw = true;
        break;
    case EV_MOVE:
        if (s_bri_drag) set_brightness_at(e->x);
        break;
    case EV_TAP:
        switch (quick_tile(e->x, e->y)) {
        case 1: settings_set("dnd", !g_app.dnd); break;
        case 2: settings_set("aon", !g_app.always_on); break;
        case 3: sys_open_app(5); return;  // flashlight
        default: break;
        }
        s_redraw = true;
        break;
    case EV_SWIPE:
        if (!s_bri_drag && e->dir == DIR_UP) sys_home();
        break;
    case EV_UP:
        s_bri_drag = false;
        if (s_quick_pressed >= 0) s_redraw = true;
        s_quick_pressed = -1;
        break;
    default:
        break;
    }
}

// ---------------------------------------------------------------- the face

// Interactive faces (those with a hint) get the finger like the pad in the apps, unless the touch
// starts at an edge or outside the face: those still navigate.
static bool s_to_face;
static int s_lcx, s_lcy;

// Where a touch lands on the face: native faces use panel pixels, old ones their enlarged canvas
static bool face_point(int x, int y, int *cx, int *cy)
{
    if (fx_list[g_app.fx].res == FX_NATIVE) {
        *cx = x;
        *cy = y;
        return true;
    }
    if (fx_list[g_app.fx].res == FX_WIDE) {
        *cx = x * 2 / 3;
        *cy = y * 2 / 3;
        return true;
    }
    return lcd_panel_to_canvas(x, y, cx, cy);
}
static int face_w(void) { int r = fx_list[g_app.fx].res; return r == FX_NATIVE ? FW : r == FX_WIDE ? WW : fx_list[g_app.fx].landscape ? LAND_W : LCD_W; }
static int face_h(void) { int r = fx_list[g_app.fx].res; return r == FX_NATIVE ? FH : r == FX_WIDE ? WH : fx_list[g_app.fx].landscape ? LAND_H : LCD_H; }

static void face_event(const ui_event_t *e)
{
    int cx, cy;
    switch (e->type) {
    case EV_DOWN: {
        bool inside = face_point(e->x, e->y, &cx, &cy);
        s_to_face = fx_list[g_app.fx].hint && inside && !e->edge;
        s_lcx = cx;
        s_lcy = cy;
        if (s_to_face) app_drag(cx * 1000 / face_w(), cy * 1000 / face_h(), 0, 0);
        break;
    }
    case EV_MOVE:
        if (s_to_face) {
            face_point(e->x, e->y, &cx, &cy);
            app_drag(cx * 1000 / face_w(), cy * 1000 / face_h(), cx - s_lcx, cy - s_lcy);
            s_lcx = cx;
            s_lcy = cy;
        }
        break;
    case EV_UP:
        if (s_to_face) app_drag_end();
        s_to_face = false;
        break;
    case EV_SWIPE:
        if (s_to_face) break;
        if (e->dir == DIR_DOWN && (e->edge & EDGE_TOP)) sys_open_quick();
        else if (e->dir == DIR_UP && (e->edge & EDGE_BOTTOM)) sys_open_launcher();
        else if (e->dir == DIR_LEFT || e->dir == DIR_RIGHT) {
            g_app.fx = e->dir == DIR_LEFT ? fx_next(g_app.fx) : fx_prev(g_app.fx);
            g_app.auto_cycle = 0;
            g_app.dirty = true;
        }
        break;
    case EV_LONG:
        if (!s_to_face) sys_open_app(0);  // face picker
        break;
    default:
        break;
    }
}

// ---------------------------------------------------------------- dispatch

void sys_event(const ui_event_t *e)
{
    switch (s_view) {
    case VIEW_FACE: face_event(e); break;
    case VIEW_QUICK: quick_event(e); break;
    case VIEW_LAUNCHER: launcher_event(e); break;
    case VIEW_APP:
        if (e->type == EV_SWIPE && e->dir == DIR_RIGHT && (e->edge & EDGE_LEFT)) {
            sys_open_launcher();
            return;
        }
        if (s_cur->event) s_cur->event(e);
        break;
    }
}

void sys_button_short(void)
{
    switch (s_view) {
    case VIEW_FACE:
        g_app.fx = fx_next(g_app.fx);
        g_app.auto_cycle = 0;
        g_app.dirty = true;
        break;
    case VIEW_APP: sys_open_launcher(); break;
    default: sys_home(); break;
    }
}

bool sys_draw(uint32_t now)
{
    bool force = s_force;
    bool redraw = force || s_redraw;
    s_force = s_redraw = false;
    switch (s_view) {
    case VIEW_QUICK: return redraw && quick_draw();
    case VIEW_LAUNCHER: return redraw && launcher_draw();
    case VIEW_APP: return s_cur->draw(now, redraw);
    default: return false;
    }
}

void sys_shell_init(void)
{
    s_view = VIEW_FACE;
}

// ---------------------------------------------------------------- remote requests

static char s_req[48];
static volatile bool s_req_pending;

void sys_request(const char *cmd)
{
    strlcpy(s_req, cmd, sizeof(s_req));
    s_req_pending = true;
}

static void synth(ev_type_t t, int x, int y, int x0, int y0, ev_dir_t dir, int edge)
{
    ui_event_t e = {.type = t, .x = x, .y = y, .x0 = x0, .y0 = y0, .dir = dir, .edge = edge};
    sys_event(&e);
}

void sys_poll_requests(void)
{
    if (!s_req_pending) return;
    char r[48];
    strlcpy(r, s_req, sizeof(r));
    s_req_pending = false;
    int a, b;
    char d[12] = "", ed[12] = "";
    app_wake();
    if (strcmp(r, "home") == 0) sys_home();
    else if (strcmp(r, "quick") == 0) sys_open_quick();
    else if (strcmp(r, "launcher") == 0) sys_open_launcher();
    else if (sscanf(r, "app %d", &a) == 1) sys_open_app(a);
    else if (sscanf(r, "tap %d %d", &a, &b) == 2) {
        synth(EV_DOWN, a, b, a, b, 0, 0);
        synth(EV_TAP, a, b, a, b, 0, 0);
        synth(EV_UP, a, b, a, b, 0, 0);
    } else if (sscanf(r, "long %d %d", &a, &b) == 2) {
        synth(EV_DOWN, a, b, a, b, 0, 0);
        synth(EV_LONG, a, b, a, b, 0, 0);
        synth(EV_UP, a, b, a, b, 0, 0);
    } else if (sscanf(r, "swipe %11s %11s", d, ed) >= 1) {
        ev_dir_t dir = !strcmp(d, "left") ? DIR_LEFT : !strcmp(d, "right") ? DIR_RIGHT : !strcmp(d, "up") ? DIR_UP : DIR_DOWN;
        int edge = !strcmp(ed, "top") ? EDGE_TOP : !strcmp(ed, "bottom") ? EDGE_BOTTOM : !strcmp(ed, "left") ? EDGE_LEFT
                                                                                       : !strcmp(ed, "right") ? EDGE_RIGHT : 0;
        int x0 = edge == EDGE_LEFT ? 10 : edge == EDGE_RIGHT ? UI_W - 10 : UI_W / 2;
        int y0 = edge == EDGE_TOP ? 10 : edge == EDGE_BOTTOM ? UI_H - 10 : UI_H / 2;
        synth(EV_DOWN, x0, y0, x0, y0, 0, edge);
        synth(EV_SWIPE, x0, y0, x0, y0, dir, edge);
        synth(EV_UP, x0, y0, x0, y0, 0, edge);
    }
}
