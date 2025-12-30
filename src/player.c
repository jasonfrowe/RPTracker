#include "player.h"
#include "opl.h"
#include "input.h"
#include "usb_hid_keys.h"
#include <stdio.h>
#include "screen.h"
#include "instruments.h"
#include "song.h"

// Current State
uint8_t current_instrument = 0; // Instrument index (0 = Piano)
uint8_t current_octave = 3; // Adjusts in jumps of 12
uint8_t active_midi_note = 0;      // Tracks the currently playing note
uint8_t current_volume = 63; // Max volume (0x3F)

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

    // 1. Scan for piano keys
    for (int k = 0; k < 256; k++) {
        if (key(k)) {
            int8_t s = get_semitone(k);
            if (s != -1) {
                semitone = s;
                target_note = (current_octave + 1) * 12 + semitone;
                note_pressed_this_frame = true;
                break; 
            }
        }
    }

    // 2. Logic: Note On & Recording
    if (note_pressed_this_frame) {
        if (target_note != active_midi_note) {
            // Live Overdrive: Cut the sequencer's note and play the keyboard note
            OPL_NoteOff(channel); 
            OPL_SetPatch(channel, &gm_bank[current_instrument]);
            OPL_SetVolume(channel, current_volume << 1); 
            OPL_NoteOn(channel, target_note);
            active_midi_note = target_note;

            if (edit_mode) {
                PatternCell c = {target_note, current_instrument, current_volume, 0};
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
    else {
        if (active_midi_note != 0) {
            OPL_NoteOff(channel);
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

    // Volume: [ and ] (with Shift detection inside modify_volume)
    if (key_pressed(KEY_LEFTBRACE))  modify_volume(-1);
    if (key_pressed(KEY_RIGHTBRACE)) modify_volume(1);
    
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
        uint8_t old_row = cur_row;

        // if (cur_row < 31) cur_row++; else cur_row = 0;
        if (cur_row < 31) {
            cur_row++;
        } else {
            // END OF PATTERN: Move to next step in the playlist
            cur_row = 0;
            
            if (is_song_mode) {
            cur_order_idx++;

                // Loop the song if we hit the end of the defined length
                if (cur_order_idx >= song_length) {
                    cur_order_idx = 0;
                }

                // Get the pattern ID for this new step
                cur_pattern = read_order_xram(cur_order_idx);

                // Refresh the whole grid for the new pattern
                render_grid();
                // update_dashboard();
            }
            
        }

        update_cursor_visuals(old_row, cur_row, cur_channel, cur_channel);
        if (cur_row == 0) update_dashboard(); // Refresh if pattern or order changed

        for (uint8_t ch = 0; ch < 9; ch++) {
            // Priority: If the user is jamming on this channel, don't play sequenced note
            if (ch == cur_channel && active_midi_note != 0) continue;

            PatternCell cell;
            read_cell(cur_pattern, cur_row, ch, &cell);
            
            if (cell.note != 0) {
                OPL_NoteOff(ch); 
                if (cell.note != 255) {
                    OPL_SetPatch(ch, &gm_bank[cell.inst]);
                    OPL_SetVolume(ch, cell.vol << 1); 
                    OPL_NoteOn(ch, cell.note);
                }
            }
        }
        update_cursor_visuals(old_row, cur_row, cur_channel, cur_channel);
    }
}

void handle_transport_controls() {
    // F5: Play / Pause
    if (key_pressed(KEY_F6)) {
        seq.is_playing = !seq.is_playing;
        seq.tick_counter = seq.ticks_per_row; // Trigger first row immediately
        update_dashboard(); // Show Play/Pause status
    }

    // F6: Stop & Reset
    if (key_pressed(KEY_F7)) {
        seq.is_playing = false;
        
        // Silence all channels
        for (uint8_t i = 0; i < 9; i++) OPL_NoteOff(i);
        
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
        if (cur_row < 31) {
            uint8_t old_row = cur_row;
            cur_row++;
            update_cursor_visuals(old_row, cur_row, cur_channel, cur_channel);
        }
        
        printf("Cell Cleared at Row %d\n", cur_row - 1);
    }

    if (key_pressed(KEY_GRAVE)) {
        PatternCell off = {255, current_instrument, 0, 0};
        write_cell(cur_pattern, cur_row, cur_channel, &off);
        render_row(cur_row);
        if (cur_row < 31) cur_row++; 
    }
}

void modify_volume(int8_t delta) {
    if (is_shift_down()) {
        // --- IN-PLACE CELL EDIT ONLY ---
        PatternCell cell;
        read_cell(cur_pattern, cur_row, cur_channel, &cell);
        
        // Apply delta to cell volume (bound 0-63)
        int16_t new_vol = (int16_t)cell.vol + delta;
        if (new_vol > 63) new_vol = 63;
        if (new_vol < 0)  new_vol = 0;
        cell.vol = (uint8_t)new_vol;
        
        write_cell(cur_pattern, cur_row, cur_channel, &cell);
        render_row(cur_row); // Update the grid color immediately
        
        // Live Preview: Update the OPL2 hardware so you hear the change
        OPL_SetVolume(cur_channel, cell.vol << 1);
    } 
    else {
        // --- GLOBAL BRUSH EDIT ONLY ---
        int16_t new_brush_vol = (int16_t)current_volume + delta;
        if (new_brush_vol > 63) new_brush_vol = 63;
        if (new_brush_vol < 0)  new_brush_vol = 0;
        current_volume = (uint8_t)new_brush_vol;
        
        update_dashboard(); // Update the "VOL" hex at the top
    }
}

void modify_instrument(int8_t delta) {
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
        OPL_SetPatch(cur_channel, &gm_bank[cell.inst]);
        OPL_SetVolume(cur_channel, cell.vol << 1);
        OPL_NoteOn(cur_channel, cell.note);
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
            if (song_length < MAX_ORDERS) song_length++; // Warning fixed by uint16_t
            update_dashboard();
        }
    }
    // 3. Just F11/F12: Navigate the Order List (Jump through the song)
    else {
        if (key_pressed(KEY_F11)) {
            if (cur_order_idx > 0) cur_order_idx--;
            else cur_order_idx = song_length - 1; // Wrap
            
            cur_pattern = read_order_xram(cur_order_idx);
            render_grid();
            update_dashboard();
        }
        if (key_pressed(KEY_F12)) {
            if (cur_order_idx < song_length - 1) cur_order_idx++;
            else cur_order_idx = 0; // Wrap
            
            cur_pattern = read_order_xram(cur_order_idx);
            render_grid();
            update_dashboard();
        }
    }
}