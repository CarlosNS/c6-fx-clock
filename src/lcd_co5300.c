// lcd.h for the Waveshare ESP32-S3-Touch-AMOLED-2.06: CO5300 AMOLED, 410x502, over QSPI.
//
// Compatibility mode: faces not redrawn for this screen yet draw on a canvas (LCD_W x LCD_H = 172x320, or 320x172 in
// landscape) and this driver enlarges it while sending: portrait x1.5 to 258x480, landscape x1.25 to
// 400x215 (the watch is worn upright, so landscape faces are shown level, not turned), both centred
// with black around. Every third (x1.5) or fifth (x1.25) output pixel is the average of its two
// neighbours instead of a repeated one, which keeps lines even.
//
// Two canvases live in PSRAM. lcd_flush hands the finished one to a task on the second core, which
// sends it while the caller draws the next. The SPI DMA can't read PSRAM, so the task builds the panel
// rows in strips in two internal buffers, filling one while the other is on the wire. That task is
// the only one talking to the panel after lcd_init (brightness changes go through it as well).
#include "lcd.h"
#include <assert.h>
#include <string.h>
#include "board.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "hang.h"

// The CO5300 speaks the SH8601 QSPI protocol: every transfer starts with a 32-bit header
// (opcode, 0x00, command, 0x00). Opcode 0x02 writes a command with 1-line parameters, opcode
// 0x32 writes pixel data on 4 lines.
#define LCD_HOST SPI2_HOST
#define LCD_PCLK (80 * 1000 * 1000)
#define PW BOARD_PANEL_W
#define PH BOARD_PANEL_H
#define FB_BYTES (LCD_W * LCD_H * sizeof(uint16_t))
// Even (the CO5300 wants even start rows and heights). Two strips of 20 lines take 33 KB of internal
// RAM; taller strips send a frame no faster and leave less room for Wi-Fi + Bluetooth + HTTPS
#define STRIP_LINES 20
#define STRIP_BYTES (STRIP_LINES * PW * sizeof(uint16_t))

#define BIT_FRAME 1
#define BIT_BRIGHTNESS 2
#define BIT_POWER 4

static const char *TAG = "co5300";
static SemaphoreHandle_t s_idle;     // given when the sender has finished the last frame
static TaskHandle_t s_sender;
// Frames come in three kinds: the 172x320 canvas (portrait or landscape, enlarged while sending) or
// a native 410x502 frame, copied as is
enum { KIND_CANVAS, KIND_CANVAS_LAND, KIND_WIDE, KIND_NATIVE };
static uint16_t *s_buf[2], *s_wide[2], *s_native[2], *s_strip[2];
static int s_widx;
static int s_idx, s_nidx;
static bool s_land;                  // the canvas is in landscape
static uint16_t *s_last;
static int s_last_kind;
static const uint16_t *s_sending;
static int s_send_kind;
static volatile uint8_t s_brightness;
static volatile bool s_want_sleep;
static volatile int s_send_us;
int g_lcd_w = LCD_W, g_lcd_h = LCD_H;

// Panel column / row -> canvas pixels to show there: a and b equal = that pixel, different = their
// average, -1 = black border. [0] portrait, [1] landscape.
// [0] portrait canvas, [1] landscape canvas, [2] wide canvas
static int16_t s_col_a[3][PW], s_col_b[3][PW], s_row_a[3][PH], s_row_b[3][PH];

typedef struct {
    uint8_t cmd;
    uint8_t data[4];
    uint8_t len;
    uint8_t delay_ms;
} init_cmd_t;

// Waveshare's sequence for this panel (BSP lcd_init_cmds), preceded by what the sh8601 driver
// sends first: memory access control (no mirroring) and 16-bit pixels.
static const init_cmd_t s_init[] = {
    {0x36, {0x00}, 1, 0},                          // MADCTL
    {0x3A, {0x55}, 1, 0},                          // COLMOD: RGB565
    {0x11, {0}, 0, 120},                           // sleep out
    {0xC4, {0x80}, 1, 0},                          // QSPI mode control
    {0x44, {0x01, 0xD1}, 2, 0},                    // tearing scanline
    {0x35, {0x00}, 1, 0},                          // tearing effect on
    {0x53, {0x20}, 1, 10},                         // brightness control on
    {0x63, {0xFF}, 1, 10},                         // high brightness mode limit
    {0x51, {0x00}, 1, 10},                         // brightness 0: dark until the first frame
    {0x2A, {0x00, 0x16, 0x01, 0xAF}, 4, 0},        // columns 22..431
    {0x2B, {0x00, 0x00, 0x01, 0xF5}, 4, 0},        // rows 0..501
    {0x29, {0}, 0, 10},                            // display on
};

// Fills one axis of the scaling tables: `src` canvas pixels become src * num / den panel pixels
// starting at `offset`. pattern[] lists, for each output pixel of a group, the two source pixels
// (relative to the group) it shows.
static void build_axis(int16_t *a, int16_t *b, int out_len, int offset, int src, int num, int den,
                       const int8_t (*pattern)[2])
{
    int len = src * num / den;
    for (int i = 0; i < out_len; i++) {
        int r = i - offset;
        if (r < 0 || r >= len) {
            a[i] = b[i] = -1;
            continue;
        }
        int g = r / num, p = r % num;
        a[i] = (int16_t)(g * den + pattern[p][0]);
        b[i] = (int16_t)(g * den + pattern[p][1]);
    }
}

static void build_tables(void)
{
    static const int8_t x15[3][2] = {{0, 0}, {0, 1}, {1, 1}};                  // 2 -> 3
    static const int8_t x125[5][2] = {{0, 0}, {1, 1}, {1, 2}, {2, 2}, {3, 3}}; // 4 -> 5
    int pw = LCD_W * 3 / 2, ph = LCD_H * 3 / 2;
    build_axis(s_col_a[0], s_col_b[0], PW, (PW - pw) / 2, LCD_W, 3, 2, x15);
    build_axis(s_row_a[0], s_row_b[0], PH, (PH - ph) / 2, LCD_H, 3, 2, x15);
    int lw = LAND_W * 5 / 4, lh = LAND_H * 5 / 4;
    build_axis(s_col_a[1], s_col_b[1], PW, (PW - lw) / 2, LAND_W, 5, 4, x125);
    build_axis(s_row_a[1], s_row_b[1], PH, (PH - lh) / 2, LAND_H, 5, 4, x125);
    // Wide canvas: 274 x 336 x1.5 = 411 x 504, a pixel wider and two taller than the panel (centred)
    build_axis(s_col_a[2], s_col_b[2], PW, (PW - LCD_WW * 3 / 2) / 2, LCD_WW, 3, 2, x15);
    build_axis(s_row_a[2], s_row_b[2], PH, (PH - LCD_WH * 3 / 2) / 2, LCD_WH, 3, 2, x15);
}

// Average of two byte-swapped RGB565 pixels
static inline uint16_t avg565(uint16_t p, uint16_t q)
{
    uint16_t a = __builtin_bswap16(p), b = __builtin_bswap16(q);
    return __builtin_bswap16((uint16_t)((((a ^ b) & 0xF7DE) >> 1) + (a & b)));
}

// Panel rows y0..y0+n-1 into a strip
static void build_strip(uint16_t *dst, const uint16_t *fb, int y0, int n, int kind)
{
    int o = kind == KIND_WIDE ? 2 : kind == KIND_CANVAS_LAND ? 1 : 0;
    int cw = kind == KIND_WIDE ? LCD_WW : kind == KIND_CANVAS_LAND ? LAND_W : LCD_W;
    const int16_t *ca = s_col_a[o], *cb = s_col_b[o];
    uint16_t mix[LAND_W > LCD_W ? LAND_W : LCD_W];
    for (int j = 0; j < n; j++, dst += PW) {
        int ra = s_row_a[o][y0 + j], rb = s_row_b[o][y0 + j];
        if (ra < 0) {
            memset(dst, 0, PW * 2);
            continue;
        }
        const uint16_t *line = fb + ra * cw;
        if (rb != ra) {  // halfway between two canvas rows
            const uint16_t *l2 = fb + rb * cw;
            for (int i = 0; i < cw; i++) mix[i] = avg565(line[i], l2[i]);
            line = mix;
        }
        for (int x = 0; x < PW; x++) {
            int a = ca[x];
            if (a < 0) dst[x] = 0;
            else if (a == cb[x]) dst[x] = line[a];
            else dst[x] = avg565(line[a], line[cb[x]]);
        }
    }
}

// ---------------------------------------------------------------- QSPI transport
//
// Driven straight through spi_master: every command is opcode 0x02 + 24-bit address (0x00, cmd, 0x00)
// + parameters on one line; pixels are opcode 0x32 + address 0x002C00 + data on four lines.
// A frame is ONE pixel write over the whole window: the bus is held and CS stays low from the header
// to the last strip, and the strips are queued back to back. (esp_lcd's panel IO, used before, mixed
// polled window commands with queued strips and now and then never came back: the watch froze.)

static spi_device_handle_t s_dev;
static spi_transaction_ext_t s_tr[2];

static void panel_cmd(uint8_t cmd, const uint8_t *data, int len)
{
    spi_transaction_t t = {
        .cmd = 0x02,
        .addr = (uint32_t)cmd << 8,
        .length = len * 8,
    };
    // A command with no data (sleep in / out, display on / off) must not ask for a data phase: the
    // driver rejects it, and the panel would never wake from sleep
    if (len == 0) {
        t.tx_buffer = NULL;
    } else if (len <= 4) {
        t.flags = SPI_TRANS_USE_TXDATA;
        for (int i = 0; i < len; i++) t.tx_data[i] = data[i];
    } else {
        t.tx_buffer = data;
    }
    esp_err_t err = spi_device_polling_transmit(s_dev, &t);
    if (err != ESP_OK) ESP_LOGE(TAG, "command 0x%02X failed: %s", cmd, esp_err_to_name(err));
}

static void send_frame(const uint16_t *fb, int kind)
{
    int64_t t0 = esp_timer_get_time();
    const int x0 = BOARD_LCD_X_GAP, x1 = x0 + PW - 1;
    uint8_t col[4] = {x0 >> 8, x0 & 0xFF, x1 >> 8, x1 & 0xFF};
    uint8_t row[4] = {0, 0, (PH - 1) >> 8, (PH - 1) & 0xFF};
    spi_device_acquire_bus(s_dev, portMAX_DELAY);
    panel_cmd(0x2A, col, 4);
    panel_cmd(0x2B, row, 4);
    int inflight = 0, strips = (PH + STRIP_LINES - 1) / STRIP_LINES;
    for (int i = 0, y = 0; i < strips; i++, y += STRIP_LINES) {
        int k = i & 1, n = PH - y < STRIP_LINES ? PH - y : STRIP_LINES;
        // Buffer k was used two strips ago: wait for that strip before filling it again
        if (inflight == 2) {
            spi_transaction_t *done;
            STAGE_LCD(4);
            spi_device_get_trans_result(s_dev, &done, portMAX_DELAY);
            inflight--;
        }
        STAGE_LCD(3);
        if (kind == KIND_NATIVE) memcpy(s_strip[k], fb + y * PW, (size_t)n * PW * 2);
        else build_strip(s_strip[k], fb, y, n, kind);
        spi_transaction_ext_t *t = &s_tr[k];
        memset(t, 0, sizeof(*t));
        t->base.length = n * PW * 16;
        t->base.tx_buffer = s_strip[k];
        t->base.flags = SPI_TRANS_MODE_QIO | (i < strips - 1 ? SPI_TRANS_CS_KEEP_ACTIVE : 0);
        if (i == 0) {  // the header opens the pixel write
            t->base.cmd = 0x32;
            t->base.addr = 0x2C << 8;
        } else {       // the rest continue it: no header
            t->base.flags |= SPI_TRANS_VARIABLE_CMD | SPI_TRANS_VARIABLE_ADDR;
            t->command_bits = 0;
            t->address_bits = 0;
        }
        STAGE_LCD(5);
        spi_device_queue_trans(s_dev, &t->base, portMAX_DELAY);
        inflight++;
    }
    STAGE_LCD(6);
    while (inflight--) {
        spi_transaction_t *done;
        spi_device_get_trans_result(s_dev, &done, portMAX_DELAY);
    }
    spi_device_release_bus(s_dev);
    s_send_us = (int)(esp_timer_get_time() - t0);
}

// Panel sleep: display off + sleep in; waking takes sleep out, the 120 ms the panel needs, display on
static void set_panel_sleep(bool sleep)
{
    if (sleep) {
        panel_cmd(0x28, NULL, 0);
        panel_cmd(0x10, NULL, 0);
    } else {
        panel_cmd(0x11, NULL, 0);
        vTaskDelay(pdMS_TO_TICKS(120));
        panel_cmd(0x29, NULL, 0);
    }
}

static void sender_task(void *arg)
{
    uint8_t applied = 0;
    bool asleep = false;
    for (;;) {
        uint32_t bits = 0;
        STAGE_LCD(1);
        xTaskNotifyWait(0, UINT32_MAX, &bits, portMAX_DELAY);
        if (asleep && !s_want_sleep) {  // awake before drawing
            STAGE_LCD(2);
            set_panel_sleep(false);
            asleep = false;
        }
        if (bits & BIT_FRAME) {
            send_frame(s_sending, s_send_kind);
            xSemaphoreGive(s_idle);
        }
        // After the frame: the panel lights up only once the first frame is on it
        uint8_t v = s_brightness;
        STAGE_LCD(7);
        if ((bits & BIT_BRIGHTNESS) && v != applied) {
            panel_cmd(0x51, &v, 1);
            applied = v;
        }
        if (!asleep && s_want_sleep) {
            STAGE_LCD(8);
            set_panel_sleep(true);
            asleep = true;
        }
    }
}

void lcd_init(int buffers)
{
    build_tables();

    spi_bus_config_t bus = {
        .sclk_io_num = BOARD_LCD_SCLK,
        .data0_io_num = BOARD_LCD_D0,
        .data1_io_num = BOARD_LCD_D1,
        .data2_io_num = BOARD_LCD_D2,
        .data3_io_num = BOARD_LCD_D3,
        .max_transfer_sz = STRIP_BYTES,
        .flags = SPICOMMON_BUSFLAG_QUAD,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &bus, SPI_DMA_CH_AUTO));
    spi_device_interface_config_t dev = {
        .command_bits = 8,
        .address_bits = 24,
        .mode = 0,
        .clock_speed_hz = LCD_PCLK,
        .spics_io_num = BOARD_LCD_CS,
        .queue_size = 2,
        .flags = SPI_DEVICE_HALFDUPLEX,
    };
    ESP_ERROR_CHECK(spi_bus_add_device(LCD_HOST, &dev, &s_dev));

    gpio_config_t rst = {.pin_bit_mask = 1ULL << BOARD_LCD_RST, .mode = GPIO_MODE_OUTPUT};
    gpio_config(&rst);
    gpio_set_level(BOARD_LCD_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(BOARD_LCD_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(150));
    for (size_t i = 0; i < sizeof(s_init) / sizeof(s_init[0]); i++) {
        const init_cmd_t *c = &s_init[i];
        panel_cmd(c->cmd, c->data, c->len);
        if (c->delay_ms) vTaskDelay(pdMS_TO_TICKS(c->delay_ms));
    }

    for (int i = 0; i < (buffers > 1 ? 2 : 1); i++) {
        s_buf[i] = heap_caps_calloc(1, FB_BYTES, MALLOC_CAP_SPIRAM);
        s_native[i] = heap_caps_calloc(1, PW * PH * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
        s_wide[i] = heap_caps_calloc(1, LCD_WW * LCD_WH * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
    }
    if (!s_native[1]) s_native[1] = s_native[0];
    if (!s_wide[1]) s_wide[1] = s_wide[0];
    for (int i = 0; i < 2; i++) s_strip[i] = heap_caps_malloc(STRIP_BYTES, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    assert(s_buf[0] && s_native[0] && s_wide[0] && s_strip[0] && s_strip[1]);

    s_idle = xSemaphoreCreateBinary();
    xSemaphoreGive(s_idle);
    // The main loop runs on core 0; sending on core 1 overlaps it with drawing the next frame
    xTaskCreatePinnedToCore(sender_task, "lcd_send", 3072, NULL, configMAX_PRIORITIES - 2, &s_sender, 1);
    ESP_LOGI(TAG, "CO5300 %dx%d ready, QSPI %d MHz, %dx%d canvas %s buffered in PSRAM", PW, PH,
             LCD_PCLK / 1000000, LCD_W, LCD_H, s_buf[1] ? "double" : "single");
}

void lcd_set_brightness(int percent)
{
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    s_brightness = (uint8_t)(percent * 255 / 100);
    xTaskNotify(s_sender, BIT_BRIGHTNESS, eSetBits);
}

void lcd_sleep(bool sleep)
{
    s_want_sleep = sleep;
    xTaskNotify(s_sender, BIT_POWER, eSetBits);
}

void lcd_set_landscape(bool landscape)
{
    // Each frame records its own kind when flushed, so nothing in flight changes here
    s_land = landscape;
    if (g_lcd_w != PW) {
        g_lcd_w = landscape ? LAND_W : LCD_W;
        g_lcd_h = landscape ? LAND_H : LCD_H;
    }
}

uint16_t *lcd_fb(void)
{
    g_lcd_w = s_land ? LAND_W : LCD_W;
    g_lcd_h = s_land ? LAND_H : LCD_H;
    return s_buf[s_idx];
}

uint16_t *lcd_wide_fb(void)
{
    g_lcd_w = LCD_WW;
    g_lcd_h = LCD_WH;
    return s_wide[s_widx];
}

void lcd_flush_wide(void)
{
    xSemaphoreTake(s_idle, portMAX_DELAY);
    s_sending = s_last = s_wide[s_widx];
    s_send_kind = s_last_kind = KIND_WIDE;
    xTaskNotify(s_sender, BIT_FRAME, eSetBits);
    if (s_wide[1] != s_wide[0]) {
        s_widx ^= 1;
    } else {
        xSemaphoreTake(s_idle, portMAX_DELAY);
        xSemaphoreGive(s_idle);
    }
}

uint16_t *lcd_native_fb(void)
{
    g_lcd_w = PW;
    g_lcd_h = PH;
    return s_native[s_nidx];
}

const uint16_t *lcd_last_frame(int *w, int *h)
{
    static const int ws[] = {LCD_W, LAND_W, LCD_WW, PW}, hs[] = {LCD_H, LAND_H, LCD_WH, PH};
    if (w) *w = ws[s_last_kind];
    if (h) *h = hs[s_last_kind];
    return s_last;
}

void lcd_flush_native(void)
{
    xSemaphoreTake(s_idle, portMAX_DELAY);
    s_sending = s_last = s_native[s_nidx];
    s_send_kind = s_last_kind = KIND_NATIVE;
    xTaskNotify(s_sender, BIT_FRAME, eSetBits);
    if (s_native[1] != s_native[0]) {
        s_nidx ^= 1;
    } else {
        xSemaphoreTake(s_idle, portMAX_DELAY);
        xSemaphoreGive(s_idle);
    }
}

void lcd_flush(void)
{
    xSemaphoreTake(s_idle, portMAX_DELAY);  // the previous frame has been sent
    s_sending = s_last = s_buf[s_idx];
    s_send_kind = s_last_kind = s_land ? KIND_CANVAS_LAND : KIND_CANVAS;
    xTaskNotify(s_sender, BIT_FRAME, eSetBits);
    if (s_buf[1]) {
        s_idx ^= 1;
    } else {
        // Single buffer: wait until it has been sent before the caller draws again
        xSemaphoreTake(s_idle, portMAX_DELAY);
        xSemaphoreGive(s_idle);
    }
}

int lcd_send_us(void)
{
    return s_send_us;
}

bool lcd_panel_to_canvas(int px, int py, int *cx, int *cy)
{
    bool land = s_land;
    int num = land ? 5 : 3, den = land ? 4 : 2;     // the enlargement used by build_tables()
    int cw = land ? LAND_W : LCD_W, ch = land ? LAND_H : LCD_H;
    int ox = (PW - cw * num / den) / 2, oy = (PH - ch * num / den) / 2;
    int x = px - ox, y = py - oy;
    // floor division, so points left of / above the canvas stay negative
    *cx = (x >= 0 ? x * den : x * den - (num - 1)) / num;
    *cy = (y >= 0 ? y * den : y * den - (num - 1)) / num;
    return *cx >= 0 && *cy >= 0 && *cx < cw && *cy < ch;
}
