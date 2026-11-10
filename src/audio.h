#pragma once
#include <stdbool.h>
#include <stdint.h>

// Speaker: ES8311 codec over I2S plus the amplifier (GPIO46). Tones are synthesised; the volume is the
// "vol" setting. The board has no vibration motor, so sounds stand in for haptics.

void audio_init(void);
// Alarm melody, repeating until audio_stop()
void audio_alarm_start(void);
// Incoming call: a two-tone ring, repeating until audio_stop()
void audio_ring_start(void);
// Short click for a touch (25 ms) or a confirmation (60 ms)
void audio_beep(int ms);
void audio_stop(void);

// Microphones (ES7210), 16 kHz mono: start, read up to 320 samples (20 ms) at a time, stop.
// Recording keeps the chip out of light sleep, like a sound playing.
bool audio_mic_start(void);
int audio_mic_read(int16_t *mono, int n, int timeout_ms);
void audio_mic_stop(void);
