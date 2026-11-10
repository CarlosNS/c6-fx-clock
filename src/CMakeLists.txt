set(srcs "main.c" "gfx.c" "fonts.c" "uifont.c" "sprites.c" "clock.c" "web.c" "net.c" "control.c" "settings.c" "ota.c" "bootmode.c" "console.c" "update.c" "ble.c" "weather.c"
         "fx_common.c" "fx_plasma.c" "fx_cube.c" "fx_tunnel.c" "fx_lava.c" "fx_life.c"
         "fx_hud.c" "fx_quest.c" "fx_rpg.c"
         "fx_starry.c" "fx_wifimon.c" "fx_earthrise.c" "fx_sakura.c"
         "fx_synth.c" "fx_sand.c" "fx_gears.c" "fx_rain.c" "fx_blackhole.c" "fx_goal.c" "fx_weather.c" "fx_classic.c" "fx_feria.c" "fx_onix.c")

# The watch system: Marea Solsticial toolkit, gestures, shell and apps
list(APPEND srcs "ui.c" "ui_fonts.c" "ui_icons.c" "input.c" "sys.c" "steps.c" "alarm.c" "audio.c" "phone.c"
                 "app_faces.c" "app_settings.c" "app_stopwatch.c" "app_torch.c" "app_alarms.c" "app_steps.c"
                 "app_music.c" "app_files.c" "usbdisk.c" "batlog.c" "bleota.c" "intro.c")
list(APPEND srcs "board.c" "lcd_co5300.c" "i2c_bus.c" "pmu_axp2101.c" "rtc_pcf85063.c" "touch_ft3168.c" "imu_qmi8658.c")

# web.pack: the page's pictures and fonts (src/web/), packed by tools/web_pack.py before each build
idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "."
    EMBED_TXTFILES "index.html" "transport.js"
    EMBED_FILES "web.pack")

# USB disk mode: notice when the computer ejects the drive (usbdisk.c wraps esp_tinyusb's handler)
target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=tud_msc_start_stop_cb")
