#include "screen.h"
#include "constants.h"
#include "input.h"
#include <rp6502.h>
#include "usb_hid_keys.h"
#include <stdio.h>
#include "instruments.h"
#include "player.h"
#include "song.h"

// Peak meter state (0-63)
uint8_t ch_peaks[9] = {0,0,0,0,0,0,0,0,0};

char message[MESSAGE_LENGTH + 1]; // Text message buffer (+1 for null terminator)

// Tracker Cursor
uint8_t cur_pattern = 0;
uint8_t cur_row = 0;        // 0-31
uint8_t cur_channel = 0;    // 0-8
bool edit_mode = false;     // Are we recording?

void write_cell(uint8_t pat, uint8_t row, uint8_t chan, PatternCell *cell) {
    RIA.addr0 = get_pattern_xram_addr(pat, row, chan);
    RIA.step0 = 1;
    RIA.rw0 = cell->note;
    RIA.rw0 = cell->inst;
    RIA.rw0 = cell->vol;
    RIA.rw0 = cell->effect;
}

void read_cell(uint8_t pat, uint8_t row, uint8_t chan, PatternCell *cell) {
    RIA.addr0 = get_pattern_xram_addr(pat, row, chan);
    RIA.step0 = 1;
    cell->note = RIA.rw0;
    cell->inst = RIA.rw0;
    cell->vol = RIA.rw0;
    cell->effect = RIA.rw0;
}

const char* const note_names[] = {
    "C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-"
};

// Change colors of a range of characters on a specific screen row
void set_text_color(uint8_t x, uint8_t y, uint8_t len, uint8_t fg, uint8_t bg) {
    uint16_t addr = text_message_addr + (y * 80 + x) * 3;
    RIA.addr0 = addr;
    RIA.step0 = 1;
    for (uint8_t i = 0; i < len; i++) {
        RIA.addr0++;     // Skip Character byte
        RIA.rw0 = fg;    // Write Foreground
        RIA.rw0 = bg;    // Write Background
    }
}

void draw_note(uint16_t vga_addr, uint8_t midi_note) {
    // const char* const names[] = {"C-","C#","D-","D#","E-","F-","F#","G-","G#","A-","A#","B-"};
    RIA.addr0 = vga_addr;
    RIA.step0 = 3;
    
    if (midi_note == 0) {
        // Transparent symmetry: 3 spaces
        RIA.rw0 = ' '; RIA.rw0 = ' '; RIA.rw0 = ' ';
    } else if (midi_note == 255) {
        // Note off
        RIA.rw0 = '='; RIA.rw0 = '='; RIA.rw0 = '=';
    } else {
        uint8_t note = midi_note % 12;
        uint8_t octave = (midi_note / 12) - 1;
        RIA.rw0 = note_names[note][0];
        RIA.rw0 = note_names[note][1];
        RIA.rw0 = '0' + octave;
    }
}

// Formatting helpers
const char hex_chars[] = "0123456789ABCDEF";

void draw_hex_byte(uint16_t vga_addr, uint8_t val) {
    RIA.addr0 = vga_addr;
    RIA.step0 = 3; 
    RIA.rw0 = hex_chars[val >> 4];
    RIA.rw0 = hex_chars[val & 0x0F];
}

// pattern_row_idx: The row index in the pattern data (0-31)
void render_row(uint8_t row_idx) {
    PatternCell row_data[9];
    uint8_t bg;

    // 1. BUFFER THE DATA: Read the row from XRAM into 6502 internal RAM
    // This prevents read_cell from clobbering RIA.addr0 during drawing.
    for (uint8_t ch = 0; ch < 9; ch++) {
        read_cell(cur_pattern, row_idx, ch, &row_data[ch]);
    }

    // 2. SETUP VGA DRAWING
    uint8_t screen_y = row_idx + GRID_SCREEN_OFFSET;
    uint16_t vga_ptr = text_message_addr + (screen_y * 80 * 3);
    
    bg = (row_idx % 4 == 0) ? HUD_COL_BAR : HUD_COL_BG;

    RIA.addr0 = vga_ptr;
    RIA.step0 = 1;

    // 3. DRAW ROW HEADER (4 chars: "00 |")
    RIA.rw0 = hex_chars[row_idx >> 4]; RIA.rw0 = HUD_COL_CYAN;  RIA.rw0 = bg;
    RIA.rw0 = hex_chars[row_idx & 0x0F]; RIA.rw0 = HUD_COL_CYAN;  RIA.rw0 = bg;
    RIA.rw0 = ' ';                       RIA.rw0 = HUD_COL_WHITE; RIA.rw0 = bg;
    RIA.rw0 = '|';                       RIA.rw0 = HUD_COL_WHITE; RIA.rw0 = bg;

    // 4. DRAW CHANNELS (9 channels * 8 chars/ch = 72 chars)
    for (uint8_t ch = 0; ch < 9; ch++) {
        PatternCell *cell = &row_data[ch];

        // Note (3 chars)
        if (cell->note == 0) {
            for(int i=0; i<3; i++) { RIA.rw0 = '.'; RIA.rw0 = HUD_COL_WHITE; RIA.rw0 = bg; }
        
            for(int i=0; i<2; i++) { RIA.rw0 = '.'; RIA.rw0 = HUD_COL_DPURPLE; RIA.rw0 = bg; }

            for(int i=0; i<2; i++) { RIA.rw0 = '.'; RIA.rw0 = HUD_COL_SAGEGREEN; RIA.rw0 = bg; }

        } else {
            // const char* names[] = {"C-","C#","D-","D#","E-","F-","F#","G-","G#","A-","A#","B-"};
            uint8_t n = cell->note % 12;
            uint8_t oct = (cell->note / 12) - 1;
            RIA.rw0 = note_names[n][0]; RIA.rw0 = HUD_COL_WHITE; RIA.rw0 = bg;
            RIA.rw0 = note_names[n][1]; RIA.rw0 = HUD_COL_WHITE; RIA.rw0 = bg;
            RIA.rw0 = '0' + oct;   RIA.rw0 = HUD_COL_WHITE; RIA.rw0 = bg;

            // Instrument (2 chars: Magenta)
            RIA.rw0 = hex_chars[cell->inst >> 4];   RIA.rw0 = HUD_COL_DPURPLE; RIA.rw0 = bg;
            RIA.rw0 = hex_chars[cell->inst & 0x0F]; RIA.rw0 = HUD_COL_DPURPLE; RIA.rw0 = bg;

            // Volume (2 chars: Green)
            RIA.rw0 = hex_chars[cell->vol >> 4];    RIA.rw0 = HUD_COL_SAGEGREEN;   RIA.rw0 = bg;
            RIA.rw0 = hex_chars[cell->vol & 0x0F];  RIA.rw0 = HUD_COL_SAGEGREEN;   RIA.rw0 = bg;

        }

        // Divider (1 char)
        RIA.rw0 = '|'; RIA.rw0 = HUD_COL_WHITE; RIA.rw0 = bg;
    }

    // This wipes the "trailing" blue from the highlight bar.
    for (uint8_t i = 0; i < 4; i++) {
        RIA.rw0 = ' ';             // Space character
        RIA.rw0 = HUD_COL_WHITE;   // Foreground color doesn't matter for space
        RIA.rw0 = bg;              // Restore Black or Grey background
    }
}

void render_grid(void) {
    // We are showing 32 rows (0x00 to 0x1F)
    for (uint8_t i = 0; i < 32; i++) {
        render_row(i); // 'i' becomes 'pattern_row_idx' inside the function
    }
}

void set_row_color(uint8_t row_idx, uint8_t bg_color) {
    // Point to the BG byte (3rd byte) of the first character in the row
    uint16_t addr = text_message_addr + ((row_idx + 2) * 80 * 3) + 2;
    RIA.addr0 = addr;
    RIA.step0 = 3; // Jump to next BG byte
    
    for (uint8_t i = 0; i < 80; i++) {
        RIA.rw0 = bg_color;
    }
}

void update_cursor_visuals(uint8_t old_row, uint8_t new_row, uint8_t old_ch, uint8_t new_ch) {
    uint8_t old_y = old_row + GRID_SCREEN_OFFSET;
    uint8_t new_y = new_row + GRID_SCREEN_OFFSET;
    
    // --- 1. DETERMINE COLORS BASED ON MODE ---
    uint8_t bar_color, cell_color;
    
    if (edit_mode) {
        bar_color = HUD_COL_EDIT_BAR;
        cell_color = HUD_COL_EDIT_CELL;
    } else {
        bar_color = HUD_COL_PLAY_BAR;
        cell_color = HUD_COL_PLAY_CELL;
    }

    // --- 2. CLEAN UP OLD ROW ---
    render_row(old_row);

    // --- 3. PAINT NEW ROW HIGHLIGHT ---
    RIA.addr0 = text_message_addr + (new_y * 80 * 3) + 2; // Point to BG byte
    RIA.step0 = 3; 
    for (uint8_t i = 0; i < 80; i++) {
        RIA.rw0 = bar_color;
    }

    // --- 4. PAINT ACTIVE CELL (Yellow text on Mode-Specific Red/Blue) ---
    uint8_t cell_x = 4 + (new_ch * 8);
    uint16_t cell_addr = text_message_addr + (new_y * 80 + cell_x) * 3;
    
    RIA.addr0 = cell_addr;
    RIA.step0 = 1; 
    for (uint8_t i = 0; i < 7; i++) {
        RIA.addr0++;         // Skip Character
        RIA.rw0 = HUD_COL_YELLOW; 
        RIA.rw0 = cell_color; // Apply Red or Blue BG
    }

    // --- 5. HEADER SYNC (Row 27) ---
    // Clear old
    uint8_t old_hdr_x = 6 + (old_ch * 8);
    uint16_t old_hdr_addr = text_message_addr + (27 * 80 + old_hdr_x) * 3 + 1;
    RIA.addr0 = old_hdr_addr;
    RIA.step0 = 3;
    for(int i=0; i<4; i++) RIA.rw0 = HUD_COL_CYAN;

    // Highlight new (Yellow on Black)
    uint8_t new_hdr_x = 6 + (new_ch * 8);
    uint16_t new_hdr_addr = text_message_addr + (27 * 80 + new_hdr_x) * 3 + 1;
    RIA.addr0 = new_hdr_addr;
    RIA.step0 = 3;
    for(int i=0; i<4; i++) RIA.rw0 = HUD_COL_YELLOW;
}

void draw_string(uint8_t x, uint8_t y, const char* s, uint8_t fg, uint8_t bg) {
    uint16_t addr = text_message_addr + (y * 80 + x) * 3;
    RIA.addr0 = addr;
    RIA.step0 = 1;
    while (*s) {
        RIA.rw0 = *s++;
        RIA.rw0 = fg;
        RIA.rw0 = bg;
    }
}

void draw_headers() {
    // Indices: 0123 45678901 23456789...
    //          0        1         2         3         4         5         6         7         8
    // Line Num 123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890
    // Header:  RN |  CH 0 |  CH 1 |  CH 2 |  CH 3 |  CH 4 |  CH 5 |  CH 6 |  CH 7 |  CH 8 |
    draw_string(0, 27, "RN |  CH 0 |  CH 1 |  CH 2 |  CH 3 |  CH 4 |  CH 5 |  CH 6 |  CH 7 |  CH 8 |", 
                HUD_COL_CYAN, HUD_COL_BG);
}

void clear_top_ui() {
    RIA.addr0 = text_message_addr;
    RIA.step0 = 1;
    // Clear only the top 27 rows
    for (uint16_t i = 0; i < (GRID_SCREEN_OFFSET * 80); i++) {
        RIA.rw0 = ' ';
        RIA.rw0 = HUD_COL_WHITE;
        RIA.rw0 = HUD_COL_BG;
    }
}

void draw_ui_dashboard(void) {
    const char* h_line = "+------------------------------------------------------------------------------+";
    const char* h_short = "+-------------------------+-------------------------+";

    // 1. Structural Borders
    draw_string(0, 0, h_line, HUD_COL_DARKGREY, HUD_COL_BG);
    draw_string(0, 2, h_line, HUD_COL_DARKGREY, HUD_COL_BG);
    draw_string(0, 5, h_line, HUD_COL_DARKGREY, HUD_COL_BG);
    
    // Vertical dividers for the Op editor
    for(uint8_t i=6; i<17; i++) {
        draw_string(0, i, "|", HUD_COL_DARKGREY, HUD_COL_BG);
        draw_string(26, i, "|", HUD_COL_DARKGREY, HUD_COL_BG);
        draw_string(52, i, "|", HUD_COL_DARKGREY, HUD_COL_BG);
        draw_string(79, i, "|", HUD_COL_DARKGREY, HUD_COL_BG);
    }
    draw_string(0, 17, h_line, HUD_COL_DARKGREY, HUD_COL_BG);

    // 2. Static Labels (Rows 1-4)
    // draw_string(2, 1, "RP6502 TRACKER v0.1  [ FILE: UNTITLED.RPT ]", HUD_COL_WHITE, HUD_COL_BG);
    draw_string(2, 1, "RP6502 TRACKER v0.1  [ FILE: ", HUD_COL_WHITE, HUD_COL_BG);
    // Draw the actual filename in a distinct color (Cyan looks good here)
    draw_string(31, 1, active_filename, HUD_COL_CYAN, HUD_COL_BG);
    // Close the bracket
    // Note: Column 31 + 12 (max filename) = 43. 
    draw_string(31 + 13, 1, " ]", HUD_COL_WHITE, HUD_COL_BG);
    
    
    draw_string(55, 1, "BPM: 150  TKS: 00", HUD_COL_CYAN, HUD_COL_BG);

    draw_string(2, 3, "MODE: [       ]  OCT:    INS:    (                  )  VOL:    REC: ", HUD_COL_CYAN, HUD_COL_BG);
    draw_string(2, 4, "EDIT PAT:       SEQ:                                   LEN:    SEQ: ", HUD_COL_CYAN, HUD_COL_BG);

    // 3. Operator Headers
    draw_string(2, 7, "[ MODULATOR / OP1 ]", HUD_COL_YELLOW, HUD_COL_BG);

    draw_string(4, 9, "MULT/VIB:", HUD_COL_CYAN, HUD_COL_BG);
    draw_string(4, 10, "KSL/LEV:", HUD_COL_CYAN, HUD_COL_BG);
    draw_string(4, 11, "ATT/DEC:", HUD_COL_CYAN, HUD_COL_BG);
    draw_string(4, 12, "SUS/REL", HUD_COL_CYAN, HUD_COL_BG);
    draw_string(4, 13, "WAVE:", HUD_COL_CYAN, HUD_COL_BG);
    draw_string(4, 14, "FEEDBACK:", HUD_COL_CYAN, HUD_COL_BG);

    draw_string(28, 7, "[ CARRIER / OP2 ]", HUD_COL_YELLOW, HUD_COL_BG);

    draw_string(30, 9, "MULT/VIB:", HUD_COL_CYAN, HUD_COL_BG);
    draw_string(30, 10, "KSL/LEV:", HUD_COL_CYAN, HUD_COL_BG);
    draw_string(30, 11, "ATT/DEC:", HUD_COL_CYAN, HUD_COL_BG);
    draw_string(30, 12, "SUS/REL", HUD_COL_CYAN, HUD_COL_BG);
    draw_string(30, 13, "WAVE:", HUD_COL_CYAN, HUD_COL_BG);

    draw_string(55, 7, "[ CHANNEL METERS ]", HUD_COL_YELLOW, HUD_COL_BG);

    // 4. Cheatsheet & System Info (New Space)
    // draw_string(1, 20, "[ COMMAND CHEATSHEET ]", HUD_COL_YELLOW, HUD_COL_BG);
    // draw_string(2, 21, "F1/F2: Octave  F3/F4: Ins   F5: Pick  F6: Play", HUD_COL_CYAN, HUD_COL_BG);
    // draw_string(2, 22, "F8: Mode    F9/10: Pattern  F11/12: Sequence", HUD_COL_CYAN, HUD_COL_BG);
    // draw_string(2, 23, "[ / ]: Vol  - / =: Transp.  Space: Record", HUD_COL_CYAN, HUD_COL_BG);
    // draw_string(2, 24, "Ctrl+C: Copy Pat  Ctrl+V: Paste Pat", HUD_COL_CYAN, HUD_COL_BG);
    // draw_string(2, 25, "Ctrl+S: Save  Ctrl+O: Load  Esc: Panic", HUD_COL_CYAN, HUD_COL_BG);

    // Row 20: Title
    draw_string(1, 20, "[ COMMAND CHEATSHEET ]", HUD_COL_YELLOW, HUD_COL_BG);

    // Columns:  Column 1 (Pos 2)       Column 2 (Pos 28)       Column 3 (Pos 54)
    // Row 21: Performance              Sequencing              Editing
    draw_string(2, 21, "F1/F2 : Octave      F8    : Mode         - / =  : Transpose", HUD_COL_CYAN, HUD_COL_BG);
    draw_string(2, 22, "F3/F4 : Instrument  F9/10 : Pattern      [ / ]  : Volume",    HUD_COL_CYAN, HUD_COL_BG);
    draw_string(2, 23, "F5    : Pick Ins    F11/12: Sequence     Space  : Record",    HUD_COL_CYAN, HUD_COL_BG);
    
    // Row 24: Transport                Clipboard               Files/System
    draw_string(2, 24, "F6/F7 : Play/Stop   Ctrl+C: Copy Pat     Ctrl+S : Save Song", HUD_COL_CYAN, HUD_COL_BG);
    draw_string(2, 25, "Esc   : Panic       Ctrl+V: Paste Pat    Ctrl+O : Load Song", HUD_COL_CYAN, HUD_COL_BG);

    // Optional: Make the Keys White to make them stand out from the labels
    for (uint8_t r = 21; r <= 25; r++) {
        set_text_color(2,  r, 7, HUD_COL_WHITE, HUD_COL_BG); // Col 1 Keys
        set_text_color(22, r, 7, HUD_COL_WHITE, HUD_COL_BG); // Col 2 Keys
        set_text_color(43, r, 7, HUD_COL_WHITE, HUD_COL_BG); // Col 3 Keys
    }
    
    draw_string(65, 20, "[ SYSTEM ]", HUD_COL_YELLOW, HUD_COL_BG);
    draw_string(66, 21, "CPU: 8.0MHz", HUD_COL_WHITE, HUD_COL_BG);
    #ifdef USE_NATIVE_OPL2
        draw_string(66, 22, "OPL: NATIVE", HUD_COL_WHITE, HUD_COL_BG);
    #else
        draw_string(66, 22, "OPL: FPGA  ", HUD_COL_WHITE, HUD_COL_BG);
    #endif
}

void update_dashboard(void) {
    const OPL_Patch* p = &gm_bank[current_instrument];

    // --- Row 3: Brush Info ---
    // Mode: PATTERN (Green) or SONG (Yellow)
    draw_string(9, 3, is_song_mode ? "SONG   " : "PATTERN", is_song_mode ? HUD_COL_YELLOW : HUD_COL_GREEN, HUD_COL_BG);
    
    // Octave & Instrument ID
    draw_hex_byte(text_message_addr + (3 * 80 + 24) * 3, current_octave);
    draw_hex_byte(text_message_addr + (3 * 80 + 32) * 3, current_instrument);
    
    // Instrument Name (Clear 18 chars, then draw)
    draw_string(36, 3, "                  ", HUD_COL_WHITE, HUD_COL_BG);
    draw_string(36, 3, patch_names[current_instrument], HUD_COL_WHITE, HUD_COL_BG);
    
    // Global Brush Volume (00-3F)
    draw_hex_byte(text_message_addr + (3 * 80 + 62) * 3, current_volume);
    
    // Record State: ON (Red) or OFF (Green)
    draw_string(70, 3, edit_mode ? "ON " : "OFF", edit_mode ? HUD_COL_RED : HUD_COL_GREEN, HUD_COL_BG);

    // --- Row 4: Pattern & Sequence ---
    // The pattern currently being edited (F9/F10)
    draw_hex_byte(text_message_addr + (4 * 80 + 12) * 3, cur_pattern);
    
    // The Playlist scrolling preview
    update_order_display(); 
    
    // Total Song Length and Sequencer Status
    draw_hex_byte(text_message_addr + (4 * 80 + 62) * 3, (uint8_t)song_length);
    draw_string(70, 4, seq.is_playing ? "PLAY" : "STOP", seq.is_playing ? HUD_COL_GREEN : HUD_COL_RED, HUD_COL_BG);

    // --- Row 8-12: Operator 1 (Modulator) ---
    draw_hex_byte(text_message_addr + (9 * 80 + 15) * 3, p->m_ave);
    draw_hex_byte(text_message_addr + (10 * 80 + 15) * 3, p->m_ksl);
    draw_hex_byte(text_message_addr + (11 * 80 + 15) * 3, p->m_atdec);
    draw_hex_byte(text_message_addr + (12 * 80 + 15) * 3, p->m_susrel);
    draw_hex_byte(text_message_addr + (13 * 80 + 15) * 3, p->m_wave);
    
    // --- Row 8-12: Operator 2 (Carrier) ---
    draw_hex_byte(text_message_addr + (9 * 80 + 42) * 3, p->c_ave);
    draw_hex_byte(text_message_addr + (10 * 80 + 42) * 3, p->c_ksl);
    draw_hex_byte(text_message_addr + (11 * 80 + 42) * 3, p->c_atdec);
    draw_hex_byte(text_message_addr + (12 * 80 + 42) * 3, p->c_susrel);
    draw_hex_byte(text_message_addr + (13 * 80 + 42) * 3, p->c_wave);

    // --- Row 13: Global FM Parameters ---
    draw_hex_byte(text_message_addr + (14 * 80 + 15) * 3, p->feedback);
    
    // Connection type display (Bit 0 of Feedback register)
    bool additive = (p->feedback & 0x01);
    draw_string(18, 14, additive ? "(ADD)" : "(FM) ", 
                HUD_COL_DPURPLE, HUD_COL_BG);
}

void draw_meter(uint8_t x, uint8_t y, uint8_t val) {
    uint16_t addr = text_message_addr + (y * 80 + x) * 3;
    RIA.addr0 = addr;
    RIA.step0 = 1;
    
    uint8_t blocks = val / 6; // Map 0-63 volume to 0-10 blocks
    
    RIA.rw0 = '['; RIA.rw0 = HUD_COL_CYAN; RIA.rw0 = HUD_COL_BG;
    for (uint8_t i = 0; i < 10; i++) {
        RIA.rw0 = (i < blocks) ? '#' : '.';
        RIA.rw0 = (i < blocks) ? HUD_COL_GREEN : HUD_COL_DARKGREY;
        RIA.rw0 = HUD_COL_BG;
    }
    RIA.rw0 = ']'; RIA.rw0 = HUD_COL_CYAN; RIA.rw0 = HUD_COL_BG;
}

void update_meters(void) {
    for (uint8_t i = 0; i < 9; i++) {
        // Underflow protection: 1-frame decay
        if (ch_peaks[i] > 1) ch_peaks[i]-=2; 
        
        // Label: CHx
        draw_string(57, 8 + i, "CH", HUD_COL_CYAN, HUD_COL_BG);
        uint16_t num_ptr = text_message_addr + ((8 + i) * 80 + 59) * 3;
        RIA.addr0 = num_ptr;
        RIA.step0 = 1;
        RIA.rw0 = '0' + i; RIA.rw0 = HUD_COL_WHITE; RIA.rw0 = HUD_COL_BG;
        
        // Meter Bar: [##########]
        draw_meter(61, 8 + i, ch_peaks[i]);
    }
}


void refresh_all_ui(void) {
    clear_top_ui();      // Wipes rows 0-27 in XRAM
    draw_ui_dashboard(); // Redraws the boxes, headers, and labels
    update_dashboard();  // Redraws all current hex values and names
    render_grid();       // Redraws the 32-row pattern grid (Row 28-59)
    update_cursor_visuals(cur_row, cur_row, cur_channel, cur_channel); // Restores cursor highlight
}