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

char dialog_buffer[13] = "NEWSONG.RPT";
char active_filename[13] = "UNTITLED.RPT"; // 8.3 format + null terminator
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

void save_song(const char* filename) {
    int fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) return;

    write(fd, "RPT2", 4); // New Version Identifier
    write(fd, &current_octave, 1);
    write(fd, &current_volume, 1);
    write(fd, &song_length, 2);

    // Save all 32 patterns ($B400 bytes)
    write_xram(0x0000, 0xB400, fd); 

    // Save the 256-step Sequence Order (at $B400)
    write_xram(0xB400, 0x0100, fd);

    close(fd);
}

void load_song(const char* filename) {
    int fd = open(filename, O_RDONLY);
    if (fd < 0) {
        printf("Error: File not found\n");
        return;
    }

    char head[4];
    if (read(fd, head, 4) != 4 || head[0] != 'R') {
        printf("Error: Invalid file\n");
        close(fd);
        return;
    }

    // Read Metadata back into 6502 RAM
    read(fd, &current_octave, 1);
    read(fd, &current_volume, 1);
    read(fd, &song_length, 2);

    // Load bulk data directly back into XRAM
    // read_xram handles the busy-wait internally
    read_xram(0x0000, 0xB400, fd);
    read_xram(0xB400, 0x0100, fd);

    // Update the active filename global
    strncpy(active_filename, filename, 12);
    active_filename[12] = '\0';

    // Refresh everything
    refresh_all_ui(); 

    close(fd);

    // Sync UI
    cur_order_idx = 0;
    cur_pattern = read_order_xram(0);
    cur_row = 0;
    refresh_all_ui();
    printf("Loaded: %s\n", filename);
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

