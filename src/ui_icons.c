// App icons: 16 x 16 pixel art, drawn 3x in the launcher (ui_icon). Palette letters in ui.c:
// k night outline, o gold, O light gold, r ember, m rose, c cream, g grey, v green, l line,
// b deep blue, a water, x coral; '.' transparent.
#include "ui_icons.h"

const char *const icon_faces[16] = {
    "....kkkkkkkk....", "...kooooooook...", "..kokkkkkkkkok..", "..kokbbbbbbkok..",
    "..kokbbcbbbkok..", "..kokbbcbbbkok..", "..kokbbcbbbkok..", "..kokbbccccok...",
    "..kokbbbbbbkok..", "..kokbbbbbbkok..", "..kokbbbbbbkok..", "..kokkkkkkkkok..",
    "...kooooooook...", "....kkooookk....", ".....kOOOOk.....", "......kkkk......",
};

const char *const icon_settings[16] = {
    "......kkkk......", "......kook......", "..kk..kook..kk..", "..kokkkookkkok..",
    "...kooooooook...", "..kooookkooook..", "kkkookkcckkookkk", "koooookcckooooko",
    "koooookcckooooko", "kkkookkcckkookkk", "..kooookkooook..", "...kooooooook...",
    "..kokkkookkkok..", "..kk..kook..kk..", "......kook......", "......kkkk......",
};

const char *const icon_alarms[16] = {
    "..kk........kk..", ".kook......kook.", ".kok.kkkkkk.kok.", "..k.kOOOOOOk.k..",
    "...kOookkooOk...", "..kOookcckooOk..", "..kOookcckooOk..", "..kOookcccooOk..",
    "..kOookkkcooOk..", "..kOooooooooOk..", "...kOooooooOk...", "....kOOOOOOk....",
    "...kk.kkkk.kk...", "..kk........kk..", "................", "................",
};

const char *const icon_steps[16] = {
    "................", "....kkk.........", "...kvvvk........", "...kvvvk........",
    "...kvvvk...kkk..", "....kvk...kvvvk.", "....kkk...kvvvk.", "...kvvk...kvvvk.",
    "...kvvk....kvk..", "....kk.....kkk..", "...........kvvk.", "...........kvvk.",
    "............kk..", "................", "..kkkkkkkkkkkk..", "..kggggggggggk..",
};

const char *const icon_stopwatch[16] = {
    "......kkkk......", ".......kk.......", ".....kkkkkk.....", "...kkcccccckk...",
    "..kccccccccccck.", "..kcccccocccck..", ".kccccccoccccck.", ".kccccccoccccck.",
    ".kccccccocccck..", ".kcccccccccccck.", ".kcccccccccccck.", "..kccccccccccck.",
    "..kccccccccccck.", "...kkcccccckk...", ".....kkkkkk.....", "................",
};

const char *const icon_music[16] = {
    "................", "........kkkkkk..", "......kkooooOk..", ".....kooookkok..",
    ".....kokkk..kok.", ".....kok....kok.", ".....kok....kok.", ".....kok....kok.",
    ".....kok....kok.", "..kkkkok.kkkkok.", ".kooooOk.kooooOk", ".koooook.kooook.",
    ".koooook.kooook.", "..kkkkk...kkkkk.", "................", "................",
};

const char *const icon_torch[16] = {
    "..kkkkkkkkkkkk..", "..kOOOOOOOOOOk..", "..kooooooooook..", "...kkkkkkkkkk...",
    "...kggggggggk...", "....kgggggk.....", "....kgggggk.....", "....kgkkkgk.....",
    "....kgkxkgk.....", "....kgkkkgk.....", "....kgggggk.....", "....kgggggk.....",
    "....kgggggk.....", "....kgggggk.....", "....kkkkkkk.....", "................",
};

// The guide's reference logo: a gold folder with a four-pointed cream star
const char *const icon_files[16] = {
    "................", ".kkkkkk.........", "kooooook........", "koooooookkkkkkk.",
    "kOOOOOOOOOOOOOOk", "kooooooocooooook", "kooooooocooooook", "koooooocccoooook",
    "kooocccccccccook", "koooooocccoooook", "kooooooocooooook", "kooooooocooooook",
    "kooooooooooooook", "kkkkkkkkkkkkkkkk", "................", "................",
};
