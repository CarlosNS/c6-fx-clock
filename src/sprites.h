#pragma once
#include <stdbool.h>
#include <stdint.h>

// Hand-drawn pixel art. Each row is a string; every character is a palette key looked up in a
// 128-entry color map ('.' or a missing character = transparent).
typedef struct {
    uint8_t w, h;
    const char *const *rows;
} sprite_t;

extern const sprite_t spr_hero_run0, spr_hero_run1, spr_hero_jump;
extern const sprite_t spr_gem, spr_slime, spr_heart, spr_cloud;
extern const sprite_t spr_warrior_a, spr_warrior_b, spr_mage_a, spr_mage_b;

// Base colors (r,g,b) for every palette key used by the sprites
void sprite_default_rgb(char key, uint8_t *r, uint8_t *g, uint8_t *b);
// Fills cmap[128] with the default palette, optionally darkened/blue-shifted by `light` (0..256)
void sprite_build_cmap(uint16_t *cmap, int light);

// Draws with each sprite pixel as a scale x scale block; (x, y) = top-left in screen pixels
void sprite_draw(uint16_t *fb, const sprite_t *s, int x, int y, int scale, bool flip, const uint16_t *cmap);
// Nearest-neighbour scaled draw, scale in 1/256 units
void sprite_draw_scaled(uint16_t *fb, const sprite_t *s, int x, int y, int scale_q8, bool flip, const uint16_t *cmap);
