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

// Volume Slide State with 8.8 Fixed Point Arithmetic
typedef struct {
    uint8_t current_vol;
    uint8_t base_note;
    uint8_t inst;
    uint8_t speed;
    uint8_t tick_counter;
    uint16_t vol_accum;   // 8.8 Fixed Point (Integer part in high byte: 0-63)
    uint16_t speed_fp;    // Fixed point increment per tick
    uint8_t  target_vol;  // Target integer volume (0-63)
    uint8_t  mode;        // 0:Up, 1:Down, 2:To Target
    bool     active;
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

typedef struct {
    uint8_t cut_tick;     // Tick count when to cut
    uint8_t tick_counter;
    bool    active;
} NoteCutState;

typedef struct {
    uint8_t delay_tick;   // Tick count when to trigger
    uint8_t note;         // Note to trigger
    uint8_t inst;
    uint8_t vol;
    uint8_t tick_counter;
    bool    active;
    bool    triggered;    // Prevent double-trigger
} NoteDelayState;

typedef struct {
    uint8_t note;
    uint8_t inst;
    uint8_t vol;
    uint8_t speed;        // Ticks between retriggers
    uint8_t tick_counter;
    bool    active;
} RetriggerState;

typedef struct {
    uint8_t base_vol;
    uint8_t note;
    uint8_t inst;
    uint8_t rate;         // Oscillation speed
    uint8_t depth;        // Volume deviation
    uint8_t waveform;     // 0=sine, 1=triangle, 2=square
    uint8_t phase;        // Current wave position (0-255)
    uint8_t tick_counter;
    bool    active;
} TremoloState;

typedef struct {
    uint8_t base_note;
    int8_t  detune;       // Signed detune in 1/16 semitones
    uint8_t inst;
    uint8_t vol;
    bool    active;
} FinePitchState;

typedef struct {
    uint8_t base_note;
    uint8_t inst;
    uint8_t vol;
    uint8_t scale;
    uint8_t range;       // D nibble
    uint8_t target_ticks;
    uint8_t timer;
    bool    active;
} GenState;

extern ArpState ch_arp[9];
extern PortamentoState ch_porta[9];
extern VolumeSlideState ch_volslide[9];
extern VibratoState ch_vibrato[9];
extern NoteCutState ch_notecut[9];
extern NoteDelayState ch_notedelay[9];
extern RetriggerState ch_retrigger[9];
extern TremoloState ch_tremolo[9];
extern FinePitchState ch_finepitch[9];
extern GenState ch_gen[9];

extern void process_arp_logic(uint8_t ch);
extern void process_portamento_logic(uint8_t ch);
extern void process_volume_slide_logic(uint8_t ch);
extern void process_vibrato_logic(uint8_t ch);
extern void process_notecut_logic(uint8_t ch);
extern void process_notedelay_logic(uint8_t ch);
extern void process_retrigger_logic(uint8_t ch);
extern void process_tremolo_logic(uint8_t ch);
extern void process_finepitch_logic(uint8_t ch);
extern int16_t get_arp_offset(uint8_t style, uint8_t depth, uint8_t index);
extern const uint8_t arp_tick_lut[16];
extern void process_gen_logic(uint8_t ch);

#endif // EFFECTS_C