#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "clock.h"
#include "fonts.h"
#include "lcd.h"

// The ESP32-S3 has a single-precision FPU; many faces still use the integer / fixed-point tables they
// were written with (sin_q14, sin_u8), which are as fast and keep results identical.

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
    // What it draws on: FX_COMPAT the old 172x320 canvas in a box (faces not redone yet), FX_WIDE the
    // whole screen at 274x336 (WW x WH, enlarged x1.5), FX_NATIVE the whole 410x502 panel (FW x FH).
    // The screen corners are rounded (BOARD_LCD_CORNER_R; WIDE_CORNER_R on the wide canvas).
    uint8_t res;
} fx_t;

enum { FX_COMPAT, FX_WIDE, FX_NATIVE };
#define FW LCD_PW
#define FH LCD_PH
#define WW LCD_WW
#define WH LCD_WH
#define WIDE_CORNER_R 80

#define FX_COUNT 29
#define FX_CUSTOM 28  // "Mis esferas" (fx_custom.c)
// Custom faces: "cust list|sel N|try N|del N|reload N" (true if it was one), and the list as JSON
bool custom_command(const char *cmd);
size_t custom_list_json(char *buf, size_t size);
// The custom faces on the card: their numbers and names (sorted by number); 0 without a card
int custom_list(int *ids, char (*names)[32], int max);
// The same, remembered from the last read (read again only if the faces on the card changed)
// Starts loading, in the background, the custom faces next to the current face in the swipe order
void custom_prefetch_around(void);
// The face picker: load every custom face in the background (for their thumbnails), and a thumbnail of
// custom face id (dw x dh) once it is loaded; false while it is not. pool: TJpgDec's 4 KB work area.
void custom_warm(void);
bool custom_thumb(int id, uint16_t *dst, int dw, int dh, uint8_t pool[4096]);
// A baseline JPEG into an RGB565 picture (panel order) of dw x dh, cropped; pool: TJpgDec's work area
bool jpeg_decode(const uint8_t *src, size_t len, uint16_t *dst, int dw, int dh, uint8_t pool[4096]);
int custom_list_cached(const int **ids, const char (**names)[32]);
// A file under /esferas was written or removed: the remembered list must be read again
void custom_list_dirty(void);
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

// The faces in the order the watch shows them, the same in the picker wheel, a swipe on the face and
// BOOT: the built-in ones by group, then every custom face (Mis esferas) by its number
typedef struct {
    int fx;            // fx_list index
    int cust;          // custom face number (fx == FX_CUSTOM), else 0
    const char *name;  // label (built-in) or the custom face's name
} face_ref_t;
#define FX_ORDER_MAX (FX_COUNT + 24)
int fx_order(face_ref_t *out, bool fresh);  // fresh: read the custom faces from the card again
// Show the next (dir > 0) or previous face in that order; returns its name for the toast
const char *fx_step(int dir);

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
void onix_enter(void);
void onix_render(uint16_t *fb, uint32_t t, uint32_t dt);
void relax_enter(void);
void relax_render(uint16_t *fb, uint32_t t, uint32_t dt);
void sculpt_enter(void);
void sculpt_render(uint16_t *fb, uint32_t t, uint32_t dt);
void metal_enter(void);
void metal_render(uint16_t *fb, uint32_t t, uint32_t dt);
void custom_enter(void);
void custom_render(uint16_t *fb, uint32_t t, uint32_t dt);

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
