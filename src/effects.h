#ifndef EFFECTS_C
#define EFFECTS_C

typedef struct {
    uint8_t base_note;
    uint8_t inst;
    uint8_t vol;
    uint8_t style;
    uint8_t depth;
    uint8_t speed_idx;   // The T nibble (0-F)
    uint8_t target_ticks; // Value from LUT
    uint8_t phase_timer;  // Accumulator
    uint8_t step_index;  
    bool    active;
    bool    just_triggered; // Prevents double-hit on same frame
} ArpState;

typedef struct {
    uint8_t current_note;
    uint8_t target_note;
    uint8_t inst;
    uint8_t vol;
    uint8_t mode;         // 0=Up, 1=Down, 2=To Target
    uint8_t speed;        // Ticks between steps
    uint8_t tick_counter;
    bool    active;
} PortamentoState;

typedef struct {
    uint8_t current_vol;
    uint8_t target_vol;
    uint8_t inst;
    uint8_t base_note;
    uint8_t mode;         // 0=Up, 1=Down, 2=To Target
    uint8_t speed;        // Volume units per tick
    uint8_t tick_counter;
    bool    active;
} VolumeSlideState;

typedef struct {
    uint8_t base_note;
    uint8_t inst;
    uint8_t vol;
    uint8_t rate;         // Oscillation speed (ticks per cycle step)
    uint8_t depth;        // Pitch deviation (semitones/fine)
    uint8_t waveform;     // 0=sine, 1=triangle, 2=square
    uint8_t phase;        // Current position in wave (0-255)
    uint8_t tick_counter;
    bool    active;
} VibratoState;

extern ArpState ch_arp[9];
extern PortamentoState ch_porta[9];
extern VolumeSlideState ch_volslide[9];
extern VibratoState ch_vibrato[9];

extern void process_arp_logic(uint8_t ch);
extern void process_portamento_logic(uint8_t ch);
extern void process_volume_slide_logic(uint8_t ch);
extern void process_vibrato_logic(uint8_t ch);
extern int16_t get_arp_offset(uint8_t style, uint8_t depth, uint8_t index);
extern const uint8_t arp_tick_lut[16];

#endif // EFFECTS_C