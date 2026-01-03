#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include "effects.h"
#include "player.h"
#include "opl.h"
#include "instruments.h"
#include "screen.h"

// State memory for all 9 channels
ArpState ch_arp[9];
PortamentoState ch_porta[9];
VolumeSlideState ch_volslide[9];
VibratoState ch_vibrato[9];
NoteCutState ch_notecut[9];
NoteDelayState ch_notedelay[9];
RetriggerState ch_retrigger[9];
TremoloState ch_tremolo[9];
FinePitchState ch_finepitch[9];

const uint8_t arp_tick_lut[16] = {
    1, 2, 3, 6, 9, 12, 18, 24, 30, 36, 42, 48, 60, 72, 84, 96
};

// Add this to effects.c
int16_t get_arp_offset(uint8_t style, uint8_t depth, uint8_t index) {
    switch (style) {
        case 0: return (index % 2 == 1) ? depth : 0; // UP
        case 1: return (index % 2 == 0) ? depth : 0; // DOWN
        case 2: // MAJOR (4-note: root, 3rd, 5th, octave)
            if (index % 4 == 1) return 4;
            if (index % 4 == 2) return 7;
            if (index % 4 == 3) return 12;
            return 0;
        case 3: // MINOR (4-note: root, minor 3rd, 5th, octave)
            if (index % 4 == 1) return 3;
            if (index % 4 == 2) return 7;
            if (index % 4 == 3) return 12;
            return 0;
        case 4: // MAJ7 (root, 3rd, 5th, maj7th)
            if (index % 4 == 1) return 4;
            if (index % 4 == 2) return 7;
            if (index % 4 == 3) return 11;
            return 0;
        case 5: // MIN7 (root, minor 3rd, 5th, minor 7th)
            if (index % 4 == 1) return 3;
            if (index % 4 == 2) return 7;
            if (index % 4 == 3) return 10;
            return 0;
        case 6: // SUS4 (root, 4th, 5th, octave)
            if (index % 4 == 1) return 5;
            if (index % 4 == 2) return 7;
            if (index % 4 == 3) return 12;
            return 0;
        case 7: // SUS2 (root, 2nd, 5th, octave)
            if (index % 4 == 1) return 2;
            if (index % 4 == 2) return 7;
            if (index % 4 == 3) return 12;
            return 0;
        case 8: // DIM (root, minor 3rd, dim 5th, dim 7th)
            if (index % 4 == 1) return 3;
            if (index % 4 == 2) return 6;
            if (index % 4 == 3) return 9;
            return 0;
        case 9: // AUG (root, maj 3rd, aug 5th, octave)
            if (index % 4 == 1) return 4;
            if (index % 4 == 2) return 8;
            if (index % 4 == 3) return 12;
            return 0;
        case 10: // POWER (root, 5th, octave, octave)
            if (index % 4 == 1) return 7;
            if (index % 4 == 2) return 12;
            if (index % 4 == 3) return 12;
            return 0;
        case 11: // UPDOWN (root, up, up, root) - bounce pattern
            if (index % 4 == 1) return depth;
            if (index % 4 == 2) return depth;
            if (index % 4 == 3) return 0;
            return 0;
        case 12: // GUITAR MAJOR STRUM (6-note cycle)
            // Emulates an open-voiced G Major: G2, B2, D3, G3, B3, G4
            {
                uint8_t s = index % 6;
                if (s == 1) return 4;  // Major 3rd
                if (s == 2) return 7;  // 5th
                if (s == 3) return 12; // Octave
                if (s == 4) return 16; // Octave + 3rd
                if (s == 5) return 19; // Octave + 5th (or 24 for high Root)
                return 0;
            }

        case 13: // GUITAR MINOR STRUM (6-note cycle)
            // Emulates an open-voiced G Minor: G2, Bb2, D3, G3, Bb3, G4
            {
                uint8_t s = index % 6;
                if (s == 1) return 3;  // Minor 3rd
                if (s == 2) return 7;  // 5th
                if (s == 3) return 12; // Octave
                if (s == 4) return 15; // Octave + Minor 3rd
                if (s == 5) return 19; // Octave + 5th
                return 0;
            }
        case 14: // GUITAR Major 9th (6-note cycle)
            // Emulates an open-voiced Major 9th: Root, Root, 3rd, 5th, 7th, 9th
            {
                uint8_t s = index % 6;
                if (s == 1) return 0;  // Minor 3rd
                if (s == 2) return 4;  // 5th
                if (s == 3) return 7; // Octave
                if (s == 4) return 11; // Octave + Minor 3rd
                if (s == 5) return 14; // Octave + 5th
                return 0;
            }
        case 15: // DOUBLE (depth value unused, repeats each note)
            return (index % 4 < 2) ? 0 : depth;
    }
    return 0;
}

void process_arp_logic(uint8_t ch) {
    if (!ch_arp[ch].active) return;

    // --- JUST TRIGGERED GUARD ---
    // If this arpeggio was just activated or a note was just struck,
    // skip processing this frame to avoid double-hit.
    if (ch_arp[ch].just_triggered) {
        ch_arp[ch].just_triggered = false;
        return;
    }

    ch_arp[ch].phase_timer++;

    if (ch_arp[ch].phase_timer < ch_arp[ch].target_ticks) return;

    ch_arp[ch].phase_timer = 0;
    ch_arp[ch].step_index++; 

    int16_t offset = get_arp_offset(ch_arp[ch].style, ch_arp[ch].depth, ch_arp[ch].step_index);

    OPL_NoteOff(ch);
    OPL_SetPatch(ch, &gm_bank[ch_arp[ch].inst]);
    
    // Use volume slide's current volume if active, otherwise use arp's original volume
    uint8_t vol = ch_volslide[ch].active ? ch_volslide[ch].current_vol : ch_arp[ch].vol;
    OPL_SetVolume(ch, vol << 1); 
    OPL_NoteOn(ch, ch_arp[ch].base_note + offset);
    ch_peaks[ch] = vol; 
}

void process_portamento_logic(uint8_t ch) {
    if (!ch_porta[ch].active) return;

    // --- TICK 0 GUARD ---
    // Let the sequencer handle the initial note strike on Tick 0.
    if (seq.tick_counter == 0) return;

    ch_porta[ch].tick_counter++;

    // Wait for speed delay (D)
    if (ch_porta[ch].tick_counter < ch_porta[ch].speed) return;
    ch_porta[ch].tick_counter = 0;

    uint8_t current = ch_porta[ch].current_note;
    uint8_t target = ch_porta[ch].target_note;

    if (current < target) {
        current++; // Slide Up
    } else if (current > target) {
        current--; // Slide Down
    } else {
        // Target reached: stop the effect logic but keep the note ringing
        ch_porta[ch].active = false;
        return;
    }

    // Update the state
    ch_porta[ch].current_note = current;

    // --- THE FIX: SMOOTH PITCH UPDATE ---
    // Use OPL_SetPitch (like Vibrato) to change frequency 
    // without triggering a new 'attack' or 'beep'.
    OPL_SetPitch(ch, current);
    
    // Keep the meters alive
    ch_peaks[ch] = ch_porta[ch].vol;
}

void process_volume_slide_logic(uint8_t ch) {
    if (!ch_volslide[ch].active) return;

    uint16_t v = ch_volslide[ch].vol_accum;
    uint16_t step = ch_volslide[ch].speed_fp;
    uint16_t target_fp = (uint16_t)ch_volslide[ch].target_vol << 8;
    bool reached = false;

    switch (ch_volslide[ch].mode) {
        case 0: // SLIDE UP
            v += step;
            // Strict clamp at 63.0 (0x3F00) to prevent wrapping to 64
            if (v >= 0x3F00) { v = 0x3F00; reached = true; }
            if (v >= target_fp && ch_volslide[ch].target_vol != 0) { v = target_fp; reached = true; }
            break;

        case 1: // SLIDE DOWN
            if (v > step) {
                v -= step;
                if (v <= target_fp) { v = target_fp; reached = true; }
            } else {
                // Hits 0.0, prevent underflow
                v = 0;
                reached = true;
            }
            break;

        case 2: // SLIDE TO TARGET (Auto-direction)
            if (v < target_fp) {
                v += step;
                if (v >= target_fp) { v = target_fp; reached = true; }
            } else if (v > target_fp) {
                if (v > step) v -= step; else v = 0;
                if (v <= target_fp) { v = target_fp; reached = true; }
            } else {
                reached = true;
            }
            break;
    }

    ch_volslide[ch].vol_accum = v;
    
    // Convert 8.8 Fixed Point to 0-63 Integer
    uint8_t final_vol = (uint8_t)(v >> 8);

    // --- AUDIO UPDATE ---
    // We pass (0-63 << 1) to your routine to satisfy the 0-127 MIDI requirement
    OPL_SetVolume(ch, final_vol << 1);
    
    // Update visual meters (0-63 scale)
    ch_peaks[ch] = final_vol;

    if (reached) {
        ch_volslide[ch].active = false;
    }
}

void process_vibrato_logic(uint8_t ch) {
    if (!ch_vibrato[ch].active) return;

    ch_vibrato[ch].tick_counter++;

    // Rate controls how many ticks per phase update
    uint8_t rate = ch_vibrato[ch].rate;
    if (rate == 0) rate = 1;
    
    if (ch_vibrato[ch].tick_counter < rate) return;

    ch_vibrato[ch].tick_counter = 0;

    // Update phase (0-255 represents full cycle)
    ch_vibrato[ch].phase += 32; // Step through wave

    // Calculate pitch offset based on waveform
    int8_t offset = 0;
    uint8_t phase = ch_vibrato[ch].phase;
    uint8_t depth = ch_vibrato[ch].depth;
    
    if (depth == 0) depth = 1;

    switch (ch_vibrato[ch].waveform) {
        case 0: // Sine wave (approximation)
            if (phase < 64) {
                offset = (phase * depth) / 64;
            } else if (phase < 128) {
                offset = (((127 - phase) * depth) / 64);
            } else if (phase < 192) {
                offset = -((phase - 128) * depth) / 64;
            } else {
                offset = -(((255 - phase) * depth) / 64);
            }
            break;
            
        case 1: // Triangle wave
            if (phase < 128) {
                offset = (phase * depth) / 128;
            } else {
                offset = (((255 - phase) * depth) / 128);
            }
            offset -= depth / 2; // Center around 0
            break;
            
        case 2: // Square wave
            offset = (phase < 128) ? depth : -depth;
            break;
            
        default: // Sine
            offset = 0;
            break;
    }

    // Apply pitch offset
    int16_t new_note = (int16_t)ch_vibrato[ch].base_note + offset;
    if (new_note < 0) new_note = 0;
    if (new_note > 127) new_note = 127;

    // Change pitch without retriggering note
    OPL_SetPitch(ch, (uint8_t)new_note);
    // Volume stays the same, no need to update
}

void process_notecut_logic(uint8_t ch) {
    if (!ch_notecut[ch].active) return;
    
    ch_notecut[ch].tick_counter++;
    
    if (ch_notecut[ch].tick_counter >= ch_notecut[ch].cut_tick) {
        OPL_NoteOff(ch);
        ch_peaks[ch] = 0;
        ch_notecut[ch].active = false;
    }
}

void process_notedelay_logic(uint8_t ch) {
    if (!ch_notedelay[ch].active) return;

    // Skip Tick 0 (handled by Sequencer)
    if (seq.tick_counter == 0) return;

    ch_notedelay[ch].tick_counter++;

    if (ch_notedelay[ch].tick_counter >= ch_notedelay[ch].delay_tick) {
        // --- TIME TO FIRE AN ECHO ---
        
        // 1. Calculate the faded volume for the NEXT echo
        // We drop the volume by roughly 25% each bounce (Shift right by 2 and subtract)
        uint8_t decay = (ch_notedelay[ch].vol >> 2); 
        if (decay == 0) decay = 4; // Minimum decay to ensure it eventually stops
        
        if (ch_notedelay[ch].vol > decay) {
            ch_notedelay[ch].vol -= decay;
        } else {
            ch_notedelay[ch].vol = 0;
        }

        // 2. Strike the hardware
        OPL_NoteOff(ch);
        OPL_SetPatch(ch, &gm_bank[ch_notedelay[ch].inst]);
        OPL_SetVolume(ch, ch_notedelay[ch].vol << 1);
        OPL_NoteOn(ch, ch_notedelay[ch].note);
        ch_peaks[ch] = ch_notedelay[ch].vol;

        // 3. RE-SCHEDULE OR STOP
        if (ch_notedelay[ch].vol > 0) {
            ch_notedelay[ch].tick_counter = 0; // Reset timer for next bounce
            // active remains true!
        } else {
            ch_notedelay[ch].active = false;   // Echo died out
        }
    }
}

void process_retrigger_logic(uint8_t ch) {
    if (!ch_retrigger[ch].active) return;
    
    ch_retrigger[ch].tick_counter++;
    
    if (ch_retrigger[ch].tick_counter >= ch_retrigger[ch].speed) {
        ch_retrigger[ch].tick_counter = 0;
        
        OPL_NoteOff(ch);
        OPL_SetPatch(ch, &gm_bank[ch_retrigger[ch].inst]);
        OPL_SetVolume(ch, ch_retrigger[ch].vol << 1);
        OPL_NoteOn(ch, ch_retrigger[ch].note);
        ch_peaks[ch] = ch_retrigger[ch].vol;
    }
}

void process_tremolo_logic(uint8_t ch) {
    if (!ch_tremolo[ch].active) return;

    // --- TICK 0 GUARD ---
    // Let the sequencer handle the initial volume setting on Tick 0.
    if (seq.tick_counter == 0) return;

    // 1. ADVANCE PHASE
    // Rate (R) now controls how much we add to the phase every frame.
    // Higher R = faster pulsing.
    uint8_t rate = ch_tremolo[ch].rate;
    if (rate == 0) rate = 1; 
    ch_tremolo[ch].phase += (rate * 4); // Scale factor of 4 for musical speeds

    // 2. CALCULATE LFO OUTPUT
    // We want a value ranging from -Depth to +Depth
    int16_t lfo_out = 0;
    uint8_t phase = ch_tremolo[ch].phase;
    uint8_t depth = ch_tremolo[ch].depth;

    switch (ch_tremolo[ch].waveform) {
        case 0: // SINE (High-res approximation)
            if (phase < 128) {
                lfo_out = (phase * depth) / 64 - depth;
            } else {
                lfo_out = depth - ((phase - 128) * depth) / 64;
            }
            break;
            
        case 1: // TRIANGLE
            if (phase < 128) {
                lfo_out = (phase * depth * 2) / 128 - depth;
            } else {
                lfo_out = depth - ((phase - 128) * depth * 2) / 128;
            }
            break;
            
        case 2: // SQUARE
            lfo_out = (phase < 128) ? depth : -depth;
            break;

        case 3: // SAWTOOTH
            lfo_out = (phase * depth * 2) / 255 - depth;
            break;
    }

    // 3. APPLY TO BASE VOLUME
    int16_t new_vol = (int16_t)ch_tremolo[ch].base_vol + lfo_out;

    // 4. STRICT CLAMPING (0-63)
    // This prevents the 'cycle back on' bug
    if (new_vol < 0)  new_vol = 0;
    if (new_vol > 63) new_vol = 63;

    // 5. OUTPUT TO HARDWARE
    // Using your MIDI << 1 mapping
    OPL_SetVolume(ch, (uint8_t)new_vol << 1);
    
    // Update visual peaks
    ch_peaks[ch] = (uint8_t)new_vol;
}

void process_finepitch_logic(uint8_t ch) {
    if (!ch_finepitch[ch].active) return;
    
    // Fine pitch is applied once on trigger, not per-tick
    // The detune is handled by OPL frequency offset
    // This is a no-op per-tick processor (effect is instant)
}