#include <rp6502.h>
#include "player.h"
#include "opl.h"
#include "input.h"
#include "usb_hid_keys.h"
#include <stdio.h>
#include "screen.h"
#include "instruments.h"
#include "song.h"
#include "effects.h"

// UI Toggle: false = Volume/Instrument, true = 16-bit Effect
bool effect_view_mode = false;
uint16_t last_effect[9] = {0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF};


// Current State
uint8_t current_instrument = 0; // Instrument index (0 = Piano)
uint8_t current_octave = 3; // Adjusts in jumps of 12
uint8_t active_midi_note = 0;      // Tracks the currently playing note
uint8_t current_volume = 63; // Max volume (0x3F)

uint16_t get_pattern_xram_addr(uint8_t pat, uint8_t row, uint8_t chan) {
    // addr = (pat * 1440) + (row * 45) + (chan * 5)
    // 1440 is 0x05A0
    uint16_t p_off = (uint16_t)pat * 1440U;
    // 45 is 0x2D
    uint16_t r_off = (uint16_t)row * 45U;
    
    return p_off + r_off + (chan * 5U);
}

static uint8_t pattern_clipboard[PATTERN_SIZE];
static bool clipboard_full = false;

SequencerState seq = {false, 6, 0, 125};

#define KEY_REPEAT_DELAY 20 // Frames before repeat starts
#define KEY_REPEAT_RATE  4  // Frames between repeats
uint8_t repeat_timer = 0;
uint8_t last_scancode = 0;

// Piano Mapping: Scancode -> MIDI Offset from C
// (0 = C, 1 = C#, 2 = D, etc.)
static int8_t get_semitone(uint8_t scancode) {
    switch (scancode) {
        // Lower Row (C-4 to B-4)
        case KEY_Z: return 0;  case KEY_S: return 1;
        case KEY_X: return 2;  case KEY_D: return 3;
        case KEY_C: return 4;  case KEY_V: return 5;
        case KEY_G: return 6;  case KEY_B: return 7;
        case KEY_H: return 8;  case KEY_N: return 9;
        case KEY_J: return 10; case KEY_M: return 11;
        case KEY_COMMA: return 12;

        // Upper Row (C-5 to B-5)
        case KEY_Q: return 12; case KEY_2: return 13;
        case KEY_W: return 14; case KEY_3: return 15;
        case KEY_E: return 16; case KEY_R: return 17;
        case KEY_5: return 18; case KEY_T: return 19;
        case KEY_6: return 20; case KEY_Y: return 21;
        case KEY_7: return 22; case KEY_U: return 23;
        case KEY_I: return 24;

        default: return -1;
    }
}

void player_tick(void) {
    uint8_t channel = cur_channel; // Map piano to the active grid channel
    bool note_pressed_this_frame = false;
    uint8_t target_note = 0;
    uint8_t semitone = 0;

    // If Ctrl is held, we check for shortcuts and then STOP processing.
    // Clipboard operations
    if (is_ctrl_down()) {
        if (key_pressed(KEY_C)) {
            pattern_copy(cur_pattern);
        }
        if (key_pressed(KEY_V)) {
            pattern_paste(cur_pattern);
        }
        
        if (active_midi_note != 0) {
            OPL_NoteOff(channel);
            active_midi_note = 0;
        }
        return; 
    }

    // 1. Scan for piano keys
    for (int k = 0; k < 256; k++) {
        if (key(k)) {
            int8_t s = get_semitone(k);
            if (s != -1) {
                semitone = s;
                target_note = (current_octave + 1) * 12 + semitone;
                note_pressed_this_frame = true;
                ch_arp[channel].active = false; // Keyboard input kills any background Arp
                ch_vibrato[channel].active = false; // Keyboard input kills vibrato
                break; 
            }
        }
    }

    // 2. Logic: Note On & Recording
    if (note_pressed_this_frame) {
        if (target_note != active_midi_note) {
            // Live Overdrive: Cut the sequencer's note and play the keyboard note
            OPL_NoteOff(channel); 
            // ch_peaks[channel] = 0; // Clear peak
            OPL_SetPatch(channel, &gm_bank[current_instrument]);
            OPL_SetVolume(channel, current_volume << 1); 
            OPL_NoteOn(channel, target_note);
            ch_peaks[channel] = current_volume; // Set peak for meter display
            active_midi_note = target_note;

            if (edit_mode) {
                // PatternCell c = {target_note, current_instrument, current_volume, 0};
                PatternCell c;
                read_cell(cur_pattern, cur_row, cur_channel, &c);
                c.note = target_note;
                c.inst = current_instrument;
                c.vol  = current_volume;
                write_cell(cur_pattern, cur_row, cur_channel, &c);
                render_row(cur_row);
                
                // ONLY advance the row if the sequencer IS NOT playing.
                // If the sequencer IS playing, it is already advancing the row for us.
                if (!seq.is_playing) {
                    // if (cur_row < 31) cur_row++;
                    if (cur_row < 31) {
                        cur_row++;
                    } else {
                        cur_row = 0; // Loop back to the start of the 32-row block
                    }
                }
            }
            
        }
    } 
    // 3. Logic: Note Off
    // Only turn off notes via keyboard when sequencer is NOT playing
    else {
        if (active_midi_note != 0 && !seq.is_playing) {
            OPL_NoteOff(channel);
            // ch_peaks[channel] = 0; // Clear peak
            active_midi_note = 0;
        }
    }

    // 4. Function Keys (Octave & Instrument)
    if (key_pressed(KEY_F1)) { if (current_octave > 0) current_octave--; update_dashboard(); }
    if (key_pressed(KEY_F2)) { if (current_octave < 8) current_octave++; update_dashboard(); }

    if (key_pressed(KEY_F5)) { // Use F5 to "Pick" the instrument under the cursor
        PatternCell cell;
        read_cell(cur_pattern, cur_row, cur_channel, &cell);
        if (cell.note != 0) {
            current_instrument = cell.inst;
            OPL_SetPatch(cur_channel, &gm_bank[current_instrument]);
            update_dashboard();
        }
    }

    // Volume / Effects: [ and ] (with Shift detection inside modify_volume_effects)
    if (key_pressed(KEY_LEFTBRACE))  modify_volume_effects(-1);
    if (key_pressed(KEY_RIGHTBRACE)) modify_volume_effects(1);

    // Low-Byte Effect Parameters
    // Using semicolon for Down and Apostrophe for Up (as they sit near each other)
    if (key_pressed(KEY_SEMICOLON))      modify_effect_low_byte(-1);
    if (key_pressed(KEY_APOSTROPHE))  modify_effect_low_byte(1);
    
    // Instrument: F3 and F4
    if (key_pressed(KEY_F3)) modify_instrument(-1);
    if (key_pressed(KEY_F4)) modify_instrument(1);

    // Note Adjustment: - and =
    if (key_pressed(KEY_MINUS)) modify_note(-1);
    if (key_pressed(KEY_EQUAL)) modify_note(1);

    // F8: Toggle Song vs Pattern Mode
    if (key_pressed(KEY_F8)) {
        is_song_mode = !is_song_mode;
        update_dashboard();
    }

    // Pattern Change: F9 and F10
    if (key_pressed(KEY_F9)) change_pattern(-1);
    if (key_pressed(KEY_F10)) change_pattern(1);

    if (key_pressed(KEY_SLASH)) {
        effect_view_mode = !effect_view_mode;
        draw_headers(); // Update RN | NOTE EFFT label
        render_grid();  // Swap columns on screen
        update_cursor_visuals(cur_row, cur_row, cur_channel, cur_channel);
    }

    handle_song_order_input();

}

void handle_navigation() {
    uint8_t move_row = 0;
    int8_t move_chan = 0;
    uint8_t active_scancode = 0;

    // Detect which key is being held
    if (key(KEY_DOWN))       active_scancode = KEY_DOWN;
    else if (key(KEY_UP))    active_scancode = KEY_UP;
    else if (key(KEY_LEFT))  active_scancode = KEY_LEFT;
    else if (key(KEY_RIGHT)) active_scancode = KEY_RIGHT;

    if (active_scancode != 0) {
        if (active_scancode != last_scancode) {
            // First press: move immediately
            repeat_timer = 0;
            if (active_scancode == KEY_DOWN)  move_row = 1;
            if (active_scancode == KEY_UP)    move_row = 2; // Signal for 'Up'
            if (active_scancode == KEY_LEFT)  move_chan = -1;
            if (active_scancode == KEY_RIGHT) move_chan = 1;
        } else {
            // Holding: wait for repeat delay
            repeat_timer++;
            if (repeat_timer >= KEY_REPEAT_DELAY) {
                if ((repeat_timer - KEY_REPEAT_DELAY) % KEY_REPEAT_RATE == 0) {
                    if (active_scancode == KEY_DOWN)  move_row = 1;
                    if (active_scancode == KEY_UP)    move_row = 2;
                    if (active_scancode == KEY_LEFT)  move_chan = -1;
                    if (active_scancode == KEY_RIGHT) move_chan = 1;
                }
            }
        }
    }
    last_scancode = active_scancode;

    // Track state to determine if we need a UI update
    uint8_t old_row = cur_row;
    uint8_t old_chan = cur_channel;

    // Apply Row Movement with Wrapping
    if (move_row == 1) { // Move Down
        if (cur_row < 31) cur_row++; 
        else cur_row = 0; // Wrap 1F -> 00
    }
    if (move_row == 2) { // Move Up
        if (cur_row > 0) cur_row--;
        else cur_row = 31; // Wrap 00 -> 1F
    }

    // Apply Channel Movement (Capped at 0-8)
    if (move_chan == -1 && cur_channel > 0) cur_channel--;
    if (move_chan == 1  && cur_channel < 8) cur_channel++;

    // Optional: Toggle Edit mode with Space inside navigation
    if (key_pressed(KEY_SPACE)) {
        edit_mode = !edit_mode;
        // Force a visual refresh even if row/column didn't move
        update_cursor_visuals(cur_row, cur_row, cur_channel, cur_channel);
        update_dashboard();
    }

}

void sequencer_step(void) {
    if (!seq.is_playing) return;
    seq.tick_counter++;

    if (seq.tick_counter >= seq.ticks_per_row) {
        seq.tick_counter = 0;
        
        for (uint8_t ch = 0; ch < 9; ch++) {
            if (ch == cur_channel && active_midi_note != 0) continue;

            PatternCell cell;
            read_cell(cur_pattern, cur_row, ch, &cell);
            
            // --- 1. IDEMPOTENT EFFECT PARSING ---
            if (cell.effect != last_effect[ch]) {
                uint16_t eff = cell.effect;
                uint8_t cmd = (eff >> 12) & 0x0F;

                if (cmd == 1) { 
                    ch_arp[ch].active = true;
                    ch_arp[ch].style  = (eff >> 8) & 0x0F;
                    ch_arp[ch].depth  = (eff >> 4) & 0x0F;
                    ch_arp[ch].speed_idx = (eff & 0x0F);
                    ch_arp[ch].target_ticks = arp_tick_lut[ch_arp[ch].speed_idx];
                    
                    // ONLY reset phase if the command actually changed
                    ch_arp[ch].phase_timer = 0;
                    ch_arp[ch].step_index = 0;
                    ch_arp[ch].just_triggered = true; // Mark as just started
                } else if (cmd == 2) {
                    // Portamento: 2SDT
                    // S = Mode (0=Up, 1=Down, 2=To Target)
                    // D = Speed (ticks between steps)
                    // T = Target note or semitone count
                    
                    // Determine starting note: use note on this row if present, else use base_note
                    uint8_t start_note = (cell.note != 0 && cell.note != 255) ? cell.note : ch_arp[ch].base_note;
                    uint8_t start_inst = (cell.note != 0 && cell.note != 255) ? cell.inst : ch_arp[ch].inst;
                    uint8_t start_vol = (cell.note != 0 && cell.note != 255) ? cell.vol : ch_arp[ch].vol;
                    
                    ch_porta[ch].current_note = start_note;
                    ch_porta[ch].inst = start_inst;
                    ch_porta[ch].vol = start_vol;
                    
                    ch_porta[ch].active = true;
                    ch_porta[ch].mode = (eff >> 8) & 0x0F;
                    ch_porta[ch].speed = ((eff >> 4) & 0x0F);
                    if (ch_porta[ch].speed == 0) ch_porta[ch].speed = 1; // Prevent divide by zero
                    ch_porta[ch].target_note = (eff & 0x0F);
                    ch_porta[ch].tick_counter = 0;
                    
                    // If mode 2, target is absolute note; otherwise use as semitone count
                    if (ch_porta[ch].mode != 2) {
                        uint8_t semitones = ch_porta[ch].target_note;
                        if (semitones == 0) semitones = 12; // Default to octave
                        if (ch_porta[ch].mode == 0) { // Up
                            ch_porta[ch].target_note = (ch_porta[ch].current_note + semitones) > 127 ? 
                                127 : ch_porta[ch].current_note + semitones;
                        } else { // Down
                            ch_porta[ch].target_note = (ch_porta[ch].current_note < semitones) ? 
                                0 : ch_porta[ch].current_note - semitones;
                        }
                    }
                    ch_arp[ch].active = false; // Portamento kills arpeggio
                } else if (cmd == 3) {
                    // Volume Slide: 3SDT
                    // S = Mode (0=Up, 1=Down, 2=To Target)
                    // D = Speed (volume units per tick)
                    // T = Target volume (0-F represents 0-63 scaled)
                    
                    // Determine starting volume: use vol on this row if present, else use current vol
                    uint8_t start_vol = (cell.note != 0 && cell.note != 255) ? cell.vol : ch_arp[ch].vol;
                    uint8_t start_note = (cell.note != 0 && cell.note != 255) ? cell.note : ch_arp[ch].base_note;
                    uint8_t start_inst = (cell.note != 0 && cell.note != 255) ? cell.inst : ch_arp[ch].inst;
                    
                    ch_volslide[ch].current_vol = start_vol;
                    ch_volslide[ch].base_note = start_note;
                    ch_volslide[ch].inst = start_inst;
                    
                    ch_volslide[ch].active = true;
                    ch_volslide[ch].mode = (eff >> 8) & 0x0F;
                    ch_volslide[ch].speed = ((eff >> 4) & 0x0F);
                    if (ch_volslide[ch].speed == 0) ch_volslide[ch].speed = 1;
                    
                    // Target volume: scale from 0-F to 0-63
                    uint8_t target_nibble = (eff & 0x0F);
                    ch_volslide[ch].target_vol = (target_nibble * 63) / 15;
                    ch_volslide[ch].tick_counter = 0;
                    
                    // For modes 0 and 1, if target is 0, slide to limit
                    if (ch_volslide[ch].mode == 0 && target_nibble == 0) {
                        ch_volslide[ch].target_vol = 63; // Slide up to max
                    } else if (ch_volslide[ch].mode == 1 && target_nibble == 0) {
                        ch_volslide[ch].target_vol = 0; // Slide down to silence
                    }
                } else if (cmd == 4) {
                    // Vibrato: 4RDT
                    // R = Rate (ticks per phase step - lower = faster)
                    // D = Depth (pitch deviation in semitones)
                    // T = Waveform (0=sine, 1=triangle, 2=square)
                    
                    // Determine starting note and volume
                    uint8_t start_note = (cell.note != 0 && cell.note != 255) ? cell.note : ch_arp[ch].base_note;
                    uint8_t start_inst = (cell.note != 0 && cell.note != 255) ? cell.inst : ch_arp[ch].inst;
                    uint8_t start_vol = (cell.note != 0 && cell.note != 255) ? cell.vol : ch_arp[ch].vol;
                    
                    ch_vibrato[ch].base_note = start_note;
                    ch_vibrato[ch].inst = start_inst;
                    ch_vibrato[ch].vol = start_vol;
                    
                    ch_vibrato[ch].active = true;
                    ch_vibrato[ch].rate = (eff >> 8) & 0x0F;
                    if (ch_vibrato[ch].rate == 0) ch_vibrato[ch].rate = 4; // Default rate
                    ch_vibrato[ch].depth = (eff >> 4) & 0x0F;
                    if (ch_vibrato[ch].depth == 0) ch_vibrato[ch].depth = 2; // Default depth
                    ch_vibrato[ch].waveform = (eff & 0x0F) % 3; // 0-2 only
                    ch_vibrato[ch].phase = 0;
                    ch_vibrato[ch].tick_counter = 0;
                    
                    ch_arp[ch].active = false; // Vibrato kills arpeggio
                } else if (cmd == 5) {
                    // Note Cut: 5__T
                    // T = Ticks before cut (0-F maps to 0-15 ticks)
                    uint8_t cut_tick = (eff & 0x0F);
                    if (cut_tick == 0) cut_tick = 1; // Prevent instant cut
                    
                    ch_notecut[ch].active = true;
                    ch_notecut[ch].cut_tick = cut_tick;
                    ch_notecut[ch].tick_counter = 0;
                } else if (cmd == 6) {
                    // Note Delay: 6NDT
                    // N = Note (0-F, 0=C, 1=C#, etc.)
                    // D = Delay ticks (0-F)
                    // T = Octave offset from base (0-F)
                    uint8_t note_offset = (eff >> 8) & 0x0F;
                    uint8_t delay_tick = (eff >> 4) & 0x0F;
                    uint8_t octave_offset = (eff & 0x0F);
                    
                    if (delay_tick == 0) delay_tick = 6; // Default to half row
                    
                    // Calculate absolute note
                    uint8_t base = (cell.note != 0 && cell.note != 255) ? cell.note : ch_arp[ch].base_note;
                    uint8_t delayed_note = base + note_offset + (octave_offset * 12);
                    if (delayed_note > 127) delayed_note = 127;
                    
                    ch_notedelay[ch].active = true;
                    ch_notedelay[ch].delay_tick = delay_tick;
                    ch_notedelay[ch].note = delayed_note;
                    ch_notedelay[ch].inst = (cell.note != 0 && cell.note != 255) ? cell.inst : ch_arp[ch].inst;
                    ch_notedelay[ch].vol = (cell.note != 0 && cell.note != 255) ? cell.vol : ch_arp[ch].vol;
                    ch_notedelay[ch].tick_counter = 0;
                    ch_notedelay[ch].triggered = false;
                } else if (cmd == 7) {
                    // Retrigger: 7__T
                    // T = Ticks between retriggers (0-F)
                    uint8_t speed = (eff & 0x0F);
                    if (speed == 0) speed = 3; // Default speed
                    
                    uint8_t note = (cell.note != 0 && cell.note != 255) ? cell.note : ch_arp[ch].base_note;
                    
                    ch_retrigger[ch].active = true;
                    ch_retrigger[ch].speed = speed;
                    ch_retrigger[ch].note = note;
                    ch_retrigger[ch].inst = (cell.note != 0 && cell.note != 255) ? cell.inst : ch_arp[ch].inst;
                    ch_retrigger[ch].vol = (cell.note != 0 && cell.note != 255) ? cell.vol : ch_arp[ch].vol;
                    ch_retrigger[ch].tick_counter = 0;
                } else if (cmd == 8) {
                    // Tremolo: 8RDT
                    // R = Rate (ticks per cycle step)
                    // D = Depth (volume deviation)
                    // T = Waveform (0=sine, 1=triangle, 2=square)
                    uint8_t start_vol = (cell.note != 0 && cell.note != 255) ? cell.vol : ch_volslide[ch].current_vol;
                    uint8_t start_note = (cell.note != 0 && cell.note != 255) ? cell.note : ch_arp[ch].base_note;
                    uint8_t start_inst = (cell.note != 0 && cell.note != 255) ? cell.inst : ch_arp[ch].inst;
                    
                    ch_tremolo[ch].base_vol = start_vol;
                    ch_tremolo[ch].note = start_note;
                    ch_tremolo[ch].inst = start_inst;
                    
                    ch_tremolo[ch].active = true;
                    ch_tremolo[ch].rate = (eff >> 8) & 0x0F;
                    if (ch_tremolo[ch].rate == 0) ch_tremolo[ch].rate = 4;
                    ch_tremolo[ch].depth = (eff >> 4) & 0x0F;
                    if (ch_tremolo[ch].depth == 0) ch_tremolo[ch].depth = 4;
                    ch_tremolo[ch].waveform = (eff & 0x0F) % 3;
                    ch_tremolo[ch].phase = 0;
                    ch_tremolo[ch].tick_counter = 0;
                } else if (cmd == 9) {
                    // Fine Pitch: 9__D
                    // D = Detune in 1/16 semitones (signed: 0-7 = +, 8-F = -)
                    uint8_t detune_raw = (eff & 0x0F);
                    int8_t detune = (detune_raw < 8) ? detune_raw : -(16 - detune_raw);
                    
                    uint8_t note = (cell.note != 0 && cell.note != 255) ? cell.note : ch_arp[ch].base_note;
                    
                    ch_finepitch[ch].active = true;
                    ch_finepitch[ch].base_note = note;
                    ch_finepitch[ch].detune = detune;
                    ch_finepitch[ch].inst = (cell.note != 0 && cell.note != 255) ? cell.inst : ch_arp[ch].inst;
                    ch_finepitch[ch].vol = (cell.note != 0 && cell.note != 255) ? cell.vol : ch_arp[ch].vol;
                    
                    // Apply fine pitch immediately
                    // Note: OPL2 doesn't support fine tuning easily, so we approximate
                    // by adjusting the F-number slightly (not implemented in OPL_SetPitch currently)
                    // For now, just set the note normally
                    OPL_NoteOff(ch);
                    OPL_SetPatch(ch, &gm_bank[ch_finepitch[ch].inst]);
                    OPL_SetVolume(ch, ch_finepitch[ch].vol << 1);
                    OPL_NoteOn(ch, ch_finepitch[ch].base_note);
                    ch_peaks[ch] = ch_finepitch[ch].vol;
                } else if (eff == 0xF000 || (cell.note != 0 && cmd == 0)) {
                    ch_arp[ch].active = false;
                    ch_porta[ch].active = false;
                    ch_volslide[ch].active = false;
                    ch_notecut[ch].active = false;
                    ch_notedelay[ch].active = false;
                    ch_retrigger[ch].active = false;
                    ch_tremolo[ch].active = false;
                    ch_finepitch[ch].active = false;
                    
                    // Deactivate vibrato - don't reset pitch here because
                    // a new note trigger will follow and set its own pitch
                    ch_vibrato[ch].active = false;
                }
                // Note: Removed the "else if (cmd == 0 && eff == 0x0000)" handler
                // Empty rows should NOT reset vibrato pitch - let it oscillate freely
                last_effect[ch] = cell.effect; // Update shadow
            }

            // --- 2. TRIGGER NOTE WITH OFFSET ---
            if (cell.note != 0) {
                // Deactivate effects when new note + no effect command (cmd=0)
                // This handles the case where effect column is 0000 but last_effect was also 0000
                // (so effect parsing block was skipped)
                uint8_t cmd = (cell.effect >> 12) & 0x0F;
                if (cmd == 0 && cell.note != 255) {
                    ch_tremolo[ch].active = false;
                    ch_retrigger[ch].active = false;
                    ch_vibrato[ch].active = false;
                }
                
                OPL_NoteOff(ch); 
                if (cell.note != 255) {
                    ch_arp[ch].base_note = cell.note;
                    ch_arp[ch].inst = cell.inst;
                    ch_arp[ch].vol  = cell.vol;
                    
                    // Initialize portamento state
                    ch_porta[ch].current_note = cell.note;
                    ch_porta[ch].inst = cell.inst;
                    ch_porta[ch].vol = cell.vol;
                    
                    // If we just triggered a new note, we reset the phase 
                    // so the melody remains predictable/on-beat.
                    ch_arp[ch].phase_timer = 0;
                    ch_arp[ch].step_index = 0;
                    ch_arp[ch].just_triggered = true; // Prevent immediate re-trigger

                    // Calculate starting offset (Style 1 "Down" starts high!)
                    int16_t start_offset = 0;
                    if (ch_arp[ch].active) {
                        start_offset = get_arp_offset(ch_arp[ch].style, ch_arp[ch].depth, 0);
                    }

                    OPL_SetPatch(ch, &gm_bank[cell.inst]);
                    OPL_SetVolume(ch, cell.vol << 1); 
                    OPL_NoteOn(ch, cell.note + start_offset);
                    ch_peaks[ch] = cell.vol;
                }
            }
        }

        // --- Advance Row / UI Pointers ---
        uint8_t old_row = cur_row;
        bool pattern_changed = false;
        if (cur_row < 31) {
            cur_row++;
        } else {
            cur_row = 0;
            if (is_song_mode) {
                cur_order_idx++;
                if (cur_order_idx >= song_length) cur_order_idx = 0;
                cur_pattern = read_order_xram(cur_order_idx);
                render_grid(); 
                pattern_changed = true;
            }
        }
        update_cursor_visuals(old_row, cur_row, cur_channel, cur_channel);
        if (pattern_changed || cur_row == 0) update_dashboard();
    }

    // --- PHASE B: PER-VSYNC TICK ---
    for (uint8_t ch = 0; ch < 9; ch++) {
        process_arp_logic(ch);
        process_portamento_logic(ch);
        process_volume_slide_logic(ch);
        process_vibrato_logic(ch);
        process_notecut_logic(ch);
        process_notedelay_logic(ch);
        process_retrigger_logic(ch);
        process_tremolo_logic(ch);
        process_finepitch_logic(ch);
    }
}

void handle_transport_controls() {
    // F6: Play / Pause
    if (key_pressed(KEY_F6)) {
        seq.is_playing = !seq.is_playing;
        
        if (seq.is_playing) {
            seq.tick_counter = seq.ticks_per_row; 

            // --- THE FIX ---
            if (is_song_mode) {
                // Sync to the song structure only if we are in SONG mode
                cur_pattern = read_order_xram(cur_order_idx);
                render_grid(); 
            } 
            // If is_song_mode is false, we don't touch cur_pattern.
            // It stays on the pattern you were manually editing.
            
            // ... Panic/Mute logic ...
        }
        update_dashboard();
    }

    // F7: Stop & Reset
    if (key_pressed(KEY_F7)) {
        seq.is_playing = false;
        
        // Silence all channels
        for (uint8_t i = 0; i < 9; i++) {
            OPL_NoteOff(i);
            // ch_peaks[i] = 0; // Clear peak
        }
        
        for (int i=0; i<9; i++) {
            last_effect[i] = 0xFFFF;
            ch_arp[i].active = false;
            ch_porta[i].active = false;
            ch_volslide[i].active = false;
            ch_vibrato[i].active = false;
            ch_notecut[i].active = false;
            ch_notedelay[i].active = false;
            ch_retrigger[i].active = false;
            ch_tremolo[i].active = false;
            ch_finepitch[i].active = false;
        }

        // Reset to beginning
        uint8_t old = cur_row;
        cur_row = 0;
        seq.tick_counter = 0;
        
        update_cursor_visuals(old, 0, cur_channel, cur_channel);
        update_dashboard();
    }
}

void handle_editing(void) {
    if (!edit_mode) return;

    // Check for Backspace or Delete key
    if (key_pressed(KEY_BACKSPACE) || key_pressed(KEY_DELETE)) {
        
        // 1. Create an empty cell (all zeros)
        PatternCell empty = {0, 0, 0, 0};
        
        // 2. Write to XRAM at current cursor position
        write_cell(cur_pattern, cur_row, cur_channel, &empty);
        
        // 3. Redraw the row to reflect the empty cell
        render_row(cur_row);
        
        // 4. (Standard Tracker Behavior) Auto-advance to the next row
        // if (cur_row < 31) {
        //     uint8_t old_row = cur_row;
        //     cur_row--;
        //     update_cursor_visuals(old_row, cur_row, cur_channel, cur_channel);
        // }
        update_cursor_visuals(cur_row, cur_row, cur_channel, cur_channel);
        
        printf("Cell Cleared at Row %d\n", cur_row - 1);
    }

    if (key_pressed(KEY_GRAVE)) {
        PatternCell off = {255, current_instrument, 0, 0};
        write_cell(cur_pattern, cur_row, cur_channel, &off);
        render_row(cur_row);
        if (cur_row < 31) cur_row++; 
    }
}

void modify_volume_effects(int8_t delta) {
    if (effect_view_mode) {
        // --- 16-BIT EFFECT EDITING (High Byte) ---
        PatternCell cell;
        read_cell(cur_pattern, cur_row, cur_channel, &cell);

        if (is_shift_down()) {
            // Highest Nibble (Command): 0x1000
            // Example: 0100 -> 1100
            cell.effect += (delta * 0x1000);
        } 
        else {
            // Second Nibble (Style): 0x0100
            // Example: 0100 -> 0200
            cell.effect += (delta * 0x0100);
        }

        write_cell(cur_pattern, cur_row, cur_channel, &cell);
        render_row(cur_row);
        update_cursor_visuals(cur_row, cur_row, cur_channel, cur_channel);
    } 
    else {
        // --- VOLUME EDITING ---
        if (is_shift_down()) {
            // In-place Cell Volume Edit
            PatternCell cell;
            read_cell(cur_pattern, cur_row, cur_channel, &cell);
            
            int16_t v = (int16_t)cell.vol + delta;
            if (v > 63) v = 63; if (v < 0) v = 0;
            cell.vol = (uint8_t)v;
            
            write_cell(cur_pattern, cur_row, cur_channel, &cell);
            render_row(cur_row);
            update_cursor_visuals(cur_row, cur_row, cur_channel, cur_channel);
            
            // Live Preview
            OPL_SetVolume(cur_channel, cell.vol << 1);
        } 
        else {
            // Global Brush Volume Edit
            int16_t v = (int16_t)current_volume + delta;
            if (v > 63) v = 63; if (v < 0) v = 0;
            current_volume = (uint8_t)v;
            update_dashboard();
        }
    }
}

void modify_effect_low_byte(int8_t delta) {
    PatternCell cell;
    read_cell(cur_pattern, cur_row, cur_channel, &cell);

    // Apply delta to the bottom 8 bits (0x00FF)
    // Shift increases the step to help scroll through 00-FF faster
    int16_t step = is_shift_down() ? (delta * 16) : delta;
    
    uint8_t lo = (uint8_t)(cell.effect & 0xFF);
    lo += (uint8_t)step;
    
    cell.effect = (cell.effect & 0xFF00) | lo;

    write_cell(cur_pattern, cur_row, cur_channel, &cell);
    render_row(cur_row);
    update_cursor_visuals(cur_row, cur_row, cur_channel, cur_channel);
}

void modify_instrument(int8_t delta) {
    // Exit effect view mode so we can see the instrument column
    if (effect_view_mode) {
        effect_view_mode = false;
        draw_headers();
        render_grid();
    }
    
    if (is_shift_down()) {
        // --- IN-PLACE CELL EDIT ONLY ---
        PatternCell cell;
        read_cell(cur_pattern, cur_row, cur_channel, &cell);
        
        // 8-bit wrap-around logic
        cell.inst = (uint8_t)(cell.inst + delta);
        
        write_cell(cur_pattern, cur_row, cur_channel, &cell);
        render_row(cur_row);
        
        // Live Preview: Update OPL2 patch immediately
        OPL_SetPatch(cur_channel, &gm_bank[cell.inst]);
    } 
    else {
        // --- GLOBAL BRUSH EDIT ONLY ---
        current_instrument = (uint8_t)(current_instrument + delta);
        update_dashboard(); // Update the "INS" hex at the top
    }
    update_cursor_visuals(cur_row, cur_row, cur_channel, cur_channel);
}

void modify_note(int8_t delta) {
    // We only perform local cell edits if we are in Edit Mode
    if (!edit_mode) return;

    PatternCell cell;
    read_cell(cur_pattern, cur_row, cur_channel, &cell);
    
    // Only transpose if there is an actual note (not empty/Note-Off)
    if (cell.note > 0 && cell.note < 255) {
        
        // If Shift is held, move by 12 semitones (1 Octave)
        // Otherwise, move by 1 semitone
        int8_t amount = is_shift_down() ? (delta * 12) : delta;
        
        int16_t new_note = (int16_t)cell.note + amount;
        
        // Clamp to MIDI/OPL2 range (C-0 to B-8)
        if (new_note < 12)  new_note = 12;
        if (new_note > 119) new_note = 119;
        
        cell.note = (uint8_t)new_note;
        
        // 1. Save to XRAM
        write_cell(cur_pattern, cur_row, cur_channel, &cell);
        
        // 2. Redraw the grid
        render_row(cur_row); 
        
        // 3. Live Preview: Play the "nudged" note
        OPL_NoteOff(cur_channel);
        // ch_peaks[cur_channel] = 0; // Clear peak
        OPL_SetPatch(cur_channel, &gm_bank[cell.inst]);
        OPL_SetVolume(cur_channel, cell.vol << 1);
        OPL_NoteOn(cur_channel, cell.note);
        ch_peaks[cur_channel] = cell.vol; // Set peak
    }
}

void change_pattern(int8_t delta) {
    uint8_t old_pat = cur_pattern;

    // 1. Calculate new pattern with wrapping (0-15)
    int16_t new_pat = (int16_t)cur_pattern + delta;
    if (new_pat < 0) new_pat = MAX_PATTERNS - 1;
    if (new_pat >= MAX_PATTERNS) new_pat = 0;
    
    cur_pattern = (uint8_t)new_pat;

    // 2. If the pattern actually changed, refresh the whole screen
    if (cur_pattern != old_pat) {
        render_grid(); // Redraw all 32 rows for the new pattern
        update_dashboard(); // Update the "PAT: XX" display
        
        // Ensure the cursor highlight is still drawn on the current row
        update_cursor_visuals(cur_row, cur_row, cur_channel, cur_channel);
        
        printf("Switched to Pattern: %02X\n", cur_pattern);
    }
}

void handle_song_order_input() {
    // 1. Shift + F11/F12: Change Pattern ID in CURRENT Order Slot
    if (is_shift_down()) {
        uint8_t p = read_order_xram(cur_order_idx);
        if (key_pressed(KEY_F11)) {
            if (p > 0) write_order_xram(cur_order_idx, p - 1);
            else write_order_xram(cur_order_idx, MAX_PATTERNS - 1); // Wrap
            update_dashboard();
        }
        if (key_pressed(KEY_F12)) {
            if (p < MAX_PATTERNS - 1) write_order_xram(cur_order_idx, p + 1);
            else write_order_xram(cur_order_idx, 0); // Wrap
            update_dashboard();
        }
    }
    // 2. Alt + F11/F12: Change total SONG LENGTH
    else if (is_alt_down()) {
        if (key_pressed(KEY_F11)) {
            if (song_length > 1) song_length--;
            update_dashboard();
        }
        if (key_pressed(KEY_F12)) {
            if (song_length < MAX_ORDERS_USER) song_length++; // Warning fixed by uint16_t
            update_dashboard();
        }

    }
    // 3. Just F11/F12: Navigate the Order List (Jump through the song)
    else {
        if (key_pressed(KEY_F11)) {
            if (cur_order_idx > 0) cur_order_idx--;
            else cur_order_idx = song_length - 1; // Wrap
            
            if (is_song_mode) {
                cur_pattern = read_order_xram(cur_order_idx);
                render_grid();
            }
            update_dashboard();
        }
        if (key_pressed(KEY_F12)) {
            if (cur_order_idx < song_length - 1) cur_order_idx++;
            else cur_order_idx = 0; // Wrap
            
            if (is_song_mode) {
                cur_pattern = read_order_xram(cur_order_idx);
                render_grid();
            }
            update_dashboard();
        }
    }
}

void pattern_copy(uint8_t pat_idx) {
    uint16_t start_addr = get_pattern_xram_addr(pat_idx, 0, 0);
    uint16_t data_found = 0;

    RIA.addr0 = start_addr;
    RIA.step0 = 1;

    for (uint16_t i = 0; i < PATTERN_SIZE; i++) {
        uint8_t b = RIA.rw0;
        pattern_clipboard[i] = b;
        if (b != 0) data_found++;
    }
    printf("Pattern %02X Copied (%u bytes)\n", pat_idx, data_found);
}

void pattern_paste(uint8_t pat_idx) {
    uint16_t start_addr = get_pattern_xram_addr(pat_idx, 0, 0);

    RIA.addr0 = start_addr;
    RIA.step0 = 1;

    for (uint16_t i = 0; i < PATTERN_SIZE; i++) {
        RIA.rw0 = pattern_clipboard[i];
    }
    
    // Force the current view to sync if we pasted into the active pattern
    if (pat_idx == cur_pattern) {
        render_grid();
        update_cursor_visuals(cur_row, cur_row, cur_channel, cur_channel);
    }
    printf("Pattern %02X Pasted.\n", pat_idx);
}

void OPL_Panic(void) {
    for (uint8_t i = 0; i < 9; i++) {
        // Clear bit 5 (Key-On) for all 9 channels
        // We write 0x00 to registers $B0 through $B8
        OPL_Write(0xB0 + i, 0x00);
    }
    
    // Reset our "keyboard memory" so the next piano press works cleanly
    active_midi_note = 0; 
}

void modify_effect(int8_t delta) {
    PatternCell cell;
    read_cell(cur_pattern, cur_row, cur_channel, &cell);

    if (is_shift_down()) {
        // COARSE: Change the High Byte (Command/Sub-type)
        // This makes it easy to jump between 1x, 2x, etc.
        uint8_t hi = (uint8_t)(cell.effect >> 8);
        hi += delta;
        cell.effect = (uint16_t)(hi << 8) | (cell.effect & 0x00FF);
    } else {
        // FINE: Change the Low Byte (Parameter values)
        uint8_t lo = (uint8_t)(cell.effect & 0xFF);
        lo += delta;
        cell.effect = (cell.effect & 0xFF00) | lo;
    }

    write_cell(cur_pattern, cur_row, cur_channel, &cell);
    render_row(cur_row);
}