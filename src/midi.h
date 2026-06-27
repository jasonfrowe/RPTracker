#ifndef MIDI_H
#define MIDI_H

#include <stdint.h>
#include <stdbool.h>

// ============================================================================
// MIDI INPUT SUPPORT
// ============================================================================

// Event-driven MIDI callbacks (implemented in player.c)
void midi_process_note_on(uint8_t chan, uint8_t note, uint8_t velocity);
void midi_process_note_off(uint8_t chan, uint8_t note);
void midi_process_cc(uint8_t chan, uint8_t cc_num, uint8_t cc_val);

// Poll MIDI input, called once per frame
void midi_task(void);

#endif // MIDI_H
