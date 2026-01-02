#include <rp6502.h>
#include <stdio.h>
#include "constants.h"
#include "input.h"
#include "instruments.h"
#include "opl.h"
#include "player.h"
#include "screen.h"
#include "song.h"
#include "usb_hid_keys.h"
#include "effects.h"

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

void init_patterns(void) {
    // We have ~44KB of pattern space at 0x0000. 
    // Let's clear at least the first few patterns (e.g., 8 patterns).
    // 8 patterns * 2304 bytes = 18,432 bytes.
    
    RIA.addr0 = 0x0000; // Start of Pattern Data
    RIA.step0 = 1;
    
    // Clear up to 0xC000 (49,152 bytes).  This is space reserved for patterns.
    for (uint16_t i = 0; i < 49152U; i++) {
        RIA.rw0 = 0; 
    }

    for (int i=0; i<9; i++) {
        last_effect[i] = 0xFFFF;
        ch_arp[i].active = false;
        ch_porta[i].active = false;
    }

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
    init_patterns();    // Clear pattern data
    render_grid();       // Initial grid draw
    update_cursor_visuals(0, 0, 0 ,0); // Initial cursor at 0,0
    mark_playhead(0);

    // 4. Software Initialization
    init_input_system(); 
    // player_init();       // Sets up initial OPL patch

    // Default all OPL channels to Piano
    for (uint8_t i = 0; i < 9; i++){
        OPL_SetPatch(i, &gm_bank[0]);
    }

    uint8_t vsync_last = RIA.vsync;
    uint8_t prev_row = 0;
    uint8_t prev_chan = 0;
    bool prev_edit_mode = false;

    while (1) {
        while (RIA.vsync == vsync_last);
        vsync_last = RIA.vsync;

        // --- LOGIC STAGE ---
        prev_row = cur_row;
        prev_chan = cur_channel;
        prev_edit_mode = edit_mode; // Track if we toggled record mode

        // --- INPUT STAGE ---
        handle_input(); // This MUST update keystates AND prev_keystates

        if (key_pressed(KEY_ESC)) {
            OPL_Panic();
            printf("PANIC: All notes killed.\n");
        }

        if (is_dialog_active) {
            handle_filename_input();
        } else 
        {
            // Check for Shortcuts
            if (is_ctrl_down()) {
                if (key_pressed(KEY_S)) {
                    is_dialog_active = true;
                    is_saving = true;
                    dialog_pos = 0;
                    dialog_buffer[0] = '\0'; // Start with empty string
                }
                if (key_pressed(KEY_O)) {
                    is_dialog_active = true;
                    is_saving = false;
                    dialog_pos = 0;
                    dialog_buffer[0] = '\0';
                }
            }

            // Check Transport (Play/Stop)
            handle_transport_controls();

            handle_navigation();
            handle_editing(); // Check for backspace/delete

            // The Sequencer "Heartbeat"
            sequencer_step();

            player_tick();

            // Always animate the meters every frame
            update_meters();

            // Playhead Visuals
            if (play_row != last_p_row || !seq.is_playing) {
                mark_playhead(play_row);
                last_p_row = play_row;
            }
            
            // --- UI REFRESH: Row or Channel Movement
            if (cur_row != prev_row || cur_channel != prev_chan || edit_mode != prev_edit_mode) {
                update_cursor_visuals(prev_row, cur_row, prev_chan, cur_channel);

                mark_playhead(play_row);
                
                // SYNC: Ensure the OPL2 hardware channel we just moved into 
                // is loaded with our current "brush" instrument.
                if (cur_channel != prev_chan) {
                    OPL_SetPatch(cur_channel, &gm_bank[current_instrument]);
                }
            }

            // If something changed the instrument, octave, or edit mode
            // we refresh the dashboard values.
            if (edit_mode != prev_edit_mode) {
                update_dashboard(); 
            }

        }

    }
}
