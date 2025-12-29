#ifndef PLAYER_H
#define PLAYER_H

#include <stdint.h>

// Initialize player state
void player_init(void);

// Process keyboard-to-OPL logic (call this once per frame in main loop)
void player_tick(void);

// Global settings
extern uint8_t current_octave;
extern uint8_t current_instrument;
extern uint8_t player_channel;

extern void handle_navigation(void);

#endif