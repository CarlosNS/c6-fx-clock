#pragma once
#include <stdbool.h>

// What the phone app tells the watch over the permanent Bluetooth link (control.c passes the lines):
//   music <playing 0|1> <volume 0..100>|<title>|<artist>   now playing (Spotify or any media app)
//   music off                                             no media session
//   call ring|<name or number>                            incoming call
//   call end                                              answered, rejected or missed elsewhere
// and what the watch sends back (Bluetooth notifications):
//   {"t":"media","cmd":"play"|"pause"|"next"|"prev"|"vol+"|"vol-"}
//   {"t":"call","cmd":"reject"|"silence"}

typedef struct {
    bool valid;          // a media session is known
    bool playing;
    int volume;          // phone media volume 0..100
    char title[64];
    char artist[48];
    unsigned version;    // changes with every update (redraws)
} phone_music_t;

typedef struct {
    bool ringing;
    char who[48];
} phone_call_t;

extern phone_music_t g_music;
extern phone_call_t g_call;

// Handles a "music ..." or "call ..." line; returns false if it is neither
bool phone_command(const char *cmd);
// Sends a media / call command to the phone; false without a link
bool phone_send(const char *type, const char *cmd);
