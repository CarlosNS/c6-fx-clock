set(srcs "main.c" "gfx.c" "fonts.c" "uifont.c" "sprites.c" "clock.c" "led.c" "web.c" "net.c" "control.c" "settings.c" "ota.c" "bootmode.c" "console.c" "update.c" "ble.c" "weather.c"
         "fx_common.c" "fx_plasma.c" "fx_cube.c" "fx_tunnel.c" "fx_lava.c" "fx_life.c"
         "fx_hud.c" "fx_quest.c" "fx_rpg.c"
         "fx_starry.c" "fx_wifimon.c" "fx_earthrise.c" "fx_sakura.c"
         "fx_synth.c" "fx_sand.c" "fx_gears.c" "fx_rain.c" "fx_blackhole.c" "fx_goal.c" "fx_weather.c" "fx_classic.c" "fx_feria.c")

# Board-specific files (board.h picks the matching header)
if(IDF_TARGET STREQUAL "esp32s3")
    # Waveshare ESP32-S3-Touch-AMOLED-2.06 (docs/PLAN-s3.md)
    list(APPEND srcs "board_s3amoled.c" "lcd_co5300.c" "i2c_bus.c" "pmu_axp2101.c")
else()
    # Waveshare ESP32-C6-LCD-1.47
    list(APPEND srcs "board_c6lcd.c" "lcd.c")
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "."
    EMBED_TXTFILES "index.html" "transport.js")
