// Pixel-art canvas shared by the faces drawn at half the wide resolution (art.h)
#include "art.h"
#include <math.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "fx.h"
#include "gfx.h"

uint16_t *art;

void art_begin(void)
{
    if (!art) art = heap_caps_calloc(ART_W * ART_H, 2, MALLOC_CAP_SPIRAM);
}

void art_rect(int x, int y, int w, int h, uint16_t c)
{
    if (x < 0) w += x, x = 0;
    if (y < 0) h += y, y = 0;
    if (x + w > ART_W) w = ART_W - x;
    if (y + h > ART_H) h = ART_H - y;
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++) art[(y + j) * ART_W + x + i] = c;
}

void art_bands(const uint16_t *cols, int n, int y0, int y1)
{
    for (int y = y0; y < y1 && y < ART_H; y++) {
        int f = (y - y0) * 256 * (n - 1) / (y1 - y0), b = f >> 8, fr = f & 255;
        for (int x = 0; x < ART_W; x++) {
            int k = b;
            if (b + 1 < n && (fr > 204 || (fr > 128 && ((x + y) & 1) == 0))) k = b + 1;
            art[y * ART_W + x] = cols[k < n ? k : n - 1];
        }
    }
}

void art_disc(float cx, float cy, float r, uint16_t c)
{
    for (int y = (int)floorf(cy - r); y <= (int)ceilf(cy + r); y++)
        for (int x = (int)floorf(cx - r); x <= (int)ceilf(cx + r); x++) {
            float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
            if (dx * dx + dy * dy <= r * r) art_px(x, y, c);
        }
}

void art_line(float x0, float y0, float x1, float y1, uint16_t c)
{
    float dx = x1 - x0, dy = y1 - y0;
    int n = (int)(fabsf(dx) > fabsf(dy) ? fabsf(dx) : fabsf(dy)) + 1;
    for (int i = 0; i <= n; i++) art_px((int)floorf(x0 + dx * i / n), (int)floorf(y0 + dy * i / n), c);
}

void art_thick(float x0, float y0, float x1, float y1, float w, uint16_t c)
{
    float dx = x1 - x0, dy = y1 - y0, len = sqrtf(dx * dx + dy * dy);
    int n = (int)len + 1;
    for (int i = 0; i <= n; i++) art_disc(x0 + dx * i / n, y0 + dy * i / n, w / 2, c);
}

void art_poly(const float *xy, int n, uint16_t c)
{
    float ymin = 1e9f, ymax = -1e9f;
    for (int i = 0; i < n; i++) {
        if (xy[2 * i + 1] < ymin) ymin = xy[2 * i + 1];
        if (xy[2 * i + 1] > ymax) ymax = xy[2 * i + 1];
    }
    for (int y = (int)floorf(ymin); y <= (int)ceilf(ymax); y++) {
        if (y < 0 || y >= ART_H) continue;
        float sy = y + 0.5f, xs[16];
        int k = 0;
        for (int i = 0; i < n && k < 16; i++) {
            float ax = xy[2 * i], ay = xy[2 * i + 1], bx = xy[2 * ((i + 1) % n)], by = xy[2 * ((i + 1) % n) + 1];
            if ((ay <= sy && by > sy) || (by <= sy && ay > sy)) xs[k++] = ax + (sy - ay) * (bx - ax) / (by - ay);
        }
        for (int a = 0; a < k; a++)  // sort the crossings (few of them)
            for (int b = a + 1; b < k; b++)
                if (xs[b] < xs[a]) { float t = xs[a]; xs[a] = xs[b]; xs[b] = t; }
        for (int a = 0; a + 1 < k; a += 2)
            for (int x = (int)ceilf(xs[a] - 0.5f); x < (int)ceilf(xs[a + 1] - 0.5f); x++) art_px(x, y, c);
    }
}

void art_tint(int x, int y, uint16_t c, int alpha)
{
    if ((unsigned)x < ART_W && (unsigned)y < ART_H) art[y * ART_W + x] = gfx_blend(art[y * ART_W + x], c, alpha);
}

void art_blit(uint16_t *fb)
{
    for (int y = 0; y < ART_H; y++) {
        const uint16_t *s = art + y * ART_W;
        uint16_t *d0 = fb + (2 * y) * WW, *d1 = d0 + WW;
        for (int x = 0; x < ART_W; x++) d0[2 * x] = d0[2 * x + 1] = d1[2 * x] = d1[2 * x + 1] = s[x];
    }
}
