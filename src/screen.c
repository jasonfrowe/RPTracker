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
uint8_t last_p_row = 255; // < and > symbols 

void write_cell(uint8_t pat, uint8_t row, uint8_t chan, PatternCell *cell) {
    // 1. Point to the start of the 5-byte cell in XRAM
    RIA.addr0 = get_pattern_xram_addr(pat, row, chan);
    RIA.step0 = 1;

    // 2. Write the three 8-bit fields
    RIA.rw0 = cell->note;
    RIA.rw0 = cell->inst;
    RIA.rw0 = cell->vol;

    // 3. Write the 16-bit effect as two 8-bit chunks
    // We write Low Byte then High Byte (Standard 6502 Little-Endian)
    RIA.rw0 = (uint8_t)(cell->effect & 0x00FF);
    RIA.rw0 = (uint8_t)(cell->effect >> 8);
}

void read_cell(uint8_t pat, uint8_t row, uint8_t chan, PatternCell *cell) {
    RIA.addr0 = get_pattern_xram_addr(pat, row, chan);
    RIA.step0 = 1;
    cell->note = RIA.rw0;
    cell->inst = RIA.rw0;
    cell->vol = RIA.rw0;
    // 16-bit effect
    uint8_t lo = RIA.rw0;
    uint8_t hi = RIA.rw0;
    cell->effect = (uint16_t)((hi << 8) | lo);
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

void draw_hex_byte_coloured(uint16_t vga_addr, uint8_t val, uint8_t fg, uint8_t bg) {
    RIA.addr0 = vga_addr;
    RIA.step0 = 1; // Write every byte (Char, FG, BG)
    
    // First Digit
    RIA.rw0 = hex_chars[val >> 4]; // Character
    RIA.rw0 = fg;                  // Foreground
    RIA.rw0 = bg;                  // Background
    
    // Second Digit
    RIA.rw0 = hex_chars[val & 0x0F]; // Character
    RIA.rw0 = fg;                    // Foreground
    RIA.rw0 = bg;                    // Background
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
        if (cell->note == 0 && cell->effect == 0) {
            // Empty cell: show all dots
            for(int i=0; i<3; i++) { RIA.rw0 = '.'; RIA.rw0 = HUD_COL_WHITE; RIA.rw0 = bg; }
            for(int i=0; i<2; i++) { RIA.rw0 = '.'; RIA.rw0 = HUD_COL_DPURPLE; RIA.rw0 = bg; }
            for(int i=0; i<2; i++) { RIA.rw0 = '.'; RIA.rw0 = HUD_COL_SAGEGREEN; RIA.rw0 = bg; }
        } else {
            // Cell has note or effect: show content
            
            // Note part (3 chars)
            if (cell->note == 0) {
                // No note but has effect: show dots for note
                for(int i=0; i<3; i++) { RIA.rw0 = '.'; RIA.rw0 = HUD_COL_WHITE; RIA.rw0 = bg; }
            } else if (cell->note == 255) {
                // Note off
                RIA.rw0 = '='; RIA.rw0 = HUD_COL_WHITE; RIA.rw0 = bg;
                RIA.rw0 = '='; RIA.rw0 = HUD_COL_WHITE; RIA.rw0 = bg;
                RIA.rw0 = '='; RIA.rw0 = HUD_COL_WHITE; RIA.rw0 = bg;
            } else {
                // Normal note
                uint8_t n = cell->note % 12;
                uint8_t oct = (cell->note / 12) - 1;
                RIA.rw0 = note_names[n][0]; RIA.rw0 = HUD_COL_WHITE; RIA.rw0 = bg;
                RIA.rw0 = note_names[n][1]; RIA.rw0 = HUD_COL_WHITE; RIA.rw0 = bg;
                RIA.rw0 = '0' + oct;   RIA.rw0 = HUD_COL_WHITE; RIA.rw0 = bg;
            }

            if (!effect_view_mode) {
                // Instrument (2 chars: Magenta)
                RIA.rw0 = hex_chars[cell->inst >> 4];   RIA.rw0 = HUD_COL_DPURPLE; RIA.rw0 = bg;
                RIA.rw0 = hex_chars[cell->inst & 0x0F]; RIA.rw0 = HUD_COL_DPURPLE; RIA.rw0 = bg;

                // Volume (2 chars: Green)
                RIA.rw0 = hex_chars[cell->vol >> 4];    RIA.rw0 = HUD_COL_SAGEGREEN;   RIA.rw0 = bg;
                RIA.rw0 = hex_chars[cell->vol & 0x0F];  RIA.rw0 = HUD_COL_SAGEGREEN;   RIA.rw0 = bg;
            } else {
                // Effect (4 chars: Yellow/Orange/Cyan/Cyan)
                RIA.rw0 = hex_chars[(cell->effect >> 12) & 0x0F]; RIA.rw0 = HUD_COL_YELLOW; RIA.rw0 = bg;
                RIA.rw0 = hex_chars[(cell->effect >> 8) & 0x0F];  RIA.rw0 = HUD_COL_ORANGE; RIA.rw0 = bg;
                RIA.rw0 = hex_chars[(cell->effect >> 4) & 0x0F];  RIA.rw0 = HUD_COL_CYAN; RIA.rw0 = bg;
                RIA.rw0 = hex_chars[cell->effect & 0x0F];         RIA.rw0 = HUD_COL_CYAN; RIA.rw0 = bg;
            }
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

    // --- 4. PAINT ACTIVE CELL ---
    // Only change background color, preserve text colors
    uint8_t cell_x = 4 + (new_ch * 8);
    uint16_t cell_addr = text_message_addr + (new_y * 80 + cell_x) * 3;
    
    // Point to first background byte (skip char + fg)
    RIA.addr0 = cell_addr + 2;
    RIA.step0 = 3; // Jump to next background byte
    
    // Change background for all 7 characters in the cell
    for (uint8_t i = 0; i < 7; i++) {
        RIA.rw0 = cell_color;  // Apply Red or Blue BG
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

void update_cpu_speed_display(void) {
    // 1. Get the real measured speed in kHz
    int khz = phi2();

    // 2. Extract digits
    // Example: 8125 kHz
    uint8_t mhz_tens   = (khz / 10000) % 10; // Usually 0
    uint8_t mhz_ones   = (khz / 1000) % 10;  // 8
    uint8_t mhz_tenths = (khz % 1000) / 100; // 1 (The .X part)

    // 3. Point to the "CPU: " value position on Row 19
    // Column 60 is where the number starts
    uint16_t vga_ptr = text_message_addr + (1 * 80 + 54) * 3;
    RIA.addr0 = vga_ptr;
    RIA.step0 = 3; // We skip color bytes to keep whatever colors draw_ui_dashboard set

    // 4. Write characters
    // Tens digit (only show if > 0, otherwise space)
    if (mhz_tens > 0) {
        RIA.rw0 = '0' + mhz_tens;
    } else {
        RIA.rw0 = ' '; 
    }

    RIA.rw0 = '0' + mhz_ones;   // Whole MHz
    RIA.rw0 = '.';              // Decimal
    RIA.rw0 = '0' + mhz_tenths; // Tenths
    RIA.rw0 = ' ';              // Space
    RIA.rw0 = 'M';
    RIA.rw0 = 'H';
    RIA.rw0 = 'z';
}

void draw_ui_dashboard(void) {
    const char* h_line = "+------------------------------------------------------------------------------+";
    const char* h_short = "+-------------------------+-------------------------+";

    // 1. Structural Borders
    draw_string(0, 0, h_line, HUD_COL_DARKGREY, HUD_COL_BG);
    draw_string(0, 2, h_line, HUD_COL_DARKGREY, HUD_COL_BG);
    draw_string(0, 7, h_line, HUD_COL_DARKGREY, HUD_COL_BG);
    draw_string(0, 10, h_short, HUD_COL_DARKGREY, HUD_COL_BG);

    draw_string(0, 1, "|", HUD_COL_DARKGREY, HUD_COL_BG);
    draw_string(79, 1, "|", HUD_COL_DARKGREY, HUD_COL_BG);

    for(uint8_t i=3; i<7; i++) {
        draw_string(0, i, "|", HUD_COL_DARKGREY, HUD_COL_BG);
        draw_string(79, i, "|", HUD_COL_DARKGREY, HUD_COL_BG);
    }

    for(uint8_t i=8; i<11; i++) {
        draw_string(0, i, "|", HUD_COL_DARKGREY, HUD_COL_BG);
        draw_string(52, i, "|", HUD_COL_DARKGREY, HUD_COL_BG);
        draw_string(79, i, "|", HUD_COL_DARKGREY, HUD_COL_BG);
    }
    
    // Vertical dividers for the Op editor
    for(uint8_t i=11; i<19; i++) {
        draw_string(0, i, "|", HUD_COL_DARKGREY, HUD_COL_BG);
        draw_string(26, i, "|", HUD_COL_DARKGREY, HUD_COL_BG);
        draw_string(52, i, "|", HUD_COL_DARKGREY, HUD_COL_BG);
        draw_string(79, i, "|", HUD_COL_DARKGREY, HUD_COL_BG);
    }
    draw_string(0, 19, h_line, HUD_COL_DARKGREY, HUD_COL_BG);

    for(uint8_t i=20; i<26; i++) {
        draw_string(0, i, "|", HUD_COL_DARKGREY, HUD_COL_BG);
        // draw_string(63, i, "|", HUD_COL_DARKGREY, HUD_COL_BG);
        draw_string(79, i, "|", HUD_COL_DARKGREY, HUD_COL_BG);
    }
    draw_string(0, 26, h_line, HUD_COL_DARKGREY, HUD_COL_BG);

    // 2. Static Labels (Rows 1-4)
    draw_string(2, 1, "RP6502 TRACKER v0.2  [ FILE: ", HUD_COL_WHITE, HUD_COL_BG);
    // Draw the actual filename in a distinct color (Cyan looks good here)
    draw_string(31, 1, active_filename, HUD_COL_CYAN, HUD_COL_BG);
    // Close the bracket
    // Note: Column 31 + 12 (max filename) = 43. 
    draw_string(31 + 13, 1, " ]", HUD_COL_WHITE, HUD_COL_BG);
    
    
    // draw_string(61, 1, "BPM: 150  TKS: 00", HUD_COL_CYAN, HUD_COL_BG);
    // draw_string(47, 1, "[ SYSTEM ]", HUD_COL_YELLOW, HUD_COL_BG);
    draw_string(50, 1, "CPU: ", HUD_COL_WHITE, HUD_COL_BG);
    update_cpu_speed_display();
    #ifdef USE_NATIVE_OPL2
        draw_string(65, 1, "OPL: NATIVE", HUD_COL_WHITE, HUD_COL_BG);
    #else
        draw_string(65, 1, "OPL: FPGA  ", HUD_COL_WHITE, HUD_COL_BG);
    #endif

    // draw_string(2, 3, "MODE: [       ]  OCT:    INS:    (                  )  VOL:    REC: ", HUD_COL_CYAN, HUD_COL_BG);
    draw_string(2, 3, "MODE:[       ] SEQ:                                                REC: ", HUD_COL_CYAN, HUD_COL_BG);
    draw_string(2, 4, "EDIT PAT:                                                          SEQ: ", HUD_COL_CYAN, HUD_COL_BG);
    draw_string(2, 5, "                                                                   LEN: ", HUD_COL_CYAN, HUD_COL_BG);
    draw_string(2, 6, "                                                                   FOL: ", HUD_COL_CYAN, HUD_COL_BG);

    // Instrument Name
    draw_string(2, 8, "INS:    (                  )  VOL:     OCT:   ", HUD_COL_CYAN, HUD_COL_BG);

    // 3. Operator Headers
    draw_string(2, 11, "[ MODULATOR / OP1 ]", HUD_COL_YELLOW, HUD_COL_BG);

    draw_string(4, 13, "MULT/VIB:", HUD_COL_CYAN, HUD_COL_BG);
    draw_string(4, 14, "KSL/LEV:", HUD_COL_CYAN, HUD_COL_BG);
    draw_string(4, 15, "ATT/DEC:", HUD_COL_CYAN, HUD_COL_BG);
    draw_string(4, 16, "SUS/REL", HUD_COL_CYAN, HUD_COL_BG);
    draw_string(4, 17, "WAVE:", HUD_COL_CYAN, HUD_COL_BG);
    draw_string(4, 18, "FEEDBACK:", HUD_COL_CYAN, HUD_COL_BG);

    draw_string(28, 11, "[ CARRIER / OP2 ]", HUD_COL_YELLOW, HUD_COL_BG);

    draw_string(30, 13, "MULT/VIB:", HUD_COL_CYAN, HUD_COL_BG);
    draw_string(30, 14, "KSL/LEV:", HUD_COL_CYAN, HUD_COL_BG);
    draw_string(30, 15, "ATT/DEC:", HUD_COL_CYAN, HUD_COL_BG);
    draw_string(30, 16, "SUS/REL", HUD_COL_CYAN, HUD_COL_BG);
    draw_string(30, 17, "WAVE:", HUD_COL_CYAN, HUD_COL_BG);

    draw_string(55, 8, "[ CHANNEL METERS ]", HUD_COL_YELLOW, HUD_COL_BG);

    // 4. Cheatsheet & System Info (New Space)
    // draw_string(1, 20, "[ COMMAND CHEATSHEET ]", HUD_COL_YELLOW, HUD_COL_BG);
    // draw_string(2, 21, "F1/F2: Octave  F3/F4: Ins   F5: Pick  F6: Play", HUD_COL_CYAN, HUD_COL_BG);
    // draw_string(2, 22, "F8: Mode    F9/10: Pattern  F11/12: Sequence", HUD_COL_CYAN, HUD_COL_BG);
    // draw_string(2, 23, "[ / ]: Vol  - / =: Transp.  Space: Record", HUD_COL_CYAN, HUD_COL_BG);
    // draw_string(2, 24, "Ctrl+C: Copy Pat  Ctrl+V: Paste Pat", HUD_COL_CYAN, HUD_COL_BG);
    // draw_string(2, 25, "Ctrl+S: Save  Ctrl+O: Load  Esc: Panic", HUD_COL_CYAN, HUD_COL_BG);

    // Row 20: Title
    draw_string(1, 20, "[ COMMAND CHEATSHEET ]", HUD_COL_YELLOW, HUD_COL_BG);

    // Row 21: Navigation & Performance
    draw_string(2, 21, "Octave  : F1/F2   Pattern   : F9/10    Effect Mode: /       ", HUD_COL_CYAN, HUD_COL_BG);
    // Row 22: Editing & Tools
    draw_string(2, 22, "Inst    : F3/F4   Sequence  : F11/12   Vol/Effect : [ / ]   ", HUD_COL_CYAN, HUD_COL_BG);
    // Row 23: Tools & Effects
    draw_string(2, 23, "Pick Ins: F5      Transpose : - / =    Effect Par : ; / '   ", HUD_COL_CYAN, HUD_COL_BG);
    // Row 24: Transport & Files
    draw_string(2, 24, "Play    : Enter   Copy/Paste: Ctrl+C/V Save/Load  : Ctrl+S/O", HUD_COL_CYAN, HUD_COL_BG);
    // Row 25: Mode & Safety
    draw_string(2, 25, "Record  : Space   Song Mode : F8       Follow Me  : F6      ", HUD_COL_CYAN, HUD_COL_BG);

    // Highlight the KEYS in White
    for (uint8_t r = 21; r <= 25; r++) {
        // Col 1 Keys (starts at 12)
        set_text_color(12, r, 5, HUD_COL_WHITE, HUD_COL_BG); 
        // Col 2 Keys (starts at 32)
        set_text_color(32, r, 8, HUD_COL_WHITE, HUD_COL_BG);
        // Col 3 Keys (starts at 53)
        set_text_color(53, r, 9, HUD_COL_WHITE, HUD_COL_BG);
    }
    
}

void update_dashboard(void) {
    const OPL_Patch* p = &gm_bank[current_instrument];

    // --- Row 3: Brush Info ---
    // Mode: PATTERN (Green) or SONG (Yellow)
    draw_string(8, 3, is_song_mode ? "SONG   " : "PATTERN", is_song_mode ? HUD_COL_YELLOW : HUD_COL_GREEN, HUD_COL_BG);
    
    // Octave & Instrument ID
    draw_hex_byte_coloured(text_message_addr + (8 * 80 + 46) * 3, current_octave, HUD_COL_WHITE, HUD_COL_BG);
    draw_hex_byte_coloured(text_message_addr + (8 * 80 + 7) * 3, current_instrument, HUD_COL_WHITE, HUD_COL_BG);
    
    // Instrument Name (Clear 18 chars, then draw)
    draw_string(11, 8, "                  ", HUD_COL_WHITE, HUD_COL_BG);
    draw_string(11, 8, patch_names[current_instrument], HUD_COL_WHITE, HUD_COL_BG);
    
    // Global Brush Volume (00-3F)
    draw_hex_byte_coloured(text_message_addr + (8 * 80 + 37) * 3, current_volume, HUD_COL_WHITE, HUD_COL_BG);
    
    // Record State: ON (Red) or OFF (Green)
    draw_string(74, 3, edit_mode ? "ON " : "OFF", edit_mode ? HUD_COL_RED : HUD_COL_GREEN, HUD_COL_BG);

    // --- Row 4: Pattern & Sequence ---
    // The pattern currently being edited (F9/F10)
    draw_hex_byte_coloured(text_message_addr + (4 * 80 + 12) * 3, cur_pattern, HUD_COL_WHITE, HUD_COL_BG);
    
    // The Playlist scrolling preview
    update_order_display(); 
    
    // Total Song Length and Sequencer Status
    draw_hex_byte_coloured(text_message_addr + (5 * 80 + 74) * 3, (uint8_t)song_length, HUD_COL_WHITE, HUD_COL_BG);
    draw_string(74, 4, seq.is_playing ? "PLAY" : "STOP", seq.is_playing ? HUD_COL_GREEN : HUD_COL_RED, HUD_COL_BG);

    // Follow Mode: ON (Green) or OFF (Red)
    draw_string(74, 6, is_follow_mode ? "ON " : "OFF", is_follow_mode ? HUD_COL_GREEN : HUD_COL_RED, HUD_COL_BG);

    // --- Row 8-12: Operator 1 (Modulator) ---
    draw_hex_byte(text_message_addr + (13 * 80 + 15) * 3, p->m_ave);
    draw_hex_byte(text_message_addr + (14 * 80 + 15) * 3, p->m_ksl);
    draw_hex_byte(text_message_addr + (15 * 80 + 15) * 3, p->m_atdec);
    draw_hex_byte(text_message_addr + (16 * 80 + 15) * 3, p->m_susrel);
    draw_hex_byte(text_message_addr + (17 * 80 + 15) * 3, p->m_wave);
    
    // --- Row 8-12: Operator 2 (Carrier) ---
    draw_hex_byte(text_message_addr + (13 * 80 + 42) * 3, p->c_ave);
    draw_hex_byte(text_message_addr + (14 * 80 + 42) * 3, p->c_ksl);
    draw_hex_byte(text_message_addr + (15 * 80 + 42) * 3, p->c_atdec);
    draw_hex_byte(text_message_addr + (16 * 80 + 42) * 3, p->c_susrel);
    draw_hex_byte(text_message_addr + (17 * 80 + 42) * 3, p->c_wave);

    // --- Row 13: Global FM Parameters ---
    draw_hex_byte(text_message_addr + (18 * 80 + 15) * 3, p->feedback);
    
    // Connection type display (Bit 0 of Feedback register)
    bool additive = (p->feedback & 0x01);
    draw_string(18, 18, additive ? "(ADD)" : "(FM) ", 
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
        draw_string(57, 10 + i, "CH", HUD_COL_CYAN, HUD_COL_BG);
        uint16_t num_ptr = text_message_addr + ((10 + i) * 80 + 59) * 3;
        RIA.addr0 = num_ptr;
        RIA.step0 = 1;
        RIA.rw0 = '0' + i; RIA.rw0 = HUD_COL_WHITE; RIA.rw0 = HUD_COL_BG;
        
        // Meter Bar: [##########]
        draw_meter(61, 10 + i, ch_peaks[i]);
    }
}


void mark_playhead(uint8_t row_to_draw) {
    static uint8_t last_drawn_row = 255;
    
    // 1. Clear previous markers if they moved
    if (last_drawn_row != 255 && last_drawn_row != row_to_draw) {
        uint8_t old_y = last_drawn_row + GRID_SCREEN_OFFSET;
        
        RIA.addr0 = text_message_addr + (old_y * 80 + 2) * 3;
        RIA.step0 = 3; // Skip FG/BG
        RIA.rw0 = ' '; 
        
        RIA.addr0 = text_message_addr + (old_y * 80 + 77) * 3;
        RIA.step0 = 3;
        RIA.rw0 = ' ';
    }

    // 2. Draw current markers
    // We draw even if not playing so the user can see where it stopped.
    uint8_t new_y = row_to_draw + GRID_SCREEN_OFFSET;

    // Left Marker (Column 2)
    RIA.addr0 = text_message_addr + (new_y * 80 + 2) * 3;
    RIA.step0 = 1; // Write Char and FG
    RIA.rw0 = '>';
    RIA.rw0 = HUD_COL_YELLOW;

    // Right Marker (Column 77)
    RIA.addr0 = text_message_addr + (new_y * 80 + 77) * 3;
    RIA.step0 = 1;
    RIA.rw0 = '<';
    RIA.rw0 = HUD_COL_YELLOW;

    last_drawn_row = row_to_draw;
}

void refresh_all_ui(void) {
    clear_top_ui();      // Wipes rows 0-27 in XRAM
    draw_ui_dashboard(); // Redraws the boxes, headers, and labels
    update_dashboard();  // Redraws all current hex values and names
    draw_headers();      // Redraws the grid headers (CH0, CH1, etc.)
    render_grid();       // Redraws the 32-row pattern grid (Row 28-59)
    update_cursor_visuals(cur_row, cur_row, cur_channel, cur_channel); // Restores cursor highlight
    mark_playhead(play_row); // Restores playhead marker
}


