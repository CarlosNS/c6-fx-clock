// Face picker: a wheel with every face in the firmware (retired ones left out), by group as in the phone
// app, and then the custom faces on the microSD ("Mis esferas"). It goes round and round: drag it (it
// carries on a little and settles on a face), and the one in the middle is shown big with its group
// under the title. A tap on any face shows it and goes back to the face. A green square marks the
// faces that answer the finger.
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "apps.h"
#include "fx.h"
#include "sys.h"
#include "ui_icons.h"

#define TOP 146          // the wheel shows below the title
#define CY 318           // the middle of the wheel
#define ROW 66           // between two faces
#define RAD 250          // the wheel's radius: faces shrink towards the top and the bottom
#define WID 310          // width of the face in the middle
#define CUSTOM_MAX 24
#define LILA MS_SWAP(0xB47F)     // the custom faces stand apart in lilac
#define LILA_PANEL MS_SWAP(0x28E9)
#define LILA_PANEL2 MS_SWAP(0x392D)

typedef struct {
    int fx;              // face number
    int cust;            // custom face number (fx == FX_CUSTOM), else 0
    int group;           // FX_G_* or -1 for Mis esferas
    char name[32];
} item_t;

static item_t s_items[FX_COUNT + CUSTOM_MAX];
static int s_n;
static float s_pos, s_vel;  // where the wheel is (in faces) and how fast it turns (faces per second)
static bool s_drag;
static uint32_t s_last;

static int wrap(int k) { return ((k % s_n) + s_n) % s_n; }

static bool is_current(const item_t *it)
{
    return it->fx == g_app.fx && (it->fx != FX_CUSTOM || it->cust == g_app.cust);
}

static void open(void)
{
    s_n = 0;
    for (int g = 0; g < FX_G_COUNT; g++)
        for (int i = 0; i < FX_COUNT; i++)
            if (!fx_list[i].retired && fx_list[i].group == g) {
                item_t *it = &s_items[s_n++];
                it->fx = i, it->cust = 0, it->group = g;
                snprintf(it->name, sizeof(it->name), "%s", fx_list[i].label);
            }
    int ids[CUSTOM_MAX];
    char names[CUSTOM_MAX][32];
    int nc = custom_list(ids, names, CUSTOM_MAX);
    for (int k = 0; k < nc; k++) {
        item_t *it = &s_items[s_n++];
        it->fx = FX_CUSTOM, it->cust = ids[k], it->group = -1;
        memcpy(it->name, names[k], sizeof(it->name));
    }
    s_pos = 0;
    for (int k = 0; k < s_n; k++)
        if (is_current(&s_items[k])) s_pos = k;  // the face on now, in the middle
    s_vel = 0;
    s_drag = false;
    s_last = 0;
}

// The face under a y on the screen (spacing is even along y; only the size changes)
static int hit(int y)
{
    if (y < TOP || !s_n) return -1;
    return wrap((int)lroundf(s_pos + (y - CY) / (float)ROW));
}

static void choose(const item_t *it)
{
    g_app.auto_cycle = 0;
    if (it->fx == FX_CUSTOM) {
        char cmd[24];
        snprintf(cmd, sizeof(cmd), "cust sel %d", it->cust);
        custom_command(cmd);
    } else {
        g_app.fx = it->fx;
    }
    g_app.dirty = true;
    sys_home();
}

// A little photo (frame, sun, hill) marking a custom face, 2 px per dot
static void photo_icon(int x, int y, uint16_t c)
{
    static const char *const rows[9] = {"#########", "#.......#", "#....##.#", "#....##.#", "#.......#",
                                        "#..#....#", "#.###.#.#", "########"  "#", "#########"};
    for (int r = 0; r < 9; r++)
        for (int k = 0; rows[r][k]; k++)
            if (rows[r][k] == '#') ui_rect(x + k * 2, y + r * 2, 2, 2, c);
}

static const char *group_name(int g)
{
    static const char *const en[FX_G_COUNT] = {"Effects", "Video games", "Art", "Nature and space", "Clocks and tools"};
    if (g < 0) return TR("Mis esferas", "My faces");
    return g_app.lang ? en[g] : fx_group_names[g];
}

static bool draw(uint32_t now, bool force)
{
    float dt = s_last ? (now - s_last) / 1000.0f : 0;
    s_last = now;
    if (dt > 0.1f) dt = 0.1f;
    bool moving = false;
    if (s_n && !s_drag) {
        if (fabsf(s_vel) > 0.6f) {  // carrying on after a flick, slowing down
            s_pos += s_vel * dt;
            s_vel *= expf(-dt * 3.5f);
            moving = true;
        } else {                     // then settling on the nearest face
            s_vel = 0;
            float d = roundf(s_pos) - s_pos;
            if (fabsf(d) > 0.002f) {
                s_pos += d * fminf(1, dt * 12);
                moving = true;
            } else {
                s_pos = roundf(s_pos);
            }
        }
    }
    if (s_n) s_pos = fmodf(fmodf(s_pos, s_n) + s_n, s_n);
    if (!force && !moving && !s_drag) return false;

    ui_background(false);
    ui_veil(60, UI_H, 3);
    if (!s_n) {
        ui_print_c(&ui_jersey, 2, UI_W / 2, CY, TR("No hay esferas", "No faces"), MS_LINEA, false);
    }
    int base = (int)floorf(s_pos);
    // from the outside in, so the middle one is drawn last, on top
    static const int order[] = {-4, 5, -3, 4, -2, 3, -1, 2, 0, 1};
    int mid = s_n ? wrap((int)lroundf(s_pos)) : -1;
    for (int o = 0; o < 10 && s_n; o++) {
        int j = base + order[o];
        float d = j - s_pos, dy = d * ROW;
        if (fabsf(dy) >= RAD - 10) continue;
        int y = CY + (int)lroundf(dy) - (ROW - 10) / 2;
        if (y + (ROW - 10) / 2 < TOP || y > UI_H - 20) continue;  // not under the title
        float c = sqrtf(1 - (dy / RAD) * (dy / RAD));  // narrower away from the middle, like a drum
        int w = (int)(WID * c), x = (UI_W - w) / 2, k = wrap(j);
        const item_t *it = &s_items[k];
        bool centre = k == mid && fabsf(d) < 0.5f, cur = is_current(it);
        bool mine = it->fx == FX_CUSTOM;
        ui_panel(x, y, w, ROW - 10, mine ? (centre ? LILA_PANEL2 : LILA_PANEL) : centre ? MS_PANEL2 : MS_PANEL,
                 centre ? MS_ORO : cur ? MS_ORO2 : mine ? LILA : MS_LINEA, 4, centre);
        if (mine) photo_icon(x + 16, y + (ROW - 10) / 2 - 9, centre ? MS_ORO2 : LILA);
        int sc = ui_fit(&ui_jersey, centre ? 3 : 2, 2, w - (mine ? 84 : 44), it->name);
        uint16_t col = cur ? MS_ORO2 : fabsf(d) < 1.5f ? MS_CREMA : MS_LINEA;
        ui_print_c(&ui_jersey, sc, UI_W / 2, y + (ROW - 10) / 2 - sc * 7, it->name, col, centre);
        if (it->fx != FX_CUSTOM && fx_list[it->fx].hint) ui_rect(x + w - 22, y + 23, 10, 10, MS_VERDE);
    }
    ui_background_band(false, 0, TOP - 10);
    ui_shade(0, 0, UI_W, TOP - 10, 1);
    ui_status_bar();
    ui_print_c(&ui_jersey, 4, UI_W / 2, 58, TR("Esferas", "Faces"), MS_ORO, true);
    if (mid >= 0) ui_print_c(&ui_jersey, 2, UI_W / 2, 104, group_name(s_items[mid].group), s_items[mid].group < 0 ? LILA : MS_LINEA, false);
    return true;
}

static void event(const ui_event_t *e)
{
    if (!s_n) return;
    switch (e->type) {
    case EV_DOWN:
        s_drag = true;
        s_vel = 0;
        break;
    case EV_MOVE:
        if (!s_drag) break;
        s_pos -= e->dy / (float)ROW;
        s_vel = 0.5f * s_vel + 0.5f * (-e->dy / (float)ROW) * 30;  // faces per second, at ~30 readings a second
        sys_dirty();
        break;
    case EV_UP:
        s_drag = false;
        sys_dirty();
        break;
    case EV_TAP: {
        int k = hit(e->y);
        if (k >= 0) choose(&s_items[k]);
        break;
    }
    default:
        break;
    }
}

const app_t app_faces = {"Esferas", "Faces", icon_faces, open, draw, event, NULL, NULL};
