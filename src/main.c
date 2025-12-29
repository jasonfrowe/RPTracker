#include <rp6502.h>
#include <stdio.h>
#include "constants.h"
#include "input.h"
#include "instruments.h"
#include "opl.h"
#include "player.h"
#include "screen.h"

unsigned text_message_addr;         // Text message address

static void init_graphics(void)
{
    // Initialize graphics here
    xregn(1, 0, 0, 1, 3); // 320x240 (4:3)

    text_message_addr = TEXT_CONFIG + sizeof(vga_mode1_config_t);
    unsigned text_storage_end = text_message_addr + MESSAGE_LENGTH * BYTES_PER_CHAR;


    xram0_struct_set(TEXT_CONFIG, vga_mode1_config_t, x_wrap, 0);
    xram0_struct_set(TEXT_CONFIG, vga_mode1_config_t, y_wrap, 0);
    xram0_struct_set(TEXT_CONFIG, vga_mode1_config_t, x_pos_px, 0); //Bug: first char duplicated if not set to zero
    xram0_struct_set(TEXT_CONFIG, vga_mode1_config_t, y_pos_px, 0);
    xram0_struct_set(TEXT_CONFIG, vga_mode1_config_t, width_chars, MESSAGE_WIDTH);
    xram0_struct_set(TEXT_CONFIG, vga_mode1_config_t, height_chars, MESSAGE_HEIGHT);
    xram0_struct_set(TEXT_CONFIG, vga_mode1_config_t, xram_data_ptr, text_message_addr);
    xram0_struct_set(TEXT_CONFIG, vga_mode1_config_t, xram_palette_ptr, 0xFFFF);
    xram0_struct_set(TEXT_CONFIG, vga_mode1_config_t, xram_font_ptr, 0xFFFF);

    // 4 parameters: text mode, 8-bit, config, plane
    xregn(1, 0, 1, 4, 1, 3, TEXT_CONFIG, 2);

    // Clear message buffer to spaces
    for (int i = 0; i < MESSAGE_LENGTH; ++i) message[i] = ' ';

    // Now write the MESSAGE_LENGTH characters into text RAM (3 bytes per char)
    RIA.addr0 = text_message_addr;
    RIA.step0 = 1;
    for (uint16_t i = 0; i < MESSAGE_LENGTH; i++) {
        RIA.rw0 = ' ';
        RIA.rw0 = HUD_COL_WHITE;
        RIA.rw0 = HUD_COL_BG;
    }

    printf("TEXT_CONFIG=0x%X\n", TEXT_CONFIG);
    printf("Text Message Addr=0x%X\n", text_message_addr);
    printf("Next Free XRAM Address: 0x%04X\n", text_storage_end);


}

int main(void)
{
    // 1. Hardware Initialization
    OPL_Config(1, OPL_ADDR);
    OPL_Init();

    // Mapping Keyboard to XRAM (Ensure KEYBOARD_INPUT matches input.c's address)
    xregn(0, 0, 0, 1, KEYBOARD_INPUT);
    xregn(0, 0, 2, 1, GAMEPAD_INPUT);

    // 2. Graphics Setup
    init_graphics();     
    clear_top_ui();      // Clear rows 0-27
    draw_ui_dashboard(); // Draw the STATIC labels (INSTRUMENT:, OP1:, etc.)
    draw_headers();      // Draw the grid headers (CH0, CH1, etc.)
    
    // 3. Initial State Draw
    update_dashboard();  // Draw the DYNAMIC values (The actual hex numbers)
    render_grid();       // Initial grid draw
    update_cursor_visuals(0, 0); 

    // 4. Software Initialization
    init_input_system(); 
    // player_init();       // Sets up initial OPL patch

    // Default all OPL channels to Piano
    for (uint8_t i = 0; i < 9; i++){
        OPL_SetPatch(i, &gm_bank[0]);
    }

    uint8_t vsync_last = RIA.vsync;

    while (1) {
        while (RIA.vsync == vsync_last);
        vsync_last = RIA.vsync;

        // --- INPUT STAGE ---
        handle_input(); // This MUST update keystates AND prev_keystates

        // --- LOGIC STAGE ---
        uint8_t prev_row = cur_row;
        bool prev_edit_mode = edit_mode; // Track if we toggled record mode

        handle_navigation(); 
        player_tick();

        // --- UI REFRESH STAGE ---
        
        // If the row moved, update highlight
        if (cur_row != prev_row) {
            update_cursor_visuals(prev_row, cur_row);
        }

        // If something changed the instrument, octave, or edit mode
        // we refresh the dashboard values.
        if (edit_mode != prev_edit_mode) {
            update_dashboard(); 
        }
    }
}
