#include "input.h"
#include "constants.h"
#include "usb_hid_keys.h"
#include <rp6502.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>

// External references
extern void handle_pause_input(void);
gamepad_t gamepad[GAMEPAD_COUNT];

// Keyboard state (defined in definitions.h, but declared here)
uint8_t keystates[KEYBOARD_BYTES] = {0};
uint8_t prev_keystates[KEYBOARD_BYTES] = {0}; // To track previous states
bool handled_key = false;

// Button mapping storage
ButtonMapping button_mappings[GAMEPAD_COUNT][ACTION_COUNT];

/**
 * Read keyboard and gamepad input
 */
void handle_input(void)
{
    // Copy current to previous before reading new
    for (uint8_t i = 0; i < KEYBOARD_BYTES; i++) {
        prev_keystates[i] = keystates[i];
    }

    // Read all keyboard state bytes
    RIA.addr0 = KEYBOARD_INPUT;
    RIA.step0 = 1;
    for (uint8_t i = 0; i < KEYBOARD_BYTES; i++) {
        keystates[i] = RIA.rw0;
    }
    
    // Read gamepad data
    RIA.addr0 = GAMEPAD_INPUT;
    RIA.step0 = 1;
    for (uint8_t i = 0; i < GAMEPAD_COUNT; i++) {
        gamepad[i].dpad = RIA.rw0;
        gamepad[i].sticks = RIA.rw0;
        gamepad[i].btn0 = RIA.rw0;
        gamepad[i].btn1 = RIA.rw0;
        gamepad[i].lx = RIA.rw0;
        gamepad[i].ly = RIA.rw0;
        gamepad[i].rx = RIA.rw0;
        gamepad[i].ry = RIA.rw0;
        gamepad[i].l2 = RIA.rw0;
        gamepad[i].r2 = RIA.rw0;
    }
    
}

/**
 * Initialize input system with default button mappings
 */
void init_input_system(void)
{
    // Try to load joystick configuration from file
    if (!load_joystick_config()) {
        // If file doesn't exist or fails to load, use defaults
        for (uint8_t player = 0; player < GAMEPAD_COUNT; player++) {
            reset_button_mappings(player);
        }
    }
}

/**
 * Reset to default button mappings for a specific player
 */
void reset_button_mappings(uint8_t player_id)
{
    if (player_id >= GAMEPAD_COUNT) return;
    
    // ACTION_UP: Up Arrow or Left Stick Up
    button_mappings[player_id][ACTION_UP].keyboard_key = KEY_UP;
    button_mappings[player_id][ACTION_UP].gamepad_button = GP_FIELD_STICKS; // sticks field
    button_mappings[player_id][ACTION_UP].gamepad_mask = GP_LSTICK_UP;
    
    // ACTION_DOWN: Down Arrow or Left Stick Down
    button_mappings[player_id][ACTION_DOWN].keyboard_key = KEY_DOWN;
    button_mappings[player_id][ACTION_DOWN].gamepad_button = GP_FIELD_STICKS; // sticks field
    button_mappings[player_id][ACTION_DOWN].gamepad_mask = GP_LSTICK_DOWN;
    
    // ACTION_LEFT: Left Arrow or Left Stick Left
    button_mappings[player_id][ACTION_LEFT].keyboard_key = KEY_LEFT;
    button_mappings[player_id][ACTION_LEFT].gamepad_button = GP_FIELD_STICKS; // sticks field
    button_mappings[player_id][ACTION_LEFT].gamepad_mask = GP_LSTICK_LEFT;
    
    // ACTION_RIGHT: Right Arrow or Left Stick Right
    button_mappings[player_id][ACTION_RIGHT].keyboard_key = KEY_RIGHT;
    button_mappings[player_id][ACTION_RIGHT].gamepad_button = GP_FIELD_STICKS; // sticks field
    button_mappings[player_id][ACTION_RIGHT].gamepad_mask = GP_LSTICK_RIGHT;
    
    // ACTION_PLAY: Space or A button
    button_mappings[player_id][ACTION_PLAY].keyboard_key = KEY_SPACE;
    button_mappings[player_id][ACTION_PLAY].gamepad_button = GP_FIELD_BTN0; // btn0 field
    button_mappings[player_id][ACTION_PLAY].gamepad_mask = GP_BTN_A;
    
    // ACTION_STOP: C key or B button (for sbullets)
    button_mappings[player_id][ACTION_STOP].keyboard_key = KEY_ESC;
    button_mappings[player_id][ACTION_STOP].gamepad_button = GP_FIELD_BTN0; // btn0 field
    button_mappings[player_id][ACTION_STOP].gamepad_mask = GP_BTN_X;
}

/**
 * Check if a game action is active for a specific player
 */
bool is_action_pressed(uint8_t player_id, GameAction action)
{
    if (player_id >= GAMEPAD_COUNT || action >= ACTION_COUNT) {
        return false;
    }
    
    ButtonMapping* mapping = &button_mappings[player_id][action];
    
    // Check keyboard (player 0 only for now)
    if (player_id == 0) {
        if (key(mapping->keyboard_key)) {
            return true;
        }
    }
    
    // Only check gamepad if one is connected
    if (!(gamepad[player_id].dpad & GP_CONNECTED)) {
        return false;
    }
    
    // Check gamepad
    uint8_t gamepad_value = 0;
    switch (mapping->gamepad_button) {
        case 0: gamepad_value = gamepad[player_id].dpad; break;
        case 1: gamepad_value = gamepad[player_id].sticks; break;
        case 2: gamepad_value = gamepad[player_id].btn0; break;
        case 3: gamepad_value = gamepad[player_id].btn1; break;
    }
    
    return (gamepad_value & mapping->gamepad_mask) != 0;
}

/**
 * Set button mapping for a specific action
 */
void set_button_mapping(uint8_t player_id, GameAction action,
                       uint8_t keyboard_key, uint8_t gamepad_button, uint8_t gamepad_mask)
{
    if (player_id >= GAMEPAD_COUNT || action >= ACTION_COUNT) {
        return;
    }
    
    button_mappings[player_id][action].keyboard_key = keyboard_key;
    button_mappings[player_id][action].gamepad_button = gamepad_button;
    button_mappings[player_id][action].gamepad_mask = gamepad_mask;
}

/**
 * Get current button mapping for an action
 */
ButtonMapping get_button_mapping(uint8_t player_id, GameAction action)
{
    ButtonMapping empty = {0, 0, 0};
    
    if (player_id >= GAMEPAD_COUNT || action >= ACTION_COUNT) {
        return empty;
    }
    
    return button_mappings[player_id][action];
}

/**
 * Load joystick configuration from JOYSTICK.DAT
 * Returns true if successful, false otherwise
 */
bool load_joystick_config(void)
{
    // Joystick mapping from file (matches gamepad_test.c format)
    typedef struct {
        uint8_t action_id;  // Index into actions array
        uint8_t field;      // 0=dpad, 1=sticks, 2=btn0, 3=btn1
        uint8_t mask;       // Bit mask
    } JoystickMapping;
    
    int fd = open("JOYSTICK.DAT", O_RDONLY);
    if (fd < 0) {
        return false;  // File doesn't exist
    }
    
    // Read number of mappings
    uint8_t num_mappings = 0;
    if (read(fd, &num_mappings, 1) != 1) {
        close(fd);
        return false;
    }
    
    // Read all mappings
    JoystickMapping file_mappings[10];
    int bytes_to_read = num_mappings * sizeof(JoystickMapping);
    if (read(fd, file_mappings, bytes_to_read) != bytes_to_read) {
        close(fd);
        return false;
    }
    
    close(fd);
    
    // Initialize all to defaults first
    for (uint8_t player = 0; player < GAMEPAD_COUNT; player++) {
        reset_button_mappings(player);
    }
    
    // Apply loaded mappings (assuming player 0 for now)
    for (uint8_t i = 0; i < num_mappings; i++) {
        uint8_t action_id = file_mappings[i].action_id;
        
        // Map action_id to GameAction enum
        // gamepad_test actions: THRUST(0), REVERSE THRUST(1), ROTATE LEFT(2), 
        //                       ROTATE RIGHT(3), FIRE(4), SUPER FIRE(5), PAUSE(6)
        GameAction action;
        switch (action_id) {
            case 0: action = ACTION_UP; break;
            case 1: action = ACTION_DOWN; break;
            case 2: action = ACTION_LEFT; break;
            case 3: action = ACTION_RIGHT; break;
            case 4: action = ACTION_PLAY; break;
            case 5: action = ACTION_STOP; break;
            default: continue;  // Skip invalid actions
        }
        
        // Update the gamepad mapping for player 0
        button_mappings[0][action].gamepad_button = file_mappings[i].field;
        button_mappings[0][action].gamepad_mask = file_mappings[i].mask;
    }
    
    return true;
}

/**
 * Save joystick configuration to JOYSTICK.DAT
 * Returns true if successful, false otherwise
 */
bool save_joystick_config(void)
{
    // Joystick mapping for file (matches gamepad_test.c format)
    typedef struct {
        uint8_t action_id;  // Index into actions array
        uint8_t field;      // 0=dpad, 1=sticks, 2=btn0, 3=btn1
        uint8_t mask;       // Bit mask
    } JoystickMapping;
    
    int fd = open("JOYSTICK.DAT", O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) {
        return false;
    }
    
    // Prepare mappings to save (player 0 only)
    JoystickMapping file_mappings[ACTION_COUNT];
    uint8_t num_mappings = ACTION_COUNT;
    
    // Map GameAction to action_id
    for (uint8_t i = 0; i < ACTION_COUNT; i++) {
        file_mappings[i].action_id = i;
        file_mappings[i].field = button_mappings[0][i].gamepad_button;
        file_mappings[i].mask = button_mappings[0][i].gamepad_mask;
    }
    
    // Write number of mappings
    if (write(fd, &num_mappings, 1) != 1) {
        close(fd);
        return false;
    }
    
    // Write all mappings
    int bytes_to_write = num_mappings * sizeof(JoystickMapping);
    if (write(fd, file_mappings, bytes_to_write) != bytes_to_write) {
        close(fd);
        return false;
    }
    
    close(fd);
    return true;
}

/**
 * Quick interactive input test: stays running until PAUSE action (START) is pressed.
 * Reports mapped action press/release events for player 0 only.
 */
#ifdef INPUT_TEST
void init_input_system_test(void)
{
    printf("\nInput test mode: press mapped gamepad buttons to see actions.\n");
    printf("Press START (mapped to PAUSE action) to finish the test.\n");

    uint8_t vsync_last_test = RIA.vsync;

    bool action_pressed[ACTION_COUNT];
    for (uint8_t i = 0; i < ACTION_COUNT; i++) action_pressed[i] = false;

    const char *action_names[ACTION_COUNT] = {
        "THRUST",
        "REVERSE_THRUST",
        "ROTATE_LEFT",
        "ROTATE_RIGHT",
        "FIRE",
        "SUPER_FIRE",
        "PAUSE"
    };

    while (true) {
        if (RIA.vsync == vsync_last_test)
            continue;
        vsync_last_test = RIA.vsync;

        // Read gamepad data only
        RIA.addr0 = GAMEPAD_INPUT;
        RIA.step0 = 1;
        for (uint8_t i = 0; i < GAMEPAD_COUNT; i++) {
            gamepad[i].dpad = RIA.rw0;
            gamepad[i].sticks = RIA.rw0;
            gamepad[i].btn0 = RIA.rw0;
            gamepad[i].btn1 = RIA.rw0;
            gamepad[i].lx = RIA.rw0;
            gamepad[i].ly = RIA.rw0;
            gamepad[i].rx = RIA.rw0;
            gamepad[i].ry = RIA.rw0;
            gamepad[i].l2 = RIA.rw0;
            gamepad[i].r2 = RIA.rw0;
        }

        // Check each mapped action for player 0 and report events
        for (uint8_t action = 0; action < ACTION_COUNT; action++) {
            ButtonMapping *m = &button_mappings[0][action];
            uint8_t value = 0;
            switch (m->gamepad_button) {
                case 0: value = gamepad[0].dpad; break;
                case 1: value = gamepad[0].sticks; break;
                case 2: value = gamepad[0].btn0; break;
                case 3: value = gamepad[0].btn1; break;
                default: value = 0; break;
            }

            bool now = (value & m->gamepad_mask) != 0;
            if (now && !action_pressed[action]) {
                action_pressed[action] = true;
                printf("Action %s pressed\n", action_names[action]);
                if (action == ACTION_PAUSE) {
                    printf("PAUSE action pressed — exiting input test.\n");
                    return;
                }
            } else if (!now && action_pressed[action]) {
                action_pressed[action] = false;
                printf("Action %s released\n", action_names[action]);
            }
        }
    }
}
#endif // INPUT_TEST
