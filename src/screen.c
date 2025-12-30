#include "screen.h"
#include "constants.h"
#include "input.h"
#include <rp6502.h>
#include "usb_hid_keys.h"
#include <stdio.h>
#include "instruments.h"
#include "player.h"
#include "song.h"

char message[MESSAGE_LENGTH + 1]; // Text message buffer (+1 for null terminator)

// Tracker Cursor
uint8_t cur_pattern = 0;
uint8_t cur_row = 0;        // 0-31
uint8_t cur_channel = 0;    // 0-8
bool edit_mode = false;     // Are we recording?

void write_cell(uint8_t pat, uint8_t row, uint8_t chan, PatternCell *cell) {
    // 2304 bytes per pattern (64 rows * 9 channels * 4 bytes)
    uint16_t addr = PATTERN_XRAM_BASE + (pat * 2304) + (row * 36) + (chan * 4);
    RIA.addr0 = addr;
    RIA.step0 = 1;
    RIA.rw0 = cell->note;
    RIA.rw0 = cell->inst;
    RIA.rw0 = cell->vol;
    RIA.rw0 = cell->effect;
}

void read_cell(uint8_t pat, uint8_t row, uint8_t chan, PatternCell *cell) {
    uint16_t addr = PATTERN_XRAM_BASE + (pat * 2304) + (row * 36) + (chan * 4);
    RIA.addr0 = addr;
    RIA.step0 = 1;
    cell->note   = RIA.rw0;
    cell->inst   = RIA.rw0;
    cell->vol    = RIA.rw0;
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

void draw_ui_dashboard() {
    // Row 1: Status
    // OCTAVE: 4 | INS: 00 (Piano) | CHAN: 0 | MODE: EDIT
    
    // Row 3-15: Instrument Parameters (Operator 1 & 2)
    // OP1: ATK: F  DEC: 1  SUS: 5  REL: 0  MULT: 1  WAVE: 0
    // OP2: ATK: D  DEC: 2  SUS: 7  REL: 6  MULT: 1  WAVE: 0
    // FEEDBACK: 6  CONNECTION: 0 (FM)
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

void update_dashboard(void) {
    const OPL_Patch* p = &gm_bank[current_instrument];

    // 1. Header Line (Row 1)
    draw_string(2, 1, "INSTRUMENT:", HUD_COL_CYAN, HUD_COL_BG);
    draw_hex_byte(text_message_addr + (1 * 80 + 14) * 3, current_instrument);
    
    // You can add a name lookup here if you have one, or just a placeholder
    draw_string(18, 1, "OCTAVE:", HUD_COL_CYAN, HUD_COL_BG);
    draw_hex_byte(text_message_addr + (1 * 80 + 26) * 3, current_octave);
    
    draw_string(32, 1, "MODE:", HUD_COL_CYAN, HUD_COL_BG);
    draw_string(38, 1, edit_mode ? "RECORDING" : "PLAYING  ", 
                edit_mode ? HUD_COL_RED : HUD_COL_GREEN, HUD_COL_BG);

    // 2. Operator Panels (Rows 3-10)
    // --- MODULATOR (OP1) ---
    draw_string(2, 4, "[ MODULATOR ]", HUD_COL_YELLOW, HUD_COL_BG);
    draw_string(2, 6, "MULT/VIB: ", HUD_COL_WHITE, HUD_COL_BG);
    draw_hex_byte(text_message_addr + (6 * 80 + 12) * 3, p->m_ave);
    
    draw_string(2, 7, "KSL/LEV:  ", HUD_COL_WHITE, HUD_COL_BG);
    draw_hex_byte(text_message_addr + (7 * 80 + 12) * 3, p->m_ksl);
    
    draw_string(2, 8, "ATK/DEC:  ", HUD_COL_WHITE, HUD_COL_BG);
    draw_hex_byte(text_message_addr + (8 * 80 + 12) * 3, p->m_atdec);
    
    draw_string(2, 9, "SUS/REL:  ", HUD_COL_WHITE, HUD_COL_BG);
    draw_hex_byte(text_message_addr + (9 * 80 + 12) * 3, p->m_susrel);

    // --- CARRIER (OP2) ---
    draw_string(25, 4, "[ CARRIER ]", HUD_COL_YELLOW, HUD_COL_BG);
    draw_string(25, 6, "MULT/VIB: ", HUD_COL_WHITE, HUD_COL_BG);
    draw_hex_byte(text_message_addr + (6 * 80 + 35) * 3, p->c_ave);
    
    draw_string(25, 7, "KSL/LEV:  ", HUD_COL_WHITE, HUD_COL_BG);
    draw_hex_byte(text_message_addr + (7 * 80 + 35) * 3, p->c_ksl);
    
    draw_string(25, 8, "ATK/DEC:  ", HUD_COL_WHITE, HUD_COL_BG);
    draw_hex_byte(text_message_addr + (8 * 80 + 35) * 3, p->c_atdec);
    
    draw_string(25, 9, "SUS/REL:  ", HUD_COL_WHITE, HUD_COL_BG);
    draw_hex_byte(text_message_addr + (9 * 80 + 35) * 3, p->c_susrel);

    // 3. Global Settings (Row 12)
    draw_string(2, 12, "FEEDBACK/CONN:", HUD_COL_CYAN, HUD_COL_BG);
    draw_hex_byte(text_message_addr + (12 * 80 + 17) * 3, p->feedback);
    
    // Connection type display (Additive vs FM)
    bool additive = (p->feedback & 0x01);
    draw_string(22, 12, additive ? "(ADDITIVE)" : "(FM SYNTH)", 
                HUD_COL_MAGENTA, HUD_COL_BG);

    // 4. Sequencer Status (Row 1, right side)
    draw_string(55, 1, "SEQ:", HUD_COL_CYAN, HUD_COL_BG);
    if (seq.is_playing) {
        draw_string(60, 1, "PLAYING", HUD_COL_GREEN, HUD_COL_BG);
    } else {
        draw_string(60, 1, "STOPPED", HUD_COL_RED, HUD_COL_BG);
    }

    // 5. Current Volume (Row 1, far right)
    draw_string(45, 1, "VOL:", HUD_COL_CYAN, HUD_COL_BG);
    draw_hex_byte(text_message_addr + (1 * 80 + 50) * 3, current_volume);

    // 6. Current Pattern (Row 2)
    draw_string(2, 2, "PATTERN:", HUD_COL_CYAN, HUD_COL_BG);
    draw_hex_byte(text_message_addr + (2 * 80 + 11) * 3, cur_pattern);

    // Display current mode
    draw_string(32, 2, "MODE:", HUD_COL_CYAN, HUD_COL_BG);
    if (is_song_mode) {
        draw_string(38, 2, "SONG   ", HUD_COL_YELLOW, HUD_COL_BG);
    } else {
        draw_string(38, 2, "PATTERN", HUD_COL_GREEN, HUD_COL_BG);
    }

    update_order_display();

}