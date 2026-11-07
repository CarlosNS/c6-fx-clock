#include "i2c_bus.h"
#include "board.h"
#include "esp_log.h"

#define I2C_HZ 400000
#define I2C_TIMEOUT_MS 50
#define MAX_DEVS 8

static const char *TAG = "i2c";
static i2c_master_bus_handle_t s_bus;
static struct {
    uint8_t addr;
    i2c_master_dev_handle_t dev;
} s_devs[MAX_DEVS];

// Chips this board is known to carry, by their usual 7-bit address
static const struct {
    uint8_t addr;
    const char *name;
} s_known[] = {
    {0x18, "ES8311 (audio out)"},
    {0x34, "AXP2101 (power)"},
    {0x38, "FT3168 (touch)"},
    {0x40, "ES7210 (microphones)"},
    {0x51, "PCF85063 (RTC)"},
    {0x6A, "QMI8658 (IMU)"},
    {0x6B, "QMI8658 (IMU)"},
};

void i2c_bus_init(void)
{
    i2c_master_bus_config_t cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = BOARD_I2C_SDA,
        .scl_io_num = BOARD_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&cfg, &s_bus));
}

i2c_master_bus_handle_t i2c_bus(void)
{
    return s_bus;
}

void i2c_bus_scan(void)
{
    int found = 0;
    for (int a = 0x08; a < 0x78; a++) {
        if (i2c_master_probe(s_bus, a, I2C_TIMEOUT_MS) != ESP_OK) continue;
        const char *name = "unknown";
        for (size_t i = 0; i < sizeof(s_known) / sizeof(s_known[0]); i++)
            if (s_known[i].addr == a) name = s_known[i].name;
        ESP_LOGI(TAG, "  0x%02X  %s", a, name);
        found++;
    }
    ESP_LOGI(TAG, "%d devices on the bus", found);
}

static i2c_master_dev_handle_t dev_for(uint8_t addr)
{
    for (int i = 0; i < MAX_DEVS; i++) {
        if (s_devs[i].dev && s_devs[i].addr == addr) return s_devs[i].dev;
        if (!s_devs[i].dev) {
            i2c_device_config_t cfg = {
                .dev_addr_length = I2C_ADDR_BIT_LEN_7,
                .device_address = addr,
                .scl_speed_hz = I2C_HZ,
            };
            if (i2c_master_bus_add_device(s_bus, &cfg, &s_devs[i].dev) != ESP_OK) return NULL;
            s_devs[i].addr = addr;
            return s_devs[i].dev;
        }
    }
    return NULL;
}

bool i2c_reg_read(uint8_t addr, uint8_t reg, uint8_t *data, size_t len)
{
    i2c_master_dev_handle_t dev = dev_for(addr);
    return dev && i2c_master_transmit_receive(dev, &reg, 1, data, len, I2C_TIMEOUT_MS) == ESP_OK;
}

bool i2c_reg_write(uint8_t addr, uint8_t reg, uint8_t value)
{
    i2c_master_dev_handle_t dev = dev_for(addr);
    uint8_t buf[2] = {reg, value};
    return dev && i2c_master_transmit(dev, buf, 2, I2C_TIMEOUT_MS) == ESP_OK;
}
