#ifndef SCREEN_H
#define SCREEN_H

#include <stdint.h>
#include <stdbool.h>

extern char message[];
extern uint8_t ch_peaks[9]; // Peak meter state

// XRAM Pattern Base (from our map)
#define PATTERN_XRAM_BASE 0x0000

#define GRID_SCREEN_OFFSET 28 // The grid starts at Screen Row 28

typedef struct {
    uint8_t note;       // MIDI Note (0=None, 255=Off)
    uint8_t inst;       // Instrument index
    uint8_t vol;        // Volume (0-63)
    uint16_t effect;    // 16-bit Command + Params
} PatternCell;

extern bool edit_mode;
extern uint8_t cur_row;
extern uint8_t cur_pattern;
extern uint8_t cur_channel;
extern uint8_t last_p_row;

extern void write_cell(uint8_t pat, uint8_t row, uint8_t chan, PatternCell *cell);
extern void render_grid(void);
extern void update_cursor_visuals(uint8_t old_row, uint8_t new_row, uint8_t old_ch, uint8_t new_ch);
extern void draw_headers(void);
extern void draw_ui_dashboard(void);
extern void clear_top_ui(void);
extern void update_dashboard(void);
extern void render_row(uint8_t pattern_row_idx);
extern void read_cell(uint8_t pat, uint8_t row, uint8_t chan, PatternCell *cell);
extern void draw_string(uint8_t x, uint8_t y, const char* s, uint8_t fg, uint8_t bg);
extern void draw_hex_byte(uint16_t vga_addr, uint8_t val);
extern void draw_hex_byte_coloured(uint16_t vga_addr, uint8_t val, uint8_t fg, uint8_t bg);
extern void set_text_color(uint8_t x, uint8_t y, uint8_t len, uint8_t fg, uint8_t bg);
extern void update_meters(void);
extern void refresh_all_ui(void);
extern void mark_playhead(uint8_t pattern_row);

#endif // SCREEN_H