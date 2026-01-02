#include <rp6502.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include "opl.h"
#include "instruments.h"
#include "constants.h"
#include "effects.h"

#ifdef USE_NATIVE_OPL2
// F-Number table for Octave 4 @ 3.58 MHz
const uint16_t fnum_table[12] = {
    345, 365, 387, 410, 435, 460, 488, 517, 547, 580, 615, 651
};
#else
// F-Number table for Octave 4 @ 4.0 MHz
const uint16_t fnum_table[12] = {
    // 308, 325, 345, 365, 387, 410, 434, 460, 487, 516, 547, 579
    309, 327, 346, 367, 389, 412, 436, 462, 490, 519, 550, 583
};
#endif

uint8_t channel_is_drum[9] = {0,0,0,0,0,0,0,0,0}; 

// Full shadow of the OPL2's 256 registers
uint8_t opl_hardware_shadow[256];

// Initialize shadow with a "dirty" value to force the first writes
void OPL_ShadowReset() {
    for (int i = 0; i < 256; i++) {
        opl_hardware_shadow[i] = 0xFF; // Non-zero/Impossible state
    }
}


// Shadow registers for all 9 channels
// We need this to remember the Block/F-Number when we send a NoteOff
uint8_t shadow_b0[9] = {0}; 

// Track the KSL bits so we don't overwrite them when changing volume
uint8_t shadow_ksl_m[9];
uint8_t shadow_ksl_c[9];

// Returns a 16-bit value: 
// High Byte: 0x20 (KeyOn) | Block << 2 | F-Number High (2 bits)
// Low Byte: F-Number Low (8 bits)
uint16_t midi_to_opl_freq(uint8_t midi_note) {
    if (midi_note < 12) midi_note = 12;   // Lowest note is C-1
    if (midi_note > 127) midi_note = 127; // Highest note is G9
    
    int block = (midi_note - 12) / 12;
    int note_idx = (midi_note - 12) % 12;
    
    if (block > 7) block = 7;

    uint16_t f_num = fnum_table[note_idx];
    uint8_t high_byte = 0x20 | (block << 2) | ((f_num >> 8) & 0x03);
    uint8_t low_byte = f_num & 0xFF;

    return (high_byte << 8) | low_byte;
}

void OPL_Write(uint8_t reg, uint8_t data) {
    // Check if the hardware already has this value
    if (opl_hardware_shadow[reg] == data) {
        return;
    }

    // Update the shadow
    opl_hardware_shadow[reg] = data;

#ifdef USE_NATIVE_OPL2
    RIA.addr1 = OPL_ADDR + reg;
    RIA.rw1 = data;
#else
    RIA.addr1 = OPL_ADDR;
    RIA.step1 = 1;
    RIA.rw1 = reg;
    RIA.rw1 = data;
#endif
}

void OPL_SilenceAll() {
    // Send Note-Off to all 9 channels
    // We let these go through the FIFO so they are timed correctly
    for (uint8_t i = 0; i < 9; i++) {
        OPL_Write(0xB0 + i, 0x00);
    }
}

void OPL_FifoClear() {
    RIA.addr1 = OPL_ADDR + 2; // Our new FIFO flush register
    RIA.step1 = 0;
    RIA.rw1 = 1;         // Trigger flush
}

void OPL_NoteOn(uint8_t channel, uint8_t midi_note) {
    if (channel > 8) return;

    // If this channel is currently a drum, force the pitch to Middle C (60)
    // This makes FM patches sound like drums instead of weird low bloops.
    if (channel_is_drum[channel]) {
        midi_note = 60; 
    }
    
    uint16_t freq = midi_to_opl_freq(midi_note);
    uint8_t b0_value = (freq >> 8) & 0xFF;  // Includes key-on bit 5
    
    OPL_Write(0xA0 + channel, freq & 0xFF);
    OPL_Write(0xB0 + channel, b0_value);
    shadow_b0[channel] = b0_value;  // Store FULL value including key-on bit
}

void OPL_SetPitch(uint8_t channel, uint8_t midi_note) {
    if (channel > 8) return;

    // Change pitch without retriggering the note
    // Keeps the key-on bit from shadow_b0
    if (channel_is_drum[channel]) {
        midi_note = 60;
    }
    
    uint16_t freq = midi_to_opl_freq(midi_note);
    uint8_t block_fnum_high = (freq >> 8) & 0x1F;
    
    OPL_Write(0xA0 + channel, freq & 0xFF);
    // Preserve key-on bit (bit 5) from shadow
    OPL_Write(0xB0 + channel, block_fnum_high | (shadow_b0[channel] & 0x20));
    shadow_b0[channel] = (shadow_b0[channel] & 0x20) | block_fnum_high;
}

void OPL_NoteOff(uint8_t channel) {
    if (channel > 8) return;

    // Kill the Arp logic for this channel immediately
    // ch_arp[channel].active = false;

    OPL_Write(0xB0 + channel, shadow_b0[channel] & 0x1F); // Write stored octave/freq with KeyOn=0
}

// Clear all 256 registers correctly
void OPL_Clear() {
    for (int i = 0; i < 256; i++) {
        OPL_Write(i, 0x00);
    }
    // Reset shadow memory
    for (int i=0; i<9; i++) shadow_b0[i] = 0;
}

void OPL_SetVolume(uint8_t chan, uint8_t velocity) {
    // Convert MIDI velocity (0-127) to OPL Total Level (63-0)
    // Formula: 63 - (velocity / 2)
    uint8_t vol = 63 - (velocity >> 1);
    
    static const uint8_t mod_offsets[] = {0x00,0x01,0x02,0x08,0x09,0x0A,0x10,0x11,0x12};
    static const uint8_t car_offsets[] = {0x03,0x04,0x05,0x0B,0x0C,0x0D,0x13,0x14,0x15};
    
    // Write to Carrier (this affects the audible volume most)
    // Mask with 0xC0 to preserve Key Scale Level bits
    OPL_Write(0x40 + car_offsets[chan], (shadow_ksl_c[chan] & 0xC0) | vol);
}

void OPL_Init() {

    OPL_ShadowReset();

    // Silence all 9 channels immediately (Key-Off)
    // Register 0xB0-0xB8 controls Key-On
    for (uint8_t i = 0; i < 9; i++) {
        OPL_Write(0xB0 + i, 0x00);
        shadow_b0[i] = 0;
    }

    // Wipe every OPL2 hardware register (0x01 to 0xF5)
    // This ensures that leftovers from a previous program 
    // (like long Release times or weird Waveforms) are gone.
    for (int i = 0x01; i <= 0xF5; i++) {
        OPL_Write(i, 0x00);
    }

    for (int i = 0; i < 9; i++) {
        channel_is_drum[i] = 0;
        shadow_b0[i] = 0;
    }

    // Re-enable the features we need
    OPL_Write(0x01, 0x20); // Enable Waveform Select
    OPL_Write(0xBD, 0x00); // Ensure Melodic Mode
}

void OPL_Silence() {
    // Just kill the 9 voices (Key-Off)
    for (uint8_t i = 0; i < 9; i++) {
        OPL_Write(0xB0 + i, 0x00);
        shadow_b0[i] = 0;
    }
}

uint32_t song_xram_ptr = 0;
uint16_t wait_ticks = 0;

void OPL_FifoFlush() {
    // Ensure the Magic Key (0xAA) matches our Verilog flush logic
    RIA.addr1 = OPL_ADDR + 2;
    RIA.step1 = 0;
    RIA.rw1 = 0xAA; 
}

void shutdown_audio() {
    OPL_SilenceAll();       // Kill any playing notes
    OPL_FifoFlush();        // Clear the hardware buffer
    OPL_Config(0, OPL_ADDR);   // Tell the FPGA to stop listening to the PIX bus
}


void OPL_Config(uint8_t enable, uint16_t addr) {
    // Configure OPL Device in FPGA
#ifdef USE_NATIVE_OPL2
    // Native RIA OPL2 Initialization (Device 0, Channel 1)
    xreg(0, 1, 0x01, addr); 
    // xregn(0, 1, 0x01, 1, addr);
#else
    // Args: dev(1), chan(0), reg(9), count(3)
    xregn(2, 0, 0, 2, enable, addr);
#endif
    
}

void OPL_NoteOn_Detuned(uint8_t channel, uint8_t midi_note, int8_t detune) {
    if (channel > 8) return;

    if (midi_note < 12) midi_note = 12;
    int block = (midi_note / 12) - 1;
    int note_idx = midi_note % 12;
    if (block > 7) block = 7;

    // 1. Get base F-Number from table
    uint16_t f_num = fnum_table[note_idx];

    // 2. Apply Detune
    // A detune of 1 (1/16th semitone) is roughly 2 units of F-Number.
    // We cast to int16 to handle negative results safely.
    int16_t adjusted_fnum = (int16_t)f_num + (detune * 2);

    // 3. Handle Underflow/Overflow
    // If the detune pushes us out of the current F-Number range (1-1023),
    // we clamp it. (Pro version would shift blocks, but clamping is safe for "fine").
    if (adjusted_fnum < 1) adjusted_fnum = 1;
    if (adjusted_fnum > 1023) adjusted_fnum = 1023;

    // 4. Write to OPL2
    uint8_t high_byte = 0x20 | (block << 2) | ((adjusted_fnum >> 8) & 0x03);
    uint8_t low_byte = adjusted_fnum & 0xFF;

    OPL_Write(0xA0 + channel, low_byte);
    OPL_Write(0xB0 + channel, high_byte);
    
    shadow_b0[channel] = high_byte & 0x1F;
}

// void OPL_SetPitch(uint8_t channel, uint8_t midi_note) {
//     if (channel > 8) return;

//     // Use your existing helper to calculate Block and F-Number
//     // High Byte: 0x20 (KeyOn) | Block << 2 | F-Number High (2 bits)
//     // Low Byte: F-Number Low (8 bits)
//     uint16_t freq = midi_to_opl_freq(midi_note);

//     // Write F-Number Low to $A0-$A8
//     OPL_Write(0xA0 + channel, freq & 0xFF);

//     // Write Block/F-Num High to $B0-$B8
//     // We force bit 5 (0x20) to 1 to ensure the note continues to sustain
//     uint8_t b_val = ((freq >> 8) & 0xFF) | 0x20;
//     OPL_Write(0xB0 + channel, b_val);
    
//     // Update the logic shadow so NoteOff knows the last frequency used
//     shadow_b0[channel] = b_val & 0x1F; 
// }
