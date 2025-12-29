#ifndef SCREEN_H
#define SCREEN_H

#include <stdint.h>
#include <stdbool.h>

extern char message[];

// XRAM Pattern Base (from our map)
#define PATTERN_XRAM_BASE 0x0000

#define GRID_SCREEN_OFFSET 28 // The grid starts at Screen Row 28

typedef struct {
    uint8_t note;       // MIDI Note (0=None, 255=Off)
    uint8_t inst;       // Instrument index
    uint8_t vol;        // Volume (0-63)
    uint8_t effect;     // Combined Effect/Param (for now)
} PatternCell;

extern bool edit_mode;
extern uint8_t cur_row;
extern uint8_t cur_pattern;
extern uint8_t cur_channel;

extern void write_cell(uint8_t pat, uint8_t row, uint8_t chan, PatternCell *cell);
extern void render_grid(void);
extern void update_cursor_visuals(uint8_t old_row, uint8_t new_row);
extern void draw_headers(void);
extern void draw_ui_dashboard(void);
extern void clear_top_ui(void);
extern void update_dashboard(void);

#endif // SCREEN_H