#include "input.h"
#include <stdlib.h>
#include "touch.h"
#include "ui.h"

#define TAP_SLOP 14      // px a tap may wander
#define TAP_MS 450
#define LONG_MS 650
#define SWIPE_MIN 60     // px along the main axis
#define SWIPE_MS 800
#define QUEUE 8

static ui_event_t s_q[QUEUE];
static int s_head, s_tail;
static bool s_down, s_long_sent, s_swallow;
static int s_x0, s_y0, s_lx, s_ly, s_edge, s_maxd;
static uint32_t s_t0;

static void push(ev_type_t t, int x, int y, int dx, int dy, ev_dir_t dir)
{
    int next = (s_tail + 1) % QUEUE;
    if (next == s_head) return;  // full: drop
    s_q[s_tail] = (ui_event_t){.type = t, .x = x, .y = y, .dx = dx, .dy = dy, .x0 = s_x0, .y0 = s_y0, .dir = dir, .edge = s_edge};
    s_tail = next;
}

void input_swallow(void)
{
    s_swallow = true;
}

bool input_next(ui_event_t *ev)
{
    if (s_head == s_tail) return false;
    *ev = s_q[s_head];
    s_head = (s_head + 1) % QUEUE;
    return true;
}

void input_poll(uint32_t now)
{
    int x, y;
    bool pressed = touch_read(&x, &y);
    if (pressed && !s_down) {
        s_down = true;
        s_swallow = false;
        s_long_sent = false;
        s_x0 = s_lx = x;
        s_y0 = s_ly = y;
        s_t0 = now;
        s_maxd = 0;
        s_edge = (y < EDGE_PX ? EDGE_TOP : 0) | (y >= UI_H - EDGE_PX ? EDGE_BOTTOM : 0) |
                 (x < EDGE_PX ? EDGE_LEFT : 0) | (x >= UI_W - EDGE_PX ? EDGE_RIGHT : 0);
        push(EV_DOWN, x, y, 0, 0, 0);
        return;
    }
    if (pressed && s_down) {
        if (s_swallow) return;
        int d = abs(x - s_x0) + abs(y - s_y0);
        if (d > s_maxd) s_maxd = d;
        if (x != s_lx || y != s_ly) push(EV_MOVE, x, y, x - s_lx, y - s_ly, 0);
        s_lx = x;
        s_ly = y;
        if (!s_long_sent && s_maxd <= TAP_SLOP && now - s_t0 >= LONG_MS) {
            s_long_sent = true;
            push(EV_LONG, x, y, 0, 0, 0);
        }
        return;
    }
    if (!pressed && s_down) {
        s_down = false;
        if (s_swallow) {
            s_swallow = false;
            return;
        }
        int dx = s_lx - s_x0, dy = s_ly - s_y0;
        uint32_t dt = now - s_t0;
        if (!s_long_sent && s_maxd <= TAP_SLOP && dt < TAP_MS) {
            push(EV_TAP, s_lx, s_ly, 0, 0, 0);
        } else if (!s_long_sent && dt < SWIPE_MS && (abs(dx) >= SWIPE_MIN || abs(dy) >= SWIPE_MIN)) {
            ev_dir_t dir = abs(dx) > abs(dy) ? (dx < 0 ? DIR_LEFT : DIR_RIGHT) : (dy < 0 ? DIR_UP : DIR_DOWN);
            push(EV_SWIPE, s_lx, s_ly, dx, dy, dir);
        }
        push(EV_UP, s_lx, s_ly, 0, 0, 0);
    }
}
