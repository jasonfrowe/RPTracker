#ifndef OPL_H
#define OPL_H

#define MUSIC_FILENAME "music.bin"

typedef struct {
    uint16_t delay_ms; 
    uint8_t type;      // 0: Off, 1: On, 3: Patch
    uint8_t channel;   
    uint8_t note;      
    uint8_t velocity;  // Unused for now
} SongEvent;

extern uint8_t shadow_b0[9]; 
extern uint8_t shadow_ksl_m[9];
extern uint8_t shadow_ksl_c[9];
extern uint8_t opl_hardware_shadow[256];

extern const uint16_t fnum_table[12];

extern uint16_t current_event_idx;
extern uint16_t ticks_until_next_event;

extern void OPL_NoteOn(uint8_t channel, uint8_t midi_note);
extern void OPL_NoteOff(uint8_t channel);
extern void OPL_SetPitch(uint8_t channel, uint8_t midi_note); // Change pitch without retriggering
extern void OPL_Clear();
extern void OPL_Write(uint8_t reg, uint8_t value);
extern void OPL_SetVolume(uint8_t chan, uint8_t velocity);
extern void OPL_Init();
extern void OPL_FifoClear();
extern void OPL_SilenceAll();
extern void OPL_Config(uint8_t enable, uint16_t addr);
extern void OPL_SetPitch(uint8_t channel, uint8_t midi_note);
extern void OPL_NoteOn_Detuned(uint8_t channel, uint8_t midi_note, int8_t detune);

#endif // OPL_H