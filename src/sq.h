/* SPDX-License-Identifier: GPL-3.0-only */
/* ZEUS-80's arpeggiator (as LAX-32's) and six-track step sequencer, after those of Skiff (github.com/illuminus5/skiff, GPL-3.0):
 * the same arpeggio modes, steps of up to 4 notes each with its own length, velocity, accent, chance, trig condition,
 * ratchet or flam and nudge, the play directions, swing, live and step recording. They run in audio() (seq.c) and
 * play the synth's sections or MIDI out; loop() talks to them through sq_cmd() and the settings below. */
#ifndef SQ_H
#define SQ_H
#include <stdint.h>
#include "zs.h"

/* where a track (or the keys) plays: the keyboard (as the key mode says), the upper or the lower section, or a
 * MIDI channel out (DST_EXT + 0..15). The first three have an arpeggiator each. */
enum { DST_KBD, DST_UPPER, DST_LOWER, DST_EXT };
#define DESTS (DST_EXT + 16)
#define ARPS 3

#define TRACKS 6
#define STEPS 64
#define PATTERNS 4

/* ---- a step */
#define SF_RATCH 3u  /* ratchet - 1 (x1..x4) */
#define SF_FLAM 4u   /* a grace hit before */
#define SF_ACCENT 8u /* velocity 127 */
typedef struct {
  uint8_t note[4];
  uint8_t n;      /* notes, 0: empty */
  uint8_t vel;    /* 1..127 */
  uint8_t len;    /* 1..15: sixteenths of a step, 16: one step, 17..79: 2..64 steps */
  uint8_t chance; /* 1..100 % */
  uint8_t cond;   /* TC_* */
  uint8_t flags;
  int8_t nudge;   /* -32..31 64ths of a step */
  uint8_t spare;
} step_t;
#define LEN_MAX 79
enum { TC_NONE, TC_FILL, TC_NFILL, TC_1_2, TC_2_2, TC_1_3, TC_2_3, TC_3_3, TC_1_4, TC_2_4, TC_3_4, TC_4_4, TC_COUNT };
extern const char *const TC_NAME[TC_COUNT];

/* the steps belong to audio(): loop() reads them, and changes them through sq_cmd */
extern step_t STEP[PATTERNS][TRACKS][STEPS];
extern volatile uint8_t TLEN[PATTERNS][TRACKS]; /* 1..64 */

/* ---- a track: where it plays (DST_*), its step length, its direction (written by loop()) */
enum { DIR_FWD, DIR_RVRS, DIR_PING, DIR_RAND };
extern const char *const DIR_NAME[4];
#define DIVS 8
extern const char *const DIV_NAME[DIVS]; /* 1/4 1/8 1/16 1/32 8T 16T 1/2 1/1 */
typedef struct {
  uint8_t part, div, dir, mute;
} track_t;
extern volatile track_t TRK[TRACKS];

/* ---- the arpeggiator: the keyboard's, the upper's, the lower's (written by loop()) */
enum { AM_OFF, AM_UP, AM_DN, AM_UPDN, AM_RND, AM_ORD, AM_REPEAT, AM_DNUP, AM_UP8, AM_CONV, AM_DIVG, AM_PINKY,
       AM_THUMB, AM_WALK, AM_CHORD, AM_COUNT };
extern const char *const AM_NAME[AM_COUNT];
typedef struct {
  uint8_t mode, rate, oct, gate;      /* AM_*, DIV_NAME, 1..4 octaves, 1..127 of the step */
  uint8_t swing, chance, hold, order; /* 0..100, 1..100 %, latch, 0 low to high / 1 as played */
} arp_t;
extern volatile arp_t ARP[ARPS];

/* ---- the whole: tempo, swing, the metronome, the pattern asked for (written by loop()) */
enum { CLICK_OFF, CLICK_REC, CLICK_ON };
typedef struct {
  uint8_t bpm;     /* 40..240 */
  uint8_t swing;   /* 0..100 */
  uint8_t click;   /* CLICK_* */
  uint8_t pattern; /* 0..3: the one to play (from the next bar while playing) */
} glob_t;
extern volatile glob_t GLOB;
extern volatile uint8_t sq_fill; /* FILL held */

/* ---- what loop() shows */
extern volatile uint8_t sq_playing, sq_rec, sq_pat;
extern volatile uint8_t sq_pos[TRACKS];  /* the step playing */
extern volatile uint8_t sq_lit[TRACKS];  /* blocks since a track's last note (the row's light) */
extern volatile uint32_t sq_edits;       /* bumped when audio() changes a step (recording) */

/* ---- loop() -> audio() */
enum {
  OP_KEY_ON,   /* part, note, velocity: a key or MIDI: to the arpeggiator, or played (and recorded) */
  OP_KEY_OFF,  /* part, note */
  OP_PLAY,     /* 1 start, 0 stop */
  OP_REC,      /* 1 arm, 0 off; b: the track recorded */
  OP_TARGET,   /* track, step: the step the next edits are on */
  OP_SREC,     /* note, 1 a new chord / 0 one more note of it: step recording into the target */
  OP_SET,      /* field (SET_*), value (c: the sign for NUDGE): on the target step */
  OP_CLEAR,    /* 0 the target step, 1 the target's track (this pattern) */
  OP_ARP_STOP, /* part: forget the notes its arpeggiator holds */
};
enum { SET_PITCH, SET_OCT, SET_LEN, SET_VEL, SET_CHANCE, SET_RATCH, SET_NUDGE, SET_ACCENT };
void sq_cmd(uint8_t op, uint8_t a, uint8_t b, uint8_t c);

void sq_init(void);    /* the factory pattern; before audio() runs */
void sq_tick(uint32_t frames); /* audio(): before z_render, in chunks of up to 64 frames */

#endif
