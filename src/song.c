#include <rp6502.h>
#include <stdint.h>
#include <stdbool.h>
#include "constants.h"
#include "screen.h"
#include "song.h"

uint8_t cur_order_idx = 0; // Where we are in the playlist
uint16_t song_length = 1;   // Total number of patterns in the song
bool is_song_mode = false;   // Default to Pattern Mode

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
    // Draw the Song Order List (Playlist)
    // Format: SEQ: 00 00 [01] 02 00 ...
    draw_string(2, 3, "SEQ:", HUD_COL_CYAN, HUD_COL_BG);
    
    // We'll show a window of 10 slots
    for (uint8_t i = 0; i < 10; i++) {
        uint16_t vga_ptr = text_message_addr + (3 * 80 + 7 + i * 3) * 3;
        
        // If we are past the song length, draw dots
        if (i >= song_length) {
            draw_string(7 + i * 3, 3, ".. ", HUD_COL_DARKGREY, HUD_COL_BG);
        } else {
            uint8_t p_id = read_order_xram(i);
            
            // Highlight the current playing/editing slot in Yellow
            uint8_t color = (i == cur_order_idx) ? HUD_COL_YELLOW : HUD_COL_WHITE;
            uint8_t bg = (i == cur_order_idx) ? HUD_COL_HIGHLIGHT : HUD_COL_BG;
            
            draw_hex_byte(vga_ptr, p_id);
            // Manually set the color for the hex byte since draw_hex_byte uses defaults
            set_text_color(7 + i * 3, 3, 2, color, bg);
        }
    }
    
    // Show total length
    draw_string(40, 3, "LEN:", HUD_COL_CYAN, HUD_COL_BG);
    draw_hex_byte(text_message_addr + (3 * 80 + 45) * 3, (uint8_t)song_length);
}