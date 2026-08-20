#include <rp6502.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include "constants.h"
#include "screen.h"
#include "song.h"
#include "player.h"
#include "input.h"
#include <string.h>
#include "usb_hid_keys.h"

uint8_t cur_order_idx = 0; // Where we are in the playlist
uint16_t song_length = 1;   // Total number of patterns in the song
bool is_song_mode = false;   // Default to Pattern Mode

char dialog_buffer[64] = "NEWSONG.RPT";
char active_filename[64] = "UNTITLED.RPT";
uint8_t dialog_pos = 0; // Current cursor position in the string
bool is_saving = false;
bool is_dialog_active = false;

void write_order_xram(uint8_t index, uint8_t pattern_id) {
    // 1. Point the RIA to the Order List + the specific slot
    RIA.addr0 = ORDER_LIST_XRAM + index;
    RIA.step0 = 1;
    
    // 2. Write the Pattern ID into that slot
    RIA.rw0 = pattern_id;
}

uint8_t read_order_xram(uint8_t index) {
    // 1. Point the RIA to the Order List + the specific slot
    RIA.addr0 = ORDER_LIST_XRAM + index;
    RIA.step0 = 1;
    
    // 2. Read the Pattern ID from that slot and return it
    return RIA.rw0;
}

void update_order_display() {
    const uint8_t start_x = 21; // Sequence start column
    const uint8_t start_y = 3;  // Sequence start row (using rows 3, 4, 5, 6)
    
    // Total 64 slots (4 rows of 16)
    for (uint8_t i = 0; i < 64; i++) {
        // --- THE MATH FIX ---
        // row = i / 16 (0, 1, 2, 3)
        // col = i % 16 (0, 1, 2 ... 15)
        uint8_t row = i / 16; 
        uint8_t col = i % 16;
        
        uint8_t x = start_x + (col * 3); // 3 chars per slot (e.g. "00 ")
        uint8_t y = start_y + row;
        uint16_t vga_ptr = text_message_addr + (y * 80 + x) * 3;

        if (i >= song_length) {
            // Unused slots show as grey dots
            draw_string(x, y, ".. ", HUD_COL_DARKGREY, HUD_COL_BG);
        } else {
            uint8_t p_id = read_order_xram(i);
            
            // Current editing slot gets Yellow highlight
            uint8_t fg = (i == cur_order_idx) ? HUD_COL_YELLOW : HUD_COL_WHITE;
            uint8_t bg;
            if (i == cur_order_idx) {
                bg = edit_mode ? HUD_COL_EDIT_CELL : HUD_COL_PLAY_CELL;
            } else {
                bg = HUD_COL_BG;
            }
            
            // 1. Draw the Pattern ID (2-digit hex)
            draw_hex_byte_coloured(vga_ptr, p_id, fg, bg);
            
            // 2. Draw the trailing space (Clear background for non-active slots)
            draw_string(x + 2, y, " ", HUD_COL_WHITE, HUD_COL_BG);
        }
    }
}

static void read_xram_loop(uint16_t xram_addr, uint16_t count, int fd) {
    uint16_t total = 0;
    while (total < count) {
        int n = read_xram(xram_addr + total, count - total, fd);
        if (n <= 0) break;
        total += (uint16_t)n;
    }
}

static void write_xram_loop(uint16_t xram_addr, uint16_t count, int fd) {
    uint16_t total = 0;
    while (total < count) {
        int n = write_xram(xram_addr + total, count - total, fd);
        if (n <= 0) break;
        total += (uint16_t)n;
    }
}

void save_song(const char* filename) {
    int fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) return;

    uint16_t save_bpm = seq.bpm;

    write(fd, "RPT4", 4); // RPT4 Version Identifier (with custom patch overrides)
    write(fd, &current_octave, 1);
    write(fd, &current_volume, 1);
    write(fd, &song_length, 2);
    write(fd, &save_bpm, 2);

    // Save custom patch bank (256 x 11 bytes = 2816 bytes)
    write(fd, user_bank, sizeof(user_bank));

    // Save all 32 patterns ($B400 bytes)
    write_xram_loop(0x0000, 0xB400, fd); 

    // Save the 256-step Sequence Order (at $B400)
    write_xram_loop(0xB400, 0x0100, fd);

    close(fd);
}

void load_song(const char* filename) {
    int fd = open(filename, O_RDONLY);
    if (fd < 0 && strncmp(filename, "0:", 2) != 0) {
        char drive_path[64];
        snprintf(drive_path, sizeof(drive_path), "0:%s", filename);
        fd = open(drive_path, O_RDONLY);
    }
    if (fd < 0) {
        printf("Error: File not found: %s\n", filename);
        return;
    }

    char head[4];
    if (read(fd, head, 4) != 4 || head[0] != 'R') {
        printf("Error: Invalid file format\n");
        close(fd);
        return;
    }

    uint16_t loaded_bpm = 150;

    // 1. Read Metadata into 6502 RAM based on version
    if (head[3] == '4') {
        // RPT4 format: Octave (1B), Volume (1B), Song Length (2B), BPM (2B), Custom Bank (2816B)
        read(fd, &current_octave, 1);
        read(fd, &current_volume, 1);
        read(fd, &song_length, 2);
        read(fd, &loaded_bpm, 2);
        read(fd, user_bank, sizeof(user_bank));
    } else if (head[3] == '3') {
        // RPT3 format: Octave (1B), Volume (1B), Song Length (2B), BPM (2B)
        read(fd, &current_octave, 1);
        read(fd, &current_volume, 1);
        read(fd, &song_length, 2);
        read(fd, &loaded_bpm, 2);
        memcpy(user_bank, gm_bank, sizeof(user_bank));
    } else {
        // RPT2 / RPT1 format: Octave (1B), Volume (1B), Song Length (2B), default BPM = 150
        read(fd, &current_octave, 1);
        read(fd, &current_volume, 1);
        read(fd, &song_length, 2);
        loaded_bpm = 150;
        memcpy(user_bank, gm_bank, sizeof(user_bank));
    }

    // 2. Load bulk data directly into XRAM
    read_xram_loop(0x0000, 0xB400, fd); // Patterns
    read_xram_loop(0xB400, 0x0100, fd); // Sequence List

    close(fd); // Close file immediately after reading

    // 3. UPDATE LOGICAL STATE BEFORE UI REFRESH
    cur_order_idx = 0;
    cur_pattern = read_order_xram(0); 
    cur_row = 0;
    set_bpm((uint8_t)loaded_bpm);
    select_instrument(current_instrument);

    // 4. SYNC GLOBALS
    strncpy(active_filename, filename, 63);
    active_filename[63] = '\0';

    // 5. SINGLE UI REFRESH (Clears dialog and draws new data in one burst)
    refresh_all_ui(); 
    
    printf("Loaded: %s (BPM: %d)\n", active_filename, loaded_bpm);
}

void handle_filename_input() {
    // 1. Draw the Dialog Box (Centered in the Operator area)
    const uint8_t box_x = 20, box_y = 10;
    draw_string(box_x, box_y,     "+----------------------------------+", HUD_COL_WHITE, HUD_COL_BLUE);
    draw_string(box_x, box_y + 1, "| ENTER FILENAME (8.3 FORMAT):     |", HUD_COL_WHITE, HUD_COL_BLUE);
    draw_string(box_x, box_y + 2, "|                                  |", HUD_COL_WHITE, HUD_COL_BLUE);
    draw_string(box_x, box_y + 3, "| [ENTER] CONFIRM    [ESC] CANCEL  |", HUD_COL_WHITE, HUD_COL_BLUE);
    draw_string(box_x, box_y + 4, "+----------------------------------+", HUD_COL_WHITE, HUD_COL_BLUE);
    
    // Draw the current string inside the box
    draw_string(box_x + 2, box_y + 2, "                    ", HUD_COL_WHITE, HUD_COL_BG); // Clear line
    draw_string(box_x + 2, box_y + 2, dialog_buffer, HUD_COL_YELLOW, HUD_COL_BG);

    // 2. Handle Keyboard Edges
    // We scan all keys to see if a new character was pressed
    for (uint16_t k = 0; k < 256; k++) {
        if (key_pressed(k)) {
            // Check for Characters
            char c = scancode_to_ascii(k);
            if (c != 0 && dialog_pos < 12) { // 12 chars max for 8.3 + safety
                dialog_buffer[dialog_pos++] = c;
                dialog_buffer[dialog_pos] = '\0';
            }
            
            // Check for Backspace
            if (k == KEY_BACKSPACE && dialog_pos > 0) {
                dialog_buffer[--dialog_pos] = '\0';
            }

            // Check for Cancel
            if (k == KEY_ESC) {
                is_dialog_active = false;
                refresh_all_ui(); // Clear the box and restore the dashboard
                //draw_ui_dashboard(); // Refresh to hide box
                return;
            }

            // Check for Confirm
            if (k == KEY_ENTER) {
                if (is_saving) save_song(dialog_buffer);
                else load_song(dialog_buffer);
                
                is_dialog_active = false;
                refresh_all_ui(); // Clear the box and restore the dashboard
                //draw_ui_dashboard();
                return;
            }
        }
    }
}

char scancode_to_ascii(uint8_t code) {
    // Letters A-Z (HID 0x04 - 0x1D)
    if (code >= 0x04 && code <= 0x1D) {
        return 'A' + (code - 0x04);
    }
    // Numbers 1-9 (HID 0x1E - 0x26)
    if (code >= 0x1E && code <= 0x26) {
        return '1' + (code - 0x1E);
    }
    // Number 0 (HID 0x27)
    if (code == 0x27) return '0';
    // Dot/Period (HID 0x37)
    if (code == 0x37) return '.';
    
    return 0; // Not a character we care about
}

