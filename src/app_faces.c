// Face picker, "the shop window": every face in the firmware (retired ones left out) by group, as in the
// phone app, and then the custom faces on the microSD ("Mis esferas"), in the same order as a swipe on
// the face. Each face is a little watch screen (its thumbnail, with the panel's rounded corners): the
// one in the middle is big, its neighbours peek in from the sides, smaller and dimmed, with a small gap
// between groups. Swipe sideways to go through them (a flick carries on), up or down to jump to the
// next or the previous group; a tap on the middle one grows it to the whole screen and puts it on, a tap
// on a side one brings it to the middle. Above: the group's name and a mark per group; below: the face's
// name, a tick if it is the one on, a green finger if it answers the finger, and where it is in its
// group. Opening it from the face, the face shrinks into its card (trans.c, T_ZOOM_OUT).
//
// Thumbnails: the firmware faces' come from web.pack (thumb/fxN.jpg, tools/face_thumbs.py), the custom
// faces' from their picture in fx_custom.c's cache (custom_warm loads them all). A low-priority task
// decodes the few around the middle into PSRAM; a face whose thumbnail is not ready shows its name.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "apps.h"
#include "board.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "fx.h"
#include "sys.h"
#include "settings.h"
#include "trans.h"
#include "ui_icons.h"
#include "web_assets.h"

#define CW 200           // the card in the middle (the thumbnails' size)
#define CH 245
#define CR 58            // its corner radius: the panel's, to scale
#define CY 250           // the middle of the cards
#define TITLE_Y 48       // the group's name, its marks and the face's name
#define MARKS_Y 114
#define NAME_Y 384
#define DOTS_Y 448
#define STEP 172         // between two cards
#define GAP 26           // and a little more between two groups
#define SIDE 0.7f        // the size of the cards beside it
#define CUSTOM_MAX 24
#define SLOTS 7          // thumbnails kept: the middle one and three on each side
#define LILA MS_SWAP(0xB47F)     // the custom faces stand apart in lilac
#define LILA_DIM MS_SWAP(0x5A32)
#define ZOOM_MS 260

typedef struct {
    int fx;              // face number
    int cust;            // custom face number (fx == FX_CUSTOM), else 0
    int group;           // FX_G_* or FX_G_COUNT for Mis esferas
    char name[32];
} item_t;

static item_t s_items[FX_COUNT + CUSTOM_MAX];
static int s_n, s_gb[FX_COUNT + CUSTOM_MAX], s_ngb;  // groups crossed up to each face, and in a lap
static float s_pos, s_target, s_vel;  // where the strip is and where it is going (in faces)
static bool s_drag;
static int s_mode;                    // this touch: 0 not yet known, 1 sideways, 2 up or down
static int s_x0, s_y0;
static float s_p0;
static uint32_t s_last, s_tmove;
static uint32_t s_zoom_t0;            // choosing: the card grows to the whole screen
static int s_zoom_k = -1;

static int wrap(int k) { return ((k % s_n) + s_n) % s_n; }
static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

static bool is_current(const item_t *it)
{
    return it->fx == g_app.fx && (it->fx != FX_CUSTOM || it->cust == g_app.cust);
}

// ---------------------------------------------------------------- thumbnails, in the background

enum { T_EMPTY, T_WANT, T_BUSY, T_READY, T_MISSING };
typedef struct {
    int item;
    volatile int state;
    int dist;            // how far from the middle (the nearest first)
    uint16_t *px;        // CW x CH
} thumb_t;

static thumb_t s_th[SLOTS];
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static TaskHandle_t s_task;
static volatile bool s_fresh;         // a thumbnail got ready: draw again
static uint32_t s_retry;

static bool decode(const item_t *it, uint16_t *dst)
{
    static uint8_t *pool;  // TJpgDec's work area, this task's own (in PSRAM: internal RAM is the scarce one)
    if (!pool) pool = heap_caps_malloc(4096, MALLOC_CAP_SPIRAM);
    if (!pool) return false;
    if (it->fx == FX_CUSTOM) return custom_thumb(it->cust, dst, CW, CH, pool);
    char name[24];
    size_t size;
    int len = snprintf(name, sizeof(name), "thumb/fx%d.jpg", it->fx);
    const uint8_t *jpg = web_asset_find(name, len, &size);
    return jpg && jpeg_decode(jpg, size, dst, CW, CH, pool);
}

static void thumb_task(void *arg)
{
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        for (;;) {
            int best = -1;
            taskENTER_CRITICAL(&s_mux);
            for (int i = 0; i < SLOTS; i++)
                if (s_th[i].state == T_WANT && s_th[i].px && (best < 0 || s_th[i].dist < s_th[best].dist)) best = i;
            if (best >= 0) s_th[best].state = T_BUSY;
            int item = best >= 0 ? s_th[best].item : -1;
            taskEXIT_CRITICAL(&s_mux);
            if (best < 0) break;
            bool ok = item < s_n && decode(&s_items[item], s_th[best].px);
            s_th[best].state = ok ? T_READY : T_MISSING;  // (only this task changes a busy one)
            s_fresh = true;
            sys_dirty();
        }
    }
}

static void thumbs_open(void)
{
    for (int i = 0; i < SLOTS; i++) {
        while (s_th[i].state == T_BUSY) vTaskDelay(1);  // (a decode left from the last time)
        if (!s_th[i].px) s_th[i].px = heap_caps_malloc(CW * CH * 2, MALLOC_CAP_SPIRAM);
        s_th[i].state = T_EMPTY;
        s_th[i].item = -1;
    }
    if (!s_task) {  // its stack in PSRAM, as fx_custom.c's loader: internal RAM is short with Bluetooth on
        const size_t stack = 6144;
        static StaticTask_t tcb;
        static StackType_t *st;
        if (!st) st = heap_caps_malloc(stack, MALLOC_CAP_SPIRAM);
        if (st) s_task = xTaskCreateStaticPinnedToCore(thumb_task, "thumbs", stack, NULL, 1, st, &tcb, tskNO_AFFINITY);
    }
}

static void thumbs_close(void)
{
    for (int i = 0; i < SLOTS; i++) {
        while (s_th[i].state == T_BUSY) vTaskDelay(1);
        free(s_th[i].px);
        s_th[i].px = NULL;
        s_th[i].state = T_EMPTY;
    }
}

// The faces around the middle get a place; the ones gone far give theirs back
static void thumbs_want(int mid)
{
    bool ask = false, retry = now_ms() - s_retry > 600;  // a custom face may have loaded meanwhile
    if (retry) s_retry = now_ms();
    taskENTER_CRITICAL(&s_mux);
    for (int i = 0; i < SLOTS; i++) {
        if (s_th[i].item < 0) continue;
        int d = abs(wrap(s_th[i].item - mid + s_n / 2) - s_n / 2);
        if (d > SLOTS / 2 && s_th[i].state != T_BUSY) s_th[i].state = T_EMPTY, s_th[i].item = -1;
        else s_th[i].dist = d;
        if (retry && s_th[i].state == T_MISSING && s_items[s_th[i].item].fx == FX_CUSTOM) s_th[i].state = T_WANT, ask = true;
    }
    for (int d = 0; d <= SLOTS / 2 && d * 2 < s_n + 1; d++)
        for (int sg = 0; sg < (d ? 2 : 1); sg++) {
            int k = wrap(mid + (sg ? -d : d)), have = 0;
            for (int i = 0; i < SLOTS; i++) have |= s_th[i].item == k;
            if (have) continue;
            for (int i = 0; i < SLOTS; i++)
                if (s_th[i].state == T_EMPTY && s_th[i].px) {
                    s_th[i].item = k, s_th[i].dist = d, s_th[i].state = T_WANT, ask = true;
                    break;
                }
        }
    taskEXIT_CRITICAL(&s_mux);
    if (ask && s_task) xTaskNotifyGive(s_task);
}

static const uint16_t *thumb(int k)
{
    for (int i = 0; i < SLOTS; i++)
        if (s_th[i].item == k && s_th[i].state == T_READY) return s_th[i].px;
    return NULL;
}

// ---------------------------------------------------------------- the strip

// Where face j (any whole number: the strip goes round) sits along the strip, in pixels
static int strip_x(int j)
{
    int lap = (int)floorf(j / (float)s_n);
    return j * STEP + GAP * (lap * s_ngb + s_gb[wrap(j)]);
}

static float strip_xf(float p)
{
    int f = (int)floorf(p);
    return strip_x(f) + (strip_x(f + 1) - strip_x(f)) * (p - f);
}

static void faces_open(void)
{
    // the same order as a swipe on the face (fx_order). The custom faces from the list remembered (read
    // again whenever their files change): reading the card here would hold the screen ~0.4 s
    static face_ref_t list[FX_ORDER_MAX];
    s_n = fx_order(list, false);
    for (int k = 0; k < s_n; k++) {
        item_t *it = &s_items[k];
        it->fx = list[k].fx, it->cust = list[k].cust;
        it->group = list[k].fx == FX_CUSTOM ? FX_G_COUNT : fx_list[list[k].fx].group;
        snprintf(it->name, sizeof(it->name), "%s", list[k].name);
    }
    s_ngb = 0;
    for (int k = 0; k < s_n; k++) {
        if (k && s_items[k].group != s_items[k - 1].group) s_ngb++;
        s_gb[k] = s_ngb;
    }
    s_ngb++;  // and from the last group back to the first
    s_pos = 0;
    for (int k = 0; k < s_n; k++)
        if (is_current(&s_items[k])) s_pos = k;  // the face on now, in the middle
    s_target = s_pos;
    s_vel = 0;
    s_drag = false;
    s_last = 0;
    s_zoom_k = -1;
    thumbs_open();
    custom_warm();
}

static void faces_close(void)
{
    thumbs_close();
    custom_prefetch_around();  // the custom faces' cache as the face now on needs it (or let go)
}

static void apply(const item_t *it)
{
    g_app.auto_cycle = 0;
    if (it->fx == FX_CUSTOM) {  // (not "cust sel": that reloads it from the card, for after the app rewrites it)
        settings_set("cust", it->cust);
        settings_set("fx", FX_CUSTOM);
    } else {
        g_app.fx = it->fx;
    }
    g_app.dirty = true;
    sys_home();
}

static void choose(int k)
{
    if (!thumb(k)) {  // no picture to grow: straight to the face
        apply(&s_items[k]);
        return;
    }
    s_zoom_k = k;
    s_zoom_t0 = now_ms();
    sys_dirty();
}

// The first face of the next group (dir 1) or of the one before (dir -1)
static void jump_group(int dir)
{
    int j = (int)lroundf(s_target);
#define G(j) (s_items[wrap(j)].group)
    if (dir > 0) {
        int n = 0;
        do j++;
        while (G(j) == G(j - 1) && n++ < s_n);
    } else {
        for (int pass = 0; pass < 2; pass++) {  // to the start of this group, then of the one before
            if (pass) j--;
            int n = 0;
            while (G(j - 1) == G(j) && n++ < s_n) j--;
        }
    }
#undef G
    s_target = j;
}

// ---------------------------------------------------------------- drawing

// A tiny pixel picture, 2 px per dot
static void pix(const char *const *rows, int n, int x, int y, uint16_t c)
{
    for (int r = 0; r < n; r++)
        for (int k = 0; rows[r][k]; k++)
            if (rows[r][k] == '#') ui_rect(x + k * 2, y + r * 2, 2, 2, c);
}
static const char *const FINGER[10] = {"...##....", "...##....", "...##....", "...#####.", "...######",
                                       "#..######", "##.######", ".########", "..#######", "...#####."};
static const char *const TICK[9] = {"........#", ".......##", "......##.", ".....##..", "#...##...",
                                    "##.##....", ".###.....", "..#......", "........."};

static const char *group_name(int g)
{
    static const char *const en[FX_G_COUNT] = {"Effects", "Video games", "Art", "Nature and space", "Clocks and tools"};
    if (g >= FX_G_COUNT) return TR("Mis esferas", "My faces");
    return g_app.lang ? en[g] : fx_group_names[g];
}

static void card(int k, int cx, float s, int dim)
{
    const item_t *it = &s_items[k];
    int w = (int)(CW * s), h = (int)(CH * s), x = cx - w / 2, y = CY - h / 2, r = (int)(CR * s);
    bool mine = it->fx == FX_CUSTOM;
    if (is_current(it)) ui_round_rect(x - 4, y - 4, w + 8, h + 8, r + 4, ui_mix(MS_NOCHE, MS_ORO, dim));
    else if (mine) ui_round_rect(x - 3, y - 3, w + 6, h + 6, r + 3, ui_mix(MS_NOCHE, LILA, dim));
    const uint16_t *px = thumb(k);
    if (px) {
        ui_blit_round(px, CW, CH, x, y, w, h, r, dim);
        return;
    }
    // not ready yet: its name on a dark card
    ui_round_rect(x, y, w, h, r, mine ? LILA_DIM : MS_PANEL);
    int sc = ui_fit(&ui_jersey, 2, 1, w - 30, it->name);
    ui_print_c(&ui_jersey, sc, cx, CY - sc * 7, it->name, ui_mix(MS_NOCHE, MS_CREMA, dim), false);
}

static bool draw(uint32_t now, bool force)
{
    float dt = s_last ? (now - s_last) / 1000.0f : 0;
    s_last = now;
    if (dt > 0.1f) dt = 0.1f;
    bool moving = false;
    if (s_n && !s_drag) {
        float d = s_target - s_pos;
        if (fabsf(d) > 0.002f) {
            s_pos += d * fminf(1, dt * 9);
            moving = true;
        } else {
            s_pos = s_target;
        }
    }
    if (s_n && fabsf(s_pos) > s_n * 4) {  // keep the numbers small (the strip goes round)
        float o = floorf(s_pos / s_n) * s_n;
        s_pos -= o, s_target -= o;
    }
    int mid = s_n ? wrap((int)lroundf(s_pos)) : -1;
    if (s_n) thumbs_want(mid);
    bool fresh = s_fresh;
    s_fresh = false;
    if (!force && !moving && !s_drag && !fresh && s_zoom_k < 0) return false;

    ui_background(false);
    ui_veil(60, UI_H, 3);
    ui_status_bar();
    if (!s_n) {
        ui_print_c(&ui_jersey, 2, UI_W / 2, CY, TR("No hay esferas", "No faces"), MS_LINEA, false);
        return true;
    }
    const item_t *m = &s_items[mid];
    bool mine = m->group >= FX_G_COUNT;
    const char *g = group_name(m->group);
    ui_print_c(&ui_jersey, ui_fit(&ui_jersey, 3, 2, 300, g), UI_W / 2, TITLE_Y, g, mine ? LILA : MS_ORO, true);
    for (int i = 0, x = UI_W / 2 - (FX_G_COUNT * 14 + 12) / 2; i <= FX_G_COUNT; i++) {  // a mark per group
        bool on = i == m->group;
        uint16_t c = i == FX_G_COUNT ? (on ? LILA : LILA_DIM) : on ? MS_ORO : MS_LINEA;
        ui_rect(x, MARKS_Y, on ? 18 : 6, 6, c);
        x += (on ? 18 : 6) + 8;
    }

    // the cards, from the outside in (the middle one on top)
    int base = (int)lroundf(s_pos);
    float xp = strip_xf(s_pos);
    static const int order[] = {-2, 2, -1, 1, 0};
    for (int o = 0; o < 5; o++) {
        int j = base + order[o];
        float dx = strip_x(j) - xp, d = fminf(fabsf(dx) / STEP, 1.6f);
        float s = 1 - (1 - SIDE) * fminf(d, 1) - 0.1f * fmaxf(0, d - 1);
        int cx = UI_W / 2 + (int)lroundf(dx);
        if (cx + CW / 2 < 0 || cx - CW / 2 > UI_W) continue;
        card(wrap(j), cx, s, (int)(fminf(d, 1) * 150));
    }

    // the name, and after it a tick if it is the face on and a green finger if it answers the finger
    bool cur = is_current(m), hint = m->fx != FX_CUSTOM && fx_list[m->fx].hint;
    int bw = (cur ? 30 : 0) + (hint ? 30 : 0);
    int sc = ui_fit(&ui_jersey, 3, 2, UI_W - 70 - bw, m->name);
    int nw = ui_print_w(&ui_jersey, sc, m->name), nx = UI_W / 2 - (nw + bw) / 2;
    ui_print_sh(&ui_jersey, sc, nx, NAME_Y, m->name, MS_CREMA);
    int bx = nx + nw + 12, by = NAME_Y + sc * 15 / 2 - 10;  // beside the capitals, centred on them
    if (cur) pix(TICK, 9, bx, by + 2, MS_ORO), bx += 30;
    if (hint) pix(FINGER, 10, bx, by, MS_VERDE);
    int first = mid, count = 0;  // where it is in its group
    while (s_items[wrap(first - 1)].group == m->group && count++ < s_n) first--;
    int n = 0, at = 0;
    for (int j = first; n < s_n && s_items[wrap(j)].group == m->group; j++, n++)
        if (wrap(j) == mid) at = n;
    if (n > 1 && n <= 12) ui_dots(UI_W / 2, DOTS_Y, n, at);

    if (s_zoom_k >= 0) {  // chosen: its card grows to the whole screen, then the face is on
        float p = fminf(1, (now - s_zoom_t0) / (float)ZOOM_MS);
        float e = 1 - (1 - p) * (1 - p) * (1 - p);
        int w = (int)(CW + (UI_W - CW) * e), h = (int)(CH + (UI_H - CH) * e);
        const uint16_t *px = thumb(s_zoom_k);
        if (px) ui_blit_round(px, CW, CH, (UI_W - w) / 2, CY - CH / 2 + (int)((0 - (CY - CH / 2)) * e), w, h,
                              CR + (int)((BOARD_LCD_CORNER_R - CR) * e), 0);
        if (p >= 1 || !px) {
            int k = s_zoom_k;
            s_zoom_k = -1;
            apply(&s_items[k]);
        }
    }
    return true;
}

// ---------------------------------------------------------------- the finger

static void event(const ui_event_t *e)
{
    if (!s_n || s_zoom_k >= 0) return;
    switch (e->type) {
    case EV_DOWN:
        s_drag = true;
        s_mode = 0;
        s_x0 = e->x, s_y0 = e->y;
        s_target = s_pos;  // a touch stops it where it is
        s_p0 = s_pos;
        s_vel = 0;
        s_tmove = now_ms();
        break;
    case EV_MOVE: {
        if (!s_drag) break;
        int dx = e->x - s_x0, dy = e->y - s_y0;
        if (!s_mode && dx * dx + dy * dy > 12 * 12) s_mode = abs(dx) > abs(dy) ? 1 : 2;
        if (s_mode == 1) {
            float np = s_p0 - dx / (float)(STEP + 8);
            uint32_t t = now_ms();
            float dts = fmaxf(0.01f, (t - s_tmove) / 1000.0f);
            s_vel = 0.6f * s_vel + 0.4f * ((np - s_pos) / dts);  // faces per second
            s_tmove = t;
            s_pos = s_target = np;
        }
        sys_dirty();
        break;
    }
    case EV_UP: {
        s_drag = false;
        int dy = e->y - s_y0;
        if (s_mode == 1) {
            if (now_ms() - s_tmove > 120) s_vel = 0;  // held still before letting go: no flick
            s_target = lroundf(s_pos + fmaxf(-8, fminf(8, s_vel * 0.22f)));
        } else if (s_mode == 2 && abs(dy) > 40) {
            jump_group(dy < 0 ? 1 : -1);
        }  // (a tap: EV_TAP may have just set where to go)
        s_target = lroundf(s_target);  // always settling on a face
        sys_dirty();
        break;
    }
    case EV_TAP:
        if (e->y < CY - CH / 2 - 10 || e->y > CY + CH / 2 + 10) break;
        if (abs(e->x - UI_W / 2) <= CW / 2) choose(wrap((int)lroundf(s_pos)));
        else s_target = lroundf(s_pos) + (e->x > UI_W / 2 ? 1 : -1);
        sys_dirty();
        break;
    default:
        break;
    }
}

// Where the face shrinks to when the picker opens from it (sys.c)
void faces_zoom_target(void)
{
    trans_zoom_target(UI_W / 2 - CW / 2, CY - CH / 2, CW, CH, CR);
}

const app_t app_faces = {"Esferas", "Faces", icon_faces, faces_open, draw, event, faces_close, NULL, NULL};
