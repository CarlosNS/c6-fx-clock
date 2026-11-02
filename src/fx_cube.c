// Flat-shaded 3D cube over a warp starfield. Drag on the phone to spin it.
#include <math.h>
#include "app.h"
#include "control.h"
#include "fx.h"
#include "gfx.h"

#define N_STARS 140
#define CUBE_DIST 4.3f
#define FOV 190.0f
#define AUTO_VX 0.55f
#define AUTO_VY 0.85f
#define CUBE_CY 116  // upper part of the screen; the time goes below

typedef struct { float x, y, z; } vec3;

static vec3 stars[N_STARS];
static uint16_t bg[LCD_H];
static float ax, ay, az, vx, vy;

// Each face: outward normal axis (0=x,1=y,2=z), sign, and its 4 corners in cyclic order.
// Corner i has x = bit0, y = bit1, z = bit2 (0 -> -1, 1 -> +1).
static const struct { uint8_t axis; int8_t sign; uint8_t v[4]; } faces[6] = {
    {0, +1, {1, 3, 7, 5}}, {0, -1, {0, 2, 6, 4}},
    {1, +1, {2, 3, 7, 6}}, {1, -1, {0, 1, 5, 4}},
    {2, +1, {4, 5, 7, 6}}, {2, -1, {0, 1, 3, 2}},
};

static void star_reset(vec3 *s, bool anywhere)
{
    s->x = ((int)(fx_rand() % 2001) - 1000) / 1000.0f * 0.8f;
    s->y = ((int)(fx_rand() % 2001) - 1000) / 1000.0f * 1.5f;
    s->z = anywhere ? (fx_rand() % 1000) / 1000.0f + 0.05f : 1.0f;
}

void cube_enter(void)
{
    for (int i = 0; i < N_STARS; i++) star_reset(&stars[i], true);
    for (int y = 0; y < LCD_H; y++) bg[y] = rgb565(4 + y * 20 / LCD_H, 2, 18 + y * 30 / LCD_H);
    ax = 0.4f;
    ay = 0.6f;
    az = 0;
    vx = AUTO_VX;
    vy = AUTO_VY;
}

void cube_render(uint16_t *fb, uint32_t t, uint32_t dt)
{
    float dts = dt / 1000.0f;
    int dx, dy;
    app_take_drag(&dx, &dy);
    if (g_app.touch) {
        // Direct manipulation while the finger is down; remember the flick speed for inertia
        ay += dx * 0.012f;
        ax += dy * 0.012f;
        if (dts > 0) {
            vy = vy * 0.6f + 0.4f * (dx * 0.012f / dts);
            vx = vx * 0.6f + 0.4f * (dy * 0.012f / dts);
        }
    } else {
        float k = fminf(1.0f, dts * 0.8f);
        vx += (AUTO_VX - vx) * k;
        vy += (AUTO_VY - vy) * k;
        ax += vx * dts;
        ay += vy * dts;
    }
    az += 0.25f * dts;
    // A new minute gives the cube a quick spin
    static int last_min = -1;
    if (g_now.valid && g_now.m != last_min) {
        if (last_min >= 0) vy += 9.0f;
        last_min = g_now.m;
    }

    // Background gradient + stars rushing towards the viewer
    for (int y = 0; y < LCD_H; y++) {
        uint16_t c = bg[y], *row = fb + y * LCD_W;
        for (int x = 0; x < LCD_W; x++) row[x] = c;
    }
    float warp = 0.25f + 0.08f * (fabsf(vx) + fabsf(vy));
    for (int i = 0; i < N_STARS; i++) {
        vec3 *s = &stars[i];
        s->z -= dts * warp;
        if (s->z <= 0.05f) star_reset(s, false);
        int sx = LCD_W / 2 + (int)(s->x / s->z * 110);
        int sy = LCD_H / 2 + (int)(s->y / s->z * 110);
        if (sx < 0 || sx >= LCD_W - 1 || sy < 0 || sy >= LCD_H - 1) {
            star_reset(s, false);
            continue;
        }
        uint8_t b = (uint8_t)(255 * (1.0f - s->z * 0.9f));
        uint16_t c = rgb565(b, b, b);
        fb[sy * LCD_W + sx] = c;
        if (s->z < 0.35f) {
            fb[sy * LCD_W + sx + 1] = c;
            fb[(sy + 1) * LCD_W + sx] = c;
            fb[(sy + 1) * LCD_W + sx + 1] = c;
        }
    }

    // Rotation matrix R = Rz * Ry * Rx
    float sx_ = sinf(ax), cx_ = cosf(ax), sy_ = sinf(ay), cy_ = cosf(ay), sz_ = sinf(az), cz_ = cosf(az);
    float R[3][3] = {
        {cz_ * cy_, cz_ * sy_ * sx_ - sz_ * cx_, cz_ * sy_ * cx_ + sz_ * sx_},
        {sz_ * cy_, sz_ * sy_ * sx_ + cz_ * cx_, sz_ * sy_ * cx_ - cz_ * sx_},
        {-sy_, cy_ * sx_, cy_ * cx_},
    };

    float bob = sinf(t * 0.0015f) * 14;
    int px[8], py[8];
    for (int i = 0; i < 8; i++) {
        float v[3] = {(i & 1) ? 1.f : -1.f, (i & 2) ? 1.f : -1.f, (i & 4) ? 1.f : -1.f};
        float x = R[0][0] * v[0] + R[0][1] * v[1] + R[0][2] * v[2];
        float y = R[1][0] * v[0] + R[1][1] * v[1] + R[1][2] * v[2];
        float z = R[2][0] * v[0] + R[2][1] * v[1] + R[2][2] * v[2] + CUBE_DIST;
        px[i] = LCD_W / 2 + (int)(x * FOV / z);
        py[i] = CUBE_CY + (int)(y * FOV / z + bob);
    }

    const float lx = -0.35f, ly = -0.55f, lz = -0.76f;  // light from upper-left, in front
    for (int f = 0; f < 6; f++) {
        int a = faces[f].axis;
        float nx = R[0][a] * faces[f].sign, ny = R[1][a] * faces[f].sign, nz = R[2][a] * faces[f].sign;
        // Face center is n (unit cube) shifted by CUBE_DIST; visible when it faces the camera at the origin
        if (nz * CUBE_DIST + 1.0f >= 0) continue;
        float diff = nx * lx + ny * ly + nz * lz;
        if (diff < 0) diff = 0;
        uint8_t val = (uint8_t)(60 + 195 * diff);
        uint16_t col = hsv565((uint8_t)(f * 42 + t / 30), 210, val);
        const uint8_t *v = faces[f].v;
        gfx_tri(fb, px[v[0]], py[v[0]], px[v[1]], py[v[1]], px[v[2]], py[v[2]], col);
        gfx_tri(fb, px[v[0]], py[v[0]], px[v[2]], py[v[2]], px[v[3]], py[v[3]], col);
        uint16_t edge = hsv565((uint8_t)(f * 42 + t / 30), 90, 255);
        for (int e = 0; e < 4; e++) gfx_line(fb, px[v[e]], py[v[e]], px[v[(e + 1) & 3]], py[v[(e + 1) & 3]], edge);
    }

    char date[24];
    clock_date_str(date);
    uint16_t white = rgb565(255, 255, 255), ink = rgb565(6, 2, 24);
    gfx_text_ol(fb, (LCD_W - gfx_text_width(date, 2)) / 2, 10, date, rgb565(200, 190, 255), ink, 2, 1);
    clock_draw_row(fb, &font_round_m, 228, -4, white, ink, FONT_FILL | FONT_OUTLINE);
    clock_draw_seconds_bar(fb, 30, 300, LCD_W - 60, 3, hsv565((uint8_t)(t / 30), 160, 255), rgb565(40, 30, 70));
}
