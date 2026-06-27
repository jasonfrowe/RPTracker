#include "midi.h"
#include <rp6502.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>

// MIDI note entry from a USB MIDI keyboard on MIDI0: in RAW mode.
// The device delivers plain wire MIDI bytes: channel voice messages
// (with running status), system common, single-byte real-time messages
// that may appear anywhere, and System Exclusive. There are no delta times.

#define MIDI_DEBUG 0

#define MIDI_DEVICE "MIDI0:"
#define MIDI_RETRY_FRAMES 60 // retry open once per second at 60 fps

static bool midi_off_pending = false;

static int midi_fd = -1;
static uint8_t midi_retry = MIDI_RETRY_FRAMES;

// Wire MIDI parser state. Persists across midi_task() calls so a message
// split across reads/frames resumes correctly.
// midi_status: current status byte for running status (0 = none).
//              The sentinel 0xF0 means "inside a System Exclusive".
// midi_data:   data bytes collected for the current message.
// midi_have:   number of data bytes collected so far.
static uint8_t midi_status = 0;
static uint8_t midi_data[2];
static uint8_t midi_have = 0;

/**
 * Data byte count for a status byte
 */
static uint8_t midi_data_len(uint8_t status)
{
    switch (status & 0xF0) {
        case 0x80:
        case 0x90:
        case 0xA0:
        case 0xB0:
        case 0xE0:
            return 2;
        case 0xC0:
        case 0xD0:
            return 1;
        case 0xF0:
            if (status == 0xF2) return 2;
            if (status == 0xF1 || status == 0xF3) return 1;
            return 0;
    }
    return 0;
}

/**
 * Track the held note from note on/off messages
 */
static void midi_handle_message(void)
{
    uint8_t kind = midi_status & 0xF0;
    uint8_t chan = midi_status & 0x0F;
    if (kind == 0x90 && midi_data[1]) {
        // Note On: velocity > 0. Note number 0 is the "none" sentinel
        // and is ignored.
        if (midi_data[0]) {
#if MIDI_DEBUG
            printf("[on %02X %02X]", midi_data[0], midi_data[1]);
#endif
            midi_process_note_on(chan, midi_data[0], midi_data[1]);
        }
    } else if (kind == 0x80 || kind == 0x90) {
        // Note Off: 0x80, or 0x90 with velocity 0.
#if MIDI_DEBUG
        printf("[off %02X]", midi_data[0]);
#endif
        midi_process_note_off(chan, midi_data[0]);
    } else if (kind == 0xB0) {
        // CC Change
#if MIDI_DEBUG
        printf("[cc %02X %02X]", midi_data[0], midi_data[1]);
#endif
        midi_process_cc(chan, midi_data[0], midi_data[1]);
    }
}

/**
 * Parse one byte of the raw wire MIDI stream
 */
static void midi_parse_byte(uint8_t b)
{
    if (b >= 0xF8)
        return; // system real-time: single byte, leaves all state untouched

    if (b & 0x80) {
        // Status byte (0x80-0xF7).
        if (b == 0xF0) {
            // System Exclusive begins; swallow data until any status byte.
            midi_status = 0xF0;
        } else if (b >= 0xF1) {
            // System common (incl. 0xF7 EOX and undefined 0xF4/0xF5):
            // cancels running status. Arm only if it carries data bytes,
            // otherwise clear immediately so it cannot eat a later data byte.
            midi_status = midi_data_len(b) ? b : 0;
        } else {
            // Channel voice: arm running status.
            midi_status = b;
        }
        midi_have = 0;
        return;
    }

    // Data byte (0x00-0x7F).
    if (midi_status == 0xF0 || midi_status == 0)
        return; // inside SysEx, or no status armed: ignore

    midi_data[midi_have++] = b;
    if (midi_have >= midi_data_len(midi_status)) {
        if (midi_status < 0xF0) {
            midi_handle_message();
            midi_have = 0;   // keep running status armed for the next message
        } else {
            midi_status = 0; // system common does not run on
        }
    }
}

/**
 * Poll MIDI input: retry open every second, close on input fail
 */
void midi_task(void)
{
    uint8_t buf[32];
    int n;

    if (midi_fd < 0) {
        if (++midi_retry < MIDI_RETRY_FRAMES)
            return;
        midi_retry = 0;
        midi_fd = open(MIDI_DEVICE, O_RDONLY);
        if (midi_fd < 0)
            return;
        midi_status = 0;
        midi_have = 0;
#if MIDI_DEBUG
        printf("[midi open]\n");
#endif
    }

    n = read_xstack(buf, sizeof(buf), midi_fd);
    if (n < 0) {
#if MIDI_DEBUG
        printf("[midi err]\n");
#endif
        close(midi_fd);
        midi_fd = -1;
        midi_retry = 0;
        midi_status = 0;
        midi_have = 0;
        return;
    }
#if MIDI_DEBUG
    for (int i = 0; i < n; i++)
        printf(" %02X", buf[i]);
#endif
    for (int i = 0; i < n; i++)
        midi_parse_byte(buf[i]);
}
