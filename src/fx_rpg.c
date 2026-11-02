// "RPG" (portrait): a 3D battle in the style of the late PlayStation-era Final Fantasy games.
// A cinematic camera sways around a stone arena with a glowing rune circle; a crystal guardian
// with orbiting shards floats in the middle; the party stands in the foreground as pixel-art
// billboards (HD-2D). The UI uses translucent windows. The three ATB bars fill during the
// minute, and when the minute changes a party member casts a spell with the new time as damage.
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "app.h"
#include "control.h"
#include "fx.h"
#include "gfx.h"
#include "sprites.h"

#define W LCD_W
#define H LCD_H
#define SCENE_H 240
#define FOCAL 150.0f
#define CX 86.0f
#define CY 118.0f
#define CAST_MS 3600

typedef struct { float x, y, z; } v3;
static inline v3 v_add(v3 a, v3 b) { return (v3){a.x + b.x, a.y + b.y, a.z + b.z}; }
static inline v3 v_sub(v3 a, v3 b) { return (v3){a.x - b.x, a.y - b.y, a.z - b.z}; }
static inline v3 v_mul(v3 a, float k) { return (v3){a.x * k, a.y * k, a.z * k}; }
static inline float v_dot(v3 a, v3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline v3 v_cross(v3 a, v3 b) { return (v3){a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
static inline v3 v_norm(v3 a) { float l = sqrtf(v_dot(a, a)); return v_mul(a, 1.0f / l); }

static v3 cam_pos, cam_r, cam_u, cam_f;

static bool project(v3 p, int *sx, int *sy, float *z)
{
    v3 d = v_sub(p, cam_pos);
    float zc = v_dot(d, cam_f);
    if (zc < 0.3f) return false;
    *sx = (int)(CX + v_dot(d, cam_r) * FOCAL / zc);
    *sy = (int)(CY - v_dot(d, cam_u) * FOCAL / zc);
    if (z) *z = zc;
    return true;
}

static const char *const names[3] = {"Aria", "Bran", "Lysa"};
static const char *const spells[4] = {"Lanza Astral", "Nova Glacial", "Tormenta Ignea", "Juicio Solar"};
static const uint8_t spell_rgb[4][3] = {{150, 230, 255}, {170, 220, 255}, {255, 140, 60}, {255, 215, 90}};
static const v3 party_pos[3] = {{-1.5f, 0, 3.1f}, {0.1f, 0, 3.7f}, {1.6f, 0, 3.0f}};

static uint16_t party_cmap[3][128];
static int s_last_min = -1, s_caster, s_spell;
static uint32_t s_cast_t;
static bool s_casting;
static char s_damage[8];

void rpg_enter(void)
{
    for (int i = 0; i < 3; i++) sprite_build_cmap(party_cmap[i], 256);
    // Third member: warrior sprite recolored (blond, teal scarf, crimson tunic)
    party_cmap[2][(int)'H'] = rgb565(200, 150, 60);
    party_cmap[2][(int)'j'] = rgb565(245, 210, 110);
    party_cmap[2][(int)'C'] = rgb565(60, 200, 190);
    party_cmap[2][(int)'q'] = rgb565(30, 140, 140);
    party_cmap[2][(int)'T'] = rgb565(180, 50, 70);
    party_cmap[2][(int)'t'] = rgb565(120, 30, 50);
    s_casting = false;
}

// ---------------------------------------------------------------- background + floor

static inline uint32_t hash(uint32_t x, uint32_t y)
{
    uint32_t h = x * 374761393u + y * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

static void sky(uint16_t *fb, int horizon, float yaw, uint32_t t)
{
    for (int y = 0; y < SCENE_H && y < horizon + 2; y++) {
        int f = horizon > 0 ? y * 256 / horizon : 256;
        if (f > 256) f = 256;
        uint16_t c = rgb565(8 + (62 * f >> 8), 10 + (36 * f >> 8), 32 + (90 * f >> 8));
        gfx_fill_rect(fb, 0, y, W, 1, c);
    }
    for (int i = 0; i < 40; i++) {
        uint32_t h = hash(i, 3);
        int x = (int)(h % 600) - (int)(yaw * 120), y = (h >> 10) % 90;
        x = ((x % 600) + 600) % 600;
        if (x < W && y < horizon - 4 && sin_u8[(uint8_t)(t / 20 + i * 29)] > 100) fb[y * W + x] = rgb565(220, 225, 255);
    }
    // A ringed planet hanging in the sky
    int px = (int)(CX + (-0.35f - yaw) * FOCAL), py = 34;
    if (px > -40 && px < W + 40) {
        gfx_glow(fb, px, py, 34, rgb565(120, 90, 200), 6);
        for (int dy = -16; dy <= 16; dy++)
            for (int dx = -16; dx <= 16; dx++) {
                int d2 = dx * dx + dy * dy;
                if (d2 > 256) continue;
                int l = 140 - (dx + dy) * 5 - d2 / 4;  // lit from the upper-left
                l = l < 20 ? 20 : l > 230 ? 230 : l;
                int x = px + dx, y = py + dy;
                if ((unsigned)x < W && (unsigned)y < SCENE_H) fb[y * W + x] = rgb565(l * 3 / 4, l * 2 / 3, l);
            }
        for (int a = 0; a < 1024; a += 4) {  // ring, front half drawn over the planet
            int x = px + (sin_q14[a] * 28 >> 14), y = py + (sin_q14[(a + 256) & 1023] * 6 >> 14);
            if ((unsigned)x < W && (unsigned)y < SCENE_H) fb[y * W + x] = gfx_blend(fb[y * W + x], rgb565(230, 210, 255), a > 512 ? 6 : 12);
        }
    }
    // Distant crystal spires on the horizon, moving with the camera
    for (int x = 0; x < W; x++) {
        int idx = (int)((yaw + (x - CX) / FOCAL) * 140) + 4000;
        int best = horizon;
        bool lit = false;
        for (int k = idx / 11 - 1; k <= idx / 11 + 1; k++) {
            uint32_t h = hash(k, 17);
            int c = k * 11 + (int)(h % 5), hh = 8 + (int)((h >> 8) % 26), w = 3 + (int)((h >> 16) % 4);
            int d = idx - c;
            if (d < -w || d > w) continue;
            int top = horizon - hh + abs(d) * hh / w;
            if (top < best) { best = top; lit = d < 0; }
        }
        for (int y = best; y < horizon && y < SCENE_H; y++)
            if (y >= 0) fb[y * W + x] = y == best ? (lit ? rgb565(140, 220, 255) : rgb565(70, 60, 130)) : rgb565(34, 24, 66);
    }
}

enum { M_STONE_A, M_STONE_B, M_GROUT, M_RIM, M_GRASS_A, M_GRASS_B, M_VOID, M_RUNE, M_SHOCK, M_SHADOW, M_COUNT };

static void floor_render(uint16_t *fb, int horizon, uint32_t t, float shock_r, uint16_t spell_col)
{
    static const uint8_t base[M_COUNT][3] = {
        {112, 108, 122}, {92, 88, 104}, {54, 50, 66}, {70, 62, 86}, {38, 40, 60},
        {30, 32, 50}, {16, 14, 34}, {0, 0, 0}, {0, 0, 0}, {48, 44, 60},
    };
    int pulse = sin_u8[(uint8_t)(t / 8)];
    uint16_t rune = rgb565(80 + pulse / 6, 200 + pulse / 12, 255);
    uint16_t fogc = rgb565(70, 46, 122);
    int ang = t / 12;
    int ca = sin_q14[(ang + 256) & 1023], sa = sin_q14[ang & 1023];
    int cb = sin_q14[(ang + 128 + 256) & 1023], sb = sin_q14[(ang + 128) & 1023];
    const int R_IN0 = 1536, R_IN1 = 1731, R_OUT0 = 2873, R_OUT1 = 3136, R_ARENA = 4516, R_RIM = 5184, R_GRASS = 20736;
    const int SQ = 589, LW = 16;  // star square half-size 2.3 and line width, Q8
    const int R_STAR = 1239;      // 2.2^2 in Q8: the star's edges lie between 2.3 and 3.25 from the center
    int sh0 = -1, sh1 = -1;
    if (shock_r > 0) {
        float a = shock_r - 0.2f, b = shock_r + 0.2f;
        sh0 = a > 0 ? (int)(a * a * 256) : 0;
        sh1 = (int)(b * b * 256);
    }
    for (int sy = horizon < 0 ? 0 : horizon; sy < SCENE_H; sy++) {
        float a = (sy - CY) / FOCAL;
        v3 dir = v_sub(cam_f, v_mul(cam_u, a));
        if (dir.y > -0.003f) continue;
        float tt = -cam_pos.y / dir.y;
        uint16_t *row = fb + sy * W;
        int fk = tt < 5 ? 0 : tt > 30 ? 16 : (int)((tt - 5) * 16 / 25);
        if (fk >= 16 || tt > 40) {
            for (int x = 0; x < W; x++) row[x] = fogc;
            continue;
        }
        uint16_t pal[M_COUNT];
        for (int m = 0; m < M_COUNT; m++) pal[m] = gfx_blend(rgb565(base[m][0], base[m][1], base[m][2]), fogc, fk);
        pal[M_RUNE] = gfx_blend(rune, fogc, fk / 2);
        pal[M_SHOCK] = spell_col;
        v3 p0 = v_add(cam_pos, v_mul(dir, tt));
        p0 = v_add(p0, v_mul(cam_r, -tt * CX / FOCAL));
        v3 dp = v_mul(cam_r, 2 * tt / FOCAL);  // one sample every 2 px (PS1-style), written twice
        int32_t ax = (int32_t)(p0.x * 65536), az = (int32_t)(p0.z * 65536);
        int32_t dax = (int32_t)(dp.x * 65536), daz = (int32_t)(dp.z * 65536);
        // The two rotated frames of the rune star are linear along the row too: step them incrementally
        int32_t xr = (int32_t)(((int64_t)ax * ca - (int64_t)az * sa) >> 14), dxr = (dax * ca - daz * sa) >> 14;
        int32_t zr = (int32_t)(((int64_t)ax * sa + (int64_t)az * ca) >> 14), dzr = (dax * sa + daz * ca) >> 14;
        int32_t xs = (int32_t)(((int64_t)ax * cb - (int64_t)az * sb) >> 14), dxs = (dax * cb - daz * sb) >> 14;
        int32_t zs = (int32_t)(((int64_t)ax * sb + (int64_t)az * cb) >> 14), dzs = (dax * sb + daz * cb) >> 14;
        for (int x = 0; x < W; x += 2, ax += dax, az += daz, xr += dxr, zr += dzr, xs += dxs, zs += dzs) {
            int x8 = ax >> 8, z8 = az >> 8;
            int r2 = (x8 * x8 + z8 * z8) >> 8;
            int m;
            if (r2 >= R_GRASS) m = M_VOID;
            else if (r2 >= R_RIM) m = ((x8 * 13 + z8 * 7) >> 7) & ((z8 * 11 - x8 * 5) >> 7) & 1 ? M_GRASS_A : M_GRASS_B;
            else if (r2 >= R_ARENA) m = M_RIM;
            else if ((r2 >= R_IN0 && r2 < R_IN1) || (r2 >= R_OUT0 && r2 < R_OUT1)) m = M_RUNE;
            else {
                m = (((x8 >> 8) + (z8 >> 8)) & 1) ? M_STONE_A : M_STONE_B;
                if ((x8 & 255) < 12 || (z8 & 255) < 12) m = M_GROUT;
                if (r2 >= R_STAR && r2 < R_OUT0) {  // star edges only exist in this annulus
                    int ar = abs(xr >> 8), br = abs(zr >> 8), as = abs(xs >> 8), bs = abs(zs >> 8);
                    if ((abs(ar - SQ) < LW && br < SQ) || (abs(br - SQ) < LW && ar < SQ) ||
                        (abs(as - SQ) < LW && bs < SQ) || (abs(bs - SQ) < LW && as < SQ))
                        m = M_RUNE;
                } else if (r2 < 310) {
                    m = M_SHADOW;  // under the guardian
                }
            }
            if (r2 >= sh0 && r2 < sh1) m = M_SHOCK;
            row[x] = row[x + 1] = pal[m];
        }
    }
    // Soft haze where the floor meets the sky
    for (int k = 0; k < 6; k++) {
        int y = horizon + k;
        if (y >= 0 && y < SCENE_H)
            for (int x = 0; x < W; x++) fb[y * W + x] = gfx_blend(fb[y * W + x], fogc, 12 - k * 2);
    }
}

// ---------------------------------------------------------------- 3D objects

typedef struct { int sx, sy; float z; v3 c; } pv_t;

static inline bool facing(pv_t *a, pv_t *b, pv_t *c)
{
    v3 n = v_cross(v_sub(b->c, a->c), v_sub(c->c, a->c));  // outward normal (CCW from outside)
    return v_dot(n, v_sub(cam_pos, a->c)) > 0;
}

static void poly_tri(uint16_t *fb, pv_t *a, pv_t *b, pv_t *c, v3 light, uint16_t base_hi, uint16_t base_lo, bool flash)
{
    if (!facing(a, b, c)) return;
    v3 n = v_norm(v_cross(v_sub(b->c, a->c), v_sub(c->c, a->c)));
    float d = v_dot(n, light);
    int k = (int)((d < 0 ? 0 : d) * 14) + 1;  // 1..15
    uint16_t col = gfx_blend(base_lo, base_hi, k);
    // Facets turned towards the viewer catch a specular highlight
    v3 view = v_norm(v_sub(cam_pos, a->c));
    float spec = v_dot(n, v_norm(v_add(light, view)));
    if (spec > 0.9f) col = gfx_blend(col, rgb565(255, 255, 255), (int)((spec - 0.9f) * 120));
    if (flash) col = rgb565(255, 255, 255);
    gfx_tri(fb, a->sx, a->sy, b->sx, b->sy, c->sx, c->sy, col);
}

// hi/lo colors alternate between two palettes per face for an iridescent look
static void solid(uint16_t *fb, const v3 *verts, int nv, const uint8_t (*tris)[3], int nt, v3 center, float scale,
                  float spin, float tilt, const uint16_t *hi, const uint16_t *lo, uint16_t edge, bool flash)
{
    pv_t pv[12];
    float cs = cosf(spin), ss = sinf(spin), ct = cosf(tilt), st = sinf(tilt);
    for (int i = 0; i < nv; i++) {
        v3 p = verts[i];
        v3 q = {p.x * cs - p.z * ss, p.y, p.x * ss + p.z * cs};
        q = (v3){q.x, q.y * ct - q.z * st, q.y * st + q.z * ct};
        pv[i].c = v_add(center, v_mul(q, scale));
        if (!project(pv[i].c, &pv[i].sx, &pv[i].sy, &pv[i].z)) return;
    }
    v3 light = v_norm((v3){-0.4f, 0.8f, 0.5f});
    for (int i = 0; i < nt; i++) poly_tri(fb, &pv[tris[i][0]], &pv[tris[i][1]], &pv[tris[i][2]], light, hi[i & 1], lo[i & 1], flash);
    if (edge && !flash)
        for (int i = 0; i < nt; i++) {
            pv_t *a = &pv[tris[i][0]], *b = &pv[tris[i][1]], *c = &pv[tris[i][2]];
            if (!facing(a, b, c)) continue;
            gfx_line(fb, a->sx, a->sy, b->sx, b->sy, edge);
        }
}

// Crystal: hexagonal bipyramid
static const v3 crystal_v[8] = {
    {0, 1.7f, 0}, {0, -1.5f, 0},
    {0.8f, 0, 0}, {0.4f, 0, 0.69f}, {-0.4f, 0, 0.69f}, {-0.8f, 0, 0}, {-0.4f, 0, -0.69f}, {0.4f, 0, -0.69f},
};
static const uint8_t crystal_t[12][3] = {
    {0, 3, 2}, {0, 4, 3}, {0, 5, 4}, {0, 6, 5}, {0, 7, 6}, {0, 2, 7},
    {1, 2, 3}, {1, 3, 4}, {1, 4, 5}, {1, 5, 6}, {1, 6, 7}, {1, 7, 2},
};
// Shard: octahedron
static const v3 shard_v[6] = {{0, 1, 0}, {0, -1, 0}, {0.5f, 0, 0}, {0, 0, 0.5f}, {-0.5f, 0, 0}, {0, 0, -0.5f}};
static const uint8_t shard_t[8][3] = {{0, 3, 2}, {0, 4, 3}, {0, 5, 4}, {0, 2, 5}, {1, 2, 3}, {1, 3, 4}, {1, 4, 5}, {1, 5, 2}};

// ---------------------------------------------------------------- UI

static void window(uint16_t *fb, int x, int y, int w, int h)
{
    uint16_t tint = rgb565(16, 34, 96);
    for (int yy = 0; yy < h; yy++) {
        int py = y + yy;
        int inset = (yy == 0 || yy == h - 1) ? 2 : (yy == 1 || yy == h - 2) ? 1 : 0;
        int a = 10 + yy * 3 / h;
        for (int xx = inset; xx < w - inset; xx++) {
            uint16_t *p = fb + py * W + x + xx;
            *p = gfx_blend(*p, tint, a);
        }
    }
    uint16_t hi = rgb565(210, 225, 255), lo = rgb565(90, 110, 170);
    gfx_fill_rect(fb, x + 2, y, w - 4, 1, hi);
    gfx_fill_rect(fb, x + 2, y + h - 1, w - 4, 1, lo);
    gfx_fill_rect(fb, x, y + 2, 1, h - 4, hi);
    gfx_fill_rect(fb, x + w - 1, y + 2, 1, h - 4, lo);
    fb[(y + 1) * W + x + 1] = hi;
    fb[(y + 1) * W + x + w - 2] = hi;
    fb[(y + h - 2) * W + x + 1] = lo;
    fb[(y + h - 2) * W + x + w - 2] = lo;
}

static void bar(uint16_t *fb, int x, int y, int w, int h, int fill, uint16_t c0, uint16_t c1)
{
    gfx_fill_rect(fb, x, y, w, h, rgb565(10, 14, 36));
    for (int i = 0; i < fill && i < w; i++) gfx_fill_rect(fb, x + i, y, 1, h, gfx_blend(c0, c1, i * 16 / w));
    if (fill > 0) gfx_fill_rect(fb, x, y, fill > w ? w : fill, 1, gfx_blend(c1, rgb565(255, 255, 255), 8));
}

// ---------------------------------------------------------------- frame

void rpg_render(uint16_t *fb, uint32_t t, uint32_t dt)
{
    int dx, dy;
    app_take_drag(&dx, &dy);

    if (g_now.valid && g_now.m != s_last_min) {
        if (s_last_min >= 0) {
            s_casting = true;
            s_cast_t = t;
            s_caster = g_now.m % 3;
            s_spell = (g_now.m / 3) % 4;
            snprintf(s_damage, sizeof(s_damage), "%02d:%02d", clock_display_hour(), g_now.m);
        }
        s_last_min = g_now.m;
    }
    uint32_t ct = t - s_cast_t;
    if (s_casting && ct >= CAST_MS) s_casting = false;
    float cp = s_casting ? ct / (float)CAST_MS : 0;
    uint16_t spell_col = rgb565(spell_rgb[s_spell][0], spell_rgb[s_spell][1], spell_rgb[s_spell][2]);

    // Camera: slow sway behind the party; during a spell it dollies in and swings
    float yaw = sinf(t * 0.00011f) * 0.55f;
    float dist = 8.6f, height = 2.7f;
    if (s_casting) {
        float e = sinf(cp * 3.14159f);
        yaw += e * 0.35f;
        dist -= e * 2.0f;
        height -= e * 0.5f;
    }
    cam_pos = (v3){sinf(yaw) * dist, height, cosf(yaw) * dist};
    cam_f = v_norm(v_sub((v3){0, 1.5f, 0}, cam_pos));
    cam_r = v_norm((v3){-cam_f.z, 0, cam_f.x});  // f x up
    cam_u = v_cross(cam_r, cam_f);
    int horizon = (int)(CY + FOCAL * cam_f.y / cam_u.y);

    sky(fb, horizon, yaw, t);
    float shock_r = s_casting && cp > 0.45f && cp < 0.8f ? (cp - 0.45f) * 22 : -1;
    floor_render(fb, horizon, t, shock_r, spell_col);

    // Guardian crystal with orbiting shards (shards behind it first)
    float bob = sinf(t * 0.0021f) * 0.15f;
    v3 boss = {0, 2.3f + bob, 0};
    bool hit = s_casting && cp > 0.45f && cp < 0.6f && ((t / 50) & 1);
    float spin = t * 0.0007f;
    int bsx, bsy;
    float bz;
    project(boss, &bsx, &bsy, &bz);
    const uint16_t sh_hi[2] = {rgb565(150, 235, 255), rgb565(120, 190, 255)};
    const uint16_t sh_lo[2] = {rgb565(20, 50, 110), rgb565(30, 40, 100)};
    const uint16_t cr_hi[2] = {rgb565(255, 140, 240), rgb565(170, 150, 255)};
    const uint16_t cr_lo[2] = {rgb565(60, 10, 70), rgb565(30, 16, 90)};
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < 6; i++) {
            float a = spin * 1.6f + i * 1.0472f;
            v3 c = {cosf(a) * 1.9f, boss.y + sinf(a * 2 + t * 0.001f) * 0.4f, sinf(a) * 1.9f};
            int sx, sy;
            float z;
            if (!project(c, &sx, &sy, &z)) continue;
            if ((pass == 0) != (z > bz)) continue;
            solid(fb, shard_v, 6, shard_t, 8, c, 0.32f, -spin * 3 + i, 0.3f, sh_hi, sh_lo, rgb565(210, 245, 255), hit);
        }
        if (pass == 0) {
            gfx_glow(fb, bsx, bsy + 6, 40, rgb565(170, 70, 230), 6 + (sin_u8[(uint8_t)(t / 10)] >> 6));
            solid(fb, crystal_v, 8, crystal_t, 12, boss, 1.0f, spin, 0.12f, cr_hi, cr_lo, rgb565(255, 225, 255), hit);
        }
    }

    // Party billboards, farthest first
    int order[3] = {0, 1, 2};
    float pz[3];
    for (int i = 0; i < 3; i++) {
        int sx, sy;
        project(party_pos[i], &sx, &sy, &pz[i]);
    }
    for (int i = 0; i < 2; i++)
        for (int j = i + 1; j < 3; j++)
            if (pz[order[j]] > pz[order[i]]) { int k = order[i]; order[i] = order[j]; order[j] = k; }
    for (int k = 0; k < 3; k++) {
        int i = order[k];
        v3 p = party_pos[i];
        if (s_casting && i == s_caster) p.z -= sinf(cp * 3.14159f) * 0.6f;  // steps forward to cast
        int sx, sy;
        float z;
        if (!project(p, &sx, &sy, &z)) continue;
        int scale = (int)(FOCAL / z * 0.06f * 256);
        const sprite_t *spr = i == 1 ? ((t / 600) & 1 ? &spr_mage_a : &spr_mage_b) : ((t / 650 + i) & 1 ? &spr_warrior_a : &spr_warrior_b);
        int w = spr->w * scale >> 8, h = spr->h * scale >> 8;
        // Shadow
        for (int yy = -2; yy <= 2; yy++) {
            int ww = w * (9 - yy * yy) / 18;
            gfx_darken_rect(fb, sx - ww, sy + yy, ww * 2, 1, 0, 1);
        }
        if (s_casting && i == s_caster && cp < 0.5f) gfx_glow(fb, sx, sy - 2, w, spell_col, 10);
        sprite_draw_scaled(fb, spr, sx - w / 2, sy - h, scale, bsx < sx, party_cmap[i]);
    }

    // Spell: particles gather at the caster, then stream to the guardian along curved paths
    if (s_casting) {
        v3 from = party_pos[s_caster];
        from.y = 0.8f;
        for (int i = 0; i < 28; i++) {
            float ph = i / 28.0f;
            float u = (cp - 0.12f - ph * 0.18f) / 0.3f;
            v3 p;
            if (u < 0) {  // swirling around the caster
                float a = t * 0.008f + i * 0.9f, r = 0.5f + ph * 0.3f;
                p = (v3){from.x + cosf(a) * r, from.y + ph * 0.8f, from.z + sinf(a) * r};
                if (cp < 0.05f) continue;
            } else if (u < 1) {
                float side = (i & 1 ? 1 : -1) * (0.6f + ph);
                v3 mid = {(from.x + boss.x) / 2 + side, 3.4f, (from.z + boss.z) / 2};
                float a = (1 - u) * (1 - u), b = 2 * u * (1 - u), c = u * u;
                p = v_add(v_add(v_mul(from, a), v_mul(mid, b)), v_mul(boss, c));
            } else {
                continue;
            }
            int sx, sy;
            float z;
            if (!project(p, &sx, &sy, &z)) continue;
            gfx_glow(fb, sx, sy, 5, spell_col, 12);
            if ((unsigned)sx < W && (unsigned)sy < SCENE_H) fb[sy * W + sx] = rgb565(255, 255, 255);
        }
        if (cp > 0.45f && cp < 0.6f) {  // impact flash
            int a = (int)((0.6f - cp) / 0.15f * 11);
            gfx_glow(fb, bsx, bsy, 50, spell_col, a + 4);
            for (int y = 0; y < SCENE_H; y += 1)
                for (int x = 0; x < W; x++) fb[y * W + x] = gfx_blend(fb[y * W + x], rgb565(255, 255, 255), a / 2);
        }
    }

    // ---- UI ----
    uint16_t white = rgb565(255, 255, 255), ink = rgb565(6, 10, 40), dim = rgb565(170, 190, 235);
    // Calligraphic menu numerals: white-to-pale-blue gradient, navy outline, soft shadow
    char hhmm[8];
    clock_hhmm(hhmm, !g_now.valid || g_now.ms < 500);
    font_draw_grad(fb, &font_nib_m, (W - font_width(&font_nib_m, "00:00", -3)) / 2, 6, hhmm, -3, rgb565(255, 255, 255),
                   rgb565(165, 195, 255), rgb565(10, 14, 50), 2);
    char date[32];
    if (g_now.valid) snprintf(date, sizeof(date), "%s %d %s", clock_wday_name(g_now.wday), g_now.day, clock_month_name(g_now.mon));
    else strcpy(date, "SIN HORA");
    ui_text(fb, &ui_s, (W - ui_width(&ui_s, date)) / 2, 69, date, dim, ink, FONT_FILL | FONT_OUTLINE);

    if (s_casting && cp < 0.75f) {  // spell banner
        const char *sp = spells[s_spell];
        int bw = ui_width(&ui_m, sp) + 24, bx = (W - bw) / 2;
        window(fb, bx, 210, bw, 24);
        ui_text(fb, &ui_m, bx + 12, 214, sp, white, ink, FONT_FILL | FONT_OUTLINE);
    }
    if (s_casting && cp > 0.5f) {  // damage number bouncing over the guardian, on top of everything
        float u = (cp - 0.5f) / 0.5f;
        float bounce = fabsf(sinf(u * 9.0f)) * expf(-u * 5) * 26;
        int fw = font_width(&font_nib_s, s_damage, -1);
        font_draw_grad(fb, &font_nib_s, bsx - fw / 2, bsy - 17 - (int)bounce, s_damage, -1, rgb565(255, 255, 255),
                       rgb565(255, 214, 120), rgb565(30, 10, 50), 2);
    }

    // Party window
    window(fb, 4, 240, W - 8, 76);
    for (int i = 0; i < 3; i++) {
        int y = 246 + i * 23;
        bool active = s_casting ? i == s_caster : (g_now.valid && (int)(g_now.minute_ms / 20000) == i);
        if (active) gfx_tri(fb, 9, y + 3, 9, y + 11, 13, y + 7, rgb565(255, 230, 140));
        ui_text(fb, &ui_m, 16, y, names[i], active ? rgb565(255, 240, 180) : white, ink, FONT_FILL | FONT_OUTLINE);
        int hp = 1800 + (int)(hash(i, g_now.day) % 900), hpmax = 2999;
        char hpb[16];
        snprintf(hpb, sizeof(hpb), "%d", hp);
        ui_text(fb, &ui_s, 92 - ui_width(&ui_s, hpb), y + 3, hpb, white, ink, FONT_FILL | FONT_OUTLINE);
        bar(fb, 16, y + 16, 76, 2, 76 * hp / hpmax, rgb565(60, 200, 120), rgb565(170, 255, 170));
        bar(fb, 16, y + 19, 76, 1, 76 * (60 + (i * 13) % 40) / 100, rgb565(60, 120, 255), rgb565(150, 200, 255));
        // ATB: each member's bar fills during its third of the minute
        int bw = W - 8 - 100 - 8, fill = 0;
        if (g_now.valid) {
            int ms = g_app.show_secs ? (int)g_now.minute_ms - i * 20000 : 20000;
            fill = ms <= 0 ? 0 : ms >= 20000 ? bw : ms * bw / 20000;
        }
        bool full = fill >= bw;
        bar(fb, 100, y + 7, bw, 6, fill, full ? rgb565(255, 190, 40) : rgb565(40, 90, 200),
            full ? rgb565(255, 250, 170) : rgb565(120, 210, 255));
        if (full) {  // shimmer
            int sx = 100 + (int)((t / 6 + i * 20) % (bw + 30)) - 15;
            for (int k = 0; k < 6; k++)
                if (sx + k >= 100 && sx + k < 100 + bw) gfx_fill_rect(fb, sx + k, y + 7, 1, 6, gfx_blend(rgb565(255, 220, 120), white, 16 - k * 2));
        }
    }
    if (!g_app.h24 && g_now.valid) ui_text(fb, &ui_s, W - 24, 52, g_now.h < 12 ? "AM" : "PM", dim, ink, FONT_FILL | FONT_OUTLINE);
}
