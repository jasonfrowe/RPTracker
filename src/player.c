#include <rp6502.h>
#include "player.h"
#include "opl.h"
#include "input.h"
#include "usb_hid_keys.h"
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include "screen.h"
#include "instruments.h"
#include "song.h"
#include "effects.h"


// Unity (1.0) is 256. 
// This scales LFO speeds relative to our 150 BPM baseline.
uint16_t lfo_tempo_scaler = 256;

void update_lfo_scaler(void) {
    // Math: (Baseline_FP << 8) / Current_FP
    // 1536 << 8 = 393216
    if (seq.ticks_per_row_fp > 0) {
        lfo_tempo_scaler = (uint16_t)(393216L / seq.ticks_per_row_fp);
    }
}

// UI Toggle: false = Volume/Instrument, true = 16-bit Effect
bool effect_view_mode = false;
uint16_t last_effect[9] = {0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF};

// Playback Options
bool is_follow_mode = true;
uint8_t play_row = 0; // The actual row being played by the engine

// Current State
uint8_t current_instrument = 0; // Instrument index (0 = Piano)
uint8_t current_octave = 3; // Adjusts in jumps of 12
uint8_t active_midi_note = 0;      // Tracks the currently playing note
uint8_t current_volume = 63; // Max volume (0x3F)

// Export State (additional to those in opl.c)
static char export_filename[16] = {0};
static int export_fd = -1;
static uint32_t export_total_bytes = 0;
static bool export_song_ended = false;

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

// Initialize: 150 BPM = 6.0 ticks/row in 8.8 fixed-point = 0x0600 (1536)
SequencerState seq = {false, 0x0600, 0, 150};

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

// ============================================================================
// TEMPO / BPM FUNCTIONS
// ============================================================================

// Convert BPM to 8.8 fixed-point ticks_per_row
// Formula: frames_per_row = 3600 frames/min / (BPM * 4 rows/beat)
//        = 900 / BPM (in 8.8 fixed point: * 256 = 230400 / BPM)
uint16_t bpm_to_ticks_fp(uint8_t bpm) {
    if (bpm < 60) bpm = 60;
    if (bpm > 240) bpm = 240;
    
    // Use 32-bit math to avoid overflow: (900 * 256) / bpm
    uint32_t ticks = ((uint32_t)230400) / bpm;
    return (uint16_t)ticks;

    // Update LFO scaler
    update_lfo_scaler();
}

// ============================================================================
// EXPORT FUNCTIONS
// ============================================================================

static void flush_export_buffer(void) {
    if (export_idx == 0) return; // Nothing to flush
    
    // Write current buffer to disk
    write_xram(EXPORT_BUF_XRAM, export_idx, export_fd);
    export_total_bytes += export_idx;
    
    // Reset buffer pointer
    export_idx = 0;
}

static void derive_export_filename(void) {
    // Start with the active tracker filename
    if (active_filename[0] == '\0') {
        // No filename set, use default
        strcpy(export_filename, "UNTITLED.BIN");
        return;
    }
    
    // Copy filename and replace extension
    strcpy(export_filename, active_filename);
    
    // Find the dot or end of string
    char *dot = strchr(export_filename, '.');
    if (dot) {
        strcpy(dot, ".BIN");
    } else {
        strcat(export_filename, ".BIN");
    }
}

static void start_export(void) {
    printf("Starting export...\n");

    OPL_Init(); // Reset OPL state
    
    // Derive filename
    derive_export_filename();
    printf("Export to: %s\n", export_filename);
    
    // Open file for writing
    export_fd = open(export_filename, O_WRONLY | O_CREAT | O_TRUNC);
    if (export_fd < 0) {
        printf("Error: Cannot create export file\n");
        return;
    }
    
    // Initialize export state
    is_exporting = true;
    export_idx = 0;
    accumulated_delay = 0;
    export_total_bytes = 0;
    export_song_ended = false;
    
    // Force song mode and reset to beginning
    is_song_mode = true;
    cur_order_idx = 0;
    play_row = 0;
    seq.is_playing = true;
    // Set to ticks_per_row_fp so first sequencer_step() processes row 0 immediately
    // (matches behavior of pressing Enter to start playback)
    seq.tick_counter_fp = seq.ticks_per_row_fp;
    
    // Clear all effect states
    for (int i = 0; i < 9; i++) {
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
        ch_generator[i].active = false;
    }
    
    // Load first pattern
    cur_pattern = read_order_xram(cur_order_idx);
    
    printf("Exporting song...\n");
}

static void finish_export(void) {
    // Write end marker: 0xFF, 0xFF, 0x00, 0x00
    RIA.addr0 = EXPORT_BUF_XRAM + export_idx;
    RIA.step0 = 1;
    RIA.rw0 = 0xFF;
    RIA.rw0 = 0xFF;
    RIA.rw0 = 0x00;
    RIA.rw0 = 0x00;
    export_idx += 4;
    
    // Flush remaining data
    flush_export_buffer();
    
    // Pad to 512-byte boundary
    uint16_t remainder = export_total_bytes % 512;
    if (remainder != 0) {
        uint16_t padding = 512 - remainder;
        
        // Fill buffer with zeros
        RIA.addr0 = EXPORT_BUF_XRAM;
        RIA.step0 = 1;
        for (uint16_t i = 0; i < padding; i++) {
            RIA.rw0 = 0x00;
        }
        
        // Write padding
        write_xram(EXPORT_BUF_XRAM, padding, export_fd);
        export_total_bytes += padding;
    }
    
    // Close file
    close(export_fd);
    export_fd = -1;
    
    // Reset export state
    is_exporting = false;
    seq.is_playing = false;
    
    printf("Export complete: %lu bytes\n", (unsigned long)export_total_bytes);
    printf("File: %s\n", export_filename);
}

static void export_loop(void) {
    // Track starting order to detect loop
    uint8_t start_order = cur_order_idx;
    bool seen_end = false;
    
    // Run sequencer until song ends
    while (is_exporting) {
        // Increment delay counter each frame
        accumulated_delay++;
        
        // Run sequencer step
        sequencer_step();
        
        // Check if buffer is getting full (leave room for end marker)
        if (export_idx >= (EXPORT_CHUNK - 8)) {
            flush_export_buffer();
        }
        
        // Detect song end: when cur_order_idx wraps back to 0 after reaching song_length
        if (cur_order_idx >= song_length - 1) {
            seen_end = true;
        }
        
        if (seen_end && cur_order_idx == 0 && play_row == 0) {
            // Song has looped
            finish_export();
            break;
        }
        
        // Safety: prevent infinite loop (max ~10 minutes at 60fps)
        if (export_total_bytes > 36000) {
            printf("Warning: Export size limit reached\n");
            finish_export();
            break;
        }
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
        if (key_pressed(KEY_E)) {
            // Start binary export
            start_export();
            export_loop();
            return;
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
    // Only turn off OPL notes via keyboard when sequencer is NOT playing
    // But always reset active_midi_note so same note can be re-entered
    else {
        if (active_midi_note != 0) {
            if (!seq.is_playing) {
                OPL_NoteOff(channel);
            }
            // ch_peaks[channel] = 0; // Clear peak
            active_midi_note = 0;
        }
    }

    // 4. Function Keys (Octave & Instrument)
    if (key_pressed(KEY_F1)) { if (current_octave > 0) current_octave--; update_dashboard(); }
    if (key_pressed(KEY_F2)) { if (current_octave < 8) current_octave++; update_dashboard(); }
    
    // F7 / Shift-F7: BPM Control (60-240 BPM range)
    if (key_pressed(KEY_F7)) {
        if (is_shift_down()) {
            // Shift-F7: Decrease BPM
            if (seq.bpm > 60) {
                seq.bpm--;
                seq.ticks_per_row_fp = bpm_to_ticks_fp(seq.bpm);
                update_dashboard();
                // draw_status_message("Tempo Changed");
            }
        } else {
            // F7: Increase BPM
            if (seq.bpm < 240) {
                seq.bpm++;
                seq.ticks_per_row_fp = bpm_to_ticks_fp(seq.bpm);
                update_dashboard();
                // draw_status_message("Tempo Changed");
            }
        }
    }

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
        mark_playhead(play_row);
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
        mark_playhead(play_row);
        update_dashboard();
    }

}

void sequencer_step(void) {
    if (!seq.is_playing) return;
    
    // Increment by 1.0 tick in 8.8 fixed-point (256 = 1.0)
    seq.tick_counter_fp += TICK_SCALE;

    // Check if we've accumulated enough ticks for a new row
    if (seq.tick_counter_fp >= seq.ticks_per_row_fp) {
        // Subtract (preserving fractional remainder for smooth timing)
        seq.tick_counter_fp -= seq.ticks_per_row_fp;


        for (uint8_t ch = 0; ch < 9; ch++) {
            if (ch == cur_channel && active_midi_note != 0) continue;

            PatternCell cell;
            read_cell(cur_pattern, play_row, ch, &cell);

            // Reset just_triggered flags for effects that need it
            bool fine_pitch_triggered = false;
            
            // --- 1. IDEMPOTENT EFFECT PARSING ---
            if (cell.effect != last_effect[ch]) {
                uint16_t eff = cell.effect;
                uint8_t cmd = (eff >> 12) & 0x0F;

                if (cmd == 1) { 
                    ch_arp[ch].active = true;
                    ch_arp[ch].style  = (eff >> 8) & 0x0F;
                    ch_arp[ch].depth  = (eff >> 4) & 0x0F;
                    ch_arp[ch].speed_idx = (eff & 0x0F);
                    
                    // --- SCALE ARP TO TEMPO ---
                    // base_frames is frames @ 150BPM (6 frames per row)
                    uint16_t base_frames = arp_tick_lut[ch_arp[ch].speed_idx];
                    
                    // We calculate the target in fixed point:
                    // target = base_frames * (current_row_duration / 6)
                    // (seq.ticks_per_row_fp / 6) is the duration of one "step" in the current tempo
                    ch_arp[ch].target_ticks_fp = (uint16_t)(((uint32_t)base_frames * seq.ticks_per_row_fp) / 6);

                    ch_arp[ch].phase_timer_fp = 0;
                    ch_arp[ch].step_index = 0;
                    ch_arp[ch].just_triggered = true; // Prevent double-trigger on same row
                } else if (cmd == 2) {
                    // Portamento: 2SDT
                    uint8_t mode = (eff >> 8) & 0x0F;
                    uint8_t speed = (eff >> 4) & 0x0F;
                    uint8_t t_val = (eff & 0x0F);

                    // Starting Note: If there's a new note on this row, start from it.
                    // Otherwise, start from whatever the channel was last playing.
                    uint8_t start_note = (cell.note != 0 && cell.note != 255) ? cell.note : ch_arp[ch].base_note;
                    
                    ch_porta[ch].active = true;
                    ch_porta[ch].current_note = start_note;
                    ch_porta[ch].mode = mode;
                    ch_porta[ch].speed = (speed == 0) ? 1 : speed;
                    ch_porta[ch].tick_counter = 0;
                    ch_porta[ch].vol = (cell.note != 0) ? cell.vol : ch_arp[ch].vol;
                    ch_porta[ch].inst = (cell.note != 0) ? cell.inst : ch_arp[ch].inst;

                    // Calculate Target
                    switch (mode) {
                        case 0: ch_porta[ch].target_note = 127; break; // Continuous Up
                        case 1: ch_porta[ch].target_note = 0;   break; // Continuous Down
                        case 2: // Up Relative
                            {
                                uint16_t t = (uint16_t)start_note + (t_val == 0 ? 12 : t_val);
                                ch_porta[ch].target_note = (t > 127) ? 127 : (uint8_t)t;
                            }
                            break;
                        case 3: // Down Relative
                            {
                                int16_t t = (int16_t)start_note - (t_val == 0 ? 12 : t_val);
                                ch_porta[ch].target_note = (t < 0) ? 0 : (uint8_t)t;
                            }
                            break;
                    }
                    
                    // Kill Arp so they don't fight over the pitch
                    ch_arp[ch].active = false; 
                } else if (cmd == 3) {
                    // Volume Slide: 3SDT
                    // S = Mode (0=Up, 1=Down, 2=To Target)
                    // D = Speed (volume units per tick)
                    // T = Target volume (0-F represents 0-63 scaled)
                    
                    uint16_t eff = cell.effect;
                    uint8_t s_nibble = (eff >> 8) & 0x0F; // Mode
                    uint8_t d_nibble = (eff >> 4) & 0x0F; // Speed (1-F)
                    uint8_t t_nibble = (eff & 0x0F);      // Target (0-F)

                    ch_volslide[ch].active = true;
                    ch_volslide[ch].mode = s_nibble;

                    // 1. Start from current row's volume (0-63)
                    ch_volslide[ch].vol_accum = (uint16_t)cell.vol << 8;

                    // 2. Scale 0-F target to 0-63
                    ch_volslide[ch].target_vol = (t_nibble * 63) / 15;

                    // 3. Set Speed: 84 is the "Magic Number" for ~32 rows at Speed 1
                    if (d_nibble == 0) d_nibble = 1;
                    ch_volslide[ch].speed_fp = (uint16_t)d_nibble * 84U;

                    // 4. Default targets for Mode 0 (Up) and 1 (Down) if T is 0
                    if (s_nibble == 0 && t_nibble == 0) ch_volslide[ch].target_vol = 63;
                    if (s_nibble == 1 && t_nibble == 0) ch_volslide[ch].target_vol = 0;
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
                    uint8_t base_cut_ticks = (eff & 0x0F);
                    if (base_cut_ticks == 0) base_cut_ticks = 1;

                    // Scale: (base * ticks_per_row_fp) / 1536
                    uint32_t scaled = ((uint32_t)base_cut_ticks * seq.ticks_per_row_fp) / 1536;
                    ch_notecut[ch].cut_tick = (uint8_t)scaled;
                    if (ch_notecut[ch].cut_tick == 0) ch_notecut[ch].cut_tick = 1;

                    ch_notecut[ch].active = true;
                    ch_notecut[ch].tick_counter = 0;
                } else if (cmd == 6) {
                    // Automatic Echo: 6VDT
                    uint8_t echo_vol_nibble = (eff >> 8) & 0x0F;
                    uint8_t delay_nibble     = (eff >> 4) & 0x0F;
                    uint8_t transposition   = (eff & 0x0F);
                    
                    uint8_t base = (cell.note != 0 && cell.note != 255) ? cell.note : ch_arp[ch].base_note;

                    ch_notedelay[ch].active = true;
                    ch_notedelay[ch].timer_fp = 0;
                    
                    // --- THE TEMPO SCALE FIX ---
                    // One logical "tick" duration in the current tempo is (ticks_per_row_fp / 6)
                    if (delay_nibble == 0) delay_nibble = 3; // Default to half row
                    uint32_t one_tick_duration = seq.ticks_per_row_fp / 6;
                    ch_notedelay[ch].target_fp = (uint16_t)(one_tick_duration * delay_nibble);
                    
                    // Set note, inst, and starting volume
                    ch_notedelay[ch].note = base + transposition;
                    if (ch_notedelay[ch].note > 127) ch_notedelay[ch].note = 127;
                    ch_notedelay[ch].vol = (echo_vol_nibble * 63) / 15;
                    ch_notedelay[ch].inst = (cell.note != 0) ? cell.inst : ch_arp[ch].inst;

                    // Note: We do NOT set skip_note_trigger. 
                    // The note in cell.note will play normally on Tick 0.
                } else if (cmd == 7) {
                    // Retrigger: 7__T
                    uint8_t speed = (eff & 0x0F);
                    if (speed == 0) speed = 3; // Default
                    
                    uint8_t note = (cell.note != 0 && cell.note != 255) ? cell.note : ch_arp[ch].base_note;
                    
                    ch_retrigger[ch].active = true;
                    ch_retrigger[ch].speed = speed;
                    ch_retrigger[ch].note = note;
                    ch_retrigger[ch].inst = (cell.note != 0 && cell.note != 255) ? cell.inst : ch_arp[ch].inst;
                    ch_retrigger[ch].vol  = (cell.note != 0 && cell.note != 255) ? cell.vol : ch_arp[ch].vol;
                    
                    // --- THE FIX: SCALE TO TEMPO ---
                    // At 150 BPM, one musical tick = 256 (TICK_SCALE).
                    // Formula: (Current Row Duration in FP / 6) * speed
                    uint32_t one_tick_fp = (seq.ticks_per_row_fp / 6);
                    ch_retrigger[ch].target_fp = (uint16_t)(one_tick_fp * speed);
                    
                    ch_retrigger[ch].timer_fp = 0;
                    ch_retrigger[ch].just_triggered = true; // Sync with sequencer strike
                } else if (cmd == 8) {
                    // Tremolo: 8RDT
                    uint8_t rate = (eff >> 8) & 0x0F;
                    uint8_t depth = (eff >> 4) & 0x0F;
                    uint8_t wave = (eff & 0x0F);

                    // Sync base state
                    ch_tremolo[ch].active = true;
                    ch_tremolo[ch].rate = rate;
                    ch_tremolo[ch].depth = depth;
                    ch_tremolo[ch].waveform = wave;
                    
                    // Anchor the oscillation to the volume on this row
                    ch_tremolo[ch].base_vol = (cell.note != 0) ? cell.vol : ch_volslide[ch].current_vol;
                    
                    // Optional: Reset phase on new note to make the pulse predictable
                    if (cell.note != 0) {
                        ch_tremolo[ch].phase = 0;
                    }
                } else if (cmd == 9) {
                    // Fine Pitch: 9_DD (8-bit signed)
                    // Interprets the lower 8 bits as a signed value (-128 to 127)
                    // representing steps of 1/32 semitone.
                    int8_t detune = (int8_t)(eff & 0xFF);
                    
                    // Deciding the note to play:
                    uint8_t note = (cell.note != 0 && cell.note != 255) ? cell.note : ch_arp[ch].base_note;
                    
                    ch_finepitch[ch].active = true;
                    ch_finepitch[ch].base_note = note;
                    ch_finepitch[ch].detune = detune;
                    ch_finepitch[ch].inst = (cell.note != 0 && cell.note != 255) ? cell.inst : ch_arp[ch].inst;
                    ch_finepitch[ch].vol = (cell.note != 0 && cell.note != 255) ? cell.vol : ch_arp[ch].vol;
                    
                    // Strike the detuned note now
                    OPL_NoteOff(ch);
                    OPL_SetPatch(ch, &gm_bank[ch_finepitch[ch].inst]);
                    OPL_SetVolume(ch, ch_finepitch[ch].vol << 1); 
                    
                    // CALL THE DETUNED FUNCTION
                    OPL_NoteOn_Detuned(ch, note, detune);
                    
                    ch_peaks[ch] = ch_finepitch[ch].vol;

                    // --- THE FIX: Mark this as handled! ---
                    fine_pitch_triggered = true; 
                } else if (cmd == 0x0A) { // Random Generator: A S D T
                    ch_generator[ch].active = true;
                    ch_generator[ch].scale  = (eff >> 8) & 0x0F;
                    ch_generator[ch].range  = (eff >> 4) & 0x0F;
                    
                    // Scale generator timing with tempo (same as arpeggio)
                    uint16_t base_frames = arp_tick_lut[eff & 0x0F];
                    uint32_t scaled = ((uint32_t)base_frames * seq.ticks_per_row_fp) / 1536;
                    ch_generator[ch].target_ticks = (uint8_t)scaled;
                    if (ch_generator[ch].target_ticks == 0) ch_generator[ch].target_ticks = 1;
                    
                    ch_generator[ch].timer = 0;
                    ch_generator[ch].just_triggered = true;

                    // --- THE FIX: CAPTURE CONTEXT ---
                    // If there is a note on this row, use it. 
                    // Otherwise, fall back to the last known state for this channel.
                    if (cell.note != 0 && cell.note != 255) {
                        ch_generator[ch].base_note = cell.note;
                        ch_generator[ch].inst = cell.inst;
                        ch_generator[ch].vol  = cell.vol;
                    } else {
                        // Fallback to Arp memory if no note on this row
                        ch_generator[ch].base_note = ch_arp[ch].base_note;
                        ch_generator[ch].inst = ch_arp[ch].inst;
                        ch_generator[ch].vol  = ch_arp[ch].vol;
                    }
                } else if (eff == 0xF000 || (cell.note != 0 && cmd == 0)) {
                    if (ch_tremolo[ch].active) {
                        ch_tremolo[ch].active = false;
                        OPL_SetVolume(ch, ch_tremolo[ch].base_vol << 1); // Restore original volume
                    }
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

                    ch_generator[ch].active = false;

                }
                // Note: Removed the "else if (cmd == 0 && eff == 0x0000)" handler
                // Empty rows should NOT reset vibrato pitch - let it oscillate freely
                last_effect[ch] = cell.effect; // Update shadow
            }

            // --- 2. TRIGGER NOTE WITH OFFSET ---
            if (cell.note != 0 && !fine_pitch_triggered) {
                // Deactivate effects when new note + no effect command (cmd=0)
                // This handles the case where effect column is 0000 but last_effect was also 0000
                // (so effect parsing block was skipped)
                uint8_t cmd = (cell.effect >> 12) & 0x0F;
                if (cmd == 0 && cell.note != 255) {
                    ch_tremolo[ch].active = false;
                    ch_retrigger[ch].active = false;
                    ch_vibrato[ch].active = false;
                    ch_generator[ch].active = false;
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
                    ch_arp[ch].phase_timer_fp = 0;
                    ch_arp[ch].step_index = 0;
                    ch_arp[ch].just_triggered = true; // DO NOT strike mid-row logic this frame

                    // If the generator is active, update its memory with the new note/inst/vol
                    if (ch_generator[ch].active) {
                        ch_generator[ch].base_note = cell.note;
                        ch_generator[ch].inst = cell.inst;
                        ch_generator[ch].vol  = cell.vol;
                        ch_generator[ch].timer = 0; // Reset timer on new note strike
                        ch_generator[ch].just_triggered = true; // Match arp timing
                    }

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

        // Handle Follow Mode (Sync cursor to the note just struck)
        if (is_follow_mode) {
            uint8_t old_edit_row = cur_row;
            cur_row = play_row;
            if (cur_row != old_edit_row) {
                update_cursor_visuals(old_edit_row, cur_row, cur_channel, cur_channel);
            }
        }


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
        process_gen_logic(ch);
    }

    // If we just finished the last tick of the row
    // Check if tick_counter_fp is >= (ticks_per_row_fp - TICK_SCALE)
    if (seq.tick_counter_fp >= (seq.ticks_per_row_fp - TICK_SCALE)) {
    // if (is_new_row) {
        if (play_row < 31) {
            play_row++;
        } else {
            play_row = 0;
            if (is_song_mode) {
                cur_order_idx++;
                if (cur_order_idx >= song_length) cur_order_idx = 0;
                cur_pattern = read_order_xram(cur_order_idx);
                render_grid();
                update_dashboard();
            }
        }
    }

}

void handle_transport_controls() {
    // Enter: Play / Pause / Stop

    if (key_pressed(KEY_ENTER)) {

        // Shift + Enter : Stop & Reset to Beginning
        if (is_shift_down()) {

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
            seq.tick_counter_fp = 0;
            play_row = 0;
            cur_order_idx = 0;
            
            update_cursor_visuals(old, 0, cur_channel, cur_channel);
            mark_playhead(play_row);
            update_dashboard();
            
        }
        // Enter : Play / Pause Toggle
        else {

            seq.is_playing = !seq.is_playing;
            
            if (seq.is_playing) {
                seq.tick_counter_fp = seq.ticks_per_row_fp; 

                // --- THE FIX ---
                if (is_song_mode) {
                    // Sync to the song structure only if we are in SONG mode
                    cur_pattern = read_order_xram(cur_order_idx);
                    render_grid(); 
                } 
                // If is_song_mode is false, we don't touch cur_pattern.
                // It stays on the pattern you were manually editing.
            }
            update_dashboard();
        }
    }

    // --- F6: TOGGLE FOLLOW MODE ---
    if (key_pressed(KEY_F6)) {
        is_follow_mode = !is_follow_mode;
        
        // If turning follow ON while playing, snap cursor to playhead immediately
        if (is_follow_mode && seq.is_playing) {
            uint8_t old_r = cur_row;
            cur_row = play_row;
            update_cursor_visuals(old_r, cur_row, cur_channel, cur_channel);
            mark_playhead(play_row);
        }
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
        mark_playhead(play_row);
        
        printf("Cell Cleared at Row %d\n", cur_row - 1);
    }

    if (key_pressed(KEY_GRAVE)) {
        // 1. Create the 'Kill' cell
        PatternCell off;
        off.note = 255;               // Note Off (===)
        off.inst = current_instrument; // Keep current instrument for context
        off.vol  = 0;                 // Silence
        off.effect = 0xF000;          // NEW: Kill Effect command

        // 2. Write to XRAM and Update Screen
        write_cell(cur_pattern, cur_row, cur_channel, &off);
        render_row(cur_row);

        // 3. Auto-advance with Wrapping (consistent with piano input)
        uint8_t old_row = cur_row;
        if (cur_row < 31) {
            cur_row++;
        } else {
            cur_row = 0; // Wrap 1F -> 00
        }

        // 4. Visual Refresh
        update_cursor_visuals(old_row, cur_row, cur_channel, cur_channel);
        mark_playhead(play_row);
    }
}

void modify_volume_effects(int8_t delta) {
    if (effect_view_mode) {
        // --- 16-BIT EFFECT EDITING (High Byte) ---
        PatternCell cell;
        read_cell(cur_pattern, cur_row, cur_channel, &cell);

        // Separate the effect into High Byte and Low Byte
        uint8_t hi = (uint8_t)(cell.effect >> 8);
        uint8_t lo = (uint8_t)(cell.effect & 0x00FF);

        if (is_shift_down()) {
            // Edit Digit 1 (Command: bits 12-15)
            uint8_t cmd = (hi >> 4);
            cmd = (cmd + delta) & 0x0F; // Apply delta and wrap 0-F
            hi = (cmd << 4) | (hi & 0x0F); // Re-insert while preserving Digit 2
        } 
        else {
            // Edit Digit 2 (Style: bits 8-11)
            uint8_t style = (hi & 0x0F);
            style = (style + delta) & 0x0F; // Apply delta and wrap 0-F
            hi = (hi & 0xF0) | style; // Re-insert while preserving Digit 1
        }

        // Re-pack into 16-bit effect
        cell.effect = ((uint16_t)hi << 8) | lo;

        write_cell(cur_pattern, cur_row, cur_channel, &cell);
        render_row(cur_row);
        update_cursor_visuals(cur_row, cur_row, cur_channel, cur_channel);
        mark_playhead(play_row);
    } 
    else {
        // --- VOLUME EDITING (Remains Linear) ---
        if (is_shift_down()) {
            PatternCell cell;
            read_cell(cur_pattern, cur_row, cur_channel, &cell);
            int16_t v = (int16_t)cell.vol + delta;
            if (v > 63) v = 63; if (v < 0) v = 0;
            cell.vol = (uint8_t)v;
            write_cell(cur_pattern, cur_row, cur_channel, &cell);
            render_row(cur_row);
            mark_playhead(play_row);
            OPL_SetVolume(cur_channel, cell.vol << 1);
        } 
        else {
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

    // Split the 16-bit effect into its two 8-bit halves
    uint8_t hi_byte = (uint8_t)(cell.effect >> 8);
    uint8_t lo_byte = (uint8_t)(cell.effect & 0xFF);

    if (is_shift_down()) {
        // --- EDITING DIGIT 3 (High nibble of the low byte: 00X0) ---
        // Example: 1F -> 0F (if decreasing)
        uint8_t val = (lo_byte >> 4);      // Extract high nibble
        val = (val + delta) & 0x0F;        // Add and wrap 0-F
        lo_byte = (val << 4) | (lo_byte & 0x0F); // Re-insert
    } 
    else {
        // --- EDITING DIGIT 4 (Low nibble of the low byte: 000X) ---
        // Example: 1F -> 10 (if increasing)
        uint8_t val = (lo_byte & 0x0F);    // Extract low nibble
        val = (val + delta) & 0x0F;        // Add and wrap 0-F
        lo_byte = (lo_byte & 0xF0) | val;  // Re-insert
    }

    // Re-pack the modified low byte back into the 16-bit effect
    cell.effect = ((uint16_t)hi_byte << 8) | lo_byte;

    write_cell(cur_pattern, cur_row, cur_channel, &cell);
    render_row(cur_row);
    update_cursor_visuals(cur_row, cur_row, cur_channel, cur_channel);
    mark_playhead(play_row);
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
    mark_playhead(play_row);
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
        mark_playhead(play_row);
        
        printf("Switched to Pattern: %02X\n", cur_pattern);
    }
}

void handle_song_order_input() {
    bool state_changed = false;

    // 1. Shift + F11/F12: Change Pattern ID in CURRENT Order Slot
    if (is_shift_down()) {
        uint8_t p = read_order_xram(cur_order_idx);
        if (key_pressed(KEY_F11)) {
            p = (p > 0) ? p - 1 : MAX_PATTERNS - 1;
            state_changed = true;
        } else if (key_pressed(KEY_F12)) {
            p = (p < MAX_PATTERNS - 1) ? p + 1 : 0;
            state_changed = true;
        }

        if (state_changed) {
            write_order_xram(cur_order_idx, p);
            // SYNC: Immediately update the current editing pattern to match
            cur_pattern = p; 
            render_grid();
        }
    }
    
    // 2. Alt + F11/F12: Change total SONG LENGTH
    else if (is_alt_down()) {
        if (key_pressed(KEY_F11)) {
            if (song_length > 1) {
                song_length--;
                // SAFETY: If we shortened the song and moved the end-line 
                // past our current cursor, snap the cursor back into bounds.
                if (cur_order_idx >= song_length) {
                    cur_order_idx = (uint8_t)(song_length - 1);
                }
                state_changed = true;
            }
        }
        else if (key_pressed(KEY_F12)) {
            if (song_length < MAX_ORDERS_USER) {
                song_length++;
                state_changed = true;
            }
        }
        
        if (state_changed) {
            // SYNC: Ensure pattern matches the (potentially snapped) index
            cur_pattern = read_order_xram(cur_order_idx);
            render_grid();
        }
    }
    
    // 3. Just F11/F12: Navigate the Order List (Jump through the song)
    else {
        if (key_pressed(KEY_F11)) {
            if (cur_order_idx > 0) cur_order_idx--;
            else cur_order_idx = (uint8_t)(song_length - 1);
            state_changed = true;
        }
        else if (key_pressed(KEY_F12)) {
            if (cur_order_idx < (uint8_t)(song_length - 1)) cur_order_idx++;
            else cur_order_idx = 0;
            state_changed = true;
        }

        if (state_changed) {
            // CONSISTENCY: Always update cur_pattern when navigating sequence.
            // This removes the "confusing view" by ensuring the grid follows the sequence highlight.
            cur_pattern = read_order_xram(cur_order_idx);
            render_grid();
        }
    }

    if (state_changed) {
        update_dashboard();
        // Force highlight update in case the pattern jumped
        update_cursor_visuals(cur_row, cur_row, cur_channel, cur_channel);
        mark_playhead(play_row);
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
        mark_playhead(play_row);
    }
    printf("Pattern %02X Pasted.\n", pat_idx);
}

// void OPL_Panic(void) {
//     for (uint8_t i = 0; i < 9; i++) {
//         // Clear bit 5 (Key-On) for all 9 channels
//         // We write 0x00 to registers $B0 through $B8
//         OPL_Write(0xB0 + i, 0x00);
//     }
    
//     // Reset our "keyboard memory" so the next piano press works cleanly
//     active_midi_note = 0; 
// }

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