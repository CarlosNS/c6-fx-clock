// Flashlight: the whole screen in white at full brightness (the AMOLED's peak). A tap switches to a
// dim red light that keeps night vision; BOOT goes back.
#include "apps.h"
#include "sys.h"
#include "ui_icons.h"

static bool s_red;

static void open(void)
{
    s_red = false;
    g_app.bri_override = 100;
}

static void close(void)
{
    g_app.bri_override = -1;
}

static bool draw(uint32_t now, bool force)
{
    if (!force) return false;
    ui_fill(s_red ? ui_rgb(150, 0, 0) : 0xFFFF);
    uint16_t hint = s_red ? ui_rgb(90, 0, 0) : ui_rgb(200, 200, 200);
    ui_print_c(&ui_vt, 1, UI_W / 2, UI_H - 74, s_red ? TR("toca: luz blanca", "tap: white light") : TR("toca: luz roja", "tap: red light"),
              hint, false);
    return true;
}

static void event(const ui_event_t *e)
{
    if (e->type == EV_TAP) {
        s_red = !s_red;
        g_app.bri_override = s_red ? 30 : 100;
        sys_dirty();
    }
}

static bool keep_on(void) { return true; }

const app_t app_torch = {"Linterna", "Flashlight", icon_torch, open, draw, event, close, keep_on};
