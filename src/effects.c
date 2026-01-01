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
        case 12: // UP3 (0, depth, depth*2, depth*3) - climbing thirds
            return (index % 4) * depth;
        case 13: // OCTAVE (root, octave, root, octave)
            return (index % 2 == 1) ? 12 : 0;
        case 14: // FIFTH (root, 5th, root, 5th)
            return (index % 2 == 1) ? 7 : 0;
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
    OPL_SetVolume(ch, ch_arp[ch].vol << 1); 
    OPL_NoteOn(ch, ch_arp[ch].base_note + offset);
    ch_peaks[ch] = ch_arp[ch].vol; 
}

void process_portamento_logic(uint8_t ch) {
    if (!ch_porta[ch].active) return;

    ch_porta[ch].tick_counter++;

    // Wait for speed delay
    if (ch_porta[ch].tick_counter < ch_porta[ch].speed) return;

    ch_porta[ch].tick_counter = 0;

    uint8_t current = ch_porta[ch].current_note;
    uint8_t target = ch_porta[ch].target_note;
    bool reached_target = false;

    // Calculate next note based on mode
    if (ch_porta[ch].mode == 0) { // Up
        if (current < 127) {
            current++;
            if (target > 0 && current >= target) reached_target = true;
        } else {
            reached_target = true;
        }
    } else if (ch_porta[ch].mode == 1) { // Down
        if (current > 0) {
            current--;
            if (target > 0 && current <= target) reached_target = true;
        } else {
            reached_target = true;
        }
    } else if (ch_porta[ch].mode == 2) { // To Target
        if (current < target) {
            current++;
        } else if (current > target) {
            current--;
        } else {
            reached_target = true;
        }
    }

    // Stop if reached target
    if (reached_target) {
        ch_porta[ch].active = false;
        return;
    }

    // Update note
    ch_porta[ch].current_note = current;
    OPL_NoteOff(ch);
    OPL_SetPatch(ch, &gm_bank[ch_porta[ch].inst]);
    OPL_SetVolume(ch, ch_porta[ch].vol << 1);
    OPL_NoteOn(ch, current);
    ch_peaks[ch] = ch_porta[ch].vol;
}

void process_volume_slide_logic(uint8_t ch) {
    if (!ch_volslide[ch].active) return;

    ch_volslide[ch].tick_counter++;

    // Process every tick (no speed delay for smooth volume changes)
    if (ch_volslide[ch].tick_counter < 1) return;

    ch_volslide[ch].tick_counter = 0;

    uint8_t current = ch_volslide[ch].current_vol;
    uint8_t target = ch_volslide[ch].target_vol;
    uint8_t speed = ch_volslide[ch].speed;
    if (speed == 0) speed = 1;
    bool reached_target = false;

    // Calculate next volume based on mode
    if (ch_volslide[ch].mode == 0) { // Up
        if (current + speed < 63) {
            current += speed;
            if (target > 0 && current >= target) {
                current = target;
                reached_target = true;
            }
        } else {
            current = 63;
            reached_target = true;
        }
    } else if (ch_volslide[ch].mode == 1) { // Down
        if (current > speed) {
            current -= speed;
            if (target > 0 && current <= target) {
                current = target;
                reached_target = true;
            }
        } else {
            current = 0;
            reached_target = true;
        }
    } else if (ch_volslide[ch].mode == 2) { // To Target
        if (current < target) {
            current += speed;
            if (current >= target) {
                current = target;
                reached_target = true;
            }
        } else if (current > target) {
            current -= speed;
            if (current <= target) {
                current = target;
                reached_target = true;
            }
        } else {
            reached_target = true;
        }
    }

    // Stop if reached target
    if (reached_target) {
        ch_volslide[ch].active = false;
    }

    // Update volume
    ch_volslide[ch].current_vol = current;
    OPL_SetVolume(ch, current << 1);
    ch_peaks[ch] = current;
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
    if (!ch_notedelay[ch].active || ch_notedelay[ch].triggered) return;
    
    ch_notedelay[ch].tick_counter++;
    
    if (ch_notedelay[ch].tick_counter >= ch_notedelay[ch].delay_tick) {
        OPL_NoteOff(ch);
        OPL_SetPatch(ch, &gm_bank[ch_notedelay[ch].inst]);
        OPL_SetVolume(ch, ch_notedelay[ch].vol << 1);
        OPL_NoteOn(ch, ch_notedelay[ch].note);
        ch_peaks[ch] = ch_notedelay[ch].vol;
        ch_notedelay[ch].triggered = true;
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
    
    ch_tremolo[ch].tick_counter++;
    
    if (ch_tremolo[ch].tick_counter < ch_tremolo[ch].rate) return;
    ch_tremolo[ch].tick_counter = 0;
    
    ch_tremolo[ch].phase += 32;
    
    // Calculate volume offset based on waveform
    int8_t offset = 0;
    uint8_t phase = ch_tremolo[ch].phase;
    uint8_t depth = ch_tremolo[ch].depth;
    
    if (depth == 0) depth = 4;
    
    switch (ch_tremolo[ch].waveform) {
        case 0: // Sine wave
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
            offset -= depth / 2;
            break;
            
        case 2: // Square wave
            offset = (phase < 128) ? depth : -depth;
            break;
    }
    
    // Apply volume offset
    int16_t new_vol = (int16_t)ch_tremolo[ch].base_vol + offset;
    if (new_vol < 0) new_vol = 0;
    if (new_vol > 63) new_vol = 63;
    
    OPL_SetVolume(ch, (uint8_t)new_vol << 1);
    ch_peaks[ch] = (uint8_t)new_vol;
}

void process_finepitch_logic(uint8_t ch) {
    if (!ch_finepitch[ch].active) return;
    
    // Fine pitch is applied once on trigger, not per-tick
    // The detune is handled by OPL frequency offset
    // This is a no-op per-tick processor (effect is instant)
}