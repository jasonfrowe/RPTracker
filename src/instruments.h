#ifndef INSTRUMENTS_H
#define INSTRUMENTS_H

#include "opl.h"

typedef struct {
    uint8_t m_ave, m_ksl, m_atdec, m_susrel, m_wave;
    uint8_t c_ave, c_ksl, c_atdec, c_susrel, c_wave;
    uint8_t feedback;
} OPL_Patch;

extern const OPL_Patch gm_bank[];
extern OPL_Patch user_bank[256];
extern const char* const patch_names[];
extern const OPL_Patch drum_bd;
extern const OPL_Patch drum_snare;
extern const OPL_Patch drum_hihat;

extern void OPL_SetPatch(uint8_t channel, const OPL_Patch* patch);

#endif // INSTRUMENTS_H