#ifndef PLAYER_H
#define PLAYER_H

#include <stdint.h>
#include <stdbool.h>
#include "instruments.h"

// Buffer to hold one full pattern (32 rows * 9 channels * 5 bytes)
#define PATTERN_SIZE 1440U 

#define is_shift_down() (key(KEY_LEFTSHIFT) || key(KEY_RIGHTSHIFT))
#define is_ctrl_down()  (key(KEY_LEFTCTRL)  || key(KEY_RIGHTCTRL))
#define is_alt_down()   (key(KEY_LEFTALT)   || key(KEY_RIGHTALT))

// Fixed-point scale for tempo system (8.8 format: 8-bit integer, 8-bit fraction)
#define TICK_SCALE 256

typedef struct {
    bool is_playing;
    uint16_t ticks_per_row_fp; // 8.8 fixed-point ticks per row (was uint8_t ticks_per_row)
    uint16_t tick_counter_fp;  // 8.8 fixed-point tick accumulator (was uint8_t tick_counter)
    uint8_t bpm;               // Current BPM (60-240)
} SequencerState;

extern SequencerState seq;

extern bool is_follow_mode;
extern uint8_t play_row;

// Initialize player state
void player_init(void);

// Process keyboard-to-OPL logic (call this once per frame in main loop)
void player_tick(void);

// Global settings
extern uint8_t current_octave;
extern uint8_t current_instrument;
extern uint8_t player_channel;
extern uint8_t current_volume;
extern bool effect_view_mode;
extern uint16_t last_effect[9];
extern uint16_t lfo_tempo_scaler;

extern void handle_navigation(void);
extern void handle_transport_controls(void);
extern void sequencer_step(void);
extern void handle_editing(void);
extern void modify_volume_effects(int8_t delta);
extern void modify_effect_low_byte(int8_t delta);
extern void modify_instrument(int8_t delta);
extern void modify_note(int8_t delta);
extern void change_pattern(int8_t delta);
extern void handle_song_order_input(void);
extern void pattern_copy(uint8_t pattern_id);
extern void pattern_paste(uint8_t pattern_id);
extern void update_lfo_scaler(void);

extern uint16_t get_pattern_xram_addr(uint8_t pat, uint8_t row, uint8_t chan);
extern uint8_t active_midi_note;
extern bool midi_polyphonic;
extern uint8_t active_midi_notes[9];
extern OPL_Patch active_patch;

extern void select_instrument(uint8_t inst_idx);
extern void midi_process_pitch_bend(uint8_t chan, uint16_t pb_val);

#endif