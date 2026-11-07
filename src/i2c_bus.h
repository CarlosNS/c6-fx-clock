#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "driver/i2c_master.h"

// The board's shared I2C bus (touch, PMIC, RTC, IMU, audio codecs)
void i2c_bus_init(void);
i2c_master_bus_handle_t i2c_bus(void);
// Probes every address and logs the devices that answer, with the chip expected there
void i2c_bus_scan(void);
// Register access for small drivers; each call adds the device on first use. false on error.
bool i2c_reg_read(uint8_t addr, uint8_t reg, uint8_t *data, size_t len);
bool i2c_reg_write(uint8_t addr, uint8_t reg, uint8_t value);
