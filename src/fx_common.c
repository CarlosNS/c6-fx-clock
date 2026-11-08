#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "app.h"
#include "esp_log.h"
#include "esp_random.h"
#include "fx.h"
#include "gfx.h"

const fx_t fx_list[FX_COUNT] = {
    {.name = "PLASMA", .res = FX_NATIVE, .enter = plasma_enter, .render = plasma_render,
     .label = "Plasma", .group = FX_G_EFFECTS, .hint = "Arrastra para remover el plasma",
     .desc = "Plasma de colores en movimiento continuo, con la hora en grande sobre un panel translúcido."},
    {.name = "CUBO 3D", .res = FX_WIDE, .enter = cube_enter, .render = cube_render,
     .label = "Cubo 3D", .group = FX_G_EFFECTS, .hint = "Arrastra para girar el cubo",
     .desc = "Cubo 3D iluminado que flota y da una vuelta rápida cada minuto. La hora, debajo."},
    {.name = "TUNEL", .res = FX_WIDE, .enter = tunnel_enter, .render = tunnel_render,
     .label = "Túnel", .group = FX_G_EFFECTS, .hint = "Horizontal: gira · vertical: avanza",
     .desc = "Viaje por un túnel infinito. La hora en 7 segmentos del color complementario al túnel."},
    {.name = "LAVA", .res = FX_WIDE, .enter = lava_enter, .render = lava_render,
     .label = "Lava", .group = FX_G_EFFECTS, .hint = "La bola grande sigue a tu dedo",
     .desc = "Lámpara de lava con bolas que se funden entre sí; los dígitos, oscuros con borde luminoso."},
    {.name = "VIDA", .res = FX_WIDE, .enter = life_enter, .render = life_render,
     .label = "Vida", .group = FX_G_EFFECTS, .hint = "Dibuja células con el dedo",
     .desc = "Juego de la vida de Conway: los dígitos son células inmortales y al cambiar el minuto se disuelven en la simulación."},
    {.name = "HUD", .res = FX_WIDE, .enter = hud_enter, .render = hud_render,
     .label = "HUD", .group = FX_G_GAMES,
     .desc = "Interfaz de ciencia ficción con anillo de segundos, temperatura del chip y señal WiFi en vivo; un glitch cada minuto."},
    {.name = "AVENTURA", .res = FX_WIDE, .enter = quest_enter, .render = quest_render,
     .label = "Aventura", .group = FX_G_GAMES,
     .desc = "Viaje en pixel art con parallax y un lago con reflejos. El cielo, el sol, la luna y los faroles siguen la hora real."},
    {.name = "RPG", .res = FX_WIDE, .enter = rpg_enter, .render = rpg_render,
     .label = "RPG", .group = FX_G_GAMES,
     .desc = "Batalla estilo JRPG clásico: las barras de turno marcan los segundos y cada minuto se lanza un hechizo con la hora como daño."},
    {.name = "ESTRELLADA", .res = FX_WIDE, .enter = starry_enter, .render = starry_render,
     .label = "Estrellada", .group = FX_G_ART,
     .desc = "La noche estrellada (Van Gogh, 1889) animada: cielo de pinceladas en remolino, halos que laten y ciprés al viento."},
    {.name = "WIFI", .res = FX_WIDE, .enter = wifimon_enter, .render = wifimon_render,
     .label = "WiFi", .group = FX_G_CLOCKS,
     .desc = "La hora en grande sobre un espectro tenue de las redes WiFi de alrededor, que se escanean en segundo plano."},
    // 10: retired (El grito, removed for good on 2026-11-07). The number stays taken.
    {.name = "RETIRED", .retired = true, .label = "", .desc = ""},
    {.name = "EARTHRISE", .landscape = true, .enter = earthrise_enter, .render = earthrise_render,
     .label = "Earthrise", .group = FX_G_ART,
     .desc = "La foto del Apolo 8 (1968): la Tierra en 3D, con continentes y nubes que giran, saliendo sobre la Luna."},
    {.name = "SAKURA", .landscape = true, .enter = sakura_enter, .render = sakura_render,
     .label = "Sakura", .group = FX_G_NATURE,
     .desc = "Cerezo en flor junto a un estanque: pétalos que caen revoloteando y ondas donde tocan el agua."},
    {.name = "SYNTHWAVE", .landscape = true, .enter = synth_enter, .render = synth_render,
     .label = "Synthwave", .group = FX_G_EFFECTS,
     .desc = "Atardecer retro de los 80: sol a franjas, rejilla de neón, palmeras y números cromados."},
    {.name = "ARENA", .res = FX_WIDE, .enter = sand_enter, .render = sand_render,
     .label = "Arena", .group = FX_G_CLOCKS,
     .desc = "Reloj de arena con física de granos: se vacía exactamente en un minuto y da la vuelta."},
    {.name = "ENGRANAJES", .res = FX_WIDE, .enter = gears_enter, .render = gears_render,
     .label = "Engranajes", .group = FX_G_CLOCKS,
     .desc = "Reloj analógico mecánico: engranajes que engranan, volante oscilante y agujas con segundero."},
    // 16: retired (Acuario, removed for good on 2026-11-07). The number stays taken.
    {.name = "RETIRED", .retired = true, .label = "", .desc = ""},
    {.name = "LLUVIA", .res = FX_WIDE, .enter = rain_enter, .render = rain_render,
     .label = "Lluvia", .group = FX_G_NATURE,
     .desc = "Gotas que resbalan por el cristal sobre una ciudad desenfocada; la hora en neón que parpadea."},
    {.name = "AGUJERO NEGRO", .landscape = true, .enter = blackhole_enter, .render = blackhole_render,
     .label = "Agujero negro", .group = FX_G_NATURE,
     .desc = "Disco de acreción con efecto Doppler y lente gravitatoria; las estrellas del fondo se curvan alrededor."},
    {.name = "GOL", .landscape = true, .enter = goal_enter, .render = goal_render,
     .label = "Gol", .group = FX_G_GAMES,
     .desc = "Jugada de fútbol en 16 bits: regate, salto sobre una entrada, gol por la escuadra y celebración. La hora, en el marcador."},
    // 20: retired (Cuento, removed for good on 2026-11-07). The number stays taken.
    {.name = "RETIRED", .retired = true, .label = "", .desc = ""},
    {.name = "TIEMPO", .res = FX_WIDE, .enter = weather_enter, .render = weather_render,
     .label = "Tiempo", .group = FX_G_CLOCKS, .opts = "wx",
     .desc = "Esfera analógica minimalista con el tiempo actual de tu ciudad: temperatura, máxima y mínima, y un icono animado."},
    {.name = "CLASICO", .res = FX_WIDE, .enter = classic_enter, .render = classic_render,
     .label = "Clásico", .group = FX_G_CLOCKS, .opts = "cl",
     .desc = "Reloj de vestir rectangular de estilo Art Déco: caja de metal pulido, esfera sunburst del color que elijas con reflejos que se mueven, agujas doradas, fecha a las 3 y pequeño segundero a las 6 que late 8 veces por segundo."},
    {.name = "FERIA DE SEVILLA", .res = FX_WIDE, .enter = feria_enter, .render = feria_render,
     .label = "Feria de Sevilla", .group = FX_G_CLOCKS,
     .desc = "Cuenta atrás hasta el alumbrado de la Feria de Abril de Sevilla, con portada de bombillas, farolillos y lunares; durante la Feria, fuegos artificiales."},
};

const char *const fx_group_names[FX_G_COUNT] = {"Efectos", "Videojuegos", "Arte", "Naturaleza y espacio", "Relojes y útiles"};

uint8_t fx_scratch[FX_SCRATCH_SIZE];
static int s_scratch_owner = -1;

bool fx_scratch_claim(int owner_id)
{
    if (s_scratch_owner == owner_id) return true;
    s_scratch_owner = owner_id;
    return false;
}

int clock_draw_stacked(uint16_t *fb, const font_t *f, int y, int gap, uint16_t fill, uint16_t outline, int mode)
{
    char hh[3] = "--", mm[3] = "--";
    if (g_now.valid) {
        snprintf(hh, sizeof(hh), "%02d", clock_display_hour());
        snprintf(mm, sizeof(mm), "%02d", g_now.m);
    }
    font_draw(fb, f, (g_lcd_w - font_width(f, hh, 0)) / 2, y, hh, 0, fill, outline, mode);
    y += f->h + gap;
    font_draw(fb, f, (g_lcd_w - font_width(f, mm, 0)) / 2, y, mm, 0, fill, outline, mode);
    return y + f->h;
}

void clock_draw_row(uint16_t *fb, const font_t *f, int y, int spacing, uint16_t fill, uint16_t outline, int mode)
{
    char buf[8];
    clock_hhmm(buf, !g_now.valid || g_now.ms < 500);
    // Measure with the colon so the digits do not jump when it blinks
    char ref[8] = "00:00";
    int x = (g_lcd_w - font_width(f, ref, spacing)) / 2;
    font_draw(fb, f, x, y, buf, spacing, fill, outline, mode);
}

void clock_draw_seconds_bar(uint16_t *fb, int x, int y, int w, int h, uint16_t fg, uint16_t bg)
{
    if (!g_app.show_secs || !g_now.valid) return;
    gfx_fill_rect(fb, x, y, w, h, bg);
    gfx_fill_rect(fb, x, y, (int)(w * g_now.minute_ms / 60000), h, fg);
}

void clock_row_grad(uint16_t *fb, const font_t *f, int cx, int y, int spacing, uint16_t top, uint16_t bottom,
                    uint16_t outline, int shadow)
{
    char buf[8];
    clock_hhmm(buf, !g_now.valid || g_now.ms < 500);
    font_draw_grad(fb, f, cx - font_width(f, "00:00", spacing) / 2, y, buf, spacing, top, bottom, outline, shadow);
}

void clock_date_str(char *buf)
{
    if (!g_now.valid) {
        sprintf(buf, "SIN HORA");
        return;
    }
    sprintf(buf, "%s %d %s", clock_wday_name(g_now.wday), g_now.day, clock_month_name(g_now.mon));
}

int16_t sin_q14[1024];
uint8_t sin_u8[256];
static uint32_t s_rng;

void fx_common_init(void)
{
    // FX_COUNT and the list must agree: a missing entry would be a NULL render at runtime
    for (int i = 0; i < FX_COUNT; i++) {
        if (fx_list[i].retired) continue;
        if (!fx_list[i].render || !fx_list[i].enter || !fx_list[i].label || !fx_list[i].desc) {
            ESP_LOGE("fx", "fx_list[%d] is incomplete", i);
            abort();
        }
    }
    for (int i = 0; i < 1024; i++) sin_q14[i] = (int16_t)lroundf(sinf(i * 6.2831853f / 1024) * 16384);
    for (int i = 0; i < 256; i++) sin_u8[i] = (uint8_t)(127.5f + 127.5f * sinf(i * 6.2831853f / 256));
    s_rng = esp_random() | 1;
}

int fx_next(int from)
{
    for (int k = 1; k <= FX_COUNT; k++) {
        int n = (from + k) % FX_COUNT;
        if (!fx_list[n].retired) return n;
    }
    return 0;
}

int fx_prev(int from)
{
    for (int k = 1; k <= FX_COUNT; k++) {
        int n = (from - k + FX_COUNT) % FX_COUNT;
        if (!fx_list[n].retired) return n;
    }
    return 0;
}

uint32_t fx_rand(void)
{
    s_rng ^= s_rng << 13;
    s_rng ^= s_rng >> 17;
    s_rng ^= s_rng << 5;
    return s_rng;
}

uint16_t hsv565(uint8_t h, uint8_t s, uint8_t v)
{
    if (s == 0) return rgb565(v, v, v);
    uint8_t region = h / 43;
    uint8_t rem = (h - region * 43) * 6;
    uint8_t p = (v * (255 - s)) >> 8;
    uint8_t q = (v * (255 - ((s * rem) >> 8))) >> 8;
    uint8_t t = (v * (255 - ((s * (255 - rem)) >> 8))) >> 8;
    switch (region) {
    case 0: return rgb565(v, t, p);
    case 1: return rgb565(q, v, p);
    case 2: return rgb565(p, v, t);
    case 3: return rgb565(p, q, v);
    case 4: return rgb565(t, p, v);
    default: return rgb565(v, p, q);
    }
}
