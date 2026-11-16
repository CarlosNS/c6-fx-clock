// Slides between the system's screens (trans.h). A slide lasts DUR ms and eases out. The picture of the
// screen that leaves comes from the last frame sent to the panel (native, or the wide canvas enlarged
// x1.5); the new screen is drawn whole every frame meanwhile and the two are put together row by row.
// Faces are not drawn on the native framebuffer, so the picture taken when the face was left is what
// slides back in on the way home (if it is recent and the face is still the same one).
#include "trans.h"
#include <string.h>
#include "app.h"
#include "board.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "lcd.h"
#include "sys.h"
#include "ui.h"

#define DUR 170
#define ZOOM_DUR 300
#define W LCD_PW
#define H LCD_PH
#define FACE_MAX_AGE 20000  // ms: an older picture of the face would show a stale time

static uint16_t *s_old, *s_face;
static trans_t s_kind;
static uint32_t s_t0, s_face_t;
static bool s_started;  // the clock starts with the first frame shown (the new screen may take a while to open)
static int s_face_fx = -1, s_face_cust;
static int s_zx, s_zy, s_zw = W, s_zh = H, s_zr;

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

// The last frame on the panel, at panel size
static bool grab(uint16_t *dst)
{
    int w, h;
    const uint16_t *src = lcd_last_frame(&w, &h);
    if (!src) return false;
    if (w == W && h == H) {
        memcpy(dst, src, W * H * 2);
        return true;
    }
    if (w != LCD_WW || h != LCD_WH) return false;  // the old portrait canvases: no slide
    for (int y = 0; y < H; y++) {
        const uint16_t *s = src + (y * 2 / 3) * w;
        uint16_t *d = dst + y * W;
        for (int x = 0; x < W; x++) d[x] = s[x * 2 / 3];
    }
    return true;
}

void trans_start(trans_t kind)
{
    s_kind = T_NONE;
    if (kind == T_NONE || g_app.screen_off) return;
    if (!s_old) s_old = heap_caps_malloc(W * H * 2, MALLOC_CAP_SPIRAM);
    if (!s_face) s_face = heap_caps_malloc(W * H * 2, MALLOC_CAP_SPIRAM);
    if (!s_old || !s_face || !grab(s_old)) return;
    if (sys_view() == VIEW_FACE) {  // leaving the face: keep it for the way back
        memcpy(s_face, s_old, W * H * 2);
        s_face_t = now_ms();
        s_face_fx = g_app.fx;
        s_face_cust = g_app.cust;
    }
    s_kind = kind;
    s_t0 = now_ms();
    s_started = false;
}

void trans_zoom_target(int x, int y, int w, int h, int r)
{
    s_zx = x, s_zy = y, s_zw = w, s_zh = h, s_zr = r;
}

static int dur(void) { return s_kind == T_ZOOM_OUT ? ZOOM_DUR : DUR; }

bool trans_active(uint32_t now)
{
    if (s_kind == T_NONE) return false;
    if (!s_started) {
        if ((int32_t)(now - s_t0) > 1000) s_kind = T_NONE;  // never shown: give up
        return s_kind != T_NONE;
    }
    if ((int32_t)(now - s_t0) >= dur()) s_kind = T_NONE;
    return s_kind != T_NONE;
}

bool trans_face_ready(void)
{
    return s_kind != T_NONE && s_face_fx == g_app.fx && s_face_cust == g_app.cust && now_ms() - s_face_t < FACE_MAX_AGE;
}

// How far the slide is, 0..k, eased out (fast first, settling)
static int eased(uint32_t now, int k)
{
    int t = (int)(now - s_t0), d = dur();
    if (t < 0) t = 0;
    if (t > d) t = d;
    float p = t / (float)d;
    p = 1 - (1 - p) * (1 - p) * (1 - p);
    return (int)(p * k + 0.5f);
}

void trans_compose(uint16_t *nfb, uint32_t now)
{
    if (s_kind == T_NONE || !s_old) return;
    if (!s_started) s_t0 = now, s_started = true;
    switch (s_kind) {
    case T_COVER_FROM_TOP: {  // rows 0..k: the bottom of the new screen; below, the old one still
        int k = eased(now, H);
        for (int y = 0; y < k; y++) memcpy(nfb + y * W, nfb + (y + H - k) * W, W * 2);
        memcpy(nfb + k * W, s_old + k * W, (size_t)(H - k) * W * 2);
        break;
    }
    case T_COVER_FROM_BOTTOM: {  // rows H-k..H: the top of the new screen
        int k = eased(now, H);
        for (int y = H - 1; y >= H - k; y--) memcpy(nfb + y * W, nfb + (y - (H - k)) * W, W * 2);
        memcpy(nfb, s_old, (size_t)(H - k) * W * 2);
        break;
    }
    case T_COVER_FROM_RIGHT: {  // columns W-k..W: the left of the new screen
        int k = eased(now, W);
        for (int y = 0; y < H; y++) {
            uint16_t *r = nfb + y * W;
            memmove(r + W - k, r, k * 2);
            memcpy(r, s_old + y * W, (W - k) * 2);
        }
        break;
    }
    case T_UNCOVER_UP: {  // the old screen moved up by k
        int k = eased(now, H);
        memcpy(nfb, s_old + k * W, (size_t)(H - k) * W * 2);
        break;
    }
    case T_UNCOVER_DOWN: {
        int k = eased(now, H);
        memcpy(nfb + k * W, s_old, (size_t)(H - k) * W * 2);
        break;
    }
    case T_UNCOVER_RIGHT: {
        int k = eased(now, W);
        for (int y = 0; y < H; y++) memcpy(nfb + y * W + k, s_old + y * W, (W - k) * 2);
        break;
    }
    case T_ZOOM_OUT: {  // the whole old screen, shrinking into the card (the new screen around it)
        int k = eased(now, 1000);
        int x = k * s_zx / 1000, y = k * s_zy / 1000;
        int w = W + k * (s_zw - W) / 1000, h = H + k * (s_zh - H) / 1000;
        int r = BOARD_LCD_CORNER_R + k * (s_zr - BOARD_LCD_CORNER_R) / 1000;
        ui_begin(nfb);
        ui_blit_round(s_old, W, H, x, y, w, h, r, 0);
        break;
    }
    default:
        break;
    }
}

void trans_compose_face(uint16_t *nfb, uint32_t now)
{
    memcpy(nfb, s_face, W * H * 2);
    trans_compose(nfb, now);
}
