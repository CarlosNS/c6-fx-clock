if(IDF_TARGET STREQUAL "esp32s3")
    # Waveshare ESP32-S3-Touch-AMOLED-2.06: bring-up firmware for now (phases 1-2 of docs/PLAN-s3.md);
    # the clock joins it in phase 3
    set(srcs "main_s3.c" "lcd_co5300.c" "i2c_bus.c" "pmu_axp2101.c" "gfx.c" "fonts.c" "uifont.c")
else()
    set(srcs "main.c" "lcd.c" "gfx.c" "fonts.c" "uifont.c" "sprites.c" "clock.c" "led.c" "web.c" "net.c" "control.c" "settings.c" "ota.c" "bootmode.c" "console.c" "update.c" "ble.c" "weather.c"
         "fx_common.c" "fx_plasma.c" "fx_cube.c" "fx_tunnel.c" "fx_lava.c" "fx_life.c"
         "fx_hud.c" "fx_quest.c" "fx_rpg.c"
         "fx_starry.c" "fx_wifimon.c" "fx_scream.c" "fx_earthrise.c" "fx_sakura.c"
         "fx_synth.c" "fx_sand.c" "fx_gears.c" "fx_aquarium.c" "fx_rain.c" "fx_blackhole.c" "fx_goal.c" "fx_tale.c" "fx_weather.c" "fx_classic.c" "fx_feria.c")
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "."
    EMBED_TXTFILES "index.html" "transport.js")
