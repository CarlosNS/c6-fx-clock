// Bring-up firmware for the Waveshare ESP32-S3-Touch-AMOLED-2.06 (phases 1-2 of docs/PLAN-s3.md):
// memory, the I2C bus, the power manager and the 30 fps render loop on the AMOLED, with a screen
// that measures the rounded corners. The clock itself (main.c and the faces) moves here in phase 3.
//
// BOOT short press: next screen (corner measurement, test face, test face in landscape).
// BOOT long press: next brightness level.
// Serial console, one letter per command: 0 / 1 / 2 = that screen, s = next screen, b = brightness.
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <unistd.h>
#include "board.h"
#include "driver/gpio.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_psram.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gfx.h"
#include "i2c_bus.h"
#include "lcd.h"
#include "lcd_co5300.h"
#include "pmu_axp2101.h"

#define FRAME_US 33333       // 30 fps, like the C6 clock
#define LONG_PRESS_MS 600

enum { SCREEN_MEASURE, SCREEN_FACE, SCREEN_FACE_LAND, SCREEN_COUNT };

static const char *TAG = "s3";
static int8_t s_sin[256];
static uint16_t s_pal[256];

static void log_system(void)
{
    esp_chip_info_t chip;
    esp_chip_info(&chip);
    uint32_t flash = 0;
    esp_flash_get_physical_size(NULL, &flash);
    ESP_LOGI(TAG, "%s: ESP32-S3 rev %d.%d, %d cores, flash %lu MB, PSRAM %u MB", BOARD_NAME,
             chip.revision / 100, chip.revision % 100, chip.cores, (unsigned long)(flash >> 20),
             (unsigned)(esp_psram_get_size() >> 20));
}

// The touch controller only answers on I2C once out of reset
static void touch_release_reset(void)
{
    gpio_config_t rst = {.pin_bit_mask = 1ULL << BOARD_TOUCH_RST, .mode = GPIO_MODE_OUTPUT};
    gpio_config(&rst);
    gpio_set_level(BOARD_TOUCH_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(BOARD_TOUCH_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(300));
}

// ---- Corner measurement ----
// Lines on the outermost pixels (white at 0, red at 2, green at 4, blue at 6 px from the edge),
// ticks every 10 px (long every 50) and, in each corner, quarter circles of radius 100..160 that
// touch both edges (a first try with 40..100 showed every arc cut by the panel's rounding). The arcs smaller than the panel's own rounding are cut by it; the first one
// drawn whole gives the radius.

static const struct {
    int r;
    uint8_t rgb[3];
    const char *name;
} s_arcs[] = {
    {100, {255, 40, 40}, "100 ROJO"},    {110, {255, 140, 0}, "110 NARANJA"}, {120, {255, 230, 0}, "120 AMARILLO"},
    {130, {40, 220, 40}, "130 VERDE"},   {140, {0, 220, 230}, "140 CIAN"},    {150, {60, 90, 255}, "150 AZUL"},
    {160, {230, 60, 230}, "160 ROSA"},
};

static void outline(uint16_t *fb, int inset, uint16_t c)
{
    int w = g_lcd_w, h = g_lcd_h;
    gfx_fill_rect(fb, inset, inset, w - 2 * inset, 1, c);
    gfx_fill_rect(fb, inset, h - 1 - inset, w - 2 * inset, 1, c);
    gfx_fill_rect(fb, inset, inset, 1, h - 2 * inset, c);
    gfx_fill_rect(fb, w - 1 - inset, inset, 1, h - 2 * inset, c);
}

static void draw_measure(uint16_t *fb)
{
    int w = g_lcd_w, h = g_lcd_h;
    gfx_fill(fb, 0);
    outline(fb, 0, rgb565(255, 255, 255));
    outline(fb, 2, rgb565(255, 0, 0));
    outline(fb, 4, rgb565(0, 255, 0));
    outline(fb, 6, rgb565(0, 80, 255));

    uint16_t tick = rgb565(150, 150, 150);
    for (int x = 10; x < w; x += 10) {
        int len = x % 50 ? 6 : 14;
        gfx_fill_rect(fb, x, 9, 1, len, tick);
        gfx_fill_rect(fb, x, h - 9 - len, 1, len, tick);
    }
    for (int y = 10; y < h; y += 10) {
        int len = y % 50 ? 6 : 14;
        gfx_fill_rect(fb, 9, y, len, 1, tick);
        gfx_fill_rect(fb, w - 9 - len, y, len, 1, tick);
    }

    // Quarter circles: centers (r, r) from each corner, 2 px thick, only inside the corner square
    for (size_t i = 0; i < sizeof(s_arcs) / sizeof(s_arcs[0]); i++) {
        int r = s_arcs[i].r;
        uint16_t c = rgb565(s_arcs[i].rgb[0], s_arcs[i].rgb[1], s_arcs[i].rgb[2]);
        for (int dy = 0; dy <= r; dy++) {
            for (int dx = 0; dx <= r; dx++) {
                float d = sqrtf((float)((r - dx) * (r - dx) + (r - dy) * (r - dy)));
                if (d < r - 2 || d > r) continue;
                fb[dy * w + dx] = c;                              // top left
                fb[dy * w + (w - 1 - dx)] = c;                    // top right
                fb[(h - 1 - dy) * w + dx] = c;                    // bottom left
                fb[(h - 1 - dy) * w + (w - 1 - dx)] = c;          // bottom right
            }
        }
    }

    uint16_t white = rgb565(255, 255, 255), dim = rgb565(170, 180, 210);
    int y = 130;
    gfx_text(fb, (w - gfx_text_width("ESQUINAS", 3)) / 2, y, "ESQUINAS", white, 3);
    y += 36;
    const char *hint = "Arco entero mas pequeno = radio";
    gfx_text(fb, (w - gfx_text_width(hint, 1)) / 2, y, hint, dim, 1);
    y += 22;
    for (size_t i = 0; i < sizeof(s_arcs) / sizeof(s_arcs[0]); i++, y += 20) {
        uint16_t c = rgb565(s_arcs[i].rgb[0], s_arcs[i].rgb[1], s_arcs[i].rgb[2]);
        gfx_text(fb, (w - gfx_text_width(s_arcs[i].name, 2)) / 2, y, s_arcs[i].name, c, 2);
    }
    y += 10;
    const char *b1 = "Bordes: blanco 0, rojo 2,", *b2 = "verde 4, azul 6 px";
    gfx_text(fb, (w - gfx_text_width(b1, 1)) / 2, y, b1, dim, 1);
    gfx_text(fb, (w - gfx_text_width(b2, 1)) / 2, y + 12, b2, dim, 1);
}

// ---- Test face: full-screen plasma, the kind of per-pixel work the real faces do ----

static void face_init(void)
{
    for (int i = 0; i < 256; i++) {
        s_sin[i] = (int8_t)(127 * sinf(i * 6.2831853f / 256));
        float a = i * 6.2831853f / 256;
        s_pal[i] = rgb565((uint8_t)(128 + 127 * sinf(a)), (uint8_t)(128 + 127 * sinf(a + 2.09f)),
                          (uint8_t)(128 + 127 * sinf(a + 4.19f)));
    }
}

static void draw_face(uint16_t *fb, uint32_t t, int fps, int render_us, int send_us)
{
    int w = g_lcd_w, h = g_lcd_h;
    static int8_t ax[LAND_W > LCD_W ? LAND_W : LCD_W], by[LCD_H > LAND_H ? LCD_H : LAND_H], cd[LCD_W + LCD_H];
    for (int x = 0; x < w; x++) ax[x] = s_sin[(x * 3 + t / 7) & 255];
    for (int y = 0; y < h; y++) by[y] = s_sin[(y * 2 - t / 11) & 255];
    for (int k = 0; k < w + h; k++) cd[k] = s_sin[(k + t / 5) & 255];
    for (int y = 0; y < h; y++) {
        uint16_t *row = fb + y * w;
        int b = by[y];
        const int8_t *c = cd + y;
        for (int x = 0; x < w; x++) row[x] = s_pal[(uint8_t)((ax[x] + b + c[x]) >> 1)];
    }

    char buf[48];
    snprintf(buf, sizeof(buf), "%lu.%lu", (unsigned long)(t / 1000), (unsigned long)(t / 100 % 10));
    gfx_text_ol(fb, (w - gfx_text_width(buf, 8)) / 2, h / 2 - 28, buf, rgb565(255, 255, 255), 0, 8, 2);
    // Top centre: the straight part of the edge, clear of the rounded corners
    snprintf(buf, sizeof(buf), "%d fps", fps);
    gfx_text_ol(fb, (w - gfx_text_width(buf, 2)) / 2, 12, buf, rgb565(120, 255, 160), 0, 2, 1);
    snprintf(buf, sizeof(buf), "dibujo %d.%d ms  envio %d.%d ms", render_us / 1000, render_us % 1000 / 100,
             send_us / 1000, send_us % 1000 / 100);
    gfx_text_ol(fb, (w - gfx_text_width(buf, 1)) / 2, 34, buf, rgb565(220, 220, 230), 0, 1, 1);
}

void app_main(void)
{
    log_system();
    touch_release_reset();
    i2c_bus_init();
    ESP_LOGI(TAG, "I2C scan:");
    i2c_bus_scan();
    pmu_log_status();

    lcd_init(2);
    face_init();
    ESP_LOGI(TAG, "free heap: internal %u KB, PSRAM %u KB",
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) >> 10),
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) >> 10));

    gpio_config_t btn = {.pin_bit_mask = 1ULL << BOARD_PIN_BUTTON, .mode = GPIO_MODE_INPUT, .pull_up_en = GPIO_PULLUP_ENABLE};
    gpio_config(&btn);

    int fd = fileno(stdin);  // console: non-blocking reads of the USB serial, no driver needed
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);

    static const int levels[] = {80, 40, 10, 100};
    int level = 0, screen = SCREEN_MEASURE, frames = 0, fps = 0, render_avg = 0, render_max = 0;
    int measure_drawn = 0;  // the measurement screen is still: drawn once into each framebuffer
    int64_t render_sum = 0, next_frame = esp_timer_get_time(), down_since = 0;
    uint32_t fps_t = 0;
    bool lit = false;
    ESP_LOGI(TAG, "BOOT: short press = next screen, long press = brightness");

    for (;;) {
        int64_t now_us = esp_timer_get_time();
        uint32_t now = (uint32_t)(now_us / 1000);

        int want_screen = -1;
        bool next_level = false;
        bool down = gpio_get_level(BOARD_PIN_BUTTON) == 0;
        if (down && !down_since) down_since = now_us;
        if (!down && down_since) {
            if (now_us - down_since > LONG_PRESS_MS * 1000) next_level = true;
            else if (now_us - down_since > 30000) want_screen = (screen + 1) % SCREEN_COUNT;
            down_since = 0;
        }
        char ch;
        while (read(fd, &ch, 1) == 1) {
            if (ch >= '0' && ch < '0' + SCREEN_COUNT) want_screen = ch - '0';
            else if (ch == 's') want_screen = (screen + 1) % SCREEN_COUNT;
            else if (ch == 'b') next_level = true;
        }
        if (next_level) {
            level = (level + 1) % (int)(sizeof(levels) / sizeof(levels[0]));
            lcd_set_brightness(levels[level]);
            ESP_LOGI(TAG, "brightness %d %%", levels[level]);
        }
        if (want_screen >= 0 && want_screen != screen) {
            screen = want_screen;
            lcd_set_landscape(screen == SCREEN_FACE_LAND);
            measure_drawn = 0;
            ESP_LOGI(TAG, "screen %d", screen);
        }

        uint16_t *fb = lcd_fb();
        int64_t r0 = esp_timer_get_time();
        if (screen == SCREEN_MEASURE) {
            if (measure_drawn < 2) draw_measure(fb);
            measure_drawn++;
        } else draw_face(fb, now, fps, render_avg, lcd_send_us());
        int ru = (int)(esp_timer_get_time() - r0);
        render_sum += ru;
        if (ru > render_max) render_max = ru;
        lcd_flush();
        if (!lit) {  // first frame on the panel: now it can light up
            lcd_set_brightness(levels[level]);
            lit = true;
        }

        frames++;
        if (now - fps_t >= 1000) {
            fps = frames * 1000 / (int)(now - fps_t);
            render_avg = (int)(render_sum / frames);
            if (fps_t) {
                ESP_LOGI(TAG, "screen %d: %d fps, draw %d us (max %d), send %d us", screen, fps, render_avg,
                         render_max, lcd_send_us());
            }
            frames = 0;
            render_sum = 0;
            render_max = 0;
            fps_t = now;
        }

        next_frame += FRAME_US;
        int64_t wait = next_frame - esp_timer_get_time();
        if (wait > 1000) vTaskDelay(pdMS_TO_TICKS(wait / 1000));
        else {
            if (wait < -FRAME_US) next_frame = esp_timer_get_time();  // fell behind: don't try to catch up
            vTaskDelay(1);
        }
    }
}
