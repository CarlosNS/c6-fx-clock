#pragma once
#include <stdbool.h>
#include <stdint.h>

// Slides between the system's screens (trans.c), so where a screen comes from is the way back to it.
// Call trans_start() just before switching views: it keeps a picture of the screen that is leaving.
// "Cover": the new screen slides in over the old one (from the side named). "Uncover": the old one
// slides away (towards the side named) and shows the new one under it.
typedef enum {
    T_NONE,
    T_COVER_FROM_TOP,      // quick settings coming down
    T_COVER_FROM_BOTTOM,   // the launcher coming up
    T_COVER_FROM_RIGHT,    // an app opening
    T_UNCOVER_UP,          // quick settings going back up
    T_UNCOVER_DOWN,        // the launcher going back down
    T_UNCOVER_RIGHT,       // leaving an app
} trans_t;

void trans_start(trans_t kind);
// A slide is under way: the main loop draws the new screen whole and calls trans_compose()
bool trans_active(uint32_t now);
// Puts the leaving picture over the new screen drawn in nfb (native, panel size), for this moment
void trans_compose(uint16_t *nfb, uint32_t now);
// Coming back to the face: true if there is a recent picture of it to slide in (then draw it with
// trans_compose_face()); false and the face simply comes back
bool trans_face_ready(void);
void trans_compose_face(uint16_t *nfb, uint32_t now);
