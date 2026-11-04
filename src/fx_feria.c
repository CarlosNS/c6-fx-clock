// "Feria de Sevilla" (portrait): cuenta atrás hasta el alumbrado de la Feria de Abril.
// Portada de bombillas, farolillos, traje de lunares y, durante la Feria, fuegos artificiales.
//
// La fecha se calcula sola cada año: el alumbrado es a la medianoche del lunes al martes, 16 días
// después del Domingo de Resurrección (2026: 21 abril, 2027: 13 abril). La Feria dura hasta el
// domingo; pasada ésta, la cuenta atrás sigue hacia la del año siguiente.
#include <stdio.h>
#include "app.h"
#include "control.h"
#include "fx.h"
#include "gfx.h"

#define W LCD_W  // 172 x 320
#define H LCD_H

// ---- Calendario (enteros, sin mktime) -------------------------------------------------------

// Días desde 1970-01-01 (algoritmo de Howard Hinnant)
static int32_t days_from_civil(int y, int m, int d)
{
    y -= m <= 2;
    int era = (y >= 0 ? y : y - 399) / 400;
    int yoe = y - era * 400;
    int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

// Domingo de Resurrección (calendario gregoriano)
static int32_t easter_days(int y)
{
    int a = y % 19, b = y / 100, c = y % 100, d = b / 4, e = b % 4, f = (b + 8) / 25;
    int g = (b - f + 1) / 3, h = (19 * a + b - d - g + 15) % 30, i = c / 4, k = c % 4;
    int l = (32 + 2 * e + 2 * i - h - k) % 7, m = (a + 11 * h + 22 * l) / 451;
    int month = (h + l - 7 * m + 114) / 31, day = (h + l - 7 * m + 114) % 31 + 1;
    return days_from_civil(y, month, day);
}

// Último domingo del mes m
static int32_t last_sunday(int y, int m)
{
    int32_t last = days_from_civil(y, m, 31);
    return last - (last + 4) % 7;  // 1970-01-01 fue jueves
}

static int summer_time(int32_t day, int y)
{
    return day >= last_sunday(y, 3) && day < last_sunday(y, 10);
}

typedef struct {
    bool fair;       // la Feria está en marcha
    int fair_day;    // 1..6 (martes..domingo)
    int days, h, m, s;  // lo que falta para el alumbrado
    int year;        // año de esa Feria
} feria_t;

static feria_t feria_now(void)
{
    feria_t f = {0};
    int32_t today = days_from_civil(g_now.year, g_now.mon, g_now.day);
    int32_t now_s = today * 86400 + g_now.h * 3600 + g_now.m * 60 + g_now.s;
    for (int y = g_now.year; y <= g_now.year + 1; y++) {
        int32_t start = easter_days(y) + 16;  // martes 00:00
        int32_t start_s = start * 86400, end_s = (start + 6) * 86400;
        if (now_s >= end_s) continue;
        f.year = y;
        if (now_s >= start_s) {
            f.fair = true;
            f.fair_day = (now_s - start_s) / 86400 + 1;
        } else {
            // el cambio de hora entre hoy y la Feria no es una hora de espera de más o de menos
            int32_t rest = start_s - now_s - (summer_time(start, y) - summer_time(today, g_now.year)) * 3600;
            if (rest < 0) rest = 0;
            f.days = rest / 86400;
            f.h = rest / 3600 % 24;
            f.m = rest / 60 % 60;
            f.s = rest % 60;
        }
        break;
    }
    return f;
}

// ---- Dibujo ---------------------------------------------------------------------------------

static uint32_t hash(uint32_t x)
{
    x ^= x >> 16; x *= 0x7feb352d; x ^= x >> 15; x *= 0x846ca68b; x ^= x >> 16;
    return x;
}

void feria_enter(void) {}

// Bombilla que parpadea; cada una con su fase
static void bulb(uint16_t *fb, int x, int y, uint32_t t, uint32_t seed)
{
    int ph = (t / 5 + hash(seed) % 400) & 1023;
    int v = 150 + (sin_q14[ph] * 105 >> 14);  // 45..255
    gfx_glow(fb, x, y, 5, rgb565(255, 200, 70), 6);
    gfx_circle(fb, x, y, 1, rgb565(v, v * 9 / 10, v / 3));
}

static void bulb_line(uint16_t *fb, int x0, int y0, int x1, int y1, int n, uint32_t t, uint32_t seed)
{
    for (int i = 0; i <= n; i++) bulb(fb, x0 + (x1 - x0) * i / n, y0 + (y1 - y0) * i / n, t, seed + i);
}

// La portada: dos torres y un arco central, dibujados con bombillas
static void portada(uint16_t *fb, uint32_t t)
{
    uint16_t body = rgb565(30, 22, 52);
    gfx_fill_rect(fb, 22, 52, 128, 44, body);
    gfx_fill_rect(fb, 22, 26, 26, 70, body);
    gfx_fill_rect(fb, 124, 26, 26, 70, body);
    gfx_tri(fb, 20, 26, 50, 26, 35, 6, body);
    gfx_tri(fb, 122, 26, 152, 26, 137, 6, body);
    // el arco: hueco oscuro con un semicírculo arriba
    gfx_circle(fb, 86, 66, 22, rgb565(10, 8, 24));
    gfx_fill_rect(fb, 64, 66, 44, 30, rgb565(10, 8, 24));

    bulb_line(fb, 22, 96, 22, 28, 9, t, 100);
    bulb_line(fb, 48, 96, 48, 28, 9, t, 200);
    bulb_line(fb, 124, 96, 124, 28, 9, t, 300);
    bulb_line(fb, 150, 96, 150, 28, 9, t, 400);
    bulb_line(fb, 22, 28, 35, 8, 4, t, 500);
    bulb_line(fb, 35, 8, 48, 28, 4, t, 600);
    bulb_line(fb, 124, 28, 137, 8, 4, t, 700);
    bulb_line(fb, 137, 8, 150, 28, 4, t, 800);
    bulb_line(fb, 48, 52, 124, 52, 12, t, 900);
    bulb_line(fb, 64, 96, 64, 66, 4, t, 1000);
    bulb_line(fb, 108, 96, 108, 66, 4, t, 1100);
    for (int i = 0; i <= 12; i++) {  // semicírculo del arco
        int a = i * 512 / 12;
        bulb(fb, 86 + (sin_q14[(a + 256) & 1023] * 22 >> 14), 66 - (sin_q14[a & 1023] * 22 >> 14), t, 1200 + i);
    }
}

// Cuerda de farolillos que cuelga entre dos puntos (comba con el seno) y se mece con t
static void farolillos(uint16_t *fb, int y0, int sag, uint32_t t, int shift)
{
    static const uint8_t cols[4][3] = {{235, 40, 40}, {255, 190, 30}, {40, 190, 70}, {255, 110, 30}};
    uint16_t cord = rgb565(70, 55, 40);
    int px = -1, py = 0;
    for (int x = 0; x < W; x += 2) {
        int y = y0 + (sin_q14[(x * 512 / W) & 1023] * sag >> 14);
        if (px >= 0) gfx_line(fb, px, py, x, y, cord);
        px = x; py = y;
    }
    for (int i = 0; i < 6; i++) {
        int x = 14 + i * 29 + shift;
        int y = y0 + (sin_q14[(x * 512 / W) & 1023] * sag >> 14);
        int sway = sin_q14[(t / 9 + i * 170) & 1023] * 2 >> 14;
        const uint8_t *c = cols[(i + shift) & 3];
        uint16_t body = rgb565(c[0], c[1], c[2]), dark = rgb565(c[0] / 2, c[1] / 2, c[2] / 2);
        gfx_glow(fb, x + sway, y + 12, 15, body, 7);
        gfx_fill_rect(fb, x + sway - 3, y + 2, 7, 3, dark);
        gfx_circle(fb, x + sway, y + 11, 7, body);
        gfx_fill_rect(fb, x + sway - 1, y + 5, 3, 12, rgb565(255, 235, 170));
        gfx_fill_rect(fb, x + sway - 3, y + 17, 7, 2, dark);
    }
}

// Un fuego artificial: n rayos que se abren y se apagan
static void firework(uint16_t *fb, uint32_t t, int k, int x0, int ytop, int yspan)
{
    uint32_t period = 2600, lt = (t + k * 1100) % period, seq = (t + k * 1100) / period;
    uint32_t hs = hash(seq * 7 + k);
    int cx = x0 + hs % 70, cy = ytop + (hs >> 8) % yspan;
    int r = lt * 34 / 1400;
    if (lt > 1700) return;
    int fade = lt < 900 ? 255 : 255 - (lt - 900) * 255 / 800;
    uint8_t hue = hs >> 16;
    uint16_t col = hsv565(hue, 160, fade);
    for (int i = 0; i < 16; i++) {
        int a = i * 64;
        int x = cx + (sin_q14[(a + 256) & 1023] * r >> 14);
        int y = cy + (sin_q14[a & 1023] * r >> 14) + lt / 60;
        gfx_circle(fb, x, y, lt < 900 ? 1 : 0, col);
    }
}

void feria_render(uint16_t *fb, uint32_t t, uint32_t dt)
{
    feria_t f = {0};
    if (g_now.valid) f = feria_now();
    bool party = f.fair || (g_now.valid && f.days == 0);  // la Feria, y el último día antes

    // Cielo de noche
    for (int y = 0; y < 250; y++) {
        uint16_t c = rgb565(6 + y / 14, 8 + y / 25, 38 + y / 5);
        uint16_t *row = fb + y * W;
        for (int x = 0; x < W; x++) row[x] = c;
    }
    for (int i = 0; i < 18; i++) {  // estrellas
        uint32_t hs = hash(i + 77);
        int v = 120 + (sin_q14[(t / 6 + hs) & 1023] * 90 >> 14);
        fb[(hs % 70) * W + (hs >> 8) % W] = rgb565(v, v, v);
    }
    if (party)
        for (int k = 0; k < 3; k++) firework(fb, t, k, k * 40, 4, 80);

    portada(fb, t);

    // Rótulo
    uint16_t gold = rgb565(255, 205, 70), ink = rgb565(60, 10, 10);
    gfx_text_ol(fb, (W - gfx_text_width("FERIA DE ABRIL", 2)) / 2, 100, "FERIA DE ABRIL", gold, ink, 2, 1);

    farolillos(fb, 120, 9, t, 0);

    // Cuenta atrás
    char buf[32];
    uint16_t white = rgb565(255, 255, 255), dark = rgb565(30, 8, 20);
    if (!g_now.valid) {
        gfx_text_ol(fb, (W - gfx_text_width("SIN HORA", 2)) / 2, 180, "SIN HORA", white, dark, 2, 1);
    } else if (f.fair) {
        const char *l1 = "ES FERIA!";
        gfx_text_ol(fb, (W - gfx_text_width(l1, 3)) / 2, 160, l1, gold, ink, 3, 1);
        static const char *const dias[] = {"", "MARTES", "MIERCOLES", "JUEVES", "VIERNES", "SABADO", "DOMINGO"};
        const char *d = dias[f.fair_day > 6 ? 6 : f.fair_day];
        gfx_text_ol(fb, (W - gfx_text_width(d, 2)) / 2, 200, d, white, dark, 2, 1);
        sprintf(buf, "DIA %d DE 6", f.fair_day);
        gfx_text_ol(fb, (W - gfx_text_width(buf, 1)) / 2, 226, buf, rgb565(255, 160, 140), dark, 1, 1);
    } else {
        gfx_text_ol(fb, (W - gfx_text_width("FALTAN", 2)) / 2, 152, "FALTAN", rgb565(255, 160, 140), dark, 2, 1);
        // los días en grande y "DIAS" a su derecha, apoyado en la misma línea
        sprintf(buf, "%d", f.days);
        const char *u = f.days == 1 ? "DIA" : "DIAS";
        int sp = -4, wd = font_width(&font_round_m, buf, sp), wu = gfx_text_width(u, 2);
        int x0 = (W - (wd + 6 + wu)) / 2;
        font_draw(fb, &font_round_m, x0, 168, buf, sp, gold, ink, FONT_FILL | FONT_OUTLINE);
        gfx_text_ol(fb, x0 + wd + 6, 190, u, white, dark, 2, 1);
        if (g_app.show_secs) sprintf(buf, "%02d:%02d:%02d", f.h, f.m, f.s);
        else sprintf(buf, "%02d:%02d", f.h, f.m);
        gfx_text_ol(fb, (W - gfx_text_width(buf, 2)) / 2, 222, buf, rgb565(255, 235, 170), dark, 2, 1);
    }

    // Caseta: toldo a rayas con festón
    for (int x = 0; x < W; x += 14) {
        bool red = (x / 14) & 1;
        uint16_t c = red ? rgb565(200, 30, 40) : rgb565(245, 240, 230);
        gfx_fill_rect(fb, x, 250, 14, 10, c);
        gfx_circle(fb, x + 7, 260, 7, c);
    }
    // Traje de gitana: rojo con lunares blancos
    uint16_t red = rgb565(190, 20, 45), dot = rgb565(250, 245, 240);
    gfx_fill_rect(fb, 0, 268, W, H - 268, red);
    for (int row = 0; row < 4; row++)
        for (int x = (row & 1) * 10 + 4; x < W; x += 20) gfx_circle(fb, x, 276 + row * 15, 3, dot);
    // Volante
    for (int x = 0; x < W; x += 12) gfx_circle(fb, x + 6, 268, 6, rgb565(235, 70, 80));

    // Hora actual
    char date[24];
    clock_date_str(date);
    gfx_text_ol(fb, (W - gfx_text_width(date, 1)) / 2, 273, date, white, red, 1, 1);
    clock_draw_row(fb, &font_round_s, 286, -3, white, rgb565(90, 5, 25), FONT_FILL | FONT_OUTLINE);
    (void)dt;
}
