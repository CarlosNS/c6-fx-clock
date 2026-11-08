#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "ui_fonts.h"

// Drawing toolkit for the system screens and apps, in the "Marea Solsticial" style (docs/PLAN-marea.md):
// night pixel art by the sea, indigo panels with pixel borders and cut corners, cream text, two pixel
// fonts, hard shadows and colour only when it means something. Everything draws on the native
// 410x502 framebuffer set by ui_begin(), clipped to it.

// ---- Palette (RGB565 already in the panel's byte order) ----
#define MS_SWAP(c) ((uint16_t)((((c) >> 8) & 0xFF) | (((c) & 0xFF) << 8)))
#define MS_NOCHE MS_SWAP(0x1085)       // background
#define MS_PANEL MS_SWAP(0x20C8)       // panels and cards
#define MS_PANEL2 MS_SWAP(0x290A)      // pressed panel, secondary button
#define MS_LINEA MS_SWAP(0x41CF)       // neutral borders
#define MS_BARRA_OFF MS_SWAP(0x39AD)   // empty part of bars
#define MS_FONDO_GRAF MS_SWAP(0x1886)  // chart background
#define MS_CREMA MS_SWAP(0xF77C)       // main text (never pure white)
#define MS_GRIS MS_SWAP(0xACF9)        // labels, secondary text
#define MS_ORO MS_SWAP(0xFD8B)         // the brand accent: titles, highlighted borders, main button
#define MS_ORO2 MS_SWAP(0xFE8C)        // subtitles, highlighted numbers
#define MS_CORAL MS_SWAP(0xEB0C)       // problems only
#define MS_VERDE MS_SWAP(0x7EB4)       // good, running, charging, connected
#define MS_PILOTO_OFF MS_SWAP(0x49F0)  // indicator off / unknown
#define MS_PARADO MS_SWAP(0x6AF3)      // stopped on purpose
#define MS_HALO_VERDE MS_SWAP(0x1A46)
#define MS_HALO_ORO MS_SWAP(0x59C3)
#define MS_HALO_CORAL MS_SWAP(0x58E5)
#define MS_NEGRO 0x0000

// Screen and the area always visible: the corners are rounded (radius BOARD_LCD_CORNER_R)
#define UI_W 410
#define UI_H 502

// Target framebuffer (native, UI_W x UI_H)
void ui_begin(uint16_t *fb);
uint16_t *ui_fb(void);

// ---- Shapes ----
void ui_fill(uint16_t c);
void ui_rect(int x, int y, int w, int h, uint16_t c);
void ui_frame(int x, int y, int w, int h, int t, uint16_t c);   // outline t px thick
// Darkens an area towards black: level 1 = 25 %, 2 = 50 %, 3 = 75 % (hard shadows, veils)
void ui_shade(int x, int y, int w, int h, int level);
// Panel: fill, pixel border g px thick with cut corners, optional hard shadow (6 right, 8 down, 35 %)
void ui_panel(int x, int y, int w, int h, uint16_t fill, uint16_t border, int g, bool shadow);
// Vertical veil from transparent to `level` darkness, so text reads over the illustrated background
void ui_veil(int y0, int y1, int max_level);
// Is (x, y) inside the visible screen (rounded corners, 2 px edge)?
bool ui_visible(int x, int y);

// ---- Text (UTF-8; y is the top of the line) ----
int ui_line_h(const ui_font_t *f, int scale);
int ui_print_w(const ui_font_t *f, int scale, const char *s);
int ui_print(const ui_font_t *f, int scale, int x, int y, const char *s, uint16_t color);
// With a hard shadow `scale` px down-right (Jersey titles: 2 px per scale step looks like the guide)
int ui_print_sh(const ui_font_t *f, int scale, int x, int y, const char *s, uint16_t color);
// Centred on cx; with shadow if `sh`
void ui_print_c(const ui_font_t *f, int scale, int cx, int y, const char *s, uint16_t color, bool sh);
// Right-aligned to x
void ui_print_r(const ui_font_t *f, int scale, int x, int y, const char *s, uint16_t color);
// Largest scale from max down to min at which s fits in max_w (min if none does)
int ui_fit(const ui_font_t *f, int max_scale, int min_scale, int max_w, const char *s);

// ---- Widgets ----
enum { UI_BTN_MAIN, UI_BTN_SECONDARY, UI_BTN_DANGER };
// Button with a 4 px hard shadow; pressed: moves 2 px down-right and the shadow shrinks
void ui_button(int x, int y, int w, int h, const char *label, int kind, bool pressed);
// Bar of w x h: background barra-off, `permille` filled in `color`
void ui_bar(int x, int y, int w, int h, int permille, uint16_t color);
// On / off switch, 64 x 32
void ui_switch(int x, int y, bool on);
// Battery: outline in cream, terminal on the right, fill gold (green while charging, coral when low)
void ui_battery(int x, int y, int w, int h, int percent, bool charging);
// The status bar's battery: 30 x 16 icon and the percentage to its right (about 80 px in all)
void ui_battery_pct(int x, int y, int percent, bool charging);
// Page indicator: n small squares centred on cx, the current one filled gold
void ui_dots(int cx, int y, int n, int cur);
// 16 x 16 pixel-art icon (rows of palette letters, see ui.c) drawn at `scale`
void ui_icon(const char *const rows[16], int x, int y, int scale);
// Status bar: the time in the middle, Bluetooth link on the left, battery on the right
void ui_status_bar(void);

// ---- Background ----
// The illustrated night sea (or dusk, for settings and pairing), generated once into PSRAM and copied
void ui_background(bool dusk);
// Copies rows y0..y1-1 of the background (to clean a band, e.g. under the status bar of a scrolled list)
void ui_background_band(bool dusk, int y0, int y1);

// ---- Colour helpers ----
uint16_t ui_rgb(uint8_t r, uint8_t g, uint8_t b);       // panel byte order
uint16_t ui_mix(uint16_t a, uint16_t b, int a_256);      // a * a_256/256 + b * (1 - a_256/256)
