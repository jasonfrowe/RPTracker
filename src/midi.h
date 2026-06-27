#ifndef MIDI_H
#define MIDI_H

#include <stdint.h>
#include <stdbool.h>

// ============================================================================
// MIDI INPUT SUPPORT
// ============================================================================

// Currently held MIDI note (0 = none)
extern uint8_t midi_note;

// Velocity of the held note (1-127)
extern uint8_t midi_vel;

// A note on arrived this frame, retrigger even if the note is unchanged
extern bool midi_fresh;

// Poll MIDI input, called once per frame
void midi_task(void);

#endif // MIDI_H
