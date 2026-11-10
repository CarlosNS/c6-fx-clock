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
#include "apps.h"
#include "esp_pm.h"
#include "esp_sleep.h"
#include "freertos/semphr.h"
#include "esp_system.h"
#include "soc/usb_serial_jtag_struct.h"
#include "hal/usb_serial_jtag_ll.h"
#include "batlog.h"
#include "usbdisk.h"
#include "web.h"
#include "weather.h"
#include "wifi_config.h"
#include "alarm.h"
#include "audio.h"
#include "input.h"
#include "pmu_axp2101.h"
#include "intro.h"
#include "voice.h"
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
    .speed = 75,  // a calmer pace by default (the owner's choice)
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
    .onix_hue = 28,      // amber
    .raise_wake = 1,
    .tap_wake = 1,
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

static bool s_force_off;  // the PWR key turned the screen off: even "always on" waits for the next wake

void app_wake(void)
{
    s_force_off = false;
    g_app.wake_until = now_ms() + g_app.wake_secs * 1000;
}

static bool screen_awake(uint32_t now)
{
    if (s_force_off) return false;
    // A pairing passkey stays visible until the pairing ends, however long typing it takes
    return g_app.always_on || g_app.pairing_code >= 0 || sys_keep_awake() || (int32_t)(g_app.wake_until - now) > 0;
}

static void toast(const char *msg)
{
    strlcpy(s_toast, msg, sizeof(s_toast));
    s_toast_t = now_ms();
}

// ---- Screen helpers ----

// Boot screen, in the system's style: the name, how to reach the watch (Bluetooth name for the app;
// the own Wi-Fi only when it is up) and its address on the home network
static void draw_splash(uint32_t t)
{
    ui_begin(lcd_native_fb());
    ui_background(false);
    ui_veil(120, UI_H, 2);
    ui_print_c(&ui_jersey, 7, UI_W / 2, 70, "Marea", MS_ORO, true);
    int dots = (int)(t / 400) % 4;
    char wait[8] = "";
    for (int i = 0; i < dots; i++) wait[i] = '.';
    bool wifi = net_enabled();  // the saved network is known even with the Wi-Fi off: say nothing about it then
    int rows = (wifi && (g_app.sta_ip[0] || g_app.sta_ssid[0]) ? 1 : 0) + (g_app.ap_ip[0] ? 1 : 0);
    ui_panel(40, 196, UI_W - 80, 104 + rows * 64, MS_PANEL, MS_LINEA, 3, true);
    ui_print_c(&ui_vt, 1, UI_W / 2, 210, TR("En la app Marea, busca:", "In the Marea app, look for:"), MS_GRIS, false);
    ui_print_c(&ui_jersey, 3, UI_W / 2, 238, ble_name()[0] ? ble_name() : wait, MS_CREMA, false);
    int y = 292;
    if (!wifi) {
    } else if (g_app.sta_ip[0]) {
        ui_print_c(&ui_vt, 1, UI_W / 2, y, TR("En tu WiFi:", "On your Wi-Fi:"), MS_GRIS, false);
        ui_print_c(&ui_vt, 2, UI_W / 2, y + 22, g_app.sta_ip, MS_VERDE, false);
        y += 64;
    } else if (g_app.sta_ssid[0]) {
        char b[48];
        snprintf(b, sizeof(b), TR("conectando a %s%s", "joining %s%s"), g_app.sta_ssid, wait);
        ui_print_c(&ui_vt, 1, UI_W / 2, y + 10, b, MS_GRIS, false);
        y += 64;
    }
    if (g_app.ap_ip[0]) {
        ui_print_c(&ui_vt, 1, UI_W / 2, y, TR("o su WiFi " AP_SSID ", clave " AP_PASS, "or its Wi-Fi " AP_SSID ", key " AP_PASS),
                   MS_GRIS, false);
        ui_print_c(&ui_vt, 2, UI_W / 2, y + 22, g_app.ap_ip, MS_CREMA, false);
    }
    lcd_flush_native();
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
// With voice commands on, holding it records one instead (voice.c) until it is let go; the
// auto-cycle and forget-phones holds then do nothing (they stay in the settings).
// Every press keeps the screen on for another wake_secs.
#define VOICE_HOLD_MS 450

static void poll_button(uint32_t now)
{
    static bool down, waking, hinted, forgot, voiced;
    static uint32_t since;
    bool pressed = gpio_get_level(BOARD_PIN_BUTTON) == 0;
    if (pressed && !down) {
        down = true;
        waking = !screen_awake(now);  // a waking press does nothing else
        hinted = forgot = voiced = false;
        since = now;
        app_wake();
    } else if (pressed && down) {
        app_wake();
        if (waking || voiced) return;
        if (g_app.voice && now - since > VOICE_HOLD_MS && !alarm_ringing()) {
            voiced = true;
            voice_begin();
            return;
        }
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
        if (voiced) {
            voice_end();
            return;
        }
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

// Raise to wake, checked while the screen is off (10 times a second). The screen comes on when
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

// ---------------------------------------------------------------- power

// Light sleep: with the screen off and no USB cable, the chip sleeps between the loop's passes (the
// accelerometer, button and touch are still read 20 times a second) and Bluetooth keeps its link
// in modem sleep. On USB it stays awake so the serial console works; "sleeptest <s>" (console)
// allows it anyway for a while, to measure it with "pm".
volatile uint32_t g_sleep_test_until;
static bool s_usb, s_ls_on;

// Plugging the cable in while the chip sleeps: the computer tries to enumerate it, gets no answer and
// gives up, and the serial console (and flashing through it) stays dead even across software resets.
// Dropping the D+ pull-up for a moment looks like unplugging and plugging back, and the computer tries
// again. Done at boot and whenever the cable appears.
static void usb_console_reconnect(void)
{
    // After the USB disk mode the internal PHY is still routed to USB OTG (TinyUSB), and a software
    // reset does not route it back: give it to the serial console again
    usb_serial_jtag_ll_phy_enable_external(false);
    USB_SERIAL_JTAG.conf0.usb_pad_enable = 1;
    USB_SERIAL_JTAG.conf0.pad_pull_override = 1;
    USB_SERIAL_JTAG.conf0.dp_pullup = 0;
    vTaskDelay(pdMS_TO_TICKS(50));
    USB_SERIAL_JTAG.conf0.dp_pullup = 1;
    USB_SERIAL_JTAG.conf0.pad_pull_override = 0;
}

static int log_nowhere(const char *fmt, va_list ap) { return 0; }

// Screen off, the loop runs 10 times a second; a touch must not wait for the next pass (a quick tap
// could slip between two). The touch controller pulls its INT line low on a touch: that wakes the
// chip from light sleep and, through this interrupt, the loop at once. The interrupt switches itself
// off until the loop has seen the touch and the line is high again (no storm while the finger stays).
static SemaphoreHandle_t s_wake_sem;
static volatile bool s_touch_irq;

static void IRAM_ATTR touch_isr(void *arg)
{
    gpio_intr_disable(BOARD_TOUCH_INT);
    s_touch_irq = true;
    BaseType_t hp = pdFALSE;
    xSemaphoreGiveFromISR(s_wake_sem, &hp);
    if (hp) portYIELD_FROM_ISR();
}

static void touch_wake_arm(bool on)
{
    if (on) {
        s_touch_irq = false;
        gpio_wakeup_enable(BOARD_TOUCH_INT, GPIO_INTR_LOW_LEVEL);  // also the interrupt type, low level
        gpio_intr_enable(BOARD_TOUCH_INT);
    } else {
        gpio_intr_disable(BOARD_TOUCH_INT);
        gpio_wakeup_disable(BOARD_TOUCH_INT);
    }
}

// A touch on the dark screen ("tap" setting): true when it should turn the screen on. A double tap
// needs a second touch 60..600 ms after the first; meanwhile the loop polls quickly (touch_tap_pending)
#define TAP2_MIN_MS 60
#define TAP2_MAX_MS 600
static uint32_t s_tap1;

static bool touch_tap_wakes(uint32_t now)
{
    if (g_app.tap_wake == 1) return true;
    if (g_app.tap_wake != 2) return false;
    if (s_tap1 && now - s_tap1 >= TAP2_MIN_MS && now - s_tap1 <= TAP2_MAX_MS) {
        s_tap1 = 0;
        return true;
    }
    s_tap1 = now ? now : 1;
    return false;
}

static bool touch_tap_pending(uint32_t now)
{
    if (s_tap1 && now - s_tap1 > TAP2_MAX_MS) s_tap1 = 0;
    return s_tap1 != 0;
}

// A touch that did not wake the screen: listen again once the finger is off (INT high)
static void touch_wake_rearm(void)
{
    if (!s_touch_irq && gpio_get_level(BOARD_TOUCH_INT) == 1) gpio_intr_enable(BOARD_TOUCH_INT);
}

static void touch_wake_init(void)
{
    s_wake_sem = xSemaphoreCreateBinary();
    gpio_config_t io = {.pin_bit_mask = 1ULL << BOARD_TOUCH_INT, .mode = GPIO_MODE_INPUT, .pull_up_en = GPIO_PULLUP_ENABLE,
                        .intr_type = GPIO_INTR_DISABLE};
    gpio_config(&io);
    esp_sleep_enable_gpio_wakeup();
    if (gpio_install_isr_service(0) != ESP_OK) ESP_LOGW(TAG, "GPIO ISR service already there");
    gpio_isr_handler_add(BOARD_TOUCH_INT, touch_isr, NULL);
}

static void light_sleep_allow(bool on)
{
    if (on == s_ls_on) return;
    esp_pm_config_t pm = {.max_freq_mhz = 240, .min_freq_mhz = 80, .light_sleep_enable = on};
    // The USB console is off while the chip sleeps: a log line written then would make its task wait
    // for a port nobody can read. Logs go nowhere whenever light sleep is allowed.
    if (on) esp_log_set_vprintf(log_nowhere);
    if (esp_pm_configure(&pm) != ESP_OK) {
        if (on) esp_log_set_vprintf(g_app.usb_data ? vprintf : log_nowhere);
        return;
    }
    s_ls_on = on;
    if (!on) esp_log_set_vprintf(g_app.usb_data ? vprintf : log_nowhere);
    if (!on && s_usb) usb_console_reconnect();  // it may have slept with the cable in ("sleeptest")
}

// An empty battery: rather than letting the voltage collapse in the middle of a flash write, say so
// and cut the power cleanly (settings saved). really = false only shows the screen (console test).
#define BAT_OFF_MV 3300   // under this for 3 readings in a row (9 s), on battery: power off
#define BAT_LOW_PCT 10    // a warning, once per discharge

// The last screen before cutting the power (an empty battery or the PWR key held): settings saved,
// then the PMU switches everything off. really = false shows the screen only (console test).
static void shut_down(const char *title, const char *l1, const char *l2, const char *l3, int show_ms, bool really)
{
    esp_task_wdt_reset();
    lcd_sleep(false);
    ui_begin(lcd_native_fb());
    ui_background(false);
    ui_veil(90, UI_H, 2);
    ui_print_c(&ui_jersey, 4, UI_W / 2, 112, title, MS_ORO, true);
    ui_panel(48, 196, UI_W - 96, 150, MS_PANEL, MS_LINEA, 3, true);
    ui_print_c(&ui_vt, 2, UI_W / 2, 222, l1, MS_CREMA, false);
    ui_print_c(&ui_vt, 1, UI_W / 2, 270, l2, MS_GRIS, false);
    ui_print_c(&ui_vt, 1, UI_W / 2, 294, l3, MS_GRIS, false);
    lcd_flush_native();
    lcd_set_brightness(g_app.brightness);
    settings_save();
    vTaskDelay(pdMS_TO_TICKS(show_ms));
    if (!really) {
        sys_dirty();
        return;
    }
    lcd_set_brightness(0);
    lcd_sleep(true);
    pmu_power_off();
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_deep_sleep_start();  // the PMU did not cut the power: at least stop everything
}

void power_off_now(bool really)
{
    ESP_LOGW(TAG, "Battery empty%s", really ? ": powering off" : " (test screen)");
    shut_down(TR("Batería agotada", "Battery empty"), TR("el reloj se apaga", "the watch switches off"),
              TR("cárgalo y enciéndelo", "charge it and switch it on"), TR("con el botón PWR", "with the PWR button"), 4000, really);
}

// ---------------------------------------------------------------- PWR key (AXP2101, polled)

// A short press turns the screen off (or on). Held, a screen asks to keep holding and fills a bar;
// at PKEY_OFF_MS the watch saves everything and switches off. The PMU's own forced cut is at 10 s.
#define PKEY_SHOW_MS 800
#define PKEY_OFF_MS 3000
static bool s_pkey_down, s_pkey_show;
static uint32_t s_pkey_since;

static void pkey_tap(uint32_t now)
{
    if (alarm_ringing()) {
        alarm_stop();
        return;
    }
    if (screen_awake(now)) {
        s_force_off = true;
        g_app.wake_until = now;
    } else {
        app_wake();
    }
}

static void poll_pwrkey(uint32_t now)
{
    int ev = pmu_pwrkey_poll();
    if (ev) ESP_LOGI(TAG, "PWR key: %s%s%s%s", ev & PMU_PKEY_DOWN ? "down " : "", ev & PMU_PKEY_UP ? "up " : "",
                     ev & PMU_PKEY_SHORT ? "short " : "", ev & PMU_PKEY_LONG ? "long" : "");
    if (ev & PMU_PKEY_DOWN) {
        s_pkey_down = true;
        s_pkey_since = now;
    }
    if (ev & PMU_PKEY_UP) {
        if (s_pkey_down) {
            uint32_t held = now - s_pkey_since;
            s_pkey_down = false;
            if (s_pkey_show) {  // let go before the end: nothing happens
                s_pkey_show = false;
                app_wake();
                sys_dirty();
            } else if (held < PKEY_SHOW_MS) {
                pkey_tap(now);
            }
        }
    } else if ((ev & PMU_PKEY_SHORT) && !s_pkey_down) {
        pkey_tap(now);  // both edges missed between two passes
    }
    if (!s_pkey_down) return;
    uint32_t held = now - s_pkey_since;
    if (held >= PKEY_SHOW_MS) {
        if (!s_pkey_show) ESP_LOGI(TAG, "PWR held: power-off screen");
        s_pkey_show = true;
        app_wake();
    }
    if (held >= PKEY_OFF_MS) {
        ESP_LOGW(TAG, "PWR held %u ms: powering off", (unsigned)held);
        shut_down(TR("Apagando", "Switching off"), TR("hasta luego", "see you soon"),
                  TR("para encenderlo, pulsa", "to switch it on, press"), TR("el botón PWR", "the PWR button"), 1500, true);
    }
}

static void draw_pkey_hold(uint32_t now)
{
    int p = (int)((now - s_pkey_since - PKEY_SHOW_MS) * 1000 / (PKEY_OFF_MS - PKEY_SHOW_MS));
    ui_background(false);
    ui_veil(90, UI_H, 2);
    ui_print_c(&ui_jersey, 5, UI_W / 2, 100, TR("¿Apagar?", "Switch off?"), MS_ORO, true);
    ui_panel(48, 200, UI_W - 96, 168, MS_PANEL, MS_LINEA, 3, true);
    // Two short lines: one of VT323 at 2x did not fit inside the box
    ui_print_c(&ui_vt, 2, UI_W / 2, 216, TR("mantén pulsado", "keep holding"), MS_CREMA, false);
    ui_print_c(&ui_vt, 2, UI_W / 2, 246, TR("el botón PWR", "the PWR button"), MS_CREMA, false);
    ui_bar(76, 290, UI_W - 152, 18, p < 0 ? 0 : p > 1000 ? 1000 : p, MS_ORO);
    ui_print_c(&ui_vt, 1, UI_W / 2, 326, TR("suéltalo para cancelar", "let go to cancel"), MS_GRIS, false);
}

static void battery_guard(const pmu_battery_t *b)
{
    static int empty_count;
    static bool warned;
    if (!b->present || b->usb) {
        empty_count = 0;
        if (b->usb) warned = false;
        return;
    }
    if (!warned && b->percent >= 0 && b->percent <= BAT_LOW_PCT) {
        warned = true;
        app_wake();
        toast(TR("BATERIA BAJA", "LOW BATTERY"));
    }
    bool empty = b->mv > 0 ? b->mv < BAT_OFF_MV : b->percent == 0;
    empty_count = empty ? empty_count + 1 : 0;
    if (empty_count >= 3) power_off_now(true);
}

void app_main(void)
{
    // A software reset out of the USB disk mode leaves the USB PHY on USB OTG, and every log line would
    // then wait on a console nobody can read: give it back to the serial console before anything logs
    usb_serial_jtag_ll_phy_enable_external(false);
    board_init();
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }
    settings_load();
    batlog_init();
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

    // Frequency scaling: the CPU drops to 80 MHz whenever nothing needs it; light sleep is switched on
    // and off by the main loop (light_sleep_allow)
    esp_pm_config_t pm = {.max_freq_mhz = 240, .min_freq_mhz = 80, .light_sleep_enable = false};
    if (esp_pm_configure(&pm) != ESP_OK) ESP_LOGW(TAG, "power management not available");

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
    g_app.wake_until = now_ms() + SPLASH_MS + g_app.wake_secs * 1000;  // show the splash and the clock once at boot
    pmu_pwrkey_init();  // the PWR key: short press = screen off/on, held = switch off (poll_pwrkey)
    if (intro_wanted()) intro_play(g_app.brightness);  // started from off: the power-on animation first
    draw_splash(0);
    lcd_set_brightness(g_app.brightness);

    if (use_wifi) {
        web_start();
        clock_start_sntp();
        weather_init();
    }
    if (use_ble) ble_start();
    sd_check();  // the microSD icon in the status bar
    touch_wake_init();
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
        draw_splash(now - start);
        vTaskDelay(pdMS_TO_TICKS(100));
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
        // Touch gestures; a touch on a fading screen only wakes it (once dark, the touch interrupt and
        // the "tap" setting decide)
        input_poll(now);
        ui_event_t ev;
        while (input_next(&ev)) {
            if (!screen_awake(now)) {
                if (ev.type == EV_DOWN && fade > 0) app_wake();
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
        poll_pwrkey(now);  // after the touch read: an AXP2101 read right before it made the touch fail
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
            bool chg = false, full = false, linked = ble_connected();
            if (pmu_battery(&b)) {
                pct = b.present ? b.percent : -1;
                chg = b.charging || (b.usb && b.present);  // like a phone: plugged in shows the bolt
                full = b.usb && b.present && !b.charging;
                static bool usb_seen;
                static int readings;
                bool first = readings++ == 0;
                s_usb = b.usb;
                if (b.usb && !usb_seen) {  // cable plugged in, or on it at boot
                    if (s_ls_on) light_sleep_allow(false);  // reconnects the console
                    else if (!first || esp_reset_reason() != ESP_RST_USB) usb_console_reconnect();
                }
                usb_seen = b.usb;
                // A computer sends a start-of-frame every millisecond; a charger never does
                static uint32_t last_sof;
                uint32_t sof = USB_SERIAL_JTAG.fram_num.sof_frame_index;
                bool data = b.usb && sof != last_sof;
                last_sof = sof;
                if (data != g_app.usb_data) sys_dirty();
                g_app.usb_data = data;
                // Without a computer reading the console, each log line could wait on the USB port and
                // stall whoever logs (the Bluetooth host stopped answering that way): drop the log
                // then, and bring it back when a computer is there
                static int log_to_usb = -1;
                if (data != log_to_usb) {
                    log_to_usb = data;
                    esp_log_set_vprintf(data && !s_ls_on ? vprintf : log_nowhere);
                }
                battery_guard(&b);
            }
            if (pct != g_app.bat_pct || chg != g_app.bat_chg || full != g_app.bat_full || linked != g_app.phone_linked)
                sys_dirty();
            g_app.bat_pct = pct;
            g_app.bat_chg = chg;
            g_app.bat_full = full;
            g_app.phone_linked = linked;
        }
        // The status bar: Bluetooth blinks while the phone is away, and the Wi-Fi rune follows the link
        {
            static uint32_t blink_t;
            static bool had_ip;
            bool ip = g_app.sta_ip[0] != 0;
            if ((!g_app.phone_linked && now - blink_t >= 500) || ip != had_ip) {
                blink_t = now;
                had_ip = ip;
                sys_dirty();
            }
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
        // Only while the face is on screen: an app marks it to start again (cur = -1), and entering it
        // here on every pass re-initialised the face 30 times a second behind every app
        if (g_app.fx != cur && sys_view() == VIEW_FACE) {
            cur = g_app.fx;
            settings_face_speed(cur);  // each face has its own animation speed
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
                if (use_wifi) net_power(false);
                g_app.screen_off = true;
                ble_link_pace(false);
                touch_wake_arm(true);
            }
            if (s_touch_irq) {  // a touch on the sleeping screen: wake it (next pass draws), as the "tap" setting says
                s_touch_irq = false;
                if (touch_tap_wakes(now)) {
                    app_wake();
                    input_swallow();
                }
            }
            if (!screen_awake(now)) touch_wake_rearm();
            batlog_tick(now, false, g_app.sta_ip[0] != 0, use_wifi && net_ap_on());
            if (have_imu) poll_raise(now, az);
            if (g_app.dirty) {
                g_app.dirty = false;
                settings_save();
            }
            g_app.fps = 0;
            light_sleep_allow(!s_usb || (int32_t)(g_sleep_test_until - now) > 0);
            // 10 passes a second, or at once on a touch; quicker while a double tap may be under way
            xSemaphoreTake(s_wake_sem, pdMS_TO_TICKS(touch_tap_pending(now) ? 10 : 100));
            last = now_ms();
            next_frame = esp_timer_get_time();
            continue;
        }

        if (panel_asleep) {
            STAGE_MAIN(9);
            light_sleep_allow(false);
            touch_wake_arm(false);
            g_app.screen_off = false;
            ble_link_pace(true);
            static uint32_t sd_t;
            if (now - sd_t > 30000) {  // a card may have gone in or out while the screen was off
                sd_t = now;
                sd_check();
            }
            lcd_sleep(false);
            panel_asleep = false;
            if (use_wifi) net_power(true);
            sys_dirty();
        }
        batlog_tick(now, true, g_app.sta_ip[0] != 0, use_wifi && net_ap_on());
        if (s_pkey_show) {  // PWR held: the power-off screen over everything
            ui_begin(lcd_native_fb());
            draw_pkey_hold(now);
            lcd_flush_native();
            cur = -1;
            goto frame_end;
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
