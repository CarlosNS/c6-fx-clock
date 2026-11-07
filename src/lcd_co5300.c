// lcd.h for the Waveshare ESP32-S3-Touch-AMOLED-2.06: CO5300 AMOLED, 410x502, over QSPI.
//
// Frames are drawn into two framebuffers in PSRAM. lcd_flush hands the finished one to a task on
// the second core, which sends it while the caller draws the next. The SPI DMA can't read PSRAM
// directly, so the task copies the frame in strips into two internal buffers, filling one while the
// other is on the wire. Landscape frames (LAND_W x LAND_H) are rotated during that copy, because the
// CO5300 can't swap rows and columns itself.
#include "lcd.h"
#include <assert.h>
#include <string.h>
#include "board.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

// The CO5300 speaks the SH8601 QSPI protocol: every transfer starts with a 32-bit header
// (opcode, 0x00, command, 0x00). Opcode 0x02 writes a command with 1-line parameters, opcode
// 0x32 writes pixel data on 4 lines.
#define LCD_HOST SPI2_HOST
#define LCD_PCLK (80 * 1000 * 1000)
#define CMD(c) ((0x02u << 24) | ((uint32_t)(c) << 8))
#define PIXELS(c) ((0x32u << 24) | ((uint32_t)(c) << 8))
#define FB_BYTES (LCD_W * LCD_H * sizeof(uint16_t))
// Even (the CO5300 wants even start rows and heights) and 38 * 410 * 2 bytes fits in one 32 KB
// SPI transfer
#define STRIP_LINES 38
#define STRIP_BYTES (STRIP_LINES * LCD_W * sizeof(uint16_t))

static const char *TAG = "co5300";
static esp_lcd_panel_io_handle_t s_io;
static SemaphoreHandle_t s_bus;      // the panel IO is not thread safe: frames vs. brightness
static SemaphoreHandle_t s_idle;     // given when the sender has finished the last frame
static TaskHandle_t s_sender;
static uint16_t *s_buf[2], *s_strip[2];
static int s_idx;
static uint16_t *s_last;
static const uint16_t *s_sending;
static bool s_send_landscape;
static volatile int s_send_us;
int g_lcd_w = LCD_W, g_lcd_h = LCD_H;

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

static void set_window(int y, int h)
{
    int x0 = BOARD_LCD_X_GAP, x1 = x0 + LCD_W - 1, y1 = y + h - 1;
    uint8_t col[4] = {x0 >> 8, x0 & 0xFF, x1 >> 8, x1 & 0xFF};
    uint8_t row[4] = {y >> 8, y & 0xFF, y1 >> 8, y1 & 0xFF};
    // tx_param first waits for the strip still on the wire, so the next strip can be copied meanwhile
    esp_lcd_panel_io_tx_param(s_io, CMD(0x2A), col, 4);
    esp_lcd_panel_io_tx_param(s_io, CMD(0x2B), row, 4);
}

// Portrait rows y0..y0+n-1 of the frame into a strip. In landscape the frame is LAND_W x LAND_H
// and is turned a quarter: its top edge ends up on the panel's right side (the USB side).
static void copy_strip(uint16_t *dst, const uint16_t *fb, int y0, int n, bool landscape)
{
    if (!landscape) {
        memcpy(dst, fb + y0 * LCD_W, (size_t)n * LCD_W * 2);
        return;
    }
    // panel (px, py) = frame (lx = py, ly = LAND_H - 1 - px); read the frame row by row
    for (int ly = 0; ly < LAND_H; ly++) {
        const uint16_t *src = fb + ly * LAND_W + y0;
        uint16_t *d = dst + (LAND_H - 1 - ly);
        for (int j = 0; j < n; j++) d[j * LCD_W] = src[j];
    }
}

static void send_frame(const uint16_t *fb, bool landscape)
{
    int64_t t0 = esp_timer_get_time();
    xSemaphoreTake(s_bus, portMAX_DELAY);
    for (int y = 0, k = 0; y < LCD_H; y += STRIP_LINES, k ^= 1) {
        int n = LCD_H - y < STRIP_LINES ? LCD_H - y : STRIP_LINES;
        // The strip sent two rounds ago has finished: set_window of the last round waited for it
        copy_strip(s_strip[k], fb, y, n, landscape);
        set_window(y, n);
        esp_lcd_panel_io_tx_color(s_io, PIXELS(0x2C), s_strip[k], (size_t)n * LCD_W * 2);
    }
    // A zero-length parameter write waits for the last strip without sending anything
    esp_lcd_panel_io_tx_param(s_io, -1, NULL, 0);
    xSemaphoreGive(s_bus);
    s_send_us = (int)(esp_timer_get_time() - t0);
}

static void sender_task(void *arg)
{
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        send_frame(s_sending, s_send_landscape);
        xSemaphoreGive(s_idle);
    }
}

void lcd_init(int buffers)
{
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

    esp_lcd_panel_io_spi_config_t io_cfg = {
        .cs_gpio_num = BOARD_LCD_CS,
        .dc_gpio_num = -1,
        .spi_mode = 0,
        .pclk_hz = LCD_PCLK,
        .trans_queue_depth = 4,
        .lcd_cmd_bits = 32,
        .lcd_param_bits = 8,
        .flags = {.quad_mode = 1},
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_cfg, &s_io));

    gpio_config_t rst = {.pin_bit_mask = 1ULL << BOARD_LCD_RST, .mode = GPIO_MODE_OUTPUT};
    gpio_config(&rst);
    gpio_set_level(BOARD_LCD_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(BOARD_LCD_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(150));
    for (size_t i = 0; i < sizeof(s_init) / sizeof(s_init[0]); i++) {
        const init_cmd_t *c = &s_init[i];
        ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(s_io, CMD(c->cmd), c->len ? c->data : NULL, c->len));
        if (c->delay_ms) vTaskDelay(pdMS_TO_TICKS(c->delay_ms));
    }

    for (int i = 0; i < (buffers > 1 ? 2 : 1); i++) {
        s_buf[i] = heap_caps_calloc(1, FB_BYTES, MALLOC_CAP_SPIRAM);
        s_strip[i] = heap_caps_malloc(STRIP_BYTES, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    }
    if (!s_strip[1]) s_strip[1] = heap_caps_malloc(STRIP_BYTES, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    assert(s_buf[0] && s_strip[0] && s_strip[1]);

    s_bus = xSemaphoreCreateMutex();
    s_idle = xSemaphoreCreateBinary();
    xSemaphoreGive(s_idle);
    // The main loop runs on core 0; sending on core 1 overlaps it with drawing the next frame
    xTaskCreatePinnedToCore(sender_task, "lcd_send", 3072, NULL, configMAX_PRIORITIES - 2, &s_sender, 1);
    ESP_LOGI(TAG, "CO5300 %dx%d ready, QSPI %d MHz, %s buffering in PSRAM", LCD_W, LCD_H,
             LCD_PCLK / 1000000, s_buf[1] ? "double" : "single");
}

void lcd_set_brightness(int percent)
{
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    uint8_t v = (uint8_t)(percent * 255 / 100);
    xSemaphoreTake(s_bus, portMAX_DELAY);
    esp_lcd_panel_io_tx_param(s_io, CMD(0x51), &v, 1);
    xSemaphoreGive(s_bus);
}

void lcd_set_landscape(bool landscape)
{
    if (landscape == (g_lcd_w == LAND_W)) return;
    xSemaphoreTake(s_idle, portMAX_DELAY);  // the frame in flight keeps the old geometry
    g_lcd_w = landscape ? LAND_W : LCD_W;
    g_lcd_h = landscape ? LAND_H : LCD_H;
    xSemaphoreGive(s_idle);
}

uint16_t *lcd_fb(void)
{
    return s_buf[s_idx];
}

const uint16_t *lcd_last_frame(void)
{
    return s_last;
}

void lcd_flush(void)
{
    xSemaphoreTake(s_idle, portMAX_DELAY);  // the previous frame has been sent
    s_sending = s_last = s_buf[s_idx];
    s_send_landscape = g_lcd_w == LAND_W;
    xTaskNotifyGive(s_sender);
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
