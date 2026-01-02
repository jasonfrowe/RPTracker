#ifndef CONSTANTS_H
#define CONSTANTS_H

// Screen dimensions
#define SCREEN_WIDTH 640
#define SCREEN_HEIGHT 480

// Text message dimensions
#define MESSAGE_WIDTH  80           // Width of the text message area in characters
#define MESSAGE_HEIGHT 60           // Height of the text message area in characters
#define MESSAGE_LENGTH (MESSAGE_WIDTH * MESSAGE_HEIGHT) // Total number of characters in the message area
#define BYTES_PER_CHAR 3            // Number of bytes per character in text RAM

#define MAX_PATTERNS 32 // Maximum number of patterns

#define TEXT_CONFIG 0xC000          // Text Plane Configuration
extern unsigned text_message_addr; // Address where text message starts in XRAM

// 5. Keyboard, Gamepad and Sound
// -------------------------------------------------------------------------
#ifdef USE_NATIVE_OPL2
  #define OPL_ADDR 0xFE00 
#else
  #define OPL_ADDR 0xFF00 // Old FPGA PIX address
#endif

#define GAMEPAD_INPUT   0xFF78  // XRAM address for gamepad data
#define KEYBOARD_INPUT  0xFFA0  // XRAM address for keyboard data
#define PSG_XRAM_ADDR   0xFFC0  // PSG memory location (must match sound.c)

// Controller input
#define GAMEPAD_COUNT 4       // Support up to 4 gamepads
#define GAMEPAD_DATA_SIZE 10  // 10 bytes per gamepad
#define KEYBOARD_BYTES  32    // 32 bytes for 256 key states

// Button definitions
#define KEY_ESC 0x29       // ESC key
#define KEY_ENTER 0x28     // ENTER key

// Hardware button bit masks - DPAD
#define GP_DPAD_UP        0x01
#define GP_DPAD_DOWN      0x02
#define GP_DPAD_LEFT      0x04
#define GP_DPAD_RIGHT     0x08
#define GP_SONY           0x40  // Sony button faces (Circle/Cross/Square/Triangle)
#define GP_CONNECTED      0x80  // Gamepad is connected

// Hardware button bit masks - ANALOG STICKS
#define GP_LSTICK_UP      0x01
#define GP_LSTICK_DOWN    0x02
#define GP_LSTICK_LEFT    0x04
#define GP_LSTICK_RIGHT   0x08
#define GP_RSTICK_UP      0x10
#define GP_RSTICK_DOWN    0x20
#define GP_RSTICK_LEFT    0x40
#define GP_RSTICK_RIGHT   0x80

// Hardware button bit masks - BTN0 (Face buttons and shoulders)
// Per RP6502 documentation: https://picocomputer.github.io/ria.html#gamepads
#define GP_BTN_A          0x01  // bit 0: A or Cross
#define GP_BTN_B          0x02  // bit 1: B or Circle
#define GP_BTN_C          0x04  // bit 2: C or Right Paddle
#define GP_BTN_X          0x08  // bit 3: X or Square
#define GP_BTN_Y          0x10  // bit 4: Y or Triangle
#define GP_BTN_Z          0x20  // bit 5: Z or Left Paddle
#define GP_BTN_L1         0x40  // bit 6: L1
#define GP_BTN_R1         0x80  // bit 7: R1

// Hardware button bit masks - BTN1 (Triggers and special buttons)
#define GP_BTN_L2         0x01  // bit 0: L2
#define GP_BTN_R2         0x02  // bit 1: R2
#define GP_BTN_SELECT     0x04  // bit 2: Select/Back
#define GP_BTN_START      0x08  // bit 3: Start/Menu
#define GP_BTN_HOME       0x10  // bit 4: Home button
#define GP_BTN_L3         0x20  // bit 5: L3
#define GP_BTN_R3         0x40  // bit 6: R3

// --- HUD CONSTANTS (ANSI 8-bit Palette) ---
#define HUD_COL_BG      0   // Black
#define HUD_COL_RED     9   // Bright Red
#define HUD_COL_GREEN   10  // Bright Green
#define HUD_COL_YELLOW  11  // Bright Yellow (Fixed)
#define HUD_COL_BLUE    12  // Bright Blue
#define HUD_COL_MAGENTA 13  // Bright Magenta
#define HUD_COL_DPURPLE 61 // Dark Purple (Custom)
#define HUD_COL_CYAN    14  // Bright Cyan   (Fixed)
#define HUD_COL_WHITE   15  // Bright White
#define HUD_COL_PURPLE 16  // Purple
#define HUD_COL_ORANGE 214  // Bright Orange (contrasts with red and blue backgrounds)

#define HUD_COL_SAGEGREEN 113 // Sage Green for text labels

#define HUD_COL_DARKGREY 8     // Dark Grey ANSI
#define HUD_COL_BAR      238   // Dark Grey for bars (every 4th row)

#define HUD_COL_HIGHLIGHT 18   // Dark Blue (Classic Tracker style)
#define HUD_COL_SELECT_BG    21  // Brighter Blue (The active cell background)
#define HUD_COL_CURSOR    12  // Bright Blue (Modern style)

    
// --- Edit Mode Colors ---
#define HUD_COL_EDIT_BAR    88   // Deep Dark Red (The whole row)
#define HUD_COL_EDIT_CELL   196  // Vivid Bright Red (The active cell)

// --- Existing Blue Colors (for reference) ---
#define HUD_COL_PLAY_BAR    18   // Deep Blue
#define HUD_COL_PLAY_CELL   21   // Bright Blue

  

#endif // CONSTANTS_H
