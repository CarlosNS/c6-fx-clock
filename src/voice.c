// Voice commands: record while BOOT is held, stream to the phone, show what it understood and ask
// before doing it. The protocol is in voice.h.
//
// The audio is 16 kHz mono from both microphones, packed as IMA ADPCM (4 bits a sample: 8 KB/s, about
// 11 KB/s once in base64), 100 ms per message. Each message carries the encoder state it starts from,
// so a lost one only loses its own 100 ms.
#include "voice.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "app.h"
#include "apps.h"
#include "audio.h"
#include "ble.h"
#include "control.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/base64.h"
#include "sys.h"

#define MAX_MS 8000        // one command; the recording stops by itself after this
#define THINK_MS 15000     // waiting for the phone's answer
#define DONE_MS 1500       // "done" stays this long
#define FRAME 320          // 20 ms
#define CHUNK_FRAMES 5     // 100 ms per message

static const char *TAG = "voice";

enum { V_IDLE, V_LISTEN, V_THINK, V_ASK, V_MSG, V_DONE };
static volatile int s_state = V_IDLE;
static volatile bool s_rec, s_stop;
static volatile int s_level;  // 0..1000, the bars while listening
static uint32_t s_t;          // when the current state began
static char s_cmd[96], s_heard[96], s_desc[96], s_title[40], s_body[120];
static const app_t app_voice;

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

static void set_state(int st)
{
    s_t = now_ms();
    s_state = st;
    sys_dirty();
}

static void message(const char *title, const char *body)
{
    strlcpy(s_title, title, sizeof(s_title));
    strlcpy(s_body, body ? body : "", sizeof(s_body));
    set_state(V_MSG);
}

// ---------------------------------------------------------------- IMA ADPCM

static const int16_t STEP[89] = {
    7,     8,     9,     10,    11,    12,    13,    14,    16,    17,    19,    21,    23,    25,    28,    31,
    34,    37,    41,    45,    50,    55,    60,    66,    73,    80,    88,    97,    107,   118,   130,   143,
    157,   173,   190,   209,   230,   253,   279,   307,   337,   371,   408,   449,   494,   544,   598,   658,
    724,   796,   876,   963,   1060,  1166,  1282,  1411,  1552,  1707,  1878,  2066,  2272,  2499,  2749,  3024,
    3327,  3660,  4026,  4428,  4871,  5358,  5894,  6484,  7132,  7845,  8630,  9493,  10442, 11487, 12635, 13899,
    15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};
static const int8_t IDX[16] = {-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8};

typedef struct {
    int pred, idx;
} adpcm_t;

static int adpcm_code(adpcm_t *st, int s)
{
    int diff = s - st->pred, code = 0;
    if (diff < 0) code = 8, diff = -diff;
    int step = STEP[st->idx], delta = step >> 3;
    if (diff >= step) code |= 4, diff -= step, delta += step;
    step >>= 1;
    if (diff >= step) code |= 2, diff -= step, delta += step;
    step >>= 1;
    if (diff >= step) code |= 1, delta += step;
    st->pred += code & 8 ? -delta : delta;
    if (st->pred > 32767) st->pred = 32767;
    if (st->pred < -32768) st->pred = -32768;
    st->idx += IDX[code];
    if (st->idx < 0) st->idx = 0;
    if (st->idx > 88) st->idx = 88;
    return code;
}

// ---------------------------------------------------------------- recording

static void send_chunk(int seq, const adpcm_t *from, const uint8_t *data, size_t n)
{
    static char json[1300];
    int h = snprintf(json, sizeof(json), "{\"t\":\"va\",\"s\":%d,\"p\":%d,\"i\":%d,\"d\":\"", seq, from->pred, from->idx);
    size_t olen = 0;
    mbedtls_base64_encode((unsigned char *)json + h, sizeof(json) - h - 3, &olen, data, n);
    strcpy(json + h + olen, "\"}");
    ble_send_json(json);
}

static void rec_task(void *arg)
{
    static int16_t pcm[FRAME];
    static uint8_t adp[FRAME / 2 * CHUNK_FRAMES];
    char msg[96];
    if (!audio_mic_start()) {
        s_rec = false;
        message(TR("Sin micrófono", "No microphone"), TR("el micrófono no responde", "the microphone does not answer"));
        vTaskDelete(NULL);
    }
    snprintf(msg, sizeof(msg), "{\"t\":\"voice\",\"st\":\"start\",\"lang\":\"%s\",\"rate\":16000}", g_app.lang ? "en-US" : "es-ES");
    ble_send_json(msg);
    adpcm_t st = {0, 0}, chunk_st = st;
    int frames = 0, seq = 0, total = 0;
    size_t n = 0;
    // The ADC starts with a thump (a few hundred ms near full scale on "mictest"): not sent
    for (int k = 0; k < 15 && !s_stop; k++) audio_mic_read(pcm, FRAME, 100);
    s_level = 0;  // the screen says "listening" from here
    uint32_t t0 = now_ms();
    while (!s_stop && now_ms() - t0 < MAX_MS) {
        int got = audio_mic_read(pcm, FRAME, 100);
        if (got <= 0) continue;
        int peak = 0;
        for (int i = 0; i < got; i++) {
            int a = abs(pcm[i]);
            if (a > peak) peak = a;
            int c = adpcm_code(&st, pcm[i]);
            if (i & 1) adp[n++] |= (uint8_t)(c << 4);
            else adp[n] = (uint8_t)c;
        }
        if (got & 1) n++;
        s_level = peak * 1000 / 12000 > 1000 ? 1000 : peak * 1000 / 12000;
        total += got;
        if (++frames == CHUNK_FRAMES) {
            send_chunk(seq++, &chunk_st, adp, n);
            chunk_st = st;
            frames = 0;
            n = 0;
        }
    }
    if (n) send_chunk(seq++, &chunk_st, adp, n);
    audio_mic_stop();
    ESP_LOGI(TAG, "recorded %d ms in %d messages", total / 16, seq);
    if (s_state == V_LISTEN) {
        ble_send_json("{\"t\":\"voice\",\"st\":\"end\"}");
        set_state(V_THINK);
    }
    s_rec = false;
    vTaskDelete(NULL);
}

void voice_begin(void)
{
    if (!g_app.voice || s_rec) return;
    if (!ble_connected()) {
        message(TR("Sin móvil", "No phone"), TR("las órdenes de voz necesitan la app Marea conectada",
                                                "voice commands need the Marea app connected"));
        sys_alert(&app_voice);
        return;
    }
    s_stop = false;
    s_level = -1;  // warming up
    s_rec = true;
    set_state(V_LISTEN);
    sys_alert(&app_voice);
    if (xTaskCreatePinnedToCore(rec_task, "voice", 4096, NULL, 4, NULL, 0) != pdPASS) {
        s_rec = false;
        message(TR("Sin memoria", "Out of memory"), NULL);
    }
}

void voice_end(void) { s_stop = true; }

// ---------------------------------------------------------------- the phone's answer

static void copy_field(char *dst, size_t n, const char *src, const char *end)
{
    size_t len = end ? (size_t)(end - src) : strlen(src);
    if (len >= n) len = n - 1;
    memcpy(dst, src, len);
    dst[len] = 0;
}

bool voice_command(const char *cmd)
{
    // Only the answers: "voice 0" / "voice 1" is the setting (settings.c)
    if (strncmp(cmd, "voice ask ", 10) && strncmp(cmd, "voice fail ", 11) && strncmp(cmd, "voice err ", 10)) return false;
    const char *a = cmd + 6;
    if (s_state != V_THINK && s_state != V_LISTEN) return true;  // closed meanwhile: too late
    if (strncmp(a, "ask ", 4) == 0) {
        const char *p1 = strchr(a + 4, '|'), *p2 = p1 ? strchr(p1 + 1, '|') : NULL;
        if (!p2) return true;
        copy_field(s_cmd, sizeof(s_cmd), a + 4, p1);
        copy_field(s_heard, sizeof(s_heard), p1 + 1, p2);
        copy_field(s_desc, sizeof(s_desc), p2 + 1, NULL);
        set_state(V_ASK);
    } else if (strncmp(a, "fail ", 5) == 0) {
        copy_field(s_heard, sizeof(s_heard), a + 5, NULL);
        char b[120];
        snprintf(b, sizeof(b), "«%s»", s_heard);
        message(TR("No sé hacer eso", "I can't do that"), b);
    } else if (strncmp(a, "err ", 4) == 0) {
        const char *r = a + 4;
        if (!strcmp(r, "nada")) message(TR("No te he oído", "I didn't hear you"), TR("habla cerca del reloj mientras mantienes BOOT", "speak close to the watch while holding BOOT"));
        else if (!strcmp(r, "permiso")) message(TR("Falta un permiso", "Permission needed"), TR("abre la app Marea y activa las órdenes de voz", "open the Marea app and switch voice commands on"));
        else if (!strcmp(r, "idioma")) message(TR("Falta el idioma", "Language missing"), TR("el móvil lo está descargando: prueba en unos minutos", "the phone is downloading it: try again in a few minutes"));
        else if (!strcmp(r, "nomovil")) message(TR("No disponible", "Not available"), TR("este móvil no reconoce voz desde el reloj (hace falta Android 13)", "this phone cannot recognize the watch's voice (Android 13 needed)"));
        else {
            char b[64];
            snprintf(b, sizeof(b), TR("el móvil dice: %s", "the phone says: %s"), r);
            message(TR("No ha funcionado", "It didn't work"), b);
        }
    }
    return true;
}

// ---------------------------------------------------------------- the screen

static void mic_icon(int cx, int y, uint16_t c)
{
    // 8 px cells: capsule, cradle and stand
    ui_rect(cx - 16, y, 32, 56, c);
    ui_rect(cx - 8, y - 8, 16, 8, c);
    ui_rect(cx - 8, y + 56, 16, 8, c);
    ui_rect(cx - 32, y + 32, 8, 24, c);
    ui_rect(cx + 24, y + 32, 8, 24, c);
    ui_rect(cx - 24, y + 56, 8, 8, c);
    ui_rect(cx + 16, y + 56, 8, 8, c);
    ui_rect(cx - 16, y + 64, 32, 8, c);
    ui_rect(cx - 4, y + 72, 8, 16, c);
    ui_rect(cx - 20, y + 88, 40, 8, c);
}

static int s_drawn_state = -1, s_drawn_tick = -1;

static void v_open(void) { s_drawn_state = -1; }

static bool v_draw(uint32_t now, bool force)
{
    int st = s_state;
    // The loop's "now" can be a moment older than s_t (set from another task): signed
    int32_t sage = (int32_t)(now - s_t);
    uint32_t age = sage < 0 ? 0 : (uint32_t)sage;
    if (st == V_THINK && age > THINK_MS) {
        ble_send_json("{\"t\":\"voice\",\"st\":\"cancel\"}");
        message(TR("Sin respuesta", "No answer"), TR("el móvil no ha contestado", "the phone did not answer"));
        st = s_state;
    }
    if (st == V_DONE && age > DONE_MS) {
        s_state = V_IDLE;
        sys_alert_done();
        return false;
    }
    int tick = st == V_LISTEN || st == V_THINK ? (int)(now / 100) : 0;
    if (!force && st == s_drawn_state && tick == s_drawn_tick) return false;
    s_drawn_state = st;
    s_drawn_tick = tick;
    ui_background(true);
    ui_veil(40, UI_H, 2);
    switch (st) {
    case V_LISTEN: {
        int lv = s_level;
        bool ready = lv >= 0;
        ui_print_c(&ui_jersey, 4, UI_W / 2, 70, ready ? TR("Te escucho", "Listening") : TR("Un segundo", "Hold on"), MS_ORO, true);
        mic_icon(UI_W / 2, 150, ready ? MS_CREMA : MS_GRIS);
        if (!ready) break;
        // Level bars either side of the microphone, from the loudest sample of the last 20 ms
        for (int k = 0; k < 4; k++) {
            int h = 8 + (lv * (40 - k * 8)) / 1000;
            uint16_t c = lv > k * 200 ? MS_VERDE : MS_PILOTO_OFF;
            ui_rect(UI_W / 2 - 64 - k * 20, 200 - h, 12, h * 2, c);
            ui_rect(UI_W / 2 + 52 + k * 20, 200 - h, 12, h * 2, c);
        }
        int left = MAX_MS - (int)age;
        ui_bar(96, 300, UI_W - 192, 12, left * 1000 / MAX_MS, MS_ORO);
        ui_print_c(&ui_vt, 2, UI_W / 2, 336, TR("habla y suelta BOOT", "speak, then let go"), MS_CREMA, false);
        ui_print_c(&ui_vt, 1, UI_W / 2, 376, TR("p. ej. «alarma a las siete»", "e.g. \"alarm at seven\""), MS_GRIS, false);
        break;
    }
    case V_THINK: {
        ui_print_c(&ui_jersey, 4, UI_W / 2, 70, TR("Un momento", "One moment"), MS_ORO, true);
        mic_icon(UI_W / 2, 150, MS_GRIS);
        int dots = (int)(age / 300) % 4;
        for (int k = 0; k < 3; k++) ui_rect(UI_W / 2 - 40 + k * 32, 300, 16, 16, k < dots ? MS_ORO : MS_PILOTO_OFF);
        ui_print_c(&ui_vt, 2, UI_W / 2, 340, TR("el móvil lo está entendiendo", "the phone is working it out"), MS_CREMA, false);
        break;
    }
    case V_ASK: {
        char body[200];
        snprintf(body, sizeof(body), "%s\n«%s»", s_desc, s_heard);
        ui_dialog(TR("¿Lo hago?", "Do it?"), body, TR("No", "No"), TR("Sí", "Yes"), UI_BTN_MAIN);
        break;
    }
    case V_DONE:
        ui_dialog(TR("Hecho", "Done"), s_desc, NULL, TR("Vale", "OK"), UI_BTN_MAIN);
        break;
    default:
        ui_dialog(s_title, s_body, NULL, TR("Vale", "OK"), UI_BTN_MAIN);
        break;
    }
    return true;
}

static void v_event(const ui_event_t *e)
{
    if (e->type != EV_TAP) return;
    int st = s_state;
    if (st == V_ASK) {
        int hit = ui_dialog_hit(e->x, e->y, true);
        if (hit == 1) {
            s_state = V_IDLE;
            sys_alert_done();
        } else if (hit == 2) {
            ESP_LOGI(TAG, "confirmed: %s", s_cmd);
            // Commands that change the screen (a face, an app) close this one first; the rest say "done"
            bool shows = strncmp(s_cmd, "fx ", 3) == 0 || strncmp(s_cmd, "ui ", 3) == 0;
            static char cmd[96];
            strlcpy(cmd, s_cmd, sizeof(cmd));
            if (shows) {
                s_state = V_IDLE;
                sys_alert_done();
            } else {
                set_state(V_DONE);
            }
            app_command(cmd);
        }
    } else if (st == V_MSG || st == V_DONE) {
        if (ui_dialog_hit(e->x, e->y, false)) {
            s_state = V_IDLE;
            sys_alert_done();
        }
    }
}

static void v_close(void)
{
    // Closed (BOOT, a swipe) before the end: stop recording and tell the phone to forget it
    if (s_rec) s_stop = true;
    if (s_state == V_LISTEN || s_state == V_THINK) ble_send_json("{\"t\":\"voice\",\"st\":\"cancel\"}");
    s_state = V_IDLE;
}

static bool v_keep_on(void) { return s_state != V_IDLE; }

static const app_t app_voice = {"Voz", "Voice", NULL, v_open, v_draw, v_event, v_close, v_keep_on};

// ---------------------------------------------------------------- console

void voice_mictest(void)
{
    static int16_t pcm[FRAME];
    if (!audio_mic_start()) {
        printf("mictest: no microphone\n");
        return;
    }
    for (int k = 0; k < 15; k++) {  // 3 s, a line every 200 ms
        int peak = 0;
        int64_t sum = 0;
        int cnt = 0;
        for (int f = 0; f < 10; f++) {
            int got = audio_mic_read(pcm, FRAME, 100);
            for (int i = 0; i < got; i++) {
                int a = abs(pcm[i]);
                if (a > peak) peak = a;
                sum += (int64_t)pcm[i] * pcm[i];
            }
            cnt += got;
        }
        int rms = 0;
        if (cnt) {
            int64_t m = sum / cnt;
            while ((int64_t)(rms + 1) * (rms + 1) <= m) rms += rms < 64 ? 1 : rms / 8;
        }
        printf("mictest: peak %5d rms %5d (%d samples)\n", peak, rms, cnt);
    }
    audio_mic_stop();
}
