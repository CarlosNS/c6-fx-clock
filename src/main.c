// Marea: a watch with animated faces and its own apps, controlled from a phone
// (Waveshare ESP32-S3-Touch-AMOLED-2.06, see board.h)
#include <stdio.h>
#include <stdlib.h>
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
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "fx.h"
#include "gfx.h"
#include "hang.h"
#include "lcd.h"
#include "nvs_flash.h"
#include "ota.h"
#include "settings.h"
#include "net.h"
#include "update.h"
#include "usbdisk.h"
#include "web.h"
#include "weather.h"
#include "wifi_config.h"
#include "alarm.h"
#include "audio.h"
#include "input.h"
#include "pmu_axp2101.h"
#include "steps.h"
#include "sys.h"
#include "ui.h"
#include "imu.h"
#include "lcd_co5300.h"
#include "control.h"
#include "touch.h"

#define AUTO_CYCLE_MS 30000
#define SPLASH_MS 5000
#define FRAME_US 33333        // 30 fps cap: keeps the chip cool and the power draw low
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
    .raise_wake = 1,
    .volume = 70,
    .bri_override = -1,
    .step_goal = 10000,
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
    return g_app.always_on || g_app.pairing_code >= 0 || sys_keep_awake() || (int32_t)(g_app.wake_until - now) > 0;
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

static void draw_splash(uint16_t *fb, uint32_t t)
{
    for (int y = 0; y < LCD_H; y++) {
        uint16_t c = hsv565((uint8_t)(170 + y / 6 + t / 30), 200, 40 + y / 8);
        for (int x = 0; x < LCD_W; x++) fb[y * LCD_W + x] = c;
    }
    uint16_t white = rgb565(255, 255, 255), dim = rgb565(170, 180, 210);
    uint16_t accent = hsv565((uint8_t)(t / 8), 160, 255);
    text_centered(fb, 56, "MAREA", accent, 4);
    // Both radios at once: the app over Bluetooth and the web page over Wi-Fi, no modes to switch
    text_centered(fb, 116, "WIFI Y BLUETOOTH", accent, 1);
    text_centered(fb, 140, "En la app del reloj, busca:", dim, 1);
    text_centered(fb, 156, ble_name()[0] ? ble_name() : "...", white, 2);
    text_centered(fb, 190, "o conecta tu movil a:", dim, 1);
    text_centered(fb, 206, AP_SSID, white, 2);
    text_centered(fb, 228, "clave: " AP_PASS, dim, 1);
    text_centered(fb, 248, "y abre", dim, 1);
    text_centered(fb, 262, g_app.ap_ip, white, 2);
    if (g_app.sta_ip[0]) {
        text_centered(fb, 288, "o en tu red WiFi:", dim, 1);
        text_centered(fb, 302, g_app.sta_ip, white, 1);
    } else if (g_app.sta_ssid[0]) {
        text_centered(fb, 288, "conectando a tu WiFi...", dim, 1);
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
// Bluetooth gives the phone 30 s to send the code (SMP timeout); the bar on top counts them down
#define PAIRING_WINDOW_MS 30000

static void draw_pairing(uint16_t *fb, int code, uint32_t now)
{
    char digits[12];
    snprintf(digits, sizeof(digits), "%06d", code % 1000000);
    int w = font_width(&font_round_s, digits, 0), bw = w + 24, bh = 92;
    int x = (g_lcd_w - bw) / 2, y = (g_lcd_h - bh) / 2;
    gfx_darken_rect(fb, x, y, bw, bh, 12, 3);
    int left = PAIRING_WINDOW_MS - (int)(now - g_app.pairing_since);
    if (left < 0) left = 0;
    int bar = (bw - 24) * left / PAIRING_WINDOW_MS;
    uint16_t bar_c = left > 10000 ? rgb565(124, 92, 255) : rgb565(255, 80, 80);
    gfx_fill_rect(fb, x + 12, y + 1, bar, 3, bar_c);
    const char *t1 = "CLAVE BLUETOOTH", *t2 = "Escribela en el movil";
    gfx_text(fb, (g_lcd_w - gfx_text_width(t1, 1)) / 2, y + 10, t1, rgb565(170, 180, 210), 1);
    font_draw(fb, &font_round_s, (g_lcd_w - w) / 2, y + 26, digits, 0, rgb565(255, 255, 255), rgb565(20, 16, 50),
              FONT_FILL | FONT_OUTLINE);
    ui_text(fb, &ui_s, (g_lcd_w - ui_width(&ui_s, t2)) / 2, y + 68, t2, rgb565(170, 180, 210), 0, FONT_FILL);
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
        if (alarm_ringing()) {
            alarm_stop();
        } else if (held > LONG_PRESS_MS && sys_view() == VIEW_FACE) {
            g_app.auto_cycle = !g_app.auto_cycle;
            g_app.dirty = true;
            toast(g_app.auto_cycle ? "AUTO: SI" : "AUTO: NO");
        } else if (held > 30) {
            sys_button_short();  // face: next face; elsewhere: back
        }
    }
}

// Raise to wake, checked while the screen is off (about 25 times a second). The screen comes on when
// it ends up facing up (z over 0.7 g for 3 samples in a row) after not facing up within the last
// 1.2 s (z under 0.3 g): turning the wrist to look. A watch lying still on the table never triggers.
#define RAISE_UP_MG 700
#define RAISE_AWAY_MG 300
#define RAISE_WINDOW_MS 1200

static void poll_raise(uint32_t now, int z)
{
    static uint32_t away_t;
    static int up_count;
    if (!g_app.raise_wake) return;
    if (z < RAISE_AWAY_MG) away_t = now ? now : 1;
    up_count = z > RAISE_UP_MG ? up_count + 1 : 0;
    if (up_count >= 3 && away_t && now - away_t < RAISE_WINDOW_MS) {
        away_t = 0;
        app_wake();
    }
}

void app_main(void)
{
    board_init();
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }
    settings_load();
    boot_mode_t mode = boot_mode_init();
    // Wi-Fi and Bluetooth run together in every mode but UPDATE (a one-shot boot that only fetches
    // an update)
    bool use_wifi = mode != BOOT_UPDATE && mode != BOOT_USB, use_ble = use_wifi;
    // Bluetooth is compiled in but not always used: then give its static RAM (~15 KB) back to the
    // heap. Irreversible until the next reboot, and changing mode always reboots.
    if (!use_ble) {
        unsigned before = (unsigned)heap_caps_get_free_size(MALLOC_CAP_DEFAULT);
        esp_bt_mem_release(ESP_BT_MODE_BTDM);
        ESP_LOGI(TAG, "Bluetooth memory released: %u bytes", (unsigned)heap_caps_get_free_size(MALLOC_CAP_DEFAULT) - before);
    }

    // The screen first, so the framebuffers get the big contiguous blocks of RAM. Update mode keeps
    // one buffer to leave room for HTTPS.
    lcd_init(mode == BOOT_UPDATE ? 1 : 2);
    fx_common_init();
    clock_init();
    steps_init();
    alarms_init();
    audio_init();
    sys_shell_init();
    gpio_config_t btn = {.pin_bit_mask = 1ULL << BOARD_PIN_BUTTON, .mode = GPIO_MODE_INPUT, .pull_up_en = GPIO_PULLUP_ENABLE};
    gpio_config(&btn);
    console_start();

    if (mode == BOOT_UPDATE) {
        lcd_set_brightness(g_app.brightness);
        update_run();  // does not return
    }
    if (mode == BOOT_USB) usbdisk_run();  // does not return

    update_boot_check();
    net_load_saved_ssid();  // known even in Bluetooth mode (it is the network updates use)
    strcpy(g_app.ap_ip, "192.168.4.1");
    g_app.wake_until = now_ms() + SPLASH_MS + g_app.wake_secs * 1000;  // show the splash and the clock once at boot
    draw_splash(lcd_fb(), 0);
    lcd_flush();
    lcd_set_brightness(g_app.brightness);

    if (use_wifi) {
        web_start();
        clock_start_sntp();
        weather_init();
    }
    if (use_ble) ble_start();
    tunnel_enter();  // builds its lookup tables once
    ESP_LOGI(TAG, "Free heap after start: %u (internal RAM %u, largest block %u)",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_DEFAULT), (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));

    // From here on the main loop must keep turning: if it stalls 8 s, the task watchdog reboots the
    // clock (and board_init() reports the step it was stuck in, see hang.h)
    esp_task_wdt_config_t wdt = {.timeout_ms = 8000, .idle_core_mask = 0, .trigger_panic = true};
    esp_task_wdt_reconfigure(&wdt);
    esp_task_wdt_add(NULL);

    // Splash
    uint32_t start = now_ms();
    while (now_ms() - start < SPLASH_MS) {
        uint32_t now = now_ms();
        console_poll();
        clock_update();
        esp_task_wdt_reset();
        draw_splash(lcd_fb(), now - start);
        lcd_flush();
        vTaskDelay(pdMS_TO_TICKS(33));
    }

    int cur = -1, frames = 0, cur_bri = -1, render_max = 0, fade = 1000;
    int64_t render_sum = 0;
    bool confirmed = false, panel_asleep = false;
    uint32_t last = now_ms(), fx_since = last, fps_t = last, anim_t = 0, dirty_t = 0, status_t = 0;
    int last_min = -1;
    int64_t next_frame = esp_timer_get_time();
    for (;;) {
        uint32_t now = now_ms();
        uint32_t dt = now - last;
        if (dt > 100) dt = 100;
        last = now;
        esp_task_wdt_reset();
        STAGE_MAIN(1);
        clock_update();
        STAGE_MAIN(2);
        poll_button(now);
        STAGE_MAIN(3);
        // Touch gestures; a touch on a sleeping screen only wakes it
        input_poll(now);
        ui_event_t ev;
        while (input_next(&ev)) {
            if (ev.type == EV_DOWN && !screen_awake(now)) {
                app_wake();
                input_swallow();
                continue;
            }
            app_wake();
            sys_event(&ev);
        }
        sys_poll_requests();
        STAGE_MAIN(4);
        console_poll();
        // Accelerometer: steps always, raise-to-wake while the screen is off
        int ax, ay, az;
        bool have_imu = imu_read(&ax, &ay, &az);
        if (have_imu) steps_feed(ax, ay, az, now);
        // Once a minute: alarms, the steps day, the time in the status bar
        if (g_now.valid && g_now.m != last_min) {
            last_min = g_now.m;
            alarms_minute();
            steps_minute();
            sys_dirty();
        }
        // Every few seconds: battery and phone link for the status bar
        if (now - status_t > 3000) {
            status_t = now;
            pmu_battery_t b;
            int pct = -1;
            bool chg = false, linked = ble_connected();
            if (pmu_battery(&b)) {
                pct = b.present ? b.percent : -1;
                chg = b.charging;
            }
            if (pct != g_app.bat_pct || chg != g_app.bat_chg || linked != g_app.phone_linked) sys_dirty();
            g_app.bat_pct = pct;
            g_app.bat_chg = chg;
            g_app.phone_linked = linked;
        }
        if (!confirmed && now > CONFIRM_MS) {  // still running after a while: keep this firmware
            ota_confirm_app();
            confirmed = true;
        }

        if (g_app.auto_cycle && now - fx_since > AUTO_CYCLE_MS) {
            // next face not switched off in the app; if there is none, stay where we are
            for (int k = 1; k <= FX_COUNT; k++) {
                int n = (g_app.fx + k) % FX_COUNT;
                if (!fx_list[n].retired && !((g_app.fx_off >> n) & 1)) {
                    g_app.fx = n;
                    break;
                }
            }
        }
        // A retired face (saved on the clock, or asked for by number) shows the next one instead
        if (fx_list[g_app.fx].retired) g_app.fx = fx_next(g_app.fx);
        if (g_app.fx != cur) {
            cur = g_app.fx;
            STAGE_MAIN(5);
            if (fx_list[cur].res == FX_COMPAT) lcd_set_landscape(fx_list[cur].landscape);
            STAGE_MAIN(6);
            fx_list[cur].enter();
            fx_since = now;
            ESP_LOGI(TAG, "Face: %s", fx_list[cur].name);
        }

        // Backlight: night dimming, then fade in/out with the wake state
        int bri = is_night() && g_app.night_bri < g_app.brightness ? g_app.night_bri : g_app.brightness;
        if (g_app.bri_override >= 0) bri = g_app.bri_override;
        bool awake = screen_awake(now);
        fade += awake ? (int)dt * 1000 / FADE_IN_MS : -(int)dt * 1000 / FADE_OUT_MS;
        fade = fade < 0 ? 0 : fade > 1000 ? 1000 : fade;
        int eff = bri * fade / 1000;
        if (eff != cur_bri) {
            lcd_set_brightness(eff);
            cur_bri = eff;
        }
        if (!awake && fade == 0) {  // asleep: no rendering at all, just watch the button
            STAGE_MAIN(8);
            if (!panel_asleep) {
                lcd_sleep(true);
                panel_asleep = true;
            }
            if (have_imu) poll_raise(now, az);
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

        if (panel_asleep) {
            STAGE_MAIN(9);
            lcd_sleep(false);
            panel_asleep = false;
            sys_dirty();
        }
        if (sys_view() != VIEW_FACE) {  // the system screens and apps: native, drawn only when they change
            uint16_t *nfb = lcd_native_fb();
            ui_begin(nfb);
            if (g_app.pairing_code >= 0) sys_dirty();
            if (sys_draw(now)) {
                if (g_app.pairing_code >= 0) draw_pairing(nfb, g_app.pairing_code, now);
                lcd_flush_native();
            }
            cur = -1;  // coming back to the face starts it again
            goto frame_end;
        }
        uint32_t sdt = dt * g_app.speed / 100;
        anim_t += sdt;
        int res = fx_list[cur].res;
        uint16_t *fb = res == FX_NATIVE ? lcd_native_fb() : res == FX_WIDE ? lcd_wide_fb() : lcd_fb();
        int64_t r0 = esp_timer_get_time();
        STAGE_MAIN(10);
        fx_list[cur].render(fb, anim_t, sdt);
        STAGE_MAIN(11);
        int ru = (int)(esp_timer_get_time() - r0);
        render_sum += ru;
        if (ru > render_max) render_max = ru;
        if (g_app.overlay) draw_overlay(fb, cur);
        draw_toast(fb, now);
        int ota = ota_progress();
        if (ota >= 0) draw_ota(fb, ota);
        int code = g_app.pairing_code;
        if (code >= 0) draw_pairing(fb, code, now);
        STAGE_MAIN(12);
        if (res == FX_NATIVE) lcd_flush_native();
        else if (res == FX_WIDE) lcd_flush_wide();
        else lcd_flush();
        STAGE_MAIN(13);

    frame_end:
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
            STAGE_MAIN(14);
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
