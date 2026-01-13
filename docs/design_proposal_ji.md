# Just Intonation (JI) Implementation Proposal

This document explores how to implement **Just Intonation** (tuning based on pure integer ratios) in RPTracker, replacing or augmenting the current 12-TET (Equal Temperament) system.

## 1. Core Concept: The "Global Root"
Unlike 12-TET, where every note is equally spaced, JI requires a **Root Frequency** (the "Key" of the song) to which all other notes are related via ratios.

*   **Current System:** `Note -> lookup TET_Table[Note]`
*   **Proposed JI System:** `Note -> Root_Freq * Ratio_Table[Note % 12] * 2^Octave`

## 2. User Interface Changes
To support this without redesigning the entire tracker grid, we can map the 12 tracker "notes" (C, C#, D...) to 12 customizable JI intervals.

### A. The Tuning Screen (New UI Page)
A new screen (accessible via `F1` or a menu) to define the scale.
```text
  JUST INTONATION SETUP
  ---------------------
  GLOBAL ROOT NOTE: C-4 (261.63 Hz) < Adjusts the base frequency >

  NOTE MAPPING (12 Slots):
  KEY   RATIO   CENTS   TYPE
  ---   -----   -----   ----
  C-    1/1     +0      Unison
  C#    16/15   +112    Min 2nd
  D-    9/8     +204    Maj 2nd
  D#    6/5     +316    Min 3rd
  E-    5/4     +386    Maj 3rd
  ...
```
*   **Presets:** Allow loading common JI scales (Ptolemy's Intense Diatonic, 5-Limit, 7-Limit, Pythogorean).
*   **Custom:** User can type `NUM / DEN` for each slot.

### B. The Tracker Grid
The grid remains visually identical (`C-4`, `E-4`), but the *sound* changes.
*   `C-4` plays the **Root**.
*   `E-4` plays the **Root * 5/4**.
*   `G-4` plays the **Root * 3/2**.

**Visual Feedback:**
Optionally, we could color-code notes based on their consonance (simple ratios like 3/2 = Green, complex like 45/32 = Red), but standard note names are likely still the most practical entry method.

## 3. Underlying Code Changes (`opl.c`)

### A. Math Strategy (Fixed Point)
The 6502 cannot easily handle floating point math ($261.63 \times 1.25$). We need a fixed-point multiplication system.

**Proposed Algorithm:**
1.  **Base Frequency Table (High Precision):** Store the frequency of the Global Root (e.g., Middle C) as a raw OPL2 "Block + F-Num" value, or as a raw linear frequency scalar.
2.  **Ratio Lookup Table:** Store the 12 ratios as 16-bit fixed-point multipliers (Format 8.8 or 4.12).
    *   Example: `5/4` = 1.25. In 12-bit fixed point: `1.25 * 4096 = 5120`.

```c
// Pseudo-code OPL adaptation
uint16_t get_ji_freq(uint8_t note_index, uint8_t octave) {
    uint8_t interval = note_index % 12;
    uint32_t ratio_fixed = current_scale_ratios[interval]; // e.g., 5120 for Maj 3rd
    
    // Calculate fundamental freq for this octave
    uint32_t octave_root = global_root_freq >> (8 - octave); // Simple bit shift
    
    // Apply Ratio
    uint32_t target_freq = (octave_root * ratio_fixed) >> 12; // Divide by fixed-point scale
    
    return convert_freq_to_opl_fnum(target_freq);
}
```

### B. Handling OPL2 "Block" Limits
The OPL2 uses `Block` (0-7) and `F-Num` (0-1023).
*   In JI, intervals aren't uniform. A "Wolf Fifth" might push a note into the next block unexpectedly.
*   **Solution:** We must use the "Dynamic Block Selection" logic I just wrote for the 8-bit finepitch. Calculate the raw linear target frequency first, *then* find the best Block/F-Num combo to represent it.

## 4. Pros & Cons

| Feature | Just Intonation | 12-TET (Current) |
| :--- | :--- | :--- |
| **Harmony** | Perfectly stable, beat-free chords | Slight beating (especially 3rds) |
| **Key Changes** | Challenging (Requires changing Root) | Seamless / Transparent |
| **CPU Usage** | Higher (Multiplication per Note On) | Zero (Table Lookup) |
| **Glissando** | Smooth ratio slides | Steppy semitones |

## 5. Recommendation
If implementing this, add a **"Tuning Mode"** toggle in the song settings:
1.  **Standard (Default):** Uses pure lookup tables (Fast).
2.  **Custom/JI:** Uses the calculation engine (Flexible).

This preserves performance for standard tracking while opening up xenharmonic capabilities for advanced users.
