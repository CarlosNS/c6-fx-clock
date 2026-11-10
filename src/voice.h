// Voice commands (voice.c): holding BOOT records the microphones and streams them to the phone app,
// which recognizes the speech in the watch's language and answers with what it understood; the
// watch asks before doing it. "voice" setting, off by default.
//
// Watch -> phone (Bluetooth notifications):
//   {"t":"voice","st":"start","lang":"es-ES","rate":16000}
//   {"t":"va","s":<seq>,"p":<predictor>,"i":<step index>,"d":"<base64 IMA ADPCM, 4 bit, low nibble first>"}
//   {"t":"voice","st":"end"}      the user let go (or 8 s passed)
//   {"t":"voice","st":"cancel"}   closed before the answer
// Phone -> watch (commands):
//   voice ask <command>|<what was heard>|<what it will do>
//   voice fail <what was heard>   understood words, but no command in them
//   voice err <reason>            nothing heard ("nada"), no permission, language missing...
#pragma once
#include <stdbool.h>

// BOOT held long enough / let go (main.c)
void voice_begin(void);
void voice_end(void);
// A "voice ..." line from the phone; false if it is not one
bool voice_command(const char *cmd);
// Console "mictest": 3 s of microphone levels on the log
void voice_mictest(void);
