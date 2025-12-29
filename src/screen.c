#include "screen.h"
#include "constants.h"
#include "input.h"
#include <rp6502.h>
#include "usb_hid_keys.h"
#include <stdio.h>
#include "instruments.h"
#include "player.h"

char message[MESSAGE_LENGTH + 1]; // Text message buffer (+1 for null terminator)

// Tracker Cursor
uint8_t cur_pattern = 0;
uint8_t cur_row = 0;        // 0-63
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

void draw_note(uint16_t xram_vga_addr, uint8_t midi_note) {
    RIA.addr0 = xram_vga_addr;
    RIA.step0 = 3; // In Mode 1, skip FG and BG bytes
    
    if (midi_note == 0) {
        RIA.rw0 = '.'; RIA.rw0 = '.'; RIA.rw0 = '.';
    } else if (midi_note == 255) {
        RIA.rw0 = '='; RIA.rw0 = '='; RIA.rw0 = '='; // Note Off
    } else {
        uint8_t note = midi_note % 12;
        uint8_t octave = (midi_note / 12) - 1;
        const char* name = note_names[note];
        RIA.rw0 = name[0];
        RIA.rw0 = name[1];
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
void render_row(uint8_t pattern_row_idx) {
    // Calculate the Screen Row (28 to 59)
    uint8_t screen_y = pattern_row_idx + GRID_SCREEN_OFFSET;
    
    // Calculate VGA start address for this screen row
    uint16_t vga_row_ptr = text_message_addr + (screen_y * 80 * 3);
    
    // 1. Draw Row Number (Columns 0-2) in Hex
    RIA.addr0 = vga_row_ptr;
    RIA.step0 = 3;
    RIA.rw0 = hex_chars[(pattern_row_idx >> 4) & 0x0F];
    RIA.rw0 = hex_chars[pattern_row_idx & 0x0F];
    RIA.rw0 = '|';

    // 2. Draw 9 Channels
    for (uint8_t ch = 0; ch < 9; ch++) {
        PatternCell cell;
        read_cell(cur_pattern, pattern_row_idx, ch, &cell);
        
        // Offset for this channel: RowNum(3 chars) + ch * 8 chars per channel
        uint16_t vga_cell_ptr = vga_row_ptr + (3 + ch * 8) * 3;
        
        // Draw Note
        draw_note(vga_cell_ptr, cell.note);
        
        // Draw Instrument (at 4 chars into channel block)
        draw_hex_byte(vga_cell_ptr + (4 * 3), cell.inst);
        
        // Draw Volume (at 6 chars into channel block)
        draw_hex_byte(vga_cell_ptr + (6 * 3), cell.vol);
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

// Add this helper to your display/ui logic
void update_cursor_visuals(uint8_t old_row, uint8_t new_row) {
    // Mapping: Cursor Row (0-31) -> Screen Row (28-59)
    
    // Clear old highlight
    uint16_t old_addr = text_message_addr + ((old_row + GRID_SCREEN_OFFSET) * 80 * 3) + 2;
    RIA.addr0 = old_addr;
    RIA.step0 = 3; 
    for (uint8_t i = 0; i < 80; i++) RIA.rw0 = HUD_COL_BG;

    // Set new highlight
    uint16_t new_addr = text_message_addr + ((new_row + GRID_SCREEN_OFFSET) * 80 * 3) + 2;
    RIA.addr0 = new_addr;
    RIA.step0 = 3;
    for (uint8_t i = 0; i < 80; i++) RIA.rw0 = HUD_COL_HIGHLIGHT;
}

void draw_headers() {
    // Set RIA to Screen Row 1
    uint16_t addr = text_message_addr + ((GRID_SCREEN_OFFSET - 1) * 80 * 3);
    RIA.addr0 = addr;
    RIA.step0 = 3;

    // Row indicator
    RIA.rw0 = 'R'; RIA.rw0 = 'N'; RIA.rw0 = ' '; 

    // Channel Headers
    for (uint8_t i = 0; i < 9; i++) {
        RIA.rw0 = '|'; RIA.rw0 = 'C'; RIA.rw0 = 'H'; 
        RIA.rw0 = '0' + i; 
        RIA.rw0 = ' '; RIA.rw0 = ' '; RIA.rw0 = ' '; RIA.rw0 = ' ';
    }
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
}