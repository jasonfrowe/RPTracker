# RPTracker 
### A Native 6502 Music Tracker for the RP6502 & FPGA OPL2

**RPTracker** is a music composition tool designed specifically for the [RP6502 Picocomputer](https://github.com/picocomputer) equipped with a Yamaha YM3812 (OPL2) sound card implemented in a TinyFPGA. It leverages the unique dual-processor architecture of the RP6502 to provide a high-resolution 640x480 tracker interface and real-time FM synthesis.

## 🛠 Hardware Architecture

- **CPU:** W65C02 running at 8MHz.
- **Sound:** Yamaha YM3812 (OPL2) core on a TinyFPGA via PIX Bus.
  - **Clock:** Optimized for 4.0MHz OPL2 timing.
  - **Bus Interface:** PIX Bus Sniffer with a 512-entry hardware FIFO.
  - **Mapping:** `$FF00` (Index), `$FF01` (Data), `$FF02` (FIFO Flush).
- **Video:** VGA Mode 1 (640x480, 80x60 Text mode, 8-bit color).
  - Each character cell uses 3 bytes: `[Character]`, `[Foreground]`, `[Background]`.
- **Memory:** 
  - **64KB RAM:** 6502 Code, Stack, Zero Page, and the 128-instrument Patch Bank.
  - **64KB XRAM:** VGA Text/Attribute buffers and Pattern Data.

## 🎼 Software Features

- **Native Sequencing:** 64 rows per pattern, 9 channels of FM synthesis.
- **Instrument Library:** 128 AdLib-compatible General MIDI patches stored in local RAM.
- **Real-time Piano Input:** Musical keyboard mapping using standard USB HID scancodes.
- **Low-Latency Input:** Direct bitmask reading of the RIA keyboard state for high-performance edge detection.

## 🎹 Keyboard Controls

### Musical Piano (Monophonic)
- **Lower Octave (C-4 to B-4):** `Z` `S` `X` `D` `C` `V` `G` `B` `H` `N` `J` `M`
- **Upper Octave (C-5 to B-5):** `Q` `2` `W` `3` `E` `R` `5` `T` `6` `Y` `7` `U`

### Navigation & Editing
- **Arrow Keys:** Navigate the pattern grid (Row/Channel).
- **Space:** Toggle Edit/Record Mode.
- **F1 / F2:** Octave Down / Octave Up.
- **F3 / F4:** Previous / Next Instrument.
- **Delete / Backspace:** Clear current note/cell.

## 📝 Roadmap
- [x] OPL2 Initialization & 4.0MHz timing.
- [x] Keyboard Scancode-to-MIDI mapping.
- [x] VGA Mode 1 text initialization.
- [ ] Pattern Grid Rendering (XRAM to Screen).
- [ ] VSync-driven Playback Engine (60Hz).
- [ ] Instrument Editor UI (Live register tweaking).
- [ ] Disk I/O (Saving/Loading patterns to USB).

---
*Created by Jason Rowe.*
