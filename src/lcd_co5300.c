#include "lcd_co5300.h"
#include "board.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_lcd_panel_io.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

// The CO5300 speaks the SH8601 QSPI protocol: every transfer starts with a 32-bit header
// (opcode, 0x00, command, 0x00). Opcode 0x02 writes a command with 1-line parameters, opcode
// 0x32 writes pixel data on 4 lines.
#define LCD_HOST SPI2_HOST
#define LCD_PCLK (40 * 1000 * 1000)
#define CMD(c) ((0x02u << 24) | ((uint32_t)(c) << 8))
#define PIXELS(c) ((0x32u << 24) | ((uint32_t)(c) << 8))

static const char *TAG = "co5300";
static esp_lcd_panel_io_handle_t s_io;
static SemaphoreHandle_t s_done;

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

static bool on_color_done(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *e, void *ctx)
{
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(s_done, &woken);
    return woken == pdTRUE;
}

void co5300_init(void)
{
    spi_bus_config_t bus = {
        .sclk_io_num = BOARD_LCD_SCLK,
        .data0_io_num = BOARD_LCD_D0,
        .data1_io_num = BOARD_LCD_D1,
        .data2_io_num = BOARD_LCD_D2,
        .data3_io_num = BOARD_LCD_D3,
        .max_transfer_sz = CO5300_MAX_BLOCK_BYTES,
        .flags = SPICOMMON_BUSFLAG_QUAD,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &bus, SPI_DMA_CH_AUTO));

    s_done = xSemaphoreCreateBinary();
    esp_lcd_panel_io_spi_config_t io_cfg = {
        .cs_gpio_num = BOARD_LCD_CS,
        .dc_gpio_num = -1,
        .spi_mode = 0,
        .pclk_hz = LCD_PCLK,
        .trans_queue_depth = 10,
        .on_color_trans_done = on_color_done,
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
    ESP_LOGI(TAG, "CO5300 %dx%d ready (QSPI %d MHz)", BOARD_LCD_W, BOARD_LCD_H, LCD_PCLK / 1000000);
}

void co5300_set_brightness(int percent)
{
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    uint8_t v = (uint8_t)(percent * 255 / 100);
    esp_lcd_panel_io_tx_param(s_io, CMD(0x51), &v, 1);
}

void co5300_draw(int x, int y, int w, int h, const uint16_t *pixels)
{
    int x0 = x + BOARD_LCD_X_GAP, x1 = x0 + w - 1, y1 = y + h - 1;
    uint8_t col[4] = {x0 >> 8, x0 & 0xFF, x1 >> 8, x1 & 0xFF};
    uint8_t row[4] = {y >> 8, y & 0xFF, y1 >> 8, y1 & 0xFF};
    esp_lcd_panel_io_tx_param(s_io, CMD(0x2A), col, 4);
    esp_lcd_panel_io_tx_param(s_io, CMD(0x2B), row, 4);
    esp_lcd_panel_io_tx_color(s_io, PIXELS(0x2C), pixels, (size_t)w * h * 2);
    xSemaphoreTake(s_done, portMAX_DELAY);
}
