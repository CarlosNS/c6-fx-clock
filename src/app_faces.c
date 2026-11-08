// Face picker: every face in the firmware (retired ones left out), by group as in the phone app. A
// tap shows it and goes back to the face. A green square marks the faces that answer the finger.
#include <stdio.h>
#include "apps.h"
#include "fx.h"
#include "sys.h"
#include "ui_icons.h"

#define TOP 122
#define ROW 70
#define X0 52
#define WID 306

static ui_scroll_t s_scroll;
static int s_ids[FX_COUNT], s_n, s_pressed = -1;

static void open(void)
{
    s_n = 0;
    for (int g = 0; g < FX_G_COUNT; g++)
        for (int i = 0; i < FX_COUNT; i++)
            if (!fx_list[i].retired && fx_list[i].group == g) s_ids[s_n++] = i;
    s_scroll.offset = 0;
    for (int k = 0; k < s_n; k++)
        if (s_ids[k] == g_app.fx) s_scroll.offset = (k - 1) * ROW;  // the current one in view
    if (s_scroll.offset < 0) s_scroll.offset = 0;
}

static int hit(int y)
{
    int k = (y - TOP + s_scroll.offset) / ROW;
    return y >= TOP && k >= 0 && k < s_n ? k : -1;
}

static bool draw(uint32_t now, bool force)
{
    if (!force) return false;
    s_scroll.view_h = UI_H - TOP - 40;
    s_scroll.content_h = s_n * ROW;
    int max = s_scroll.content_h - s_scroll.view_h;
    if (s_scroll.offset > max) s_scroll.offset = max > 0 ? max : 0;
    ui_background(false);
    ui_veil(60, UI_H, 3);
    for (int k = 0; k < s_n; k++) {
        int y = TOP + k * ROW - s_scroll.offset;
        if (y + ROW < TOP - 20 || y > UI_H) continue;
        const fx_t *f = &fx_list[s_ids[k]];
        bool cur = s_ids[k] == g_app.fx, p = k == s_pressed;
        ui_panel(X0, y, WID, ROW - 10, p ? MS_PANEL2 : MS_PANEL, cur ? MS_ORO : MS_LINEA, 4, false);
        int sc = ui_fit(&ui_jersey, 3, 2, WID - 50, f->label);
        ui_print(&ui_jersey, sc, X0 + 18, y + 8 + (3 - sc) * 8, f->label, cur ? MS_ORO2 : MS_CREMA);
        if (f->hint) ui_rect(X0 + WID - 26, y + 24, 10, 10, MS_VERDE);
    }
    ui_background_band(false, 0, TOP - 16);
    ui_shade(0, 0, UI_W, TOP - 16, 1);
    ui_status_bar();
    ui_print_c(&ui_jersey, 4, UI_W / 2, 64, TR("Esferas", "Faces"), MS_ORO, true);
    return true;
}

static void event(const ui_event_t *e)
{
    if (ui_scroll_event(&s_scroll, e)) sys_dirty();
    if (e->type == EV_DOWN) {
        s_pressed = hit(e->y);
        sys_dirty();
    } else if (e->type == EV_MOVE && s_scroll.moved > 12 && s_pressed >= 0) {
        s_pressed = -1;
    } else if (e->type == EV_UP) {
        s_pressed = -1;
        sys_dirty();
    } else if (e->type == EV_TAP) {
        int k = hit(e->y);
        if (k >= 0) {
            g_app.fx = s_ids[k];
            g_app.auto_cycle = 0;
            g_app.dirty = true;
            sys_home();
        }
    }
}

const app_t app_faces = {"Esferas", "Faces", icon_faces, open, draw, event, NULL, NULL};
