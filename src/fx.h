#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "clock.h"
#include "fonts.h"
#include "lcd.h"

// The ESP32-C6 has no FPU: per-pixel work uses integer / fixed-point math and lookup tables,
// floats are only used for a handful of per-frame values.

enum { FX_G_EFFECTS, FX_G_GAMES, FX_G_ART, FX_G_NATURE, FX_G_CLOCKS, FX_G_COUNT };
extern const char *const fx_group_names[FX_G_COUNT];

typedef struct {
    const char *name;      // short ASCII name for the debug overlay and logs
    bool landscape;        // LAND_W x LAND_H instead of LCD_W x LCD_H
    void (*enter)(void);
    // t: animation time in ms (scaled by the speed setting), dt: scaled ms since last frame.
    // Each face draws its own clock using g_now.
    void (*render)(uint16_t *fb, uint32_t t, uint32_t dt);
    // Catalogue for the control apps (served as /faces)
    const char *label;     // display name (UTF-8)
    int group;             // FX_G_*
    const char *hint;      // what touching the pad does; NULL = not interactive
    const char *desc;      // what the face shows
    const char *opts;      // face-specific settings block in the apps ("wx", "cl"); NULL = none
    // Retired face: its code is gone but its number stays taken (clocks save the chosen number), so
    // the list never shifts. The clock skips it and the catalogue sends it with group -1.
    bool retired;
} fx_t;

#define FX_COUNT 24
extern const fx_t fx_list[FX_COUNT];
_Static_assert(FX_COUNT <= 31, "g_app.fx_off is a 31-bit mask: widen it before adding more faces");

extern int16_t sin_q14[1024];  // sin(i * 2pi / 1024) * 16384
extern uint8_t sin_u8[256];    // (sin(i * 2pi / 256) + 1) * 127.5

void fx_common_init(void);
uint16_t hsv565(uint8_t h, uint8_t s, uint8_t v);
uint32_t fx_rand(void);
// Next face after `from` that is not retired (wraps around)
int fx_next(int from);
// Previous face before `from` that is not retired (wraps around)
int fx_prev(int from);

// Shared clock layouts (centered horizontally on the current orientation)
// Hours above minutes starting at y; returns the y just below the minutes
int clock_draw_stacked(uint16_t *fb, const font_t *f, int y, int gap, uint16_t fill, uint16_t outline, int mode);
// "HH:MM" on one row at y, colon blinking with the seconds
void clock_draw_row(uint16_t *fb, const font_t *f, int y, int spacing, uint16_t fill, uint16_t outline, int mode);
// Progress of the current minute (only when seconds are enabled)
void clock_draw_seconds_bar(uint16_t *fb, int x, int y, int w, int h, uint16_t fg, uint16_t bg);
// "MAR 27 OCT"
void clock_date_str(char *buf);
// "HH:MM" with gradient fill, centered on cx, blinking colon
void clock_row_grad(uint16_t *fb, const font_t *f, int cx, int y, int spacing, uint16_t top, uint16_t bottom,
                    uint16_t outline, int shadow);

// Faces
void plasma_enter(void);
void plasma_render(uint16_t *fb, uint32_t t, uint32_t dt);
void cube_enter(void);
void cube_render(uint16_t *fb, uint32_t t, uint32_t dt);
void tunnel_enter(void);
void tunnel_render(uint16_t *fb, uint32_t t, uint32_t dt);
void lava_enter(void);
void lava_render(uint16_t *fb, uint32_t t, uint32_t dt);
void life_enter(void);
void life_render(uint16_t *fb, uint32_t t, uint32_t dt);
void hud_enter(void);
void hud_render(uint16_t *fb, uint32_t t, uint32_t dt);
void quest_enter(void);
void quest_render(uint16_t *fb, uint32_t t, uint32_t dt);
void rpg_enter(void);
void rpg_render(uint16_t *fb, uint32_t t, uint32_t dt);
// Based on famous public-domain works
void starry_enter(void);
void starry_render(uint16_t *fb, uint32_t t, uint32_t dt);
void wifimon_enter(void);
void wifimon_render(uint16_t *fb, uint32_t t, uint32_t dt);
void earthrise_enter(void);
void earthrise_render(uint16_t *fb, uint32_t t, uint32_t dt);
void sakura_enter(void);
void sakura_render(uint16_t *fb, uint32_t t, uint32_t dt);
void blackhole_enter(void);
void blackhole_render(uint16_t *fb, uint32_t t, uint32_t dt);
// Original
void synth_enter(void);
void synth_render(uint16_t *fb, uint32_t t, uint32_t dt);
void sand_enter(void);
void sand_render(uint16_t *fb, uint32_t t, uint32_t dt);
void gears_enter(void);
void gears_render(uint16_t *fb, uint32_t t, uint32_t dt);
void rain_enter(void);
void rain_render(uint16_t *fb, uint32_t t, uint32_t dt);
void goal_enter(void);
void goal_render(uint16_t *fb, uint32_t t, uint32_t dt);
void weather_enter(void);
void weather_render(uint16_t *fb, uint32_t t, uint32_t dt);
void classic_enter(void);
void classic_render(uint16_t *fb, uint32_t t, uint32_t dt);
void feria_enter(void);
void feria_render(uint16_t *fb, uint32_t t, uint32_t dt);

// Scratch memory shared by faces that need big lookup tables. Only one face runs at a time:
// a face claims it in enter() (rebuilding its tables if another face used it in between).
#define FX_SCRATCH_SIZE 21600
extern uint8_t fx_scratch[FX_SCRATCH_SIZE];
// Returns true if the caller already owns the scratch (tables still valid); claims it otherwise
bool fx_scratch_claim(int owner_id);

// Shared helper: deterministic integer hash
static inline uint32_t fx_hash(uint32_t x, uint32_t y)
{
    uint32_t h = x * 374761393u + y * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}
