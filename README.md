# RPTracker (v0.2)
### A Native 6502 Music Tracker for the RP6502 (Picocomputer)

**RPTracker** is a high-performance music composition tool designed for the [RP6502 Picocomputer](https://github.com/picocomputer). It features a 640x480 VGA interface and targets both the **Native RIA OPL2** emulation and the **FPGA OPL2** sound card. It provides a classic "tracker" workflow with modern high-resolution visual feedback.


![Screen Shot](images/Tracker_screenshot.png)

---

## 🎹 Keyboard Control Reference

### 1. Musical Keyboard (Piano Layout)
RPTracker uses a standard "FastTracker II" style layout mapping:
*   **Lower Octave (C-3 to B-3):** `Z S X D C V G B H N J M`
*   **Upper Octave (C-4 to B-4):** `Q 2 W 3 E R 5 T 6 Y 7 U`

### 2. Global "Brush" Controls
These keys adjust the settings used when recording **new** notes.
*   **F1 / F2**: Decrease / Increase global keyboard **Octave**.
*   **F3 / F4**: Previous / Next **Instrument** (Wraps 00-FF).
*   **[ / ]**: Decrease / Increase global **Volume** (Range 00-3F).
*   **F5**: **Instrument Pick.** Samples the Note, Instrument, and Volume from the grid into your current Brush.

### 3. Navigation & Transport
*   **Arrow Keys**: Navigate the 9-channel pattern grid.
*   **F6**: **Play / Pause.** In Song Mode, starts from the current Sequence slot. In Pattern Mode, loops the current pattern.
*   **F7 / ESC**: **Stop & Panic.** Stops playback, resets the cursor to Row 00, and silences all hardware voices immediately.
*   **F8**: **Toggle Playback Mode.** Switch between **PATTERN** (loops current patterns) and **SONG** (follows the Sequence Order List).

### 4. Editing & Grid Commands
*   **Spacebar**: Toggle **Edit Mode**.
    *   *Blue Cursor:* Navigation only. Piano keys play sounds for preview.
    *   *Red Cursor:* Record Mode. Piano keys enter data and auto-advance.
*   **Backspace / Delete**: Clear the current cell.
*   **Tilde ( ` )**: Insert **Note Off** (`===`).
*   **- / =**: **Transpose** the note in the current cell by **1 semitone**.
*   **SHIFT + - / =**: **Transpose** the current cell by **12 semitones (1 Octave)**.
*   **SHIFT + F3 / F4**: Change the **Instrument** of the current cell only.
*   **SHIFT + [ / ]**: Adjust the **Volume** of the current cell only.

### 5. Pattern & Sequence Management
*   **F9 / F10**: Jump to Previous / Next **Pattern ID**.
*   **F11 / F12**: Jump to Previous / Next **Sequence Slot** (Playlist position).
*   **SHIFT + F11 / F12**: Change the **Pattern ID** assigned to the current Sequence Slot.
*   **ALT + F11 / F12**: Decrease / Increase total **Song Length**.

### 6. Clipboard & Files
*   **Ctrl + C**: **Copy** the current 32-row pattern to the internal RAM clipboard.
*   **Ctrl + V**: **Paste** the clipboard into the current pattern (overwrites existing data).
*   **Ctrl + S**: **Save Song.** Opens a dialog to save the current song to the USB drive as an `.RPT` file.
*   **Ctrl + O**: **Load Song.** Opens a dialog to load an `.RPT` file from the USB drive.

### Effect Mode (Toggle with '/')
*   **SHIFT + [ / ]** : Change Command (Digit 1 - `X000`)
*   **[ / ]**       : Change Style   (Digit 2 - `0X00`)
*   **' (Apostrophe)**: Increase Params (Digits 3 & 4 - `00XX`)
*   **; (Semicolon)** : Decrease Params (Digits 3 & 4 - `00XX`)
*   *Note: Hold SHIFT with ; or ' to jump by 0x10 for faster parameter scrolling.*

### 🎹 Effect Command 1: Advanced Arpeggio (1SDT)
RPTracker uses a 16-bit effect system (4 hex digits) when in Effect View (`/`).
The Arpeggio engine retriggers the note on every cycle step to ensure a crisp, chiptune attack.

**Format: `1 S D T`**

*   **1**: Command ID (Arpeggio).
*   **S (Style)**: The movement pattern (16 styles available):
    
    **Basic 2-Note Patterns:**
    *   `0`: **UP** - Root, +Depth (classic oscillation)
    *   `1`: **DOWN** - +Depth, Root (inverted oscillation)
    *   `D`: **OCTAVE** - Root, +12 (octave jumps)
    *   `E`: **FIFTH** - Root, +7 (power chord pattern)
    
    **Major/Minor Triads (4-Note with Octave):**
    *   `2`: **MAJOR** - Root, +4, +7, +12 - *Ignores D*
    *   `3`: **MINOR** - Root, +3, +7, +12 - *Ignores D*
    *   `6`: **SUS4** - Root, +5, +7, +12 - *Ignores D*
    *   `7`: **SUS2** - Root, +2, +7, +12 - *Ignores D*
    *   `9`: **AUG** - Root, +4, +8, +12 (augmented) - *Ignores D*
    
    **7th Chords (4-Note):**
    *   `4`: **MAJ7** - Root, +4, +7, +11 (jazzy) - *Ignores D*
    *   `5`: **MIN7** - Root, +3, +7, +10 (bluesy) - *Ignores D*
    *   `8`: **DIM** - Root, +3, +6, +9 (diminished tension) - *Ignores D*
    
    **Special Effects:**
    *   `A`: **POWER** - Root, +7, +12, +12 (rock power chord) - *Ignores D*
    *   `B`: **UPDOWN** - Root, +Depth, +Depth, Root (bounce pattern)
    *   `C`: **UP3** - Root, +Depth, +Depth×2, +Depth×3 (climbing)
    *   `F`: **DOUBLE** - Root, Root, +Depth, +Depth (stutter effect)

*   **D (Depth)**: The interval in semitones (0-F). Used by styles 0, 1, B, C, F. Ignored by chord patterns.
*   **T (Timing)**: How fast the notes cycle (mapped to a Musical LUT):
    *   `0-2`: High-speed "Buzz" (1-3 VSync frames)
    *   `3`: **1 Row** (6 ticks) - Perfect for 4-note chords in 32-row patterns
    *   `4-6`: Mid-tempo (9-18 ticks)
    *   `7`: **2 Rows** (24 ticks)
    *   `B`: **4 Rows** (48 ticks)
    *   `F`: **16 Rows** (96 ticks)

**Usage:**
- `12C3`: Major chord arpeggio, 1 row per note (4 rows/cycle, 8 cycles per pattern).
- `13C3`: Minor chord arpeggio, 1 row per note.
- `10C3`: Simple octave oscillation (Up, 12 semitones, every 1 row).
- `14C3`: Jazzy Maj7 arpeggio.
- `1AC1`: Fast power chord buzz effect.
- `F000` or `0000`: Stop all effects on the channel.

---

### 🎶 Effect Command 2: Portamento (2SDT)
Portamento creates smooth pitch slides between notes, adding expression and fluidity to melodies.
The effect continuously steps through semitones until reaching the target note.

**Format: `2 S D T`**

*   **2**: Command ID (Portamento).
*   **S (Mode)**: The slide direction:
    *   `0`: **UP** - Slides upward by T semitones from the current note
    *   `1`: **DOWN** - Slides downward by T semitones from the current note
    *   `2`: **TO TARGET** - Slides to the absolute MIDI note specified in T
    
*   **D (Speed)**: Ticks between each semitone step (1-F):
    *   `1`: Very fast (every tick, 6 steps per row)
    *   `2-3`: Fast slide
    *   `4-6`: Medium slide (musical default)
    *   `7-9`: Slow slide
    *   `A-F`: Very slow (10-15 ticks per step)

*   **T (Target)**: The slide distance or destination:
    *   **Modes 0 & 1**: Number of semitones to slide (0 = 12 semitones/1 octave default)
    *   **Mode 2**: Absolute MIDI note number to slide to (0-7F)

**Usage:**
- `2036`: Slide up 3 semitones, moving every 6 ticks (smooth upward bend).
- `211C`: Slide down 12 semitones (1 octave), moving every tick (fast drop).
- `2043`: Slide up 4 semitones at medium speed (classic pitch bend).
- `221C`: Slide to MIDI note 28 (middle C) at fast speed.
- `F000` or `0000`: Stop portamento effect.

**Notes:**
- Portamento and Arpeggio are mutually exclusive - activating one disables the other.
- The effect automatically stops when reaching the target note.
- Combine with Note Off (`===`) for creative pitch drops.

---

### 🎚️ Effect Command 3: Volume Slide (3SDT)
Volume Slide creates smooth dynamic changes - fade-ins, fade-outs, crescendos, and swells.
The effect continuously adjusts volume each tick until reaching the target.

**Format: `3 S D T`**

*   **3**: Command ID (Volume Slide).
*   **S (Mode)**: The slide direction:
    *   `0`: **UP** - Increases volume toward maximum or target
    *   `1`: **DOWN** - Decreases volume toward silence or target
    *   `2`: **TO TARGET** - Slides to the specific volume in T
    
*   **D (Speed)**: Volume units to change per tick (1-F):
    *   `1`: Very slow fade (1 unit per tick)
    *   `2-4`: Slow fade (smooth, musical)
    *   `5-8`: Medium fade (noticeable change)
    *   `9-C`: Fast fade (dramatic effect)
    *   `D-F`: Very fast fade (instant change)

*   **T (Target)**: The target volume or limit (0-F, scaled to 0-63):
    *   **Mode 0**: Target volume for fade-in (0 = fade to max volume 63)
    *   **Mode 1**: Target volume for fade-out (0 = fade to silence)
    *   **Mode 2**: Exact target volume (0=silent, F=max)

**Usage:**
- `3020`: Fade up to max volume, 2 units per tick (smooth crescendo).
- `3130`: Fade down to silence, 3 units per tick (smooth fade-out).
- `3048`: Fade up to medium volume (8/15 = ~34/63), 4 units per tick.
- `3218`: Fade to medium-low volume (8/15), 1 unit per tick (slow swell).
- `310F`: Fast fade to max, 1 unit per tick.
- `F000` or `0000`: Stop volume slide effect.

**Notes:**
- Volume slide works independently with arpeggio and portamento effects.
- The effect automatically stops when reaching the target volume.
- Perfect for dynamic expression, drum envelopes, and atmospheric pads.

---

### 🎸 Effect Command 4: Vibrato (4RDT)
Vibrato adds expressive pitch oscillation to sustained notes, creating warmth and character.
The effect continuously modulates pitch using different waveforms at adjustable rates and depths.

**Format: `4 R D T`**

*   **4**: Command ID (Vibrato).
*   **R (Rate)**: Oscillation speed - ticks per phase step (1-F):
    *   `1`: Very fast vibrato (rapid warble)
    *   `2-4`: Fast vibrato (noticeable oscillation)
    *   `5-8`: Medium vibrato (musical default)
    *   `9-C`: Slow vibrato (gentle wave)
    *   `D-F`: Very slow vibrato (subtle modulation)
    *   Lower values = faster oscillation
    
*   **D (Depth)**: Pitch deviation in semitones (1-F):
    *   `1-2`: Subtle vibrato (smooth, realistic)
    *   `3-5`: Medium vibrato (expressive, musical)
    *   `6-9`: Wide vibrato (dramatic)
    *   `A-F`: Very wide vibrato (extreme effect)

*   **T (Waveform)**: The oscillation shape:
    *   `0`: **SINE** - Smooth, natural vibrato (default)
    *   `1`: **TRIANGLE** - Linear rise/fall (vintage synth)
    *   `2`: **SQUARE** - Abrupt pitch jumps (trill effect)
    *   Other values wrap to 0-2

**Usage:**
- `4420`: Classic vibrato - medium rate, 2 semitones, sine wave (leads/vocals).
- `4230`: Fast subtle vibrato - quick rate, 3 semitones, sine (strings).
- `4651`: Wide slow vibrato - slow rate, 5 semitones, triangle (pads).
- `4322`: Medium vibrato with square wave for trill effect.
- `4A10`: Ultra-slow, subtle sine vibrato for atmospheric textures.
- `F000` or `0000`: Stop vibrato effect.

**Notes:**
- Vibrato and Arpeggio are mutually exclusive - activating one disables the other.
- Vibrato works independently with portamento and volume slide.
- Continuous effect - runs until explicitly stopped or note changes.
- Perfect for leads, pads, strings, and adding life to sustained notes.
- Combine with Note Off (`===`) for creative pitch drops.

---

## 🖥 User Interface Guide

### The Dashboard (Top)
The top 27 rows provide a real-time view of the synthesizer and sequencer state:
*   **Status Bar:** Displays current Mode, Octave, Instrument name, Volume, and Sequencer status.
*   **Sequence Row:** A horizontal view of your song structure (e.g., `00 00 01 02`). The active slot is highlighted in **Yellow**.
*   **Operator Panels:** Shows the 11 raw OPL2 registers for the currently selected instrument (Modulator and Carrier).
*   **Channel Meters:** Visual bars that react to note volume and decay over time.
*   **System Panel:** Displays active hardware (Native OPL2 vs FPGA) and CPU speed.

### The Grid (Bottom)
The pattern grid starts at **Row 28**.
*   **Dark Grey Bars:** Highlights every 4th row (0, 4, 8, etc.) to indicate the musical beat.
*   **Syntax Highlighting:**
    *   **White:** Musical Notes.
    *   **Muted Purple:** Instrument IDs.
    *   **Sage Green:** Volume Levels.
    *   **Cyan:** Dividers and Row Numbers.

---
*Created by Jason Rowe. Developed for the RP6502 Picocomputer Project.*
