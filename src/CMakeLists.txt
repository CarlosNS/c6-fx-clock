idf_component_register(
    SRCS "main.c" "lcd.c" "gfx.c" "fonts.c" "uifont.c" "sprites.c" "clock.c" "led.c" "web.c" "net.c" "control.c" "settings.c" "ota.c" "ble_spike.c" "weather.c"
         "fx_common.c" "fx_plasma.c" "fx_cube.c" "fx_tunnel.c" "fx_lava.c" "fx_life.c"
         "fx_hud.c" "fx_quest.c" "fx_rpg.c"
         "fx_starry.c" "fx_wifimon.c" "fx_scream.c" "fx_earthrise.c" "fx_sakura.c"
         "fx_synth.c" "fx_sand.c" "fx_gears.c" "fx_aquarium.c" "fx_rain.c" "fx_blackhole.c" "fx_goal.c" "fx_tale.c" "fx_weather.c" "fx_classic.c"
    INCLUDE_DIRS "."
    EMBED_TXTFILES "index.html")
