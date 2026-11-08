// "Gol" (landscape): a 16-bit football sequence on loop. Number 10 in a navy/white/red kit
// dribbles in, hops over a sliding tackle, sends the keeper the wrong way into the top corner,
// celebrates with arms up and ends in the crossed-arms pose. The stadium scoreboard shows the time.
// Players are a 2D skeleton (joint angles per pose) rasterized at half resolution into a part
// buffer, then shaded and outlined like hand-made pixel art, so every pose blends smoothly.
#include <stdio.h>
#include <string.h>
#include "app.h"
#include "fx.h"
#include "gfx.h"

#define W LAND_W
#define H LAND_H
#define CYCLE 12000
#define PITCH_TOP 66
#define RUN_Y 146  // ground line of the dribble

// ---------------------------------------------------------------- rig

#define BW 50
#define BH 50
#define HX 25
#define HY 31
enum { EMPTY, SKIN_F, SHIRT_F, SHORT_F, SOCK_F, BOOT_F, SHORT_N, SHIRT_N, SKIN_N, SOCK_N, BOOT_N, HEAD, HAIR, EYE, NPARTS };
enum { KIT_STAR, KIT_KEEPER, KIT_DEF, NKITS };

static uint8_t s_buf[BH][BW];
static uint16_t s_tone[NKITS][NPARTS][3];  // shade, base, light

typedef struct { int tor, sh_n, el_n, sh_f, el_f, hip_n, kn_n, hip_f, kn_f; } pose_t;  // degrees

static const pose_t P_WINDUP = {8, -50, 60, 60, 70, -45, 95, 8, 18};
static const pose_t P_STRIKE = {-12, -60, 50, 70, 60, 70, 10, 2, 14};
static const pose_t P_FOLLOW = {-6, -40, 60, 60, 60, 105, 30, 0, 10};
static const pose_t P_HOP = {14, -60, 60, 50, 60, 70, 110, 10, 100};
static const pose_t P_SLIDE = {-62, -20, 30, -60, 20, 72, 0, 30, 70};
static const pose_t P_STAND = {0, -6, 12, 8, 14, 3, 4, -3, 4};
static const pose_t P_DIVE = {0, 160, 0, 175, 0, 0, 0, -12, 10};

static inline int isin(int deg) { return sin_q14[(deg * 1024 / 360) & 1023]; }
static inline int icos(int deg) { return sin_q14[(deg * 1024 / 360 + 256) & 1023]; }

// Fills a capsule (segment a-b with radius r, all in 1/16 of a buffer pixel)
static void capsule(int ax, int ay, int bx, int by, int r, uint8_t part)
{
    int x0 = ((ax < bx ? ax : bx) - r) >> 4, x1 = ((ax > bx ? ax : bx) + r) >> 4;
    int y0 = ((ay < by ? ay : by) - r) >> 4, y1 = ((ay > by ? ay : by) + r) >> 4;
    if (x0 < 1) x0 = 1;
    if (y0 < 1) y0 = 1;
    if (x1 > BW - 2) x1 = BW - 2;
    if (y1 > BH - 2) y1 = BH - 2;
    int dx = bx - ax, dy = by - ay, len = dx * dx + dy * dy, rr = r * r;
    if (!len) len = 1;
    for (int y = y0; y <= y1; y++) {
        int py = y * 16 + 8 - ay;
        for (int x = x0; x <= x1; x++) {
            int px = x * 16 + 8 - ax, dot = px * dx + py * dy;
            int t = dot <= 0 ? 0 : dot >= len ? 256 : (int)(((int64_t)dot << 8) / len);
            int ex = px - (dx * t >> 8), ey = py - (dy * t >> 8);
            if (ex * ex + ey * ey <= rr) s_buf[y][x] = part;
        }
    }
}

// Point along a limb pointing down, rotated forward by deg, len in 1/16 px
#define DIRX(deg, len) (isin(deg) * (len) >> 14)
#define DIRY(deg, len) (icos(deg) * (len) >> 14)

static void leg(int hx, int hy, int hip, int knee, bool near)
{
    int kx = hx + DIRX(hip, 96), ky = hy + DIRY(hip, 96);
    int fx = kx + DIRX(hip - knee, 96), fy = ky + DIRY(hip - knee, 96);
    uint8_t sk = near ? SKIN_N : SKIN_F, sh = near ? SHORT_N : SHORT_F, so = near ? SOCK_N : SOCK_F, bo = near ? BOOT_N : BOOT_F;
    capsule(hx, hy, kx, ky, 27, sk);
    capsule(hx, hy, hx + (kx - hx) * 3 / 10, hy + (ky - hy) * 3 / 10, 30, sh);
    capsule(kx, ky, fx, fy, 22, sk);
    capsule(kx + (fx - kx) * 35 / 100, ky + (fy - ky) * 35 / 100, fx, fy, 23, so);
    int fa = hip - knee + 90;
    capsule(fx, fy, fx + DIRX(fa, 38), fy + DIRY(fa, 38), 18, bo);
}

static void arm(int ax, int ay, int sh, int elbow, bool near)
{
    int ex = ax + DIRX(sh, 61), ey = ay + DIRY(sh, 61);
    int wx = ex + DIRX(sh + elbow, 58), wy = ey + DIRY(sh + elbow, 58);
    uint8_t sk = near ? SKIN_N : SKIN_F, sl = near ? SHIRT_N : SHIRT_F;
    capsule(ax, ay, ex, ey, 20, sk);
    capsule(ax, ay, ax + (ex - ax) * 55 / 100, ay + (ey - ay) * 55 / 100, 26, sl);
    capsule(ex, ey, wx, wy, 18, sk);
}

// Side view facing right; root rotates the whole body forward (dives, slides)
static void rig_side(const pose_t *p, int root)
{
    memset(s_buf, 0, sizeof(s_buf));
    int hx = HX * 16 + 8, hy = HY * 16 + 8, a = p->tor + root;
    int ux = isin(a), uy = -icos(a);  // torso "up" (Q14)
    int fx = icos(a), fy = isin(a);   // facing direction
#define UP(k) hx + (ux * (k) >> 14), hy + (uy * (k) >> 14)
    int shx = hx + (ux * 122 >> 14), shy = hy + (uy * 122 >> 14);
    arm(shx, shy, p->sh_f + root, p->el_f, false);
    leg(hx, hy, p->hip_f + root, p->kn_f, false);
    capsule(UP(24), UP(136), 46, SHIRT_N);
    capsule(hx, hy, UP(19), 42, SHORT_N);
    leg(hx, hy, p->hip_n + root, p->kn_n, true);
    int nx = hx + (ux * 182 >> 14), ny = hy + (uy * 182 >> 14);
    capsule(UP(136), nx, ny, 18, HEAD);
    int cx = nx + (fx * 6 >> 14) + (ux * 5 >> 14), cy = ny + (fy * 6 >> 14) + (uy * 5 >> 14);
    capsule(cx, cy, cx, cy, 46, HEAD);
    // Close-cropped hair: the back/top of the head
    for (int y = (ny >> 4) - 4; y <= (ny >> 4) + 4; y++)
        for (int x = (nx >> 4) - 4; x <= (nx >> 4) + 4; x++) {
            if ((unsigned)y >= BH || (unsigned)x >= BW || s_buf[y][x] != HEAD) continue;
            int px = x * 16 + 8 - nx, py = y * 16 + 8 - ny;
            if ((px * ux + py * uy) >> 14 > 21 && (px * fx + py * fy) >> 14 < 29) s_buf[y][x] = HAIR;
        }
    arm(shx, shy, p->sh_n + root, p->el_n, true);
    int ex = (nx + (fx * 29 >> 14) + (ux * 10 >> 14)) >> 4, ey = (ny + (fy * 29 >> 14) + (uy * 10 >> 14)) >> 4;
    if ((unsigned)ex < BW && (unsigned)ey < BH && s_buf[ey][ex] == HEAD) s_buf[ey][ex] = EYE;
#undef UP
}

// Facing the camera, arms crossed with the hands tucked under the armpits, chin up
static void rig_front(int lean)
{
    memset(s_buf, 0, sizeof(s_buf));
    int hx = HX * 16 + 8, hy = HY * 16 + 8;
    for (int side = -1; side <= 1; side += 2) {
        int kx = hx + side * 35, ky = hy + 96, fx = hx + side * 42, fy = hy + 186;
        capsule(hx + side * 22, hy, kx, ky, 27, SKIN_N);
        capsule(kx, ky, fx, fy, 23, SKIN_N);
        capsule(kx + (fx - kx) * 3 / 10, ky + (fy - ky) * 3 / 10, fx, fy, 24, SOCK_N);
        capsule(fx, fy + 10, fx + side * 16, fy + 14, 19, BOOT_N);
        capsule(hx + side * 22, hy, hx + side * 29, hy + 35, 30, SHORT_N);
    }
    capsule(hx - 19, hy - 3, hx + 19, hy - 3, 38, SHORT_N);
    capsule(hx, hy - 32, hx + lean / 2, hy - 106, 50, SHIRT_N);
    capsule(hx - 26 + lean / 2, hy - 112, hx + 26 + lean / 2, hy - 112, 38, SHIRT_N);
    int nx = hx + lean, ny = hy - 186;
    capsule(hx + lean / 2, hy - 136, nx, ny, 18, HEAD);
    capsule(nx, ny, nx, ny, 46, HEAD);
    for (int y = 0; y < BH; y++)
        for (int x = 0; x < BW; x++)
            if (s_buf[y][x] == HEAD && y * 16 + 8 < ny - 26) s_buf[y][x] = HAIR;
    int sx = hx + lean / 2;
    for (int side = -1; side <= 1; side += 2) {
        int ax = sx + side * 64, ay = hy - 112, ex = sx + side * 70, ey = hy - 58;
        capsule(ax, ay, ex, ey, 21, SKIN_F);
        capsule(ax, ay, (ax + ex) / 2, (ay + ey) / 2, 26, SHIRT_F);
    }
    capsule(sx - 70, hy - 58, sx + 48, hy - 96, 18, SKIN_F);
    capsule(sx + 70, hy - 62, sx - 48, hy - 86, 18, SKIN_N);
    s_buf[(ny - 3) >> 4][(nx - 19) >> 4] = EYE;
    s_buf[(ny - 3) >> 4][(nx + 16) >> 4] = EYE;
}

// Blits the part buffer at 2x with shading and a dark outline. (sx, sy) = hip on screen.
// ground >= 0 places the lowest pixel on that line instead of using sy.
static void rig_blit(uint16_t *fb, int kit, int sx, int sy, int ground, bool flip)
{
    if (ground >= 0) {
        int yb = BH - 2;
        while (yb > 0) {
            bool any = false;
            for (int x = 1; x < BW - 1 && !any; x++) any = s_buf[yb][x] != EMPTY;
            if (any) break;
            yb--;
        }
        sy = ground - (yb - HY) * 2 - 2;
    }
    const uint16_t outline = rgb565(22, 16, 28);
    for (int y = 1; y < BH - 1; y++) {
        int py = sy + (y - HY) * 2;
        if (py <= -2 || py >= H) continue;
        for (int x = 1; x < BW - 1; x++) {
            uint8_t p = s_buf[y][x];
            uint16_t c;
            if (p == EMPTY) {
                if (!(s_buf[y][x - 1] | s_buf[y][x + 1] | s_buf[y - 1][x] | s_buf[y + 1][x])) continue;
                c = outline;
            } else if (p == EYE) {
                c = outline;
            } else {
                // Light from the top-left: lit where the part starts, shaded where it ends
                uint8_t up = s_buf[y - 1][flip ? x + 1 : x - 1], dn = s_buf[y + 1][flip ? x - 1 : x + 1];
                int tone = (up != p && dn == p) ? 2 : (dn != p && up == p) ? 0 : 1;
                c = s_tone[kit][p][tone];
            }
            int col = flip ? 2 * HX - x : x, px = sx + (col - HX) * 2;
            for (int yy = py; yy < py + 2; yy++) {
                if ((unsigned)yy >= H) continue;
                for (int xx = px; xx < px + 2; xx++)
                    if ((unsigned)xx < W) fb[yy * W + xx] = c;
            }
        }
    }
}

static void run_pose(pose_t *p, int phase_deg, int arms_up)
{
    int s = isin(phase_deg), c = icos(phase_deg);
    p->tor = arms_up ? 6 : 16;
    p->hip_n = 15 + (42 * s >> 14);
    p->hip_f = 15 - (42 * s >> 14);
    p->kn_n = 25 + (c > 0 ? 70 * c >> 14 : 0);
    p->kn_f = 25 + (c < 0 ? -70 * c >> 14 : 0);
    if (arms_up) {
        p->sh_n = 165 + (12 * s >> 14);
        p->sh_f = 150 - (12 * s >> 14);
        p->el_n = p->el_f = 15;
    } else {
        p->sh_n = -38 * s >> 14;
        p->sh_f = 38 * s >> 14;
        p->el_n = p->el_f = 80;
    }
}

static void pose_mix(pose_t *o, const pose_t *a, const pose_t *b, int f)  // f 0..256
{
    const int *pa = &a->tor, *pb = &b->tor;
    int *po = &o->tor;
    for (int i = 0; i < 9; i++) po[i] = pa[i] + ((pb[i] - pa[i]) * f >> 8);
}

static void set_kit(int kit, const uint8_t rgb[][3])
{
    for (int p = 1; p < NPARTS; p++) {
        // Near/far parts share a base; far ones are darker
        static const uint8_t src[NPARTS] = {0, 0, 1, 2, 3, 4, 2, 1, 0, 3, 4, 0, 5, 0};
        const uint8_t *c = rgb[src[p]];
        int k = (p >= SKIN_F && p <= BOOT_F) ? 184 : 256;
        int r = c[0] * k >> 8, g = c[1] * k >> 8, b = c[2] * k >> 8;
#define CL(v) ((v) > 255 ? 255 : (v))
        s_tone[kit][p][0] = rgb565(r * 180 >> 8, g * 180 >> 8, b * 190 >> 8);
        s_tone[kit][p][1] = rgb565(r, g, b);
        s_tone[kit][p][2] = rgb565(CL(r * 5 / 4 + 14), CL(g * 5 / 4 + 14), CL(b * 5 / 4 + 14));
#undef CL
    }
}

// ---------------------------------------------------------------- scene

static inline int goal_x(int y) { return 282 + (150 - y) * 14 / 38; }  // goal line on the ground

static void line_blend(uint16_t *fb, int x0, int y0, int x1, int y1, uint16_t c, int a)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, dy = y1 > y0 ? y0 - y1 : y1 - y0;
    int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx + dy;
    for (;;) {
        if ((unsigned)x0 < W && (unsigned)y0 < H) fb[y0 * W + x0] = gfx_blend(fb[y0 * W + x0], c, a);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

static void ellipse_shadow(uint16_t *fb, int cx, int cy, int rx)
{
    for (int y = -2; y <= 2; y++) {
        int w = rx * (9 - y * y) / 9, py = cy + y;
        if ((unsigned)py >= H) continue;
        for (int x = cx - w; x <= cx + w; x++)
            if ((unsigned)x < W) fb[py * W + x] = gfx_blend(fb[py * W + x], 0, 6);
    }
}

static void stands(uint16_t *fb, uint32_t t, int cheer)
{
    for (int y = 0; y < PITCH_TOP - 10; y++) {
        uint16_t c = rgb565(14 + y / 4, 16 + y / 4, 34 + y / 3), *row = fb + y * W;
        for (int x = 0; x < W; x++) row[x] = c;
    }
    // Floodlight glow at the top corners
    gfx_glow(fb, 18, -6, 70, rgb565(200, 220, 255), 6);
    gfx_glow(fb, W - 18, -6, 70, rgb565(200, 220, 255), 6);
    static const uint8_t shirts[6][3] = {{40, 70, 180}, {235, 235, 245}, {210, 45, 55}, {40, 70, 180}, {60, 60, 80}, {245, 200, 60}};
    static const uint8_t skins[3][3] = {{120, 78, 52}, {220, 170, 130}, {176, 124, 86}};
    for (int tier = 0; tier < 9; tier++) {
        int ty = 4 + tier * 5, shade = 150 + tier * 12;
        // Tier step
        gfx_fill_rect(fb, 0, ty + 4, W, 1, rgb565(26 + tier * 3, 28 + tier * 3, 52 + tier * 3));
        for (int i = 0; i < 81; i++) {
            uint32_t h = fx_hash(i, tier * 31 + 7);
            int x = i * 4 + (int)(h & 1) - (tier & 1) * 2;
            int jump = cheer ? ((sin_u8[(uint8_t)(t / 3 + h)] > 128) ? -2 : 0) : ((h >> 3) % 23 == (t / 300) % 23 ? -1 : 0);
            const uint8_t *sc = shirts[(h >> 6) % 6], *kc = skins[(h >> 9) % 3];
            uint16_t body = rgb565(sc[0] * shade >> 8, sc[1] * shade >> 8, sc[2] * shade >> 8);
            gfx_fill_rect(fb, x, ty + 2 + jump, 3, 2, body);
            fb[(ty + 1 + jump) * W + x + 1] = rgb565(kc[0] * shade >> 8, kc[1] * shade >> 8, kc[2] * shade >> 8);
            if (cheer && (h >> 12) % 3 == 0) {  // arms up
                fb[(ty + jump) * W + x] = body;
                if (x + 2 < W) fb[(ty + jump) * W + x + 2] = body;
            }
        }
    }
    // Camera flashes
    int nflash = cheer ? 6 : 1;
    for (int i = 0; i < nflash; i++) {
        uint32_t h = fx_hash(t / 90, i * 13 + 3);
        if (!cheer && (h & 7)) continue;
        int x = (int)(h % W), y = 4 + (int)((h >> 10) % 44);
        gfx_glow(fb, x, y, 6, rgb565(255, 255, 255), 12);
        fb[y * W + x] = rgb565(255, 255, 255);
    }
}

static void boards(uint16_t *fb, uint32_t t, bool goal)
{
    int y0 = PITCH_TOP - 10;
    gfx_fill_rect(fb, 0, y0, W, 10, rgb565(10, 10, 18));
    gfx_fill_rect(fb, 0, y0 + 9, W, 1, rgb565(60, 60, 80));
    const char *msg = goal ? "GOOOL  *  MBAPPE 10  *  " : "MAREA  *  RELOJ  *  ";
    int mw = gfx_text_width(msg, 1) + 6, off = (int)(t / 25 % (uint32_t)mw);
    uint16_t c = goal ? ((t / 150) & 1 ? rgb565(255, 230, 60) : rgb565(255, 255, 255)) : rgb565(90, 200, 255);
    for (int x = -off; x < W; x += mw) gfx_text_ol(fb, x, y0 + 1, msg, c, rgb565(10, 10, 18), 1, 0);
}

static void pitch(uint16_t *fb)
{
    for (int y = PITCH_TOP; y < H; y++) {
        int depth = (y - PITCH_TOP) * 256 / (H - PITCH_TOP);
        int k = 150 + (depth * 106 >> 8);  // farther = darker
        uint16_t c0 = rgb565(52 * k >> 8, 140 * k >> 8, 60 * k >> 8), c1 = rgb565(62 * k >> 8, 158 * k >> 8, 70 * k >> 8);
        uint16_t *row = fb + y * W;
        int off = (y - PITCH_TOP) * 94 >> 8, x = 0, band = (off / 24) & 1, rem = 24 - off % 24;
        while (x < W) {
            int n = rem < W - x ? rem : W - x;
            uint16_t c = band ? c1 : c0;
            for (int i = 0; i < n; i++) row[x + i] = c;
            x += n;
            rem = 24;
            band ^= 1;
        }
    }
    uint16_t ln = rgb565(214, 232, 214);
    gfx_line(fb, 0, PITCH_TOP + 4, goal_x(PITCH_TOP + 4), PITCH_TOP + 4, ln);  // far touchline
    gfx_line(fb, goal_x(H - 1), H - 1, goal_x(PITCH_TOP + 4), PITCH_TOP + 4, ln);
    // Penalty area, six-yard box and spot
    gfx_line(fb, goal_x(162), 162, goal_x(162) - 96, 162, ln);
    gfx_line(fb, goal_x(84), 84, goal_x(84) - 96, 84, ln);
    gfx_line(fb, goal_x(162) - 96, 162, goal_x(84) - 96, 84, ln);
    gfx_line(fb, goal_x(156), 156, goal_x(156) - 30, 156, ln);
    gfx_line(fb, goal_x(104), 104, goal_x(104) - 30, 104, ln);
    gfx_line(fb, goal_x(156) - 30, 156, goal_x(104) - 30, 104, ln);
    gfx_fill_rect(fb, goal_x(131) - 62, 131, 3, 1, ln);
}

// Goal frame: front post at ground y 150, back post at 112, net 20 px deep
typedef struct { int x, y; } pt_t;
static const pt_t FPb = {282, 150}, FPt = {282, 100}, BPb = {296, 112}, BPt = {296, 72};
static const pt_t RNb = {303, 150}, RNt = {303, 110}, RFb = {313, 112}, RFt = {313, 82};

static pt_t lerp_pt(pt_t a, pt_t b, int f) { return (pt_t){a.x + (b.x - a.x) * f / 256, a.y + (b.y - a.y) * f / 256}; }

// Net panel a-b-c-d as a grid of translucent strings; the bulge pushes the middle outwards
static void net_panel(uint16_t *fb, pt_t a, pt_t b, pt_t c, pt_t d, int nu, int nv, int bulge, int bu, int bv)
{
    uint16_t col = rgb565(235, 240, 245);
    for (int pass = 0; pass < 2; pass++) {
        int n = pass ? nv : nu;
        for (int i = 0; i <= n; i++) {
            int f = i * 256 / n;
            pt_t prev = {0, 0};
            for (int s = 0; s <= 6; s++) {
                int g = s * 256 / 6, u = pass ? g : f, v = pass ? f : g;
                pt_t p = lerp_pt(lerp_pt(a, b, u), lerp_pt(d, c, u), v);
                if (bulge) {
                    int du = u - bu, dv = v - bv, dd = (du * du + dv * dv) >> 8;
                    if (dd < 64) p.x += bulge * (64 - dd) / 64;
                }
                if (s) line_blend(fb, prev.x, prev.y, p.x, p.y, col, 7);
                prev = p;
            }
        }
    }
}

static void post(uint16_t *fb, pt_t b, pt_t t, int w)
{
    gfx_fill_rect(fb, t.x - w / 2 - 1, t.y - 1, w + 2, b.y - t.y + 2, rgb565(30, 40, 40));
    gfx_fill_rect(fb, t.x - w / 2, t.y, w, b.y - t.y, rgb565(245, 248, 250));
}

static void ball_draw(uint16_t *fb, int x, int ground, int height, uint32_t spin)
{
    ellipse_shadow(fb, x, ground, 4 - height / 20);
    int y = ground - 3 - height;
    gfx_circle(fb, x, y, 4, rgb565(22, 16, 28));
    gfx_circle(fb, x, y, 3, rgb565(250, 250, 250));
    // Two dark patches rolling round
    int a = (int)(spin & 1023);
    for (int k = 0; k < 2; k++) {
        int px = x + (sin_q14[(a + k * 512) & 1023] * 2 >> 14), py = y + (sin_q14[(a + k * 512 + 256) & 1023] >> 14);
        gfx_fill_rect(fb, px - 1, py - 1, 2, 2, rgb565(40, 40, 50));
    }
    fb[(y - 2) * W + x - 1] = rgb565(255, 255, 255);
}

static void fade(uint16_t *fb, int a)  // 0..16 toward black
{
    if (a <= 0) return;
    if (a >= 16) {
        memset(fb, 0, W * H * 2);
        return;
    }
    for (int i = 0; i < W * H; i++) fb[i] = gfx_blend(fb[i], 0, a);
}

void goal_enter(void)
{
    static const uint8_t star[6][3] = {{120, 78, 52}, {34, 56, 140}, {236, 238, 246}, {205, 38, 50}, {250, 150, 40}, {30, 22, 22}};
    static const uint8_t keeper[6][3] = {{228, 178, 140}, {40, 170, 96}, {40, 40, 52}, {40, 170, 96}, {30, 30, 34}, {110, 66, 36}};
    static const uint8_t def[6][3] = {{196, 140, 100}, {196, 40, 46}, {30, 30, 40}, {196, 40, 46}, {240, 240, 240}, {60, 40, 30}};
    set_kit(KIT_STAR, star);
    set_kit(KIT_KEEPER, keeper);
    set_kit(KIT_DEF, def);
}

static inline int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static inline int ramp(int ms, int a, int b) { return clampi((ms - a) * 256 / (b - a), 0, 256); }  // 0..256

#define T_KICK 3950
#define T_HIT 4450
#define T_FRONT 6800
#define T_FADE 11200

void goal_render(uint16_t *fb, uint32_t t, uint32_t dt)
{
    (void)dt;
    int ms = (int)(t % CYCLE);
    bool cheer = ms >= T_HIT && ms < 9500;
    stands(fb, t, cheer);
    boards(fb, t, ms >= T_HIT && ms < T_FADE);
    pitch(fb);

    // Behind the keeper: far net panels, back post, crossbar
    int bulge = ms >= T_HIT && ms < T_HIT + 700 ? (12 * (T_HIT + 700 - ms) / 700) * sin_q14[((ms - T_HIT) * 3) & 1023] / 16384 + 4 * (T_HIT + 700 - ms) / 700 : 0;
    net_panel(fb, BPt, RFt, RFb, BPb, 4, 6, 0, 0, 0);
    net_panel(fb, RNt, RFt, RFb, RNb, 6, 6, bulge, 150, 120);
    net_panel(fb, FPt, BPt, RFt, RNt, 6, 3, 0, 0, 0);
    post(fb, BPb, BPt, 2);
    line_blend(fb, FPt.x, FPt.y, BPt.x, BPt.y, rgb565(245, 248, 250), 16);
    line_blend(fb, FPt.x, FPt.y + 1, BPt.x, BPt.y + 1, rgb565(245, 248, 250), 16);

    // ---- the ball (3D: x, ground line, height)
    int bx, bg, bh = 0;
    uint32_t spin = t;
    int px = -20 + 216 * clampi(ms, 0, 3700) / 3700;  // dribble
    if (ms < T_KICK) {
        int touch = ms % 760;
        bx = px + 8 + touch * 12 / 760;
        bg = RUN_Y + 1;
        if (ms >= 2050 && ms < 2450) {  // flicked over the tackle
            int u = ms - 2050;
            bh = u * (400 - u) / 3200;
        }
        if (ms > 3700) bx = 204;
        spin = t * 2;
    } else if (ms < T_HIT) {
        int f = ramp(ms, T_KICK, T_HIT);
        bx = 204 + (304 - 204) * f / 256;
        bg = RUN_Y + 1 + (118 - RUN_Y - 1) * f / 256;
        bh = 3 + 27 * f / 256 + (f * (256 - f) >> 11);
        spin = t * 6;
    } else {
        int u = ms - T_HIT, f = ramp(ms, T_HIT, T_HIT + 380);
        bx = 304 - (u < 600 ? u / 120 : 5);
        bg = 120;
        bh = 30 - 30 * f * f / 65536;
        if (u > 380 && u < 620) bh = (u - 380) * (240 - (u - 380)) / 1600;  // small bounce
        spin = u < 700 ? t * 2 : 0;
    }

    // ---- keeper: sways on the line, then is sent the wrong way
    pose_t kp = P_STAND;
    int k_x = 278 + (sin_q14[(ms / 3) & 1023] * 3 >> 14), k_g = 132, k_root = 0, k_air = -1;
    if (ms >= T_KICK + 60) {
        int f = ramp(ms, T_KICK + 60, T_KICK + 420);
        pose_mix(&kp, &P_STAND, &P_DIVE, f);
        k_root = 78 * f / 256;
        k_x = 278 - 22 * f / 256;
        k_g = 132 + 10 * f / 256;
        int lift = f * (256 - f) >> 11;
        k_air = k_g - 22 - lift + 10 * f / 256;
    }
    if (ms > 8200) {  // gets back up
        int f = ramp(ms, 8200, 8700);
        pose_mix(&kp, &P_DIVE, &P_STAND, f);
        k_root = 78 - 78 * f / 256;
        k_x = 256;
        k_g = 142;
        k_air = f >= 256 ? -1 : k_g - 12 - 10 * f / 256;
    }

    // ---- defender: runs in, slides, ends up on the grass
    pose_t dp;
    int d_x;
    if (ms < 1900) {
        run_pose(&dp, ms * 360 * 3 / 1000, 0);
        d_x = 330 - 165 * clampi(ms - 300, 0, 1600) / 1600;
    } else {
        int f = ramp(ms, 1900, 2600), e = 256 - ((256 - f) * (256 - f) >> 8);
        pose_t run;
        run_pose(&run, 1900 * 360 * 3 / 1000, 0);
        pose_mix(&dp, &run, &P_SLIDE, ramp(ms, 1900, 2050));
        d_x = 165 - 85 * e / 256;
        if (ms > 9000) {
            int g = ramp(ms, 9000, 9500);
            pose_mix(&dp, &P_SLIDE, &P_STAND, g);
        }
    }

    // ---- the star
    pose_t sp;
    int s_x = px, s_g = RUN_Y, s_air = -1;
    bool s_flip = false, s_front = false;
    if (ms < 3700) {
        run_pose(&sp, ms * 360 * 26 / 10000, 0);
        if (ms >= 2050 && ms < 2450) {  // hop over the slide
            int u = ms - 2050, f = u < 200 ? u * 256 / 200 : (400 - u) * 256 / 200;
            pose_t run = sp;
            pose_mix(&sp, &run, &P_HOP, clampi(f * 2, 0, 256));
            s_air = s_g - 25 - u * (400 - u) / 2600;
        }
    } else if (ms < T_KICK) {
        pose_t run;
        run_pose(&run, 3700 * 360 * 26 / 10000, 0);
        pose_mix(&sp, &run, &P_WINDUP, ramp(ms, 3700, T_KICK - 50));
        s_x = 196;
    } else if (ms < T_KICK + 400) {
        if (ms < T_KICK + 100) pose_mix(&sp, &P_WINDUP, &P_STRIKE, ramp(ms, T_KICK - 50, T_KICK + 60));
        else pose_mix(&sp, &P_STRIKE, &P_FOLLOW, ramp(ms, T_KICK + 100, T_KICK + 400));
        s_x = 196;
    } else if (ms < 4700) {
        pose_mix(&sp, &P_FOLLOW, &P_STAND, ramp(ms, T_KICK + 400, 4700));
        s_x = 196;
    } else if (ms < T_FRONT) {
        // Celebration run back towards the crowd, arms up
        int f = ramp(ms, 4700, T_FRONT - 200);
        run_pose(&sp, ms * 360 * 2 / 1000, 1);
        s_x = 196 - 70 * f / 256;
        s_g = RUN_Y + 10 * f / 256;
        s_flip = true;
        if (ms > T_FRONT - 200) pose_mix(&sp, &sp, &P_STAND, ramp(ms, T_FRONT - 200, T_FRONT));
    } else {
        s_x = 126;
        s_g = RUN_Y + 10;
        s_front = true;
    }

    // GOOOL! (behind the players so the celebration stays visible)
    if (ms >= T_HIT + 80 && ms < 7400) {
        int u = ms - T_HIT - 80, sc = u < 160 ? 6 - u / 80 : 4;
        const char *txt = "GOOOL!";
        int tw = gfx_text_width(txt, sc), y = 74 + (u < 160 ? 0 : (sin_q14[(u * 2) & 1023] * 3 >> 14));
        uint16_t c = (u / 120) & 1 ? rgb565(255, 236, 80) : rgb565(255, 255, 255);
        gfx_text_ol(fb, 118 - tw / 2 + 4, y + 4, txt, rgb565(10, 20, 70), rgb565(10, 20, 70), sc, 2);
        gfx_text_ol(fb, 118 - tw / 2, y, txt, c, rgb565(170, 30, 40), sc, 2);
    }

    // Draw back to front by ground line
    struct { int g; int who; } order[3] = {{k_g, 0}, {RUN_Y, 1}, {s_g, 2}};
    for (int i = 0; i < 3; i++)
        for (int j = i + 1; j < 3; j++)
            if (order[j].g < order[i].g) {
                int g = order[i].g, w = order[i].who;
                order[i] = order[j];
                order[j].g = g;
                order[j].who = w;
            }
    bool ball_drawn = false;
    for (int i = 0; i < 3; i++) {
        if (!ball_drawn && bg <= order[i].g) {
            ball_draw(fb, bx, bg, bh, spin);
            ball_drawn = true;
        }
        switch (order[i].who) {
        case 0:
            ellipse_shadow(fb, k_x, k_g, 10);
            rig_side(&kp, k_root);
            rig_blit(fb, KIT_KEEPER, k_x, k_air, k_air < 0 ? k_g : -1, true);
            break;
        case 1:
            ellipse_shadow(fb, d_x, RUN_Y, 10);
            rig_side(&dp, 0);
            rig_blit(fb, KIT_DEF, d_x, 0, RUN_Y, true);
            break;
        default:
            ellipse_shadow(fb, s_x, s_g, s_air < 0 ? 10 : 7);
            if (s_front) {
                int breathe = sin_q14[(ms / 2) & 1023] * 4 >> 14;
                rig_front(breathe);
            } else {
                rig_side(&sp, 0);
            }
            rig_blit(fb, KIT_STAR, s_x, s_air, s_air < 0 ? s_g : -1, s_flip);
            break;
        }
    }
    if (!ball_drawn) ball_draw(fb, bx, bg, bh, spin);

    // In front of everyone: near post and the near side of the net
    net_panel(fb, FPt, RNt, RNb, FPb, 4, 6, 0, 0, 0);
    post(fb, FPb, FPt, 3);

    // Name tag over the crossed-arms pose
    if (s_front && ms < T_FADE) {
        const char *tag = "MBAPPE 10";
        int tw = gfx_text_width(tag, 1);
        gfx_text_ol(fb, s_x - tw / 2, s_g - 66, tag, rgb565(255, 255, 255), rgb565(22, 16, 28), 1, 1);
    }

    // Confetti in the tricolour
    if (ms > T_HIT + 150) {
        uint16_t cols[4] = {rgb565(50, 90, 230), rgb565(255, 255, 255), rgb565(230, 50, 60), rgb565(255, 210, 60)};
        int el = ms - T_HIT - 150;
        for (int i = 0; i < 56; i++) {
            uint32_t h = fx_hash(i, 99);
            int start = (int)(h % 1400);
            if (el < start) continue;
            int u = el - start;
            int x = (int)((h >> 8) % W) + (sin_q14[(u / 2 + i * 97) & 1023] * 6 >> 14);
            int y = -6 + u * (20 + (int)((h >> 20) % 18)) / 1000;
            if (y >= H) continue;
            bool edge = ((u / 90 + i) & 1);
            gfx_fill_rect(fb, x, y, edge ? 1 : 2, 2, cols[(h >> 4) & 3]);
        }
    }

    // Fade the loop in and out (the clock stays)
    fade(fb, ms < 250 ? 16 - ms * 16 / 250 : ms > T_FADE ? (ms - T_FADE) * 17 / (CYCLE - T_FADE) : 0);

    // Stadium scoreboard with the time
    int sbw = 172, sbx = (W - sbw) / 2;
    gfx_darken_rect(fb, sbx, 2, sbw, 52, 6, 3);
    gfx_fill_rect(fb, sbx + 6, 3, sbw - 12, 1, rgb565(90, 200, 255));
    const uint16_t amber[4] = {rgb565(255, 250, 220), rgb565(255, 228, 140), rgb565(255, 190, 70), rgb565(230, 130, 40)};
    char hhmm[8];
    clock_hhmm(hhmm, !g_now.valid || g_now.ms < 500);
    pix_digits_draw(fb, (W - pix_digits_width("00:00", 4)) / 2, 7, hhmm, 4, amber, rgb565(40, 16, 10));
    if (g_app.show_secs && g_now.valid) {
        int bw = 120, bx2 = (W - bw) / 2;
        gfx_fill_rect(fb, bx2, 50, bw, 2, rgb565(40, 40, 60));
        gfx_fill_rect(fb, bx2, 50, (int)(bw * g_now.minute_ms / 60000), 2, amber[1]);
    }
}
