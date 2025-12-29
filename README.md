# RPTracker 
### A Native 6502 Music Tracker for the RP6502 & FPGA OPL2

**RPTracker** is a music composition tool for the [RP6502 Picocomputer](https://github.com/picocomputer) and the Yamaha YM3812 (OPL2) sound card. It features a high-resolution 640x480 interface, real-time FM synthesis, and a low-latency 6502 sequencing engine.

---

## 🎹 Keyboard Control Reference

RPTracker is designed to be operated entirely from the keyboard for high-speed composition.

### 1. Musical Keyboard (Piano Layout)
The keyboard is mapped into two octaves using a standard "FastTracker II" style layout.
*   **Lower Octave (C-4 to B-4):**
    *   `Z` (C) `S` (C#) `X` (D) `D` (D#) `C` (E) `V` (F) `G` (F#) `B` (G) `H` (G#) `N` (A) `J` (A#) `M` (B)
*   **Upper Octave (C-5 to B-5):**
    *   `Q` (C) `2` (C#) `W` (D) `3` (D#) `E` (E) `R` (F) `5` (F#) `T` (G) `6` (G#) `Y` (A) `7` (A#) `U` (B)

### 2. Transport & Playback
*   **F6:** Play / Pause Toggle.
*   **F7:** Stop & Reset (Stops all sound and moves cursor to Row 00).

### 3. Navigation & Mode Selection
*   **Arrow Keys:** Navigate the 9-channel pattern grid. (Includes auto-repeat logic).
*   **Spacebar:** Toggle **Edit Mode**. 
    *   *Blue Cursor:* Safe/Preview Mode. Piano keys play sound but do not record.
    *   *Red Cursor:* Record Mode. Piano keys record notes into the grid and auto-advance the cursor.

### 4. Editing Commands
*   **Backspace / Delete:** Clear the current cell (sets Note, Instrument, and Volume to zero).
*   **Tilde ( ` ):** Insert **Note Off** (`===`). This stops the sound on that channel during playback.
*   **F5:** **Instrument Pick.** Samples the instrument ID from the current cell and sets it as your active "brush."

### 5. Instrument & Octave Management
*   **F1 / F2:** Decrease / Increase the base Octave for the piano keys.
*   **F3 / F4:** Previous / Next Instrument. 
    *   *Wraps around* from `00` to `FF` for fast access to percussion kits at the end of the bank.

---

## 🖥 User Interface Guide

The screen is divided into the **Dashboard** (top) and the **Pattern Grid** (bottom).

### Visual Indicators
*   **Blue Cursor:** You are in **Navigation Mode**.
*   **Red Cursor:** You are in **Record Mode**. Any notes played will be written to the current pattern.
*   **Yellow Glow:** The specific data cell (Note/Inst/Vol) currently targeted by the cursor.
*   **Dark Grey Bars:** Every 4th row (0, 4, 8, etc.) is highlighted to indicate the musical "Bar" and assist with timing.

### Color Coding (Syntax Highlighting)
To make the dense grid readable, data is color-coded by type:
*   **White:** Musical Notes (e.g., `C-4`, `G#2`).
*   **Muted Purple:** Instrument Index (00-FF).
*   **Muted Green:** Volume Level (00-3F).
*   **Cyan:** Row Numbers and Static Headers.

---

## 🛠 Memory Architecture

- **Patterns:** Stored in XRAM starting at `$0000`. Each pattern uses 2,304 bytes.
- **Instrument Bank:** 256 AdLib-compatible patches stored in 6502 Internal RAM for instant access.
- **Video Buffer:** VGA Mode 1 (3-bytes per pixel) located at `$C000`.

---
*Developed for the RP6502 Picocomputer Project.*