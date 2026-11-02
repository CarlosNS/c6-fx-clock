#include "sprites.h"
#include <string.h>
#include "gfx.h"

// Adventurer, 16x16, facing right
static const char *const hero_run0[] = {
    "......kkkk......",
    ".....krrrrk.....",
    "....krrrrrrk....",
    "...kkkkkkkkkk...",
    "....khhsswk.....",
    "....khssskk.....",
    ".....ksssk......",
    "...krrkkkkk.....",
    "..krrkbbbbbk....",
    "..kk.kbbbbsk....",
    "....kbyyybk.....",
    "....kbbbbbk.....",
    "....kBk.kBk.....",
    "...kgk...kgk....",
    "...kgk....kgk...",
    "...kk.....kk....",
};
static const char *const hero_run1[] = {
    "................",
    "......kkkk......",
    ".....krrrrk.....",
    "....krrrrrrk....",
    "...kkkkkkkkkk...",
    "....khhsswk.....",
    "....khssskk.....",
    ".....ksssk......",
    "..krrkkkkkk.....",
    ".krrkbbbbbk.....",
    "..k.kbbbbbsk....",
    "....kbyyybk.....",
    "....kBBkBBk.....",
    ".....kgkgk......",
    "....kgk.kgk.....",
    "....kk...kk.....",
};
static const char *const hero_jump[] = {
    "......kkkk......",
    ".....krrrrk.....",
    "....krrrrrrk....",
    "...kkkkkkkkkk...",
    "....khhsswk.ks..",
    "....khssskk.kk..",
    ".....ksssk.kbk..",
    ".krrkkkkkkkbk...",
    "krrk.kbbbbbk....",
    "kk...kbbbbbk....",
    "....kbyyybk.....",
    "....kbbbbbk.....",
    "...kBBk.kBBk....",
    "..kgk.....kgk...",
    "..kk.......kk...",
    "................",
};
static const char *const gem[] = {
    "...kk...",
    "..kcck..",
    ".kwccck.",
    "kwccccdk",
    "kccccddk",
    ".kccddk.",
    "..kddk..",
    "...kk...",
};
static const char *const slime[] = {
    "....kkkk....",
    "..kkPPppkk..",
    ".kPPppppppk.",
    ".kPppppppppk",
    "kPpwkpppwkpk",
    "kppwkpppwkpk",
    "kppppppppppk",
    "kpppkkkkpppk",
    "kppppppppppk",
    ".kkkkkkkkkk.",
};
static const char *const heart[] = {
    ".kk.kk.",
    "krwkrrk",
    "krrrrrk",
    ".krrrk.",
    "..krk..",
    "...k...",
};
static const char *const cloud[] = {
    "..........wwww..........",
    "....www..wwwwwww........",
    "..wwwwwwwwwwwwwwww.ww...",
    ".wwwwwwwwwwwwwwwwwwwwww.",
    "wwwwwwwwwwwwwwwwwwwwwwww",
    "wwwwwwwwwwwwwwwwwwwwwwww",
    ".llllllllllllllllllllll.",
    "...lllllll...lllllll....",
};

// Travelling party for "Aventura", native resolution, colored outlines. Body rows are shared and
// combined with two leg poses (stride / passing) to make the walk cycle.
#define WR0 "......ooooo....."
#define WR1 "....ooHjjjHo...."
#define WR2 "...oHjjjjjjjo..."
#define WR3 "..oHjHjjjjjjHo.."
#define WR4 "..oHHHjjjjjHHo.."
#define WR5 "..oHHHHHSSSSSo.."
#define WR6 "..oHHHHSSSSESo.."
#define WR7 "...oHHHzSSSSSo.."
#define WR8 "....oHozSSSzo..."
#define WR9 "...ooqCCCCCqoo.."
#define WR10 ".oqCoTTTTTTtCo.."
#define WR11 "oqCo.oTTTTTtSo.."
#define WR12 ".oo..otTTTTtoo.."
#define WR13 ".....oLLGLLLo..."
#define WR14 ".....oTTTTTto..."
#define WR15 ".....otTTTtto..."
#define WR16 ".....ommmmmmo..."
#define WARRIOR_BODY WR0, WR1, WR2, WR3, WR4, WR5, WR6, WR7, WR8, WR9, WR10, WR11, WR12, WR13, WR14, WR15, WR16
static const char *const warrior_a[] = {
    WARRIOR_BODY,
    "....ommo.ommo...",
    "...ommo...ommo..",
    "...omno....omno.",
    "..omno......omo.",
    "..oOuo......oOo.",
    ".oOOuo.....oOOo.",
    ".ooooo.....oooo.",
};
static const char *const warrior_b[] = {
    WARRIOR_BODY,
    ".....ommmmmo....",
    "......ommmo.....",
    "......omnmo.....",
    "......omno......",
    "......oOuo......",
    ".....oOOuo......",
    ".....ooooo......",
};
#define MR0 "........oo......"
#define MR1 ".......oVvo....."
#define MR2 "......oVVvo....."
#define MR3 ".....oVVVVvo...."
#define MR4 "....oVVGVVVvo..."
#define MR5 "..oooVVVVVVVooo."
#define MR6 ".oVVVVVVVVVVVVvo"
#define MR7 "..oooooooooooo.."
#define MR8 "....oxNNSSSSo..."
#define MR9 "....oxNNSSESo..."
#define MR10 "....oxNNzSSSo..."
#define MR11 ".....oxozSSzo..."
#define MR12 "....oQKKKKKKo..."
#define MR13 "...oQKKKKKKKKo.."
#define MR14 "...oQKKGKKKKSo.."
#define MR15 "...oQKKKKKKKKo.."
#define MR16 "...oQQKKKKKKQo.."
#define MR17 "...oQQQKKKKQQo.."
#define MR18 "....oQQQQQQQo..."
#define MAGE_BODY MR0, MR1, MR2, MR3, MR4, MR5, MR6, MR7, MR8, MR9, MR10, MR11, MR12, MR13, MR14, MR15, MR16, MR17, MR18
static const char *const mage_a[] = {
    MAGE_BODY,
    ".....oOo.oOo....",
    "....oOOo.oOOo...",
    "....oooo.oooo...",
};
static const char *const mage_b[] = {
    MAGE_BODY,
    "......oOOo......",
    ".....oOOOo......",
    ".....ooooo......",
};

const sprite_t spr_warrior_a = {16, 24, warrior_a};
const sprite_t spr_warrior_b = {16, 24, warrior_b};
const sprite_t spr_mage_a = {16, 22, mage_a};
const sprite_t spr_mage_b = {16, 22, mage_b};

const sprite_t spr_hero_run0 = {16, 16, hero_run0};
const sprite_t spr_hero_run1 = {16, 16, hero_run1};
const sprite_t spr_hero_jump = {16, 16, hero_jump};
const sprite_t spr_gem = {8, 8, gem};
const sprite_t spr_slime = {12, 10, slime};
const sprite_t spr_heart = {7, 6, heart};
const sprite_t spr_cloud = {24, 8, cloud};

void sprite_default_rgb(char key, uint8_t *r, uint8_t *g, uint8_t *b)
{
    static const struct { char k; uint8_t r, g, b; } pal[] = {
        {'k', 24, 18, 36},   {'r', 220, 50, 60},  {'s', 250, 200, 160}, {'h', 120, 70, 40},
        {'w', 255, 255, 255}, {'b', 60, 100, 210}, {'B', 35, 55, 140},  {'y', 250, 210, 60},
        {'g', 110, 70, 40},  {'c', 80, 220, 255}, {'d', 30, 120, 210},  {'p', 150, 70, 200},
        {'P', 210, 140, 250}, {'l', 200, 210, 230},
        // party palette
        {'o', 40, 28, 50},   {'H', 95, 55, 40},   {'j', 165, 105, 65}, {'S', 250, 208, 170},
        {'z', 214, 150, 120}, {'E', 35, 30, 60},  {'C', 245, 155, 45}, {'q', 195, 95, 35},
        {'T', 65, 90, 170},  {'t', 42, 55, 120},  {'L', 125, 85, 45},  {'m', 75, 65, 90},
        {'n', 52, 44, 66},   {'O', 115, 75, 48},  {'u', 75, 48, 32},   {'V', 115, 72, 185},
        {'v', 78, 46, 135},  {'N', 95, 155, 235}, {'x', 60, 105, 185}, {'K', 125, 72, 175},
        {'Q', 82, 46, 124},  {'G', 245, 205, 85},
    };
    for (unsigned i = 0; i < sizeof(pal) / sizeof(pal[0]); i++) {
        if (pal[i].k == key) {
            *r = pal[i].r;
            *g = pal[i].g;
            *b = pal[i].b;
            return;
        }
    }
    *r = *g = *b = 255;
}

void sprite_build_cmap(uint16_t *cmap, int light)
{
    static const char keys[] = "krshwbBygcdpPloHjSzECqTtLmnOuVvNxKQG";
    memset(cmap, 0, 128 * sizeof(uint16_t));
    for (const char *k = keys; *k; k++) {
        uint8_t r, g, b;
        sprite_default_rgb(*k, &r, &g, &b);
        // Night: darker and slightly blue
        int lb = light + (256 - light) / 3;
        cmap[(int)*k] = rgb565(r * light >> 8, g * light >> 8, (b * lb >> 8) > 255 ? 255 : b * lb >> 8);
    }
}

void sprite_draw(uint16_t *fb, const sprite_t *s, int x, int y, int scale, bool flip, const uint16_t *cmap)
{
    for (int row = 0; row < s->h; row++) {
        const char *line = s->rows[row];
        int len = strlen(line);
        for (int col = 0; col < s->w && col < len; col++) {
            char c = line[col];
            if (c == '.' || (unsigned char)c >= 128 || !cmap[(int)c]) continue;
            int sx = flip ? s->w - 1 - col : col;
            gfx_fill_rect(fb, x + sx * scale, y + row * scale, scale, scale, cmap[(int)c]);
        }
    }
}

void sprite_draw_scaled(uint16_t *fb, const sprite_t *s, int x, int y, int scale_q8, bool flip, const uint16_t *cmap)
{
    int dw = s->w * scale_q8 >> 8, dh = s->h * scale_q8 >> 8;
    if (dw <= 0 || dh <= 0) return;
    for (int dy = 0; dy < dh; dy++) {
        int py = y + dy;
        if ((unsigned)py >= (unsigned)g_lcd_h) continue;
        const char *line = s->rows[dy * s->h / dh];
        int len = strlen(line);
        for (int dx = 0; dx < dw; dx++) {
            int px = x + dx;
            if ((unsigned)px >= (unsigned)g_lcd_w) continue;
            int col = dx * s->w / dw;
            if (flip) col = s->w - 1 - col;
            if (col >= len) continue;
            char c = line[col];
            if (c == '.' || (unsigned char)c >= 128 || !cmap[(int)c]) continue;
            fb[py * g_lcd_w + px] = cmap[(int)c];
        }
    }
}
