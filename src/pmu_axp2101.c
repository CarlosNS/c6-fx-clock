#include "pmu_axp2101.h"
#include <stdint.h>
#include "esp_log.h"
#include "i2c_bus.h"

#define AXP_ADDR 0x34
#define AXP_CHIP_ID 0x4A

// Registers (AXP2101 datasheet)
#define REG_STATUS1 0x00     // bit5 VBUS good, bit3 battery present
#define REG_STATUS2 0x01     // bits 6:5 battery current direction, 2:0 charger state
#define REG_CHIP_ID 0x03
#define REG_ADC_EN 0x30      // bit0 battery voltage measurement
#define REG_VBAT_H 0x34      // 14-bit battery voltage in mV
#define REG_DCDC_EN 0x80     // bits 0..4: DC1..DC5
#define REG_DC1_V 0x82       // 1500 mV + 100 mV per step
#define REG_LDO_EN0 0x90     // ALDO1..4, BLDO1..2, CPUSLDO, DLDO1
#define REG_LDO_EN1 0x91     // bit0 DLDO2
#define REG_ALDO1_V 0x92     // ALDO1..4 then BLDO1..2: 500 mV + 100 mV per step
#define REG_BAT_PCT 0xA4     // fuel gauge, 0..100 %

static const char *TAG = "axp2101";

static int reg(uint8_t r)
{
    uint8_t v;
    return i2c_reg_read(AXP_ADDR, r, &v, 1) ? v : -1;
}

bool pmu_log_status(void)
{
    int id = reg(REG_CHIP_ID);
    if (id < 0) {
        ESP_LOGE(TAG, "not answering at 0x%02X", AXP_ADDR);
        return false;
    }
    if (id != AXP_CHIP_ID) ESP_LOGW(TAG, "unexpected chip id 0x%02X", id);

    int s1 = reg(REG_STATUS1), s2 = reg(REG_STATUS2);
    static const char *const charger[] = {"trickle", "pre-charge", "constant current", "constant voltage",
                                          "done", "not charging", "?", "?"};
    ESP_LOGI(TAG, "USB %s, battery %s, charger: %s", (s1 >> 5) & 1 ? "yes" : "no",
             (s1 >> 3) & 1 ? "present" : "absent", charger[s2 & 7]);
    if ((s1 >> 3) & 1) {
        uint8_t v[2];
        if ((reg(REG_ADC_EN) & 1) && i2c_reg_read(AXP_ADDR, REG_VBAT_H, v, 2))
            ESP_LOGI(TAG, "battery %d mV, %d %%", ((v[0] & 0x3F) << 8) | v[1], reg(REG_BAT_PCT));
        else
            ESP_LOGI(TAG, "battery %d %% (voltage ADC off)", reg(REG_BAT_PCT));
    }

    int dc = reg(REG_DCDC_EN), ldo = reg(REG_LDO_EN0), ldo1 = reg(REG_LDO_EN1);
    ESP_LOGI(TAG, "DC1 %s %d mV, DC2..5 enabled mask 0x%X", dc & 1 ? "on" : "off",
             1500 + (reg(REG_DC1_V) & 0x1F) * 100, (dc >> 1) & 0xF);
    static const char *const ldos[] = {"ALDO1", "ALDO2", "ALDO3", "ALDO4", "BLDO1", "BLDO2"};
    for (int i = 0; i < 6; i++)
        ESP_LOGI(TAG, "%s %s %d mV", ldos[i], (ldo >> i) & 1 ? "on " : "off",
                 500 + (reg(REG_ALDO1_V + i) & 0x1F) * 100);
    ESP_LOGI(TAG, "CPUSLDO %s, DLDO1 %s, DLDO2 %s", (ldo >> 6) & 1 ? "on" : "off", (ldo >> 7) & 1 ? "on" : "off",
             ldo1 & 1 ? "on" : "off");
    return true;
}
