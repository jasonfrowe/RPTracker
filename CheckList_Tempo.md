# Tempo Control Implementation Plan

## Overview
Add precise BPM (Beats Per Minute) control to the tracker with fine-grained adjustment (1 BPM increments).

## Current System Analysis

### Existing Timing Model
- **VSync Rate:** 60 Hz (60 frames per second)
- **Current Variables:**
  - `seq.ticks_per_row` = 6 (default)
  - `seq.bpm` = 125 (currently stored but not used)
- **Current Formula:** Rows per second = 60 Hz / ticks_per_row = 10 rows/sec @ speed 6
- **Problem:** Only coarse control via ticks_per_row (discrete integer steps)

### BPM Calculation Mathematics

**Standard Tracker Formula:**
```
BPM = (Frames_Per_Second * 60 * Rows_Per_Beat) / (Ticks_Per_Row * Rows_Per_Beat)
BPM = (60 * 60) / Ticks_Per_Row  (assuming 4 rows per beat)
BPM = 3600 / Ticks_Per_Row / Rows_Per_Beat
```

**Simplified (4 rows = 1 beat):**
```
At ticks_per_row = 6:
  Rows/sec = 60/6 = 10 rows/sec
  Beats/sec = 10/4 = 2.5 beats/sec
  BPM = 2.5 * 60 = 150 BPM

At ticks_per_row = 5:
  Rows/sec = 60/5 = 12 rows/sec
  Beats/sec = 12/4 = 3 beats/sec
  BPM = 3 * 60 = 180 BPM
```

**Problem:** Can only hit certain BPMs (150, 180, 120, 100...), missing 151, 152, etc.

---

## Implementation Options

### Option 1: Variable Frame Skip (Simplest)
**Concept:** Keep integer ticks_per_row but skip frames occasionally to fine-tune tempo.

**Implementation:**
- Add `uint16_t tempo_accumulator` to track fractional frames
- Add `uint16_t tempo_increment` calculated from target BPM
- Each frame: `tempo_accumulator += tempo_increment`
- When accumulator overflows 256, skip that frame's tick

**Pros:**
- Minimal code changes
- Works with existing integer tick system
- No floating point math

**Cons:**
- Tempo drifts slightly (accumulates error)
- Irregular timing (some frames skipped)
- Not precise enough for fine BPM control

**BPM Range:** 60-240 BPM with ~1 BPM steps

---

### Option 2: Fixed-Point Fractional Ticks (Recommended) ⭐
**Concept:** Use 8.8 fixed-point math (8-bit integer, 8-bit fraction) for sub-tick precision.

**Implementation:**
- Change `ticks_per_row` to `uint16_t ticks_per_row_fp` (8.8 fixed point)
- Change `tick_counter` to `uint16_t tick_counter_fp` (8.8 fixed point)
- Increment by 256 each frame (representing 1.0 tick)
- Compare: `if (tick_counter_fp >= ticks_per_row_fp)`

**Example:**
```c
// 150 BPM target:
// ticks_per_row = 6.0 → 0x0600 (1536)
// 151 BPM target:
// ticks_per_row = 5.96 → 0x05F5 (1525)

uint16_t tick_counter_fp = 0;  // 8.8 fixed point
uint16_t ticks_per_row_fp = 0x0600;  // 6.0 ticks

// Each frame:
tick_counter_fp += 256;  // Add 1.0 tick
if (tick_counter_fp >= ticks_per_row_fp) {
    tick_counter_fp -= ticks_per_row_fp;
    process_row();
}
```

**BPM Formula:**
```
ticks_per_row = (60 * 60 * 4) / BPM  // 4 rows per beat
ticks_per_row_fp = ((60 * 60 * 4) / BPM) * 256
```

**Lookup Table (for common BPMs):**
```c
// BPM → ticks_per_row (8.8 fixed point)
120 BPM → 8.00 ticks → 0x0800 (2048)
125 BPM → 7.68 ticks → 0x07AE (1966)
140 BPM → 6.86 ticks → 0x06DC (1756)
150 BPM → 6.40 ticks → 0x0666 (1638)
151 BPM → 6.36 ticks → 0x0652 (1618)
180 BPM → 5.33 ticks → 0x0555 (1365)
```

**Pros:**
- Precise BPM control (1 BPM increments)
- Smooth, consistent timing
- No drift or accumulation errors
- Fast integer-only math
- Compatible with export system

**Cons:**
- Requires refactoring tick_counter comparisons
- Need to update all tick_counter logic
- Slightly more complex to understand

**BPM Range:** 60-300 BPM with perfect 1 BPM steps

---

### Option 3: Microsecond Timer (Complex)
**Concept:** Track exact microseconds between rows instead of ticks.

**Implementation:**
- Add `uint32_t microseconds_per_row` 
- Add `uint32_t microsecond_accumulator`
- Increment accumulator by frame time (16667 µs @ 60Hz)
- Process row when accumulator exceeds microseconds_per_row

**Pros:**
- Most accurate possible timing
- Frame rate independent
- Easy BPM → microsecond conversion

**Cons:**
- 32-bit math overhead (slow on 6502)
- Requires frame timing measurement
- Complex to integrate with existing code
- Overkill for tracker needs

**BPM Range:** Any BPM with microsecond precision

---

## Recommended Approach: Option 2 (Fixed-Point)

### Why Fixed-Point is Best:
1. ✅ Precise 1 BPM control
2. ✅ Fast integer math (6502 friendly)
3. ✅ No timing drift
4. ✅ Compatible with export
5. ✅ Moderate implementation effort
6. ✅ Industry standard (many trackers use this)

---

## Implementation Checklist

### Phase 1: Data Structure Changes ✅ COMPLETED
- [x] Change `SequencerState.ticks_per_row` from `uint8_t` to `uint16_t` (8.8 fixed point) → `ticks_per_row_fp`
- [x] Change `SequencerState.tick_counter` from `uint8_t` to `uint16_t` (8.8 fixed point) → `tick_counter_fp`
- [x] Keep `SequencerState.bpm` as `uint8_t` (60-255 BPM range)
- [x] Add `#define TICK_SCALE 256` constant for readability

### Phase 2: BPM Conversion Function ✅ COMPLETED
- [x] Create `uint16_t bpm_to_ticks_fp(uint8_t bpm)` function
  - [x] Formula: `((60 * 60 * 4) / bpm) * 256` = `921600 / bpm`
  - [x] Use 32-bit intermediate math to avoid overflow
  - [x] Return 8.8 fixed-point value
- [ ] Create `uint8_t ticks_fp_to_bpm(uint16_t ticks_fp)` function (optional, for display)

### Phase 3: Update Sequencer Logic ✅ COMPLETED
- [x] Update `sequencer_step()` to use fixed-point math
  - [x] Change: `tick_counter_fp += TICK_SCALE` (add 1.0 tick per frame)
  - [x] Change: `if (tick_counter_fp >= ticks_per_row_fp)`
  - [x] Change: `tick_counter_fp -= ticks_per_row_fp` (preserve fractional part)
- [x] Update initialization in `start_export()`
  - [x] Change: `seq.tick_counter_fp = seq.ticks_per_row_fp`
- [x] Update initialization in `handle_transport_controls()` (Enter key)
  - [x] Change: `seq.tick_counter_fp = seq.ticks_per_row_fp`
- [x] Update Shift+Enter reset
  - [x] Change: `seq.tick_counter_fp = 0`

### Phase 4: UI Integration
- [ ] Add BPM display to dashboard (below "INS:" line)
  - [ ] Format: "BPM: XXX"
  - [ ] Update in `update_dashboard()` in screen.c
- [ ] Add keyboard controls for BPM change
  - [ ] **F7**: Increase BPM by 1
  - [ ] **Shift-F7**: Decrease BPM by 1
  - [ ] Clamp: 60-240 BPM reasonable range
- [ ] Update BPM when changed
  - [ ] Recalculate `seq.ticks_per_row_fp = bpm_to_ticks_fp(seq.bpm)`
  - [ ] Call from keyboard handler in player.c
- [ ] Update help screen with new shortcuts (main.hlp)

### Phase 5: File Format Integration
- [ ] Add BPM to song file format (.RPT)
  - [ ] Currently saves: octave, volume, song_length
  - [ ] Add: 1 byte for BPM after version header
  - [ ] Update save_song() in song.c
  - [ ] Update load_song() in song.c
  - [ ] Default: 125 BPM for old files

### Phase 6: Export System
- [ ] Verify export still works with fixed-point timing
  - [ ] `accumulated_delay` should still track correctly
  - [ ] Test: Export same song at different BPMs
  - [ ] Verify: Delays scale correctly with BPM
- [ ] No changes needed (export already uses frame-based delays)

### Phase 7: Effect System Compatibility ✅ NO CHANGES NEEDED

**Good News:** Effects are **frame-based**, not tick-based. The fixed-point tempo system only affects **row timing**, not effect timing.

#### How Effects Work Currently:
- Effects run in "Phase B" of `sequencer_step()` - **every frame**
- Each effect has its own **integer frame counter**
- Example: `ch_arp[ch].phase_timer++` increments each frame (60 Hz)
- When counter reaches `target_ticks`, effect advances to next step

#### Separation of Concerns:
```
┌─────────────────────────────────────────────────┐
│ SEQUENCER TIMING (8.8 fixed-point)             │
│ - Controls WHEN rows are processed             │
│ - tick_counter_fp += 256 each frame            │
│ - When tick_counter_fp >= ticks_per_row_fp:    │
│   → Process next row (notes, effect parsing)   │
└─────────────────────────────────────────────────┘
                     ↓
        Row processed at 150.0 BPM
                     ↓
┌─────────────────────────────────────────────────┐
│ EFFECT TIMING (integer, frame-based)           │
│ - Runs EVERY FRAME regardless of row timing    │
│ - phase_timer++ (integer)                      │
│ - target_ticks (integer from arp_tick_lut)     │
│ - Independent of sequencer tick_counter         │
└─────────────────────────────────────────────────┘
```

#### Effect-by-Effect Analysis:

**✅ Arpeggio** (`process_arp_logic`)
- Uses: `phase_timer++` (integer frame counter)
- Uses: `target_ticks` from `arp_tick_lut[0..15]` (integer)
- Speed values: 1, 2, 3, 6, 9, 12... frames between steps
- **Impact:** NONE - operates independently at 60 Hz frame rate
- **BPM interaction:** Arpeggio speed is frame-absolute, so faster tempo = more arp cycles per row (correct behavior)

**✅ Portamento** (`process_portamento_logic`)
- Uses: `tick_counter++` (integer frame counter)
- Uses: `speed` parameter (integer frames between pitch steps)
- **Impact:** NONE - frame-based sliding
- **Note:** Has `if (seq.tick_counter == 0) return;` guard
  - This compares against sequencer tick_counter
  - **Action needed:** Change to `if (seq.tick_counter_fp < 256) return;` (check if less than 1.0 tick)

**✅ Volume Slide** (`process_volume_slide_logic`)
- Uses: `vol_accum` (16-bit fixed-point accumulator)
- Uses: `speed_fp` (fixed-point speed)
- **Impact:** NONE - already uses fixed-point internally
- Completely independent of sequencer timing

**✅ Vibrato** (`process_vibrato_logic`)
- Uses: `tick_counter++` (integer frame counter)
- Uses: `rate` parameter (integer frames per phase step)
- **Impact:** NONE - frame-based oscillation
- Frequency modulation is frame-absolute

**✅ Note Cut** (`process_notecut_logic`)
- Uses: `tick_counter++` (integer frame counter)
- Uses: `cut_tick` parameter (integer frames until cut)
- **Impact:** NONE - frame countdown
- Example: cut_tick=5 means cut after 5 frames (83ms @ 60Hz)

**✅ Note Delay** (`process_notedelay_logic`)
- Uses: `tick_counter++` (integer frame counter)
- Uses: `delay_tick` parameter (integer frames until trigger)
- **Impact:** NONE - frame countdown

**✅ Retrigger** (`process_retrigger_logic`)
- Uses: `tick_counter++` (integer frame counter)
- Uses: `speed` parameter (integer frames between retriggers)
- **Impact:** NONE - frame-based repetition

**✅ Tremolo** (`process_tremolo_logic`)
- Uses: `phase` (integer phase counter)
- Uses: `rate` parameter (frames per phase step)
- **Impact:** NONE - frame-based volume oscillation

**✅ Fine Pitch** (`process_finepitch_logic`)
- One-shot effect, applies detune immediately
- **Impact:** NONE - no timing component

**✅ Generator** (`process_gen_logic`)
- Uses: `timer++` (integer frame counter)
- Uses: `target_ticks` from `arp_tick_lut` (integer)
- **Impact:** NONE - frame-based random note generation

#### Summary:
- **0 effects need modification** for basic fixed-point tempo
- **1 minor fix needed:** Portamento's `seq.tick_counter == 0` guard
  - Change: `if (seq.tick_counter_fp < 256) return;`
  - Ensures portamento waits for first full tick before sliding

#### Tempo-Effect Interaction (By Design):
- **Faster tempo (higher BPM):** More effect cycles per row → effects feel faster ✅
- **Slower tempo (lower BPM):** Fewer effect cycles per row → effects feel slower ✅
- **This is correct tracker behavior!** Effects sync to musical tempo
- Example: Arpeggio speed 6 (6 frames/step):
  - At 150 BPM: 6.4 ticks/row → arp cycles ~1x per row
  - At 180 BPM: 5.3 ticks/row → arp cycles faster (same absolute Hz)

#### Why This Works:
Effects are **frame-locked** (60 Hz absolute), not **tick-locked** (variable with BPM).
This is the industry-standard approach because:
1. Hardware (OPL2) operates at fixed rate
2. Effects need consistent timing regardless of tempo
3. Row timing can vary smoothly with BPM
4. Musical feel: effects "ride" on top of the tempo

- [ ] Review all effect processors for tick-based timing
  - [x] Arpeggio: Uses `target_ticks` (stays integer - OK)
  - [x] Vibrato: Uses `tick_counter` (stays integer - OK)
  - [x] Retrigger: Uses `tick_counter` (stays integer - OK)
  - [x] Note Cut: Uses `tick_counter` (stays integer - OK)
  - [x] Note Delay: Uses `tick_counter` (stays integer - OK)
  - [x] Portamento: Needs minor fix for tick 0 guard
  - [x] Volume Slide: Already uses fixed-point internally - OK
  - [x] Tremolo: Frame-based - OK
  - [x] Fine Pitch: One-shot - OK
  - [x] Generator: Frame-based - OK
- [ ] Fix portamento tick 0 guard:
  - [ ] Change `if (seq.tick_counter == 0)` to `if (seq.tick_counter_fp < 256)`
- [ ] Test effects at different tempos (60, 125, 180, 240 BPM)
  - [ ] Verify arpeggio cycles correctly
  - [ ] Verify vibrato oscillates smoothly
  - [ ] Verify portamento slides correctly
  - [ ] Verify all effects feel tempo-appropriate

### Phase 8: Testing
- [ ] Test tempo changes during playback
  - [ ] Start song, change BPM, verify immediate effect
  - [ ] Verify no clicks or glitches at BPM transition
- [ ] Test BPM range
  - [ ] 60 BPM (slowest) - verify no overflow
  - [ ] 240 BPM (fastest) - verify smooth playback
  - [ ] 150→151 BPM - verify noticeable difference
- [ ] Test export at different tempos
  - [ ] Same song at 120, 125, 150 BPM
  - [ ] Verify playback timing matches
- [ ] Test save/load with BPM
  - [ ] Save song with custom BPM
  - [ ] Load and verify BPM restored
  - [ ] Load old file, verify 125 BPM default

### Phase 9: Documentation
- [ ] Update README.md with tempo controls
- [ ] Document BPM range (60-240)
- [ ] Document keyboard shortcuts
- [ ] Update file format documentation

---

## Code Example: Fixed-Point Implementation

### Convert BPM to Fixed-Point Ticks
```c
// Convert BPM to 8.8 fixed-point ticks_per_row
uint16_t bpm_to_ticks_fp(uint8_t bpm) {
    if (bpm < 60) bpm = 60;
    if (bpm > 240) bpm = 240;
    
    // Formula: ticks = (60 * 60 * 4) / BPM
    // Multiply by 256 for 8.8 fixed point
    // Use 32-bit to avoid overflow
    uint32_t ticks = ((uint32_t)14400 * 256) / bpm;
    return (uint16_t)ticks;
}
```

### Sequencer Loop
```c
void sequencer_step(void) {
    if (!seq.is_playing) return;
    
    // Increment by 1.0 tick (256 in 8.8 fixed point)
    seq.tick_counter_fp += 256;
    
    // Check if we've accumulated enough ticks for a new row
    if (seq.tick_counter_fp >= seq.ticks_per_row_fp) {
        // Subtract (preserving fractional remainder for smooth timing)
        seq.tick_counter_fp -= seq.ticks_per_row_fp;
        
        // Process row...
        for (uint8_t ch = 0; ch < 9; ch++) {
            // ... existing row processing code
        }
    }
    
    // Effect processing runs every frame regardless
    for (uint8_t ch = 0; ch < 9; ch++) {
        process_arp_logic(ch);
        // ... other effects
    }
}
```

### BPM Control
```c
void handle_tempo_control(void) {
    if (is_ctrl_down()) {
        if (key_pressed(KEY_UP)) {
            if (is_shift_down()) {
                seq.bpm = (seq.bpm <= 230) ? seq.bpm + 10 : 240;
            } else {
                if (seq.bpm < 240) seq.bpm++;
            }
            seq.ticks_per_row_fp = bpm_to_ticks_fp(seq.bpm);
            update_dashboard();
        }
        if (key_pressed(KEY_DOWN)) {
            if (is_shift_down()) {
                seq.bpm = (seq.bpm >= 70) ? seq.bpm - 10 : 60;
            } else {
                if (seq.bpm > 60) seq.bpm--;
            }
            seq.ticks_per_row_fp = bpm_to_ticks_fp(seq.bpm);
            update_dashboard();
        }
    }
}
```

---

## Alternative: Hybrid Approach (Simpler Start)

If fixed-point seems too complex initially, consider a **hybrid approach**:

1. **Keep integer ticks_per_row for now**
2. **Add BPM as display-only** (calculated from ticks)
3. **Later migrate to fixed-point** when needed

This allows:
- Quick BPM display implementation
- Existing code works unchanged
- Smooth upgrade path to precise BPM later

**Hybrid Formula:**
```c
// Display BPM based on current ticks_per_row
uint8_t calculate_bpm_display(void) {
    // BPM ≈ (60 * 60 * 4) / (ticks_per_row * 4)
    return (uint8_t)(3600 / (seq.ticks_per_row * 4));
}
```

---

## Recommended Timeline

1. **Session 1:** Implement BPM display (hybrid approach)
2. **Session 2:** Implement fixed-point data structures
3. **Session 3:** Update sequencer logic
4. **Session 4:** Add UI controls
5. **Session 5:** File format + testing

---

## Questions to Decide

1. **BPM Range?** 60-240 recommended, or 80-200 for narrower range?
2. **Keyboard Shortcut?** Ctrl+Up/Down or different keys?
3. **File Format?** Add BPM byte now or wait?
4. **Default BPM?** Keep 125 or change to 120 (standard)?
5. **Implementation Path?** Full fixed-point or hybrid first?

---

## References

- **ProTracker:** Uses CIA timer for precise BPM (Amiga)
- **FastTracker 2:** Fixed-point timing system
- **ModPlug Tracker:** Microsecond-based timing
- **Most Modern Trackers:** 8.8 or 16.16 fixed-point standard
