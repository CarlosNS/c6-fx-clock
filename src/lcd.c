#include "lcd.h"
#include "board.h"
#include <assert.h>
#include <string.h>
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define PIN_MOSI   BOARD_LCD_MOSI
#define PIN_SCLK   BOARD_LCD_SCLK
#define PIN_CS     BOARD_LCD_CS
#define PIN_DC     BOARD_LCD_DC
#define PIN_RST    BOARD_LCD_RST
#define PIN_BL     BOARD_LCD_BL
#define PIN_SD_CS  BOARD_SD_CS
#define LCD_HOST   SPI2_HOST
#define LCD_PCLK   (40 * 1000 * 1000)
#define LCD_X_GAP  BOARD_LCD_X_GAP
#define FB_BYTES   (LCD_W * LCD_H * sizeof(uint16_t))

static const char *TAG = "lcd";
static esp_lcd_panel_handle_t s_panel;
static SemaphoreHandle_t s_done;
static uint16_t *s_buf[2];
static int s_idx;
static uint16_t *s_last;
int g_lcd_w = LCD_W, g_lcd_h = LCD_H;

static bool on_color_done(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *e, void *ctx)
{
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(s_done, &woken);
    return woken == pdTRUE;
}

static void backlight_init(void)
{
    ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = 5000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer));
    ledc_channel_config_t ch = {
        .gpio_num = PIN_BL,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ch));
}

void lcd_set_brightness(int percent)
{
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, percent * 1023 / 100);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

void lcd_init(int buffers)
{
    gpio_config_t sd = {.pin_bit_mask = 1ULL << PIN_SD_CS, .mode = GPIO_MODE_OUTPUT};
    gpio_config(&sd);
    gpio_set_level(PIN_SD_CS, 1);

    backlight_init();

    spi_bus_config_t bus = {
        .mosi_io_num = PIN_MOSI,
        .miso_io_num = -1,
        .sclk_io_num = PIN_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = FB_BYTES,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &bus, SPI_DMA_CH_AUTO));

    s_done = xSemaphoreCreateBinary();
    xSemaphoreGive(s_done);

    esp_lcd_panel_io_handle_t io;
    esp_lcd_panel_io_spi_config_t io_cfg = {
        .dc_gpio_num = PIN_DC,
        .cs_gpio_num = PIN_CS,
        .pclk_hz = LCD_PCLK,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 4,
        .on_color_trans_done = on_color_done,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_cfg, &io));

    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = PIN_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io, &panel_cfg, &s_panel));
    esp_lcd_panel_reset(s_panel);
    esp_lcd_panel_init(s_panel);
    esp_lcd_panel_invert_color(s_panel, true);
    esp_lcd_panel_set_gap(s_panel, LCD_X_GAP, 0);
    esp_lcd_panel_disp_on_off(s_panel, true);

    // Two framebuffers let the CPU draw the next frame while DMA sends the current one; with one,
    // drawing waits for the previous transfer (slower, but frees 110 KB)
    for (int i = 0; i < (buffers > 1 ? 2 : 1); i++) {
        s_buf[i] = heap_caps_malloc(FB_BYTES, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
        if (s_buf[i]) memset(s_buf[i], 0, FB_BYTES);
    }
    assert(s_buf[0]);
    ESP_LOGI(TAG, "ST7789 %dx%d ready, %s buffering, free heap %u", LCD_W, LCD_H,
             s_buf[1] ? "double" : "single", (unsigned)heap_caps_get_free_size(MALLOC_CAP_DEFAULT));
}

void lcd_set_landscape(bool landscape)
{
    if (landscape == (g_lcd_w == LAND_W)) return;
    xSemaphoreTake(s_done, portMAX_DELAY);  // no transfer may run with the old geometry
    esp_lcd_panel_swap_xy(s_panel, landscape);
    esp_lcd_panel_mirror(s_panel, landscape, false);
    // The 172 px side sits 34 px into the controller's 240 px RAM (symmetric, so mirroring keeps it)
    esp_lcd_panel_set_gap(s_panel, landscape ? 0 : LCD_X_GAP, landscape ? LCD_X_GAP : 0);
    g_lcd_w = landscape ? LAND_W : LCD_W;
    g_lcd_h = landscape ? LAND_H : LCD_H;
    xSemaphoreGive(s_done);
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
    xSemaphoreTake(s_done, portMAX_DELAY);  // previous transfer has finished
    esp_lcd_panel_draw_bitmap(s_panel, 0, 0, g_lcd_w, g_lcd_h, s_buf[s_idx]);
    s_last = s_buf[s_idx];
    if (s_buf[1]) {
        s_idx ^= 1;
    } else {
        // Single buffer: wait until DMA is done before the caller draws again
        xSemaphoreTake(s_done, portMAX_DELAY);
        xSemaphoreGive(s_done);
    }
}
