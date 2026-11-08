#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "app.h"
#include "input.h"
#include "ui.h"

// Watch apps, opened from the launcher (sys.c). Each one draws on the native 410x502 framebuffer in the
// Marea Solsticial style and gets the touch gestures; BOOT (or a swipe right from the left edge) goes
// back, which sys.c handles.

// Text in the language chosen in the settings (Spanish is the reference)
#define TR(es, en) (g_app.lang ? (en) : (es))

typedef struct {
    const char *name_es, *name_en;
    const char *const *icon;          // 16 x 16 pixel art (ui_icon)
    void (*open)(void);
    // Draws the app (ui_begin already done). force: the whole screen must be drawn (first frame,
    // coming back from another view). Returns false when nothing changed, and then the panel keeps
    // showing the last frame.
    bool (*draw)(uint32_t now, bool force);
    void (*event)(const ui_event_t *e);
    void (*close)(void);              // optional
    bool (*keep_on)(void);            // optional: the screen must not turn itself off (flashlight)
} app_t;

extern const app_t app_faces, app_settings, app_stopwatch, app_torch, app_alarms, app_steps, app_music,
    app_files;

// Shared look of an app screen: background, status bar and a title in gold
void app_frame(const char *title, bool dusk);
// Vertical scrolling list helper: the offset follows the finger and stays within the content
typedef struct {
    int offset;      // px scrolled
    int content_h;   // total height of the content
    int view_h;      // visible height
    bool dragging;
    int moved;       // px moved in this touch (a tap only counts if it hardly moved)
} ui_scroll_t;
// Feeds a gesture; returns true if the view must be redrawn
bool ui_scroll_event(ui_scroll_t *s, const ui_event_t *e);
