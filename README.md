# RPTracker 
### A Native 6502 Music Tracker for the RP6502 & FPGA OPL2

**RPTracker** is a music composition tool for the [RP6502 Picocomputer](https://github.com/picocomputer) and the Yamaha YM3812 (OPL2) sound card. It features a high-resolution 640x480 interface, real-time FM synthesis, and a low-latency 6502 sequencing engine.

---

## 🎹 Keyboard Control Reference

RPTracker uses a dual-mode input system: **Global Brush** adjustments (for future notes) and **In-Place Editing** (tweaking existing notes).

### 1. Musical Keyboard (Piano Layout)
*   **Lower Octave (C-4 to B-4):** `Z S X D C V G B H N J M`
*   **Upper Octave (C-5 to B-5):** `Q 2 W 3 E R 5 T 6 Y 7 U`

### 2. Global "Brush" Controls
These keys change the settings for the **next** notes you record. 
*   **F1 / F2** or **- / =**: Decrease / Increase the global keyboard **Octave** (Range: 0-8).
*   **F3 / F4**: Previous / Next **Instrument** (Wraps 00-FF).
*   **[ / ]**: Decrease / Increase global **Volume** (Range: 00-3F).

### 3. In-Place Editing (SHIFT + Key)
Use these while in **Edit Mode** to tweak the cell under the cursor without re-playing the note.
*   **SHIFT + - / =**: **Transpose** the note in the current cell up/down by one semitone.
*   **SHIFT + F3 / F4**: Change the **Instrument** of the current cell only.
*   **SHIFT + [ / ]**: Adjust the **Volume** of the current cell only.
*   *Note: In-Place edits provide a "Live Preview," immediately updating the OPL2 hardware so you can hear the change.*

### 4. Transport & Navigation
*   **F6 / F7**: Play-Pause / Stop-Reset.
*   **Arrow Keys**: Navigate the 9-channel pattern grid.
*   **Spacebar**: Toggle **Edit Mode** (Red Cursor = Recording, Blue Cursor = Navigation).
*   **Backspace / Delete**: Clear the current cell.
*   **Tilde ( ` )**: Insert **Note Off** (`===`).
*   **F5**: **Instrument Pick.** Samples the instrument ID from the grid into your global brush.

### 5. Live Recording
*   **F6 (Play) + Space (Edit):** Enables Live Recording.
*   Play notes on the keyboard while the sequence is running to capture them into the grid in real-time.
*   Move the cursor with **Arrows** while playing to jump between channels for multi-track jamming.

### 7. Transposition & Octave
*   **F1 / F2**: Decrease / Increase global keyboard **Octave** (The Brush).
*   **- / =**: Transpose the current cell down/up by **1 semitone**.
*   **SHIFT + - / =**: Transpose the current cell down/up by **12 semitones (1 Octave)**.

---

## 🖥 User Interface Guide

### Dashboard & Grid
The screen is split into a **Dashboard** (top) and the **Pattern Grid** (bottom).
*   **Blue Cursor:** Navigation Mode.
*   **Red Cursor:** Record Mode.
*   **Yellow Glow:** Highlights the active data cell (Note, Instrument, or Volume).
*   **Dark Grey Bars:** Every 4th row is highlighted (Row 0, 4, 8, etc.) to help track musical bars.

### Syntax Highlighting
*   **White:** Musical Notes (e.g., `C-4`).
*   **Muted Purple:** Instrument Index (00-FF).
*   **Muted Green:** Volume Level (00-3F).
*   **Cyan:** Headers and Row Numbers.

---

## 🛠 Memory Architecture
- **Patterns:** XRAM `$0000`. 2,304 bytes per 64-row pattern.
- **Instruments:** 256 patches in 6502 RAM.
- **Video:** VGA Mode 1 at `$C000`.

---
*Developed for the RP6502 Picocomputer Project.*