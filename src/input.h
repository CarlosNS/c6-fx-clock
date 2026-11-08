#pragma once
#include <stdbool.h>
#include <stdint.h>

// Touch gestures, built from the raw touch readings (touch_ft3168.c) once per frame.

typedef enum {
    EV_DOWN,    // finger down at (x, y)
    EV_MOVE,    // finger moved by (dx, dy) to (x, y)
    EV_UP,      // finger lifted (always comes, after a TAP / SWIPE / LONG or not)
    EV_TAP,     // short touch without moving
    EV_LONG,    // held still for LONG_MS (sent while the finger is still down)
    EV_SWIPE,   // quick stroke, sent on release; dir and the edge it started from
} ev_type_t;

typedef enum { DIR_LEFT, DIR_RIGHT, DIR_UP, DIR_DOWN } ev_dir_t;

enum { EDGE_NONE = 0, EDGE_TOP = 1, EDGE_BOTTOM = 2, EDGE_LEFT = 4, EDGE_RIGHT = 8 };

typedef struct {
    ev_type_t type;
    int x, y;      // current position (panel pixels)
    int dx, dy;    // movement since the previous MOVE
    int x0, y0;    // where the touch started
    ev_dir_t dir;  // SWIPE
    int edge;      // EDGE_* the touch started at (within EDGE_PX of it)
} ui_event_t;

#define EDGE_PX 44

// Reads the touch controller and queues the events it produces; call once per loop
void input_poll(uint32_t now_ms);
// Next queued event; false when there are none
bool input_next(ui_event_t *ev);
// Drops a touch in progress (e.g. the one that woke the screen): no more events until it ends
void input_swallow(void);
