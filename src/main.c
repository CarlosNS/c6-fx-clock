// ESP32-C6 FX Clock — animated clock faces on the Waveshare ESP32-C6-LCD-1.47, controlled from a phone
#include <stdio.h>
#include <string.h>
#include "app.h"
#include "ble.h"
#include "board.h"
#include "bootmode.h"
#include "clock.h"
#include "console.h"
#include "driver/gpio.h"
#include "esp_bt.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "fx.h"
#include "gfx.h"
#include "lcd.h"
#include "led.h"
#include "nvs_flash.h"
#include "ota.h"
#include "settings.h"
#include "update.h"
#include "web.h"
#include "weather.h"
#include "wifi_config.h"

#define AUTO_CYCLE_MS 30000
#define SPLASH_MS 5000
#define FRAME_US 33333        // 30 fps cap: keeps the chip cool and the power draw low
#define MODE_HOLD_MS 2000     // holding BOOT this long on the splash switches Wi-Fi <-> Bluetooth mode
#define LONG_PRESS_MS 800
#define FORGET_HINT_MS 3000   // holding BOOT this long shows the "forget phones" hint...
#define FORGET_MS 10000       // ...and this long forgets every paired phone
#define TOAST_MS 1500
#define SAVE_DELAY_MS 3000
#define FADE_IN_MS 150
#define FADE_OUT_MS 400
#define CONFIRM_MS 30000     // a new OTA firmware that runs this long is marked as good

static const char *TAG = "main";

app_state_t g_app = {
    .fx = 0,
    .auto_cycle = 0,
    .speed = 100,
    .brightness = 80,
    .overlay = 0,
    .h24 = 1,
    .show_secs = 1,
    .night_on = 1,
    .night_start = 23,
    .night_end = 7,
    .night_bri = 10,
    .wake_secs = 10,
    .classic_hue = 146,  // deep green
    .dbg_h = -1,
    .pairing_code = -1,
};

static char s_toast[32];
static uint32_t s_toast_t;

static inline uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

void app_wake(void)
{
    g_app.wake_until = now_ms() + g_app.wake_secs * 1000;
}

static bool screen_awake(uint32_t now)
{
    // A pairing passkey stays visible until the pairing ends, however long typing it takes
    return g_app.always_on || g_app.pairing_code >= 0 || (int32_t)(g_app.wake_until - now) > 0;
}

static void toast(const char *msg)
{
    strlcpy(s_toast, msg, sizeof(s_toast));
    s_toast_t = now_ms();
}

// ---- Screen helpers ----

static void text_centered(uint16_t *fb, int y, const char *s, uint16_t color, int scale)
{
    gfx_text(fb, (g_lcd_w - gfx_text_width(s, scale)) / 2, y, s, color, scale);
}

// hold: 0..1000 progress of a BOOT press that switches the mode
static void draw_splash(uint16_t *fb, uint32_t t, int hold)
{
    for (int y = 0; y < LCD_H; y++) {
        uint16_t c = hsv565((uint8_t)(170 + y / 6 + t / 30), 200, 40 + y / 8);
        for (int x = 0; x < LCD_W; x++) fb[y * LCD_W + x] = c;
    }
    uint16_t white = rgb565(255, 255, 255), dim = rgb565(170, 180, 210);
    uint16_t accent = hsv565((uint8_t)(t / 8), 160, 255);
    text_centered(fb, 40, "ESP32-C6", accent, 3);
    text_centered(fb, 72, "FX CLOCK", white, 3);
    bool wifi = boot_mode() == BOOT_WIFI;
    if (wifi) {
        text_centered(fb, 116, "MODO WIFI", accent, 1);
        text_centered(fb, 136, "Conecta tu movil a:", dim, 1);
        text_centered(fb, 152, AP_SSID, white, 2);
        text_centered(fb, 174, "clave: " AP_PASS, dim, 1);
        text_centered(fb, 194, "y abre", dim, 1);
        text_centered(fb, 208, g_app.ap_ip, white, 2);
        if (g_app.sta_ip[0]) {
            text_centered(fb, 238, "o en tu red WiFi:", dim, 1);
            text_centered(fb, 254, g_app.sta_ip, white, 2);
        } else if (g_app.sta_ssid[0]) {
            text_centered(fb, 238, "conectando a tu WiFi...", dim, 1);
        }
    } else {
        text_centered(fb, 116, "MODO BLUETOOTH", accent, 1);
        text_centered(fb, 152, "Abre la app del reloj", white, 1);
        text_centered(fb, 170, "en tu movil y busca:", dim, 1);
        text_centered(fb, 192, ble_name()[0] ? ble_name() : "...", white, 2);
    }
    // Hold BOOT to switch mode; a bar fills while it is held
    const char *hint = wifi ? "Manten BOOT: modo Bluetooth" : "Manten BOOT: modo WiFi";
    text_centered(fb, 288, hint, dim, 1);
    if (hold > 0) {
        gfx_fill_rect(fb, 20, 302, LCD_W - 40, 4, rgb565(40, 46, 70));
        gfx_fill_rect(fb, 20, 302, (LCD_W - 40) * hold / 1000, 4, accent);
    }
}

static void draw_overlay(uint16_t *fb, int fx)
{
    char buf[32];
    gfx_text(fb, 4, 4, fx_list[fx].name, rgb565(255, 255, 255), 1);
    snprintf(buf, sizeof(buf), "%d fps", g_app.fps);
    gfx_text(fb, g_lcd_w - 4 - gfx_text_width(buf, 1), 4, buf, rgb565(120, 255, 160), 1);
    const char *ip = g_app.sta_ip[0] ? g_app.sta_ip : g_app.ap_ip;
    gfx_text(fb, (g_lcd_w - gfx_text_width(ip, 1)) / 2, g_lcd_h - 11, ip, rgb565(200, 200, 220), 1);
}

static void draw_toast(uint16_t *fb, uint32_t now)
{
    if (!s_toast[0] || now - s_toast_t > TOAST_MS) return;
    int w = gfx_text_width(s_toast, 2) + 20, h = 28;
    int x = (g_lcd_w - w) / 2, y = g_lcd_h - h - 18;
    gfx_darken_rect(fb, x, y, w, h, 8, 3);
    gfx_text(fb, x + 10, y + 7, s_toast, rgb565(255, 255, 255), 2);
}

// Firmware update in progress: a bar over whatever face is showing
static void draw_ota(uint16_t *fb, int pct)
{
    int w = g_lcd_w - 40, x = 20, y = g_lcd_h / 2 - 22;
    gfx_darken_rect(fb, x - 10, y - 12, w + 20, 62, 10, 3);
    char buf[32];
    snprintf(buf, sizeof(buf), "ACTUALIZANDO %d%%", pct);
    gfx_text(fb, (g_lcd_w - gfx_text_width(buf, 1)) / 2, y, buf, rgb565(255, 255, 255), 1);
    gfx_fill_rect(fb, x, y + 18, w, 8, rgb565(50, 56, 80));
    gfx_fill_rect(fb, x, y + 18, w * pct / 100, 8, rgb565(124, 92, 255));
    gfx_text(fb, (g_lcd_w - gfx_text_width("NO DESCONECTES", 1)) / 2, y + 34, "NO DESCONECTES", rgb565(170, 180, 210), 1);
}

// Bluetooth pairing: the passkey the phone has to type, over whatever face is showing
static void draw_pairing(uint16_t *fb, int code)
{
    char digits[12];
    snprintf(digits, sizeof(digits), "%06d", code % 1000000);
    int w = font_width(&font_round_s, digits, 0), bw = w + 24, bh = 92;
    int x = (g_lcd_w - bw) / 2, y = (g_lcd_h - bh) / 2;
    gfx_darken_rect(fb, x, y, bw, bh, 12, 3);
    gfx_fill_rect(fb, x + 12, y + 1, bw - 24, 1, rgb565(124, 92, 255));
    const char *t1 = "CLAVE BLUETOOTH", *t2 = "Escribela en el movil";
    gfx_text(fb, (g_lcd_w - gfx_text_width(t1, 1)) / 2, y + 10, t1, rgb565(170, 180, 210), 1);
    font_draw(fb, &font_round_s, (g_lcd_w - w) / 2, y + 26, digits, 0, rgb565(255, 255, 255), rgb565(20, 16, 50),
              FONT_FILL | FONT_OUTLINE);
    ui_text(fb, &ui_s, (g_lcd_w - ui_width(&ui_s, t2)) / 2, y + 68, t2, rgb565(170, 180, 210), 0, FONT_FILL);
}

// The RGB LED mirrors the average color of a few screen pixels, like a tiny ambilight
static void led_follow_screen(const uint16_t *fb, int bri)
{
    int r = 0, g = 0, b = 0;
    for (int i = 0; i < 16; i++) {
        uint16_t p = fb[(g_lcd_h / 5 + (i / 4) * g_lcd_h / 5) * g_lcd_w + (i % 4 + 1) * g_lcd_w / 5];
        p = (p >> 8) | (p << 8);
        r += (p >> 11) << 3;
        g += ((p >> 5) & 0x3F) << 2;
        b += (p & 0x1F) << 3;
    }
    int k = 16 * 6 * 100 / (bri < 1 ? 1 : bri);
    led_set(r / k, g / k, b / k);
}

static bool is_night(void)
{
    if (!g_app.night_on || !g_now.valid) return false;
    int h = g_now.h, s = g_app.night_start, e = g_app.night_end;
    return s <= e ? (h >= s && h < e) : (h >= s || h < e);
}

// Screen off: any press just wakes it. Screen on: short press = next face, long press (released
// before 10 s) = auto-cycle on/off, held 10 s = forget every paired phone.
// Every press keeps the screen on for another wake_secs.
static void poll_button(uint32_t now)
{
    static bool down, waking, hinted, forgot;
    static uint32_t since;
    bool pressed = gpio_get_level(BOARD_PIN_BUTTON) == 0;
    if (pressed && !down) {
        down = true;
        waking = !screen_awake(now);  // a waking press does nothing else
        hinted = forgot = false;
        since = now;
        app_wake();
    } else if (pressed && down) {
        app_wake();
        if (waking) return;
        if (!hinted && now - since > FORGET_HINT_MS) {
            hinted = true;
            toast("10 S: OLVIDAR MOVILES");
        }
        if (!forgot && now - since > FORGET_MS) {
            forgot = true;
            ble_forget_all();
            toast("MOVILES OLVIDADOS");
        }
    } else if (!pressed && down) {
        down = false;
        app_wake();
        if (waking || forgot) return;
        uint32_t held = now - since;
        if (held > LONG_PRESS_MS) {
            g_app.auto_cycle = !g_app.auto_cycle;
            g_app.dirty = true;
            toast(g_app.auto_cycle ? "AUTO: SI" : "AUTO: NO");
        } else if (held > 30) {
            g_app.fx = (g_app.fx + 1) % FX_COUNT;
            g_app.auto_cycle = 0;
            g_app.dirty = true;
        }
    }
}

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }
    settings_load();
    boot_mode_t mode = boot_mode_init();
    // Bluetooth is compiled in but only used in BLE mode: elsewhere give its static RAM (~15 KB)
    // back to the heap. Irreversible until the next reboot, and changing mode always reboots.
    if (mode != BOOT_BLE) {
        unsigned before = (unsigned)heap_caps_get_free_size(MALLOC_CAP_DEFAULT);
        esp_bt_mem_release(ESP_BT_MODE_BTDM);
        ESP_LOGI(TAG, "Bluetooth memory released: %u bytes", (unsigned)heap_caps_get_free_size(MALLOC_CAP_DEFAULT) - before);
    }

    // The screen first, so the framebuffers get the big contiguous blocks of RAM. Update mode keeps
    // one buffer to leave room for HTTPS.
    lcd_init(mode == BOOT_UPDATE ? 1 : BOARD_FB_COUNT);
    fx_common_init();
    clock_init();
    led_init();
    gpio_config_t btn = {.pin_bit_mask = 1ULL << BOARD_PIN_BUTTON, .mode = GPIO_MODE_INPUT, .pull_up_en = GPIO_PULLUP_ENABLE};
    gpio_config(&btn);
    console_start();

    if (mode == BOOT_UPDATE) {
        lcd_set_brightness(g_app.brightness);
        update_run();  // does not return
    }

    strcpy(g_app.ap_ip, "192.168.4.1");
    g_app.wake_until = now_ms() + SPLASH_MS + g_app.wake_secs * 1000;  // show the splash and the clock once at boot
    draw_splash(lcd_fb(), 0, 0);
    lcd_flush();
    lcd_set_brightness(g_app.brightness);

    if (mode == BOOT_WIFI) {
        web_start();
        clock_start_sntp();
        weather_init();
    }
    if (mode == BOOT_BLE) ble_start();
    tunnel_enter();  // builds its lookup tables once
    ESP_LOGI(TAG, "Free heap after start: %u", (unsigned)heap_caps_get_free_size(MALLOC_CAP_DEFAULT));

    // Splash; holding BOOT through it switches between the Wi-Fi and Bluetooth modes
    uint32_t start = now_ms(), held_since = 0;
    while (now_ms() - start < SPLASH_MS || held_since) {
        uint32_t now = now_ms();
        bool pressed = gpio_get_level(BOARD_PIN_BUTTON) == 0;
        if (pressed && !held_since) held_since = now ? now : 1;
        if (!pressed) held_since = 0;
        int hold = held_since ? (int)(now - held_since) * 1000 / MODE_HOLD_MS : 0;
        if (hold >= 1000) boot_mode_switch(mode == BOOT_WIFI ? BOOT_BLE : BOOT_WIFI);
        console_poll();
        clock_update();
        draw_splash(lcd_fb(), now - start, hold);
        lcd_flush();
        vTaskDelay(pdMS_TO_TICKS(33));
    }

    int cur = -1, frames = 0, cur_bri = -1, render_max = 0, fade = 1000;
    int64_t render_sum = 0;
    bool confirmed = false;
    uint32_t last = now_ms(), fx_since = last, fps_t = last, led_t = last, anim_t = 0, dirty_t = 0;
    int64_t next_frame = esp_timer_get_time();
    for (;;) {
        uint32_t now = now_ms();
        uint32_t dt = now - last;
        if (dt > 100) dt = 100;
        last = now;
        clock_update();
        poll_button(now);
        console_poll();
        if (!confirmed && now > CONFIRM_MS) {  // still running after a while: keep this firmware
            ota_confirm_app();
            confirmed = true;
        }

        if (g_app.auto_cycle && now - fx_since > AUTO_CYCLE_MS) g_app.fx = (g_app.fx + 1) % FX_COUNT;
        if (g_app.fx != cur) {
            cur = g_app.fx;
            lcd_set_landscape(fx_list[cur].landscape);
            fx_list[cur].enter();
            fx_since = now;
            ESP_LOGI(TAG, "Face: %s", fx_list[cur].name);
        }

        // Backlight: night dimming, then fade in/out with the wake state
        int bri = is_night() && g_app.night_bri < g_app.brightness ? g_app.night_bri : g_app.brightness;
        bool awake = screen_awake(now);
        fade += awake ? (int)dt * 1000 / FADE_IN_MS : -(int)dt * 1000 / FADE_OUT_MS;
        fade = fade < 0 ? 0 : fade > 1000 ? 1000 : fade;
        int eff = bri * fade / 1000;
        if (eff != cur_bri) {
            lcd_set_brightness(eff);
            cur_bri = eff;
        }
        if (!awake && fade == 0) {  // asleep: no rendering at all, just watch the button
            led_set(0, 0, 0);
            if (g_app.dirty) {
                g_app.dirty = false;
                settings_save();
            }
            g_app.fps = 0;
            vTaskDelay(pdMS_TO_TICKS(40));
            last = now_ms();
            next_frame = esp_timer_get_time();
            continue;
        }

        uint32_t sdt = dt * g_app.speed / 100;
        anim_t += sdt;
        uint16_t *fb = lcd_fb();
        int64_t r0 = esp_timer_get_time();
        fx_list[cur].render(fb, anim_t, sdt);
        int ru = (int)(esp_timer_get_time() - r0);
        render_sum += ru;
        if (ru > render_max) render_max = ru;
        if (g_app.overlay) draw_overlay(fb, cur);
        draw_toast(fb, now);
        int ota = ota_progress();
        if (ota >= 0) draw_ota(fb, ota);
        int code = g_app.pairing_code;
        if (code >= 0) draw_pairing(fb, code);
        if (now - led_t > 40) {
            if (is_night()) led_set(0, 0, 0);
            else led_follow_screen(fb, eff);
            led_t = now;
        }
        lcd_flush();

        if (g_app.freeze_req) {  // a screenshot is reading the last frame
            g_app.frozen = true;
            while (g_app.freeze_req) vTaskDelay(pdMS_TO_TICKS(10));
            g_app.frozen = false;
            last = now_ms();
            next_frame = esp_timer_get_time();
        }

        if (g_app.dirty) {
            g_app.dirty = false;
            dirty_t = now ? now : 1;
        }
        if (dirty_t && now - dirty_t > SAVE_DELAY_MS) {
            dirty_t = 0;
            settings_save();
        }

        frames++;
        if (now - fps_t >= 1000) {
            g_app.fps = frames * 1000 / (now - fps_t);
            g_app.render_us = frames ? (int)(render_sum / frames) : 0;
            g_app.render_max_us = render_max;
            render_sum = 0;
            render_max = 0;
            frames = 0;
            fps_t = now;
        }

        // Frame limiter
        next_frame += FRAME_US;
        int64_t wait = next_frame - esp_timer_get_time();
        if (wait > 1000) vTaskDelay(pdMS_TO_TICKS(wait / 1000));
        else {
            if (wait < -FRAME_US) next_frame = esp_timer_get_time();  // fell behind: don't try to catch up
            vTaskDelay(1);
        }
    }
}
