#ifndef SONG_H
#define SONG_H

#define ORDER_LIST_XRAM 0xB000
#define MAX_ORDERS 256

extern uint8_t cur_order_idx;
extern uint16_t song_length;
extern bool is_song_mode;

extern void update_order_display();
extern uint8_t read_order_xram(uint8_t index);
extern void write_order_xram(uint8_t index, uint8_t pattern_id);

#endif // SONG_H