// Speaker. The ES8311 is set up over I2C as a slave DAC at 16 kHz with MCLK = 256 fs (register values
// from Espressif's esp_codec_dev es8311 driver), fed by I2S0; a task synthesises the sounds.
#include "audio.h"
#include <math.h>
#include <string.h>
#include "app.h"
#include "board.h"
#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "i2c_bus.h"

#define ES_ADDR 0x18
#define RATE 16000
#define CHUNK 256

static const char *TAG = "audio";
static i2s_chan_handle_t s_tx;
static TaskHandle_t s_task;
static volatile int s_mode;      // 0 silence, 1 alarm, 2 ring, 3 beep
static volatile int s_beep_ms;
static bool s_ok;

typedef struct {
    uint8_t reg, val;
} rv_t;

// Power-up, clocks (16 kHz from a 4.096 MHz MCLK) and a 16-bit I2S input to the DAC
static const rv_t ES_INIT[] = {
    {0x44, 0x08}, {0x44, 0x08}, {0x01, 0x30}, {0x02, 0x00}, {0x03, 0x10}, {0x16, 0x24}, {0x04, 0x10},
    {0x05, 0x00}, {0x0B, 0x00}, {0x0C, 0x00}, {0x10, 0x1F}, {0x11, 0x7F}, {0x00, 0x80}, {0x01, 0x3F},
    {0x13, 0x10}, {0x1B, 0x0A}, {0x1C, 0x6A}, {0x06, 0x03}, {0x07, 0x00}, {0x08, 0xFF}, {0x09, 0x0C},
    {0x0A, 0x4C}, {0x17, 0xBF}, {0x0E, 0x02}, {0x12, 0x00}, {0x14, 0x1A}, {0x0D, 0x01}, {0x15, 0x40},
    {0x37, 0x08}, {0x45, 0x00}, {0x32, 0xBF},
};

static void set_volume(int vol)
{
    // 0xBF is 0 dB; 0.5 dB per step. 0..100 % maps to -50..0 dB (0 = mute)
    int v = vol <= 0 ? 0 : 0xBF - (100 - vol);
    i2c_reg_write(ES_ADDR, 0x32, (uint8_t)v);
}

// One note: frequency (0 = rest) and length; the melodies end with {0, 0}
typedef struct {
    uint16_t hz, ms;
} note_t;

static const note_t ALARM[] = {{880, 120}, {0, 60}, {880, 120}, {0, 60}, {1175, 220}, {0, 500}, {0, 0}};
static const note_t RING[] = {{784, 400}, {659, 400}, {0, 900}, {0, 0}};

static void play_tone(int hz, int ms, int amp)
{
    static int16_t buf[CHUNK * 2];
    int total = RATE * ms / 1000, done = 0;
    uint32_t phase = 0, step = hz ? (uint32_t)((uint64_t)hz * 65536 * 65536 / RATE) : 0;
    while (done < total) {
        int n = total - done < CHUNK ? total - done : CHUNK;
        for (int i = 0; i < n; i++) {
            int16_t v = 0;
            if (hz) {
                // soft square wave (a triangle would be too quiet on this small speaker)
                v = (phase >> 31) ? amp : -amp;
                int edge = done + i;
                if (edge < 80) v = v * edge / 80;  // short attack/decay: no clicks
                else if (total - edge < 80) v = v * (total - edge) / 80;
                phase += step;
            }
            buf[2 * i] = buf[2 * i + 1] = v;
        }
        size_t w;
        i2s_channel_write(s_tx, buf, n * 4, &w, portMAX_DELAY);
        done += n;
        if (!s_mode) break;
    }
}

static void task(void *arg)
{
    for (;;) {
        int mode = s_mode;
        if (!mode) {
            gpio_set_level(BOARD_PIN_AMP, 0);
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            continue;
        }
        gpio_set_level(BOARD_PIN_AMP, 1);
        set_volume(g_app.volume);
        if (mode == 3) {
            play_tone(1600, s_beep_ms, 6000);
            if (s_mode == 3) s_mode = 0;
            continue;
        }
        const note_t *m = mode == 1 ? ALARM : RING;
        for (int i = 0; m[i].ms && s_mode == mode; i++) play_tone(m[i].hz, m[i].ms, 9000);
    }
}

void audio_init(void)
{
    for (size_t i = 0; i < sizeof(ES_INIT) / sizeof(ES_INIT[0]); i++) {
        if (!i2c_reg_write(ES_ADDR, ES_INIT[i].reg, ES_INIT[i].val)) {
            ESP_LOGW(TAG, "ES8311 not answering");
            return;
        }
    }
    gpio_config_t pa = {.pin_bit_mask = 1ULL << BOARD_PIN_AMP, .mode = GPIO_MODE_OUTPUT};
    gpio_config(&pa);
    gpio_set_level(BOARD_PIN_AMP, 0);

    i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    cc.auto_clear = true;
    if (i2s_new_channel(&cc, &s_tx, NULL) != ESP_OK) return;
    i2s_std_config_t sc = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {.mclk = BOARD_I2S_MCLK, .bclk = BOARD_I2S_SCLK, .ws = BOARD_I2S_LRCK, .dout = BOARD_I2S_DOUT,
                     .din = I2S_GPIO_UNUSED},
    };
    sc.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
    if (i2s_channel_init_std_mode(s_tx, &sc) != ESP_OK || i2s_channel_enable(s_tx) != ESP_OK) return;
    xTaskCreate(task, "audio", 3072, NULL, 5, &s_task);
    s_ok = true;
    ESP_LOGI(TAG, "ES8311 ready");
}

static void start(int mode)
{
    if (!s_ok) return;
    s_mode = mode;
    xTaskNotifyGive(s_task);
}

void audio_alarm_start(void) { start(1); }
void audio_ring_start(void) { start(2); }
void audio_stop(void) { s_mode = 0; }

void audio_beep(int ms)
{
    if (!s_ok || s_mode == 1 || s_mode == 2) return;
    s_beep_ms = ms;
    start(3);
}
