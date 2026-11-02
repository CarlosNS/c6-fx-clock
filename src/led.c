#include "led.h"
#include "board.h"
#include "driver/rmt_tx.h"

#define PIN_LED    BOARD_PIN_LED
#define RMT_RES_HZ (10 * 1000 * 1000)  // 0.1 us per tick

static rmt_channel_handle_t s_chan;
static rmt_encoder_handle_t s_enc;
static uint8_t s_grb[3];

void led_init(void)
{
    rmt_tx_channel_config_t cfg = {
        .gpio_num = PIN_LED,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = RMT_RES_HZ,
        .mem_block_symbols = 48,
        .trans_queue_depth = 2,
    };
    ESP_ERROR_CHECK(rmt_new_tx_channel(&cfg, &s_chan));
    // WS2812 bit timing: 0 = 0.3us high + 0.9us low, 1 = 0.9us high + 0.3us low
    rmt_bytes_encoder_config_t enc = {
        .bit0 = {.level0 = 1, .duration0 = 3, .level1 = 0, .duration1 = 9},
        .bit1 = {.level0 = 1, .duration0 = 9, .level1 = 0, .duration1 = 3},
        .flags.msb_first = 1,
    };
    ESP_ERROR_CHECK(rmt_new_bytes_encoder(&enc, &s_enc));
    ESP_ERROR_CHECK(rmt_enable(s_chan));
    led_set(0, 0, 0);
}

void led_set(uint8_t r, uint8_t g, uint8_t b)
{
    rmt_tx_wait_all_done(s_chan, 10);  // s_grb must not change while it is being sent
    s_grb[0] = g;
    s_grb[1] = r;
    s_grb[2] = b;
    rmt_transmit_config_t tx = {.loop_count = 0};
    rmt_transmit(s_chan, s_enc, s_grb, sizeof(s_grb), &tx);
}
