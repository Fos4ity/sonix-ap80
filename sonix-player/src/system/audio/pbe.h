#ifndef PBE_H
#define PBE_H

#include <stdbool.h>
#include <stdint.h>

// Perceptual bass enhancement, ported from Rockbox (lib/rbcodec/dsp/pbe.c,
// Chiwen Chang, GPLv2): the low band is delayed against the rest so the ear
// hears more bass than is there, then the "Biophonic EQ" (a 64 Hz lift, a
// 2 kHz presence, a 7.5 and 10 kHz cut and a high shelf) is laid over it.
// Strength 0..100 %, precut -4.5..0 dB, both saved to config like Rockbox's
// "pbe" and "pbe precut".
#define PBE_STRENGTH_MAX 100
#define PBE_PRECUT_MIN (-45) // tenths of a dB
#define PBE_PRECUT_MAX 0

void pbe_init(void);
// The master switch of the whole Rockbox DSP page: while it is off none of
// PBE, Bass/Treble, AFR or surround touch the sound. Saved as [sound] rbdsp.
void rbdsp_set_enabled(bool on);
bool rbdsp_enabled(void);
void pbe_set_strength(int percent); // 0 = off
int pbe_get_strength(void);
void pbe_set_precut(int tenths);
int pbe_get_precut(void);

// Rockbox's Bass / Treble tone controls, -12..+12 dB.
#define TONE_MIN (-12)
#define TONE_MAX 12
void tone_set_bass(int db);
void tone_set_treble(int db);
int tone_get_bass(void);
int tone_get_treble(void);

// Rockbox's Haas surround (lib/rbcodec/dsp/surround.c): delay 0 (off), 5, 8,
// 10, 15 or 30 ms; balance 0..99 %; fx1 600..8000 Hz; fx2 40..400 Hz; "side
// only"; dry/wet mix 0..100 %. Saved as [sound] surround_*.
#define SURROUND_DELAYS 6
extern const int surround_delay_table[SURROUND_DELAYS];
void surround_set_delay(int ms);
int surround_get_delay(void);
void surround_set_balance(int percent);
int surround_get_balance(void);
void surround_set_fx1(int hz);
int surround_get_fx1(void);
void surround_set_fx2(int hz);
int surround_get_fx2(void);
void surround_set_side_only(bool on);
bool surround_get_side_only(void);
void surround_set_mix(int percent);
int surround_get_mix(void);

// Rockbox's auditory fatigue reduction (afr.c): 0 off, 1 weak, 2 moderate,
// 3 strong. Saved as [sound] afr.
void afr_set(int level);
int afr_get(void);

void pbe_process(short *frames, int frame_count, int channels, int sample_rate);
void pbe_process_s32(int32_t *frames, int frame_count, int channels, int sample_rate);

#endif
