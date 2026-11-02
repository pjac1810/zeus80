/* SPDX-License-Identifier: GPL-3.0-only */
/* ZEUS-80: an eight-voice analogue-style polysynth module for the M-VAVE FM-1, after the 1984 Roland MKS-80.
 * Shared by the panel (main.c), the sound (synth.c) and the sequencer (seq.c). */
#ifndef ZS_H
#define ZS_H
#include <stdint.h>

#ifndef RING_PUBLISH
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")
#endif

#define VOICES 8
#define NTP 48 /* a tone's parameters (tones.h names them) */
enum {         /* their places */
  T_LFO_RATE, T_LFO_DELAY, T_LFO_WAVE, T_VCA_LFO, T_MOD_LFO, T_MOD_ENV1, T_VCO1_MOD, T_VCO2_MOD,
  T_PW, T_PWM, T_PWM_SEL, T_PWM_POL, T_W1, T_R1, T_KF, T_KF_SEL,
  T_XMOD, T_XMOD_ENV, T_XMOD_POL, T_SYNC, T_W2, T_R2, T_FINE, T_MIX,
  T_FREQ, T_RES, T_HPF, T_VKEY, T_ENV_SEL, T_ENV_POL, T_VENV, T_VLFO,
  T_A1, T_D1, T_S1, T_R1T, T_A2, T_D2, T_S2, T_R2T,
  T_K1, T_K2, T_DYN1, T_DYN2, T_VCA, T_DYN, T_RESET, T_SPARE
};
typedef struct {
  uint8_t p[NTP];
} tone_t;

/* a patch: the key mode and, for the upper (0) and the lower (1) section, how its tone is played */
enum { KM_WHOLE, KM_DUAL, KM_SPLIT };
enum { AS_SOLO, AS_UNI1, AS_UNI2, AS_POLY1, AS_POLY2 };
enum { HOLD_OFF, HOLD_ON, HOLD_MIDI };
typedef struct {
  uint8_t tone, assign, detune, oct;   /* 0..63 (11..88); AS_*; 0..100; 0..4 = -2..+2 octaves */
  uint8_t glide, hold, lfo2, at_sens;  /* 0..100; HOLD_*; 0..100; 0..100 */
  uint8_t bend, b1, b2, at_sel;        /* 0..100; VCO-1 and VCO-2 bend 0 OFF 1 NORM 2 WIDE; aftertouch 0 VCO 1 VCF */
} sect_t;
typedef struct {
  uint8_t key_mode, split, balance, stereo; /* KM_*; MIDI note; 0 lower .. 100 upper; 0 MIX 1 SEPARATE */
  sect_t s[2];
} patch_t;
#define PATCH_BYTES 28

/* what sounds: the edit buffers (loop() writes them, audio() reads them) */
extern volatile tone_t EDT[2];
extern volatile patch_t EDP;
extern volatile int8_t master_tune; /* cents */

/* ---- synth.c */
void z_init(void);
void z_midi(uint8_t st, uint8_t d1, uint8_t d2); /* from loop(): queued. Channel 1 the keyboard (as the key mode says),
                                                    2 the upper section, 3 the lower */
void z_panic(void);                              /* from loop() */
void z_note(int sect, int note, int vel);        /* audio() only: sect 0 upper, 1 lower, 2 the keyboard; vel 0: off */
void z_click(int accent);                        /* audio() only: the metronome */
void z_render(int32_t *out, uint32_t frames);    /* frames <= 64 */
extern volatile uint8_t z_voice_sect[VOICES];    /* each module: 0 idle, 1 upper, 2 lower (the panel's lights) */
extern volatile uint32_t z_midi_seen;

#endif
