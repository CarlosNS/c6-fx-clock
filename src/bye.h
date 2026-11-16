#pragma once
#include <stdint.h>

// "Bajamar": the power-off scene, with no words. While PWR is held the power symbol's ring fills and
// the moon sets over the sea; let go and it all goes back; held to the end, the moon sinks, the stars
// go out, the horizon closes into a point of light and the watch switches off. Draws on the native
// framebuffer set by ui_begin().

#define BYE_CANCEL_MS 340   // the ring drains and the moon rises back
#define BYE_OFF_MS 1650     // the switch-off, from the full ring to black

// Held: permille of the hold (0..1000); t is any running clock in ms (waves, stars)
void bye_hold(uint32_t t, int permille);
// Let go at `from` permille, `since` ms ago (0..BYE_CANCEL_MS)
void bye_cancel(uint32_t t, int from, uint32_t since);
// The switch-off, `t2` ms in (0..BYE_OFF_MS; black after it)
void bye_off(uint32_t t, uint32_t t2);
