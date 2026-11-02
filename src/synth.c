/* SPDX-License-Identifier: GPL-3.0-only */
/* ZEUS-80's sound: eight voice modules, each two VCOs (VCO-1 triangle, saw, pulse, square; VCO-2 triangle, saw,
 * pulse, noise) with cross-modulation and sync, a mixer, a non-resonant high-pass, a resonant 24 dB low-pass (two
 * state-variable stages), a VCA, two ADSR envelopes; an LFO-1 for each section (5 waves, delayed per voice) and an
 * LFO-2 for the aftertouch. The upper and the lower section share the eight modules as the key mode says (WHOLE: all
 * eight; DUAL and SPLIT: four each), each assigned SOLO, UNISON 1 or 2, or POLY 1 or 2, with glide, hold, octave shift,
 * bender. MIDI from loop() is queued and handled here, in audio(). All integer. */
#include "fm1.h"
#include "zs.h"
#include "tables.h"

#define Q15 32767
#define ONE30 1073741824
#define CTRL 16u /* frames per control step */

volatile tone_t EDT[2];
volatile patch_t EDP;
volatile int8_t master_tune;
volatile uint8_t z_voice_sect[VOICES];
volatile uint32_t z_midi_seen;

static inline int32_t clampi(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }
static inline int32_t sat15(int32_t x) {
  if (x >= 49152) return Q15;
  if (x <= -49152) return -Q15;
  int32_t x2 = (int32_t)(((int64_t)x * x) >> 15);
  int32_t x3 = (int32_t)(((int64_t)x2 * x) >> 15);
  return x - ((x3 * 4855) >> 15);
}
static uint32_t nz = 77777u;
static inline int32_t noise(void) {
  nz ^= nz << 13, nz ^= nz >> 17, nz ^= nz << 5;
  return (int32_t)nz >> 16;
}
static uint32_t coef_block(int32_t ms) { /* a decay per CTRL frames, Q16 */
  int32_t n = ms * 441;
  if (n <= (int32_t)CTRL * 10) return 0;
  return 65536u - (uint32_t)(10485760 / n);
}
static int32_t attack_step(int32_t ms) {
  int32_t blocks = (ms * 441) / ((int32_t)CTRL * 10);
  return blocks < 1 ? ONE30 : ONE30 / blocks;
}
static inline int32_t tms(int v) { return ENV_MS[clampi(v, 0, 127)]; }
static inline int32_t onepole_a(int32_t hz) {
  int32_t w = (clampi(hz, 1, 20000) * 4669) >> 10;
  return (int32_t)(((uint32_t)w << 15) / (uint32_t)(32768 + w));
}
static inline int32_t blep(uint32_t t, uint32_t inc, uint32_t rcp) {
  if (t < inc) {
    uint64_t xx = ((uint64_t)(t >> 16) * rcp) >> 17;
    int32_t x = xx > 32768u ? 32768 : (int32_t)xx; /* (cross-mod moves inc away from rcp's) */
    return 2 * x - ((x * x) >> 15) - 32768;
  }
  uint32_t d = 0u - t;
  if (d < inc) {
    uint64_t xx = ((uint64_t)(d >> 16) * rcp) >> 17;
    int32_t x = 32768 - (xx > 32768u ? 32768 : (int32_t)xx);
    return (x * x) >> 15;
  }
  return 0;
}
static uint32_t inc_q8(int32_t p) { /* pitch (MIDI note x 256) -> phase step */
  p = clampi(p, 0, 127 * 256);
  int n = p >> 8, f = p & 255;
  uint32_t a = note_inc(n), b = note_inc(n + 1);
  return a + (uint32_t)(((uint64_t)(b - a) * (uint32_t)f) >> 8);
}

enum { ST_IDLE, ST_ATTACK, ST_DECAY, ST_SUSTAIN, ST_RELEASE };
/* an envelope step; times 0..127 (ENV_MS), sustain 0..100 */
static void adsr(int32_t *lvl, uint8_t *st, int a, int d, int s, int r) {
  int32_t sus = (int32_t)(((int64_t)clampi(s, 0, 100) * ONE30) / 100);
  switch (*st) {
    case ST_ATTACK:
      *lvl += attack_step(tms(a));
      if (*lvl >= ONE30) *lvl = ONE30, *st = ST_DECAY;
      break;
    case ST_DECAY:
      *lvl = sus + (int32_t)(((int64_t)(*lvl - sus) * coef_block(tms(d))) >> 16);
      if (*lvl - sus < 65536 && *lvl - sus > -65536) *lvl = sus, *st = ST_SUSTAIN;
      break;
    case ST_SUSTAIN: *lvl = sus; break;
    case ST_RELEASE:
      *lvl = (int32_t)(((int64_t)*lvl * coef_block(tms(r))) >> 16);
      if (*lvl < 32768) *lvl = 0, *st = ST_IDLE;
      break;
    default: break;
  }
}

/* ---------------------------------------------------------------- the modules */

typedef struct {
  uint8_t sect, note, gate, held, vel, on;
  uint32_t age;
  int32_t pitch, target, det; /* MIDI note x 256: now (gliding), where it goes; unison detune */
  uint32_t ph1, ph2;
  int32_t e1, e2, ldly;
  uint8_t s1, s2;
  int32_t hp, ic1, ic2, ic3, ic4, amp;
} voice_t;
static voice_t V[VOICES];
static uint32_t vage;

/* each section: the notes held (most recent last), and its controllers */
#define STK 16
static struct {
  uint8_t stack[STK], n;
  uint8_t down[16];   /* keys down now (bits) */
  int16_t bend;       /* -8192..8191 */
  uint8_t mod, at, pedal;
  uint32_t lph, l2ph; /* LFO-1, LFO-2 */
  int32_t lfo, lfo2, rnd;
} S[2];

static void pool(int s, int *lo, int *hi) {
  if (EDP.key_mode == KM_WHOLE) *lo = 0, *hi = VOICES;
  else *lo = s ? VOICES / 2 : 0, *hi = s ? VOICES : VOICES / 2;
}
static int holding(int s) { return EDP.s[s].hold == HOLD_ON || (EDP.s[s].hold == HOLD_MIDI && S[s].pedal); }
static int is_down(int s, int n) { return S[s].down[n >> 3] >> (n & 7) & 1; }

static void release(voice_t *v) {
  v->gate = 0, v->held = 0;
  if (v->s1 != ST_IDLE) v->s1 = ST_RELEASE;
  if (v->s2 != ST_IDLE) v->s2 = ST_RELEASE;
}

/* a module starts note (retrig: the envelopes start again; else it only glides there) */
static void start(voice_t *v, int s, int note, int vel, int retrig, int32_t det) {
  volatile sect_t *P = &EDP.s[s];
  int key = clampi(note + (clampi(P->oct, 0, 4) - 2) * 12, 0, 127);
  int was = v->on && v->sect == s;
  v->sect = (uint8_t)s, v->note = (uint8_t)note, v->gate = 1, v->held = 0, v->on = 1, v->vel = (uint8_t)clampi(vel, 1, 127);
  v->det = det;
  v->target = key * 256 + det;
  if (!P->glide || !v->pitch) v->pitch = v->target;
  if (retrig || !was) {
    if (!was) v->hp = v->ic1 = v->ic2 = v->ic3 = v->ic4 = 0, v->amp = 0, v->e1 = v->e2 = 0;
    if (EDT[s].p[T_RESET]) v->e1 = v->e2 = 0;
    v->s1 = v->s2 = ST_ATTACK, v->ldly = 0;
  }
  v->age = ++vage;
}

/* SOLO and the UNISONs: the modules of the section's pool follow the notes held */
static void reassign(int s, int vel, int pressed) {
  int lo, hi, n = S[s].n, np;
  pool(s, &lo, &hi);
  np = hi - lo;
  int as = EDP.s[s].assign, per, groups;
  if (as == AS_SOLO) per = 1, groups = 1, hi = lo + 1;
  else if (as == AS_UNI1) per = np, groups = 1;
  else per = n <= 1 ? np : n == 2 ? np / 2 : n <= 4 ? np / 4 : 1, per = per < 1 ? 1 : per, groups = np / per;
  int32_t cents = EDP.s[s].detune * 12 / 100; /* between neighbours */
  for (int i = lo; i < hi; i++) {
    voice_t *v = &V[i];
    int g = (i - lo) / per, j = (i - lo) % per;
    if (g >= groups || g >= n) {
      if (v->sect == s && v->gate) release(v);
      continue;
    }
    int note = S[s].stack[n - 1 - g];
    int32_t det = per > 1 ? (2 * j - (per - 1)) * cents * 256 / 200 : 0;
    if (v->sect == s && v->gate && v->note == note) continue;
    /* a new key down retriggers; a key let go falls back to the one before, legato */
    start(v, s, note, vel ? vel : v->vel, pressed || !(v->sect == s && v->gate), det);
  }
}

static void stack_remove(int s, int note) {
  for (int i = 0; i < S[s].n; i++)
    if (S[s].stack[i] == note) {
      for (int j = i; j + 1 < S[s].n; j++) S[s].stack[j] = S[s].stack[j + 1];
      S[s].n--;
      return;
    }
}
static void drop_released(int s) { /* the notes hold kept: gone */
  for (int i = S[s].n - 1; i >= 0; i--)
    if (!is_down(s, S[s].stack[i])) stack_remove(s, S[s].stack[i]);
  for (int i = 0; i < VOICES; i++)
    if (V[i].sect == s && V[i].held) release(&V[i]);
}

static void sect_on(int s, int note, int vel) {
  if (EDP.s[s].hold == HOLD_ON) drop_released(s); /* a new key ends what hold kept */
  S[s].down[note >> 3] |= (uint8_t)(1u << (note & 7));
  stack_remove(s, note);
  if (S[s].n == STK) stack_remove(s, S[s].stack[0]);
  S[s].stack[S[s].n++] = (uint8_t)note;
  int as = EDP.s[s].assign;
  if (as == AS_POLY1 || as == AS_POLY2) {
    int lo, hi, best = -1;
    pool(s, &lo, &hi);
    for (int i = lo; i < hi; i++)
      if (V[i].on && V[i].sect == s && V[i].note == note) best = i;
    uint32_t old = 0xFFFFFFFFu;
    if (best < 0)
      for (int i = lo; i < hi; i++) /* free: POLY 1 the one free longest (release tails ring on), POLY 2 the first */
        if (!V[i].on) {
          if (as == AS_POLY2) {
            best = i;
            break;
          }
          if (V[i].age < old) old = V[i].age, best = i;
        }
    if (best < 0)
      for (int i = lo; i < hi; i++)
        if (!V[i].gate && V[i].age < old) old = V[i].age, best = i;
    if (best < 0)
      for (int i = lo; i < hi; i++)
        if (V[i].age < old) old = V[i].age, best = i;
    if (best >= 0) start(&V[best], s, note, vel, 1, ((best * 37) % 9 - 4) * 256 / 100); /* a few cents apart */
  } else reassign(s, vel, 1);
}
static void sect_off(int s, int note) {
  S[s].down[note >> 3] &= (uint8_t)~(1u << (note & 7));
  if (holding(s)) {
    for (int i = 0; i < VOICES; i++)
      if (V[i].sect == s && V[i].note == note && V[i].gate) V[i].held = 1;
    return;
  }
  stack_remove(s, note);
  int as = EDP.s[s].assign;
  if (as == AS_POLY1 || as == AS_POLY2) {
    for (int i = 0; i < VOICES; i++)
      if (V[i].sect == s && V[i].note == note && V[i].gate) release(&V[i]);
  } else reassign(s, 0, 0);
}
static void sect_all_off(int s) {
  S[s].n = 0;
  for (int i = 0; i < 16; i++) S[s].down[i] = 0;
  for (int i = 0; i < VOICES; i++)
    if (V[i].sect == s && V[i].on) release(&V[i]);
}

void z_note(int sect, int note, int vel) {
  note &= 127;
  if (sect == 2) { /* the keyboard: as the key mode says */
    int km = EDP.key_mode;
    if (km == KM_DUAL) {
      z_note(0, note, vel), z_note(1, note, vel);
      return;
    }
    sect = km == KM_SPLIT && note < EDP.split ? 1 : 0;
  }
  if (sect < 0 || sect > 1) return;
  if (vel) sect_on(sect, note, vel);
  else sect_off(sect, note);
}

/* ---------------------------------------------------------------- MIDI: channel 1 the keyboard, 2 upper, 3 lower */

static void direct(uint8_t st, uint8_t d1, uint8_t d2) {
  int ch = st & 15, t = st & 0xF0;
  if (ch > 2) return;
  z_midi_seen = z_midi_seen + 1;
  int s0 = ch == 2 ? 1 : 0, s1 = ch == 1 ? 0 : 1; /* the sections it reaches */
  switch (t) {
    case 0x90:
    case 0x80: z_note(ch == 0 ? 2 : ch - 1, d1, t == 0x90 ? d2 : 0); break;
    case 0xB0:
      for (int s = s0; s <= s1; s++) switch (d1) {
          case 1: S[s].mod = d2 & 127; break;
          case 64: {
            int was = S[s].pedal;
            S[s].pedal = d2 >= 64;
            if (was && !S[s].pedal && EDP.s[s].hold == HOLD_MIDI) {
              drop_released(s);
              if (EDP.s[s].assign <= AS_UNI2) reassign(s, 0, 0);
            }
            break;
          }
          case 121: S[s].mod = 0, S[s].bend = 0, S[s].at = 0, S[s].pedal = 0; break;
          case 120:
          case 123: sect_all_off(s); break;
          default: break;
        }
      break;
    case 0xD0:
      for (int s = s0; s <= s1; s++) S[s].at = d1 & 127;
      break;
    case 0xE0:
      for (int s = s0; s <= s1; s++) S[s].bend = (int16_t)(((d2 & 127) << 7 | (d1 & 127)) - 8192);
      break;
    default: break;
  }
}
#define QN 256
static volatile uint32_t q[QN];
static volatile uint32_t q_wr, q_rd;
static volatile uint8_t panic_req;
void z_midi(uint8_t st, uint8_t d1, uint8_t d2) {
  if (q_wr - q_rd >= QN) return;
  q[q_wr % QN] = (uint32_t)st << 16 | (uint32_t)d1 << 8 | d2;
  RING_PUBLISH();
  q_wr = q_wr + 1;
}
void z_panic(void) { panic_req = 1; }

/* ---------------------------------------------------------------- the metronome */
static int32_t click_env, click_amp;
static uint32_t click_ph, click_inc;
void z_click(int accent) { click_env = Q15, click_amp = accent ? 9000 : 5000, click_inc = accent ? 0x0A000000u : 0x07800000u, click_ph = 0; }

void z_init(void) {
  for (int i = 0; i < VOICES; i++) V[i] = (voice_t){0}, V[i].ph1 = (uint32_t)i * 0x1234567u, V[i].ph2 = (uint32_t)i * 0x2345679u;
  for (int s = 0; s < 2; s++) {
    S[s].n = 0, S[s].bend = 0, S[s].mod = S[s].at = S[s].pedal = 0;
    for (int i = 0; i < 16; i++) S[s].down[i] = 0;
  }
}

/* ---------------------------------------------------------------- the sound */

static int32_t lfo_wave(int w, uint32_t ph, int32_t rnd) {
  switch (w) {
    case 1: return (int32_t)(ph >> 16) - 32768; /* saw up */
    case 2: return 32767 - (int32_t)(ph >> 16);                                                                        /* saw down */
    case 3: return ph < 0x80000000u ? Q15 : -Q15;
    case 4: return rnd;
    default: { /* triangle */
      int32_t x = (int32_t)(ph >> 15);                                      /* 0..131071 */
      return x < 65536 ? x - 32768 : 98303 - x;
    }
  }
}

void z_render(int32_t *out, uint32_t frames) {
  if (frames > 64) frames = 64;
  if (panic_req) {
    panic_req = 0;
    for (int s = 0; s < 2; s++) sect_all_off(s), S[s].pedal = 0;
  }
  while (q_rd != q_wr) {
    uint32_t m = q[q_rd % QN];
    q_rd = q_rd + 1;
    direct((uint8_t)(m >> 16), (uint8_t)(m >> 8), (uint8_t)m);
  }
  static int32_t L[64], R[64];
  for (uint32_t i = 0; i < frames; i++) L[i] = R[i] = 0;
  int km = EDP.key_mode;

  for (uint32_t b0 = 0; b0 < frames; b0 += CTRL) {
    uint32_t n = frames - b0 < CTRL ? frames - b0 : CTRL;
    /* the sections' LFOs */
    for (int s = 0; s < 2; s++) {
      volatile uint8_t *t = EDT[s].p;
      uint32_t inc = LFO_INC[clampi(t[T_LFO_RATE] * 127 / 100, 0, 127)] * n;
      uint32_t was = S[s].lph;
      S[s].lph += inc;
      if (S[s].lph < was) S[s].rnd = noise(); /* a new random step each cycle */
      S[s].lfo = lfo_wave(t[T_LFO_WAVE], S[s].lph, S[s].rnd);
      S[s].l2ph += LFO_INC[clampi(EDP.s[s].lfo2 * 127 / 100, 0, 127)] * n;
      S[s].lfo2 = lfo_wave(0, S[s].l2ph, 0);
    }
    for (int vi = 0; vi < VOICES; vi++) {
      voice_t *v = &V[vi];
      if (!v->on) {
        z_voice_sect[vi] = 0;
        continue;
      }
      int s = v->sect;
      z_voice_sect[vi] = (uint8_t)(s + 1);
      uint8_t t[NTP];
      for (int i = 0; i < NTP; i++) t[i] = EDT[s].p[i];
      volatile sect_t *P = &EDP.s[s];
      int key = clampi(v->target >> 8, 0, 127);

      /* envelopes: times shorter up the keyboard as KEY says, levels with the velocity as DYN says */
      int k1 = (key - 60) * t[T_K1] / 200, k2 = (key - 60) * t[T_K2] / 200;
      adsr(&v->e1, &v->s1, t[T_A1] * 127 / 100 - k1, t[T_D1] * 127 / 100 - k1, t[T_S1], t[T_R1T] * 127 / 100 - k1);
      adsr(&v->e2, &v->s2, t[T_A2] * 127 / 100 - k2, t[T_D2] * 127 / 100 - k2, t[T_S2], t[T_R2T] * 127 / 100 - k2);
      if (v->s2 == ST_IDLE) {
        v->on = 0, v->gate = 0;
        continue;
      }
      int32_t dynv = 100 - t[T_DYN] + t[T_DYN] * v->vel / 127; /* 0..100 */
      int32_t e1 = v->e1 >> 15, e2 = v->e2 >> 15;
      if (t[T_DYN1]) e1 = e1 * dynv / 100;
      if (t[T_DYN2]) e2 = e2 * dynv / 100;
      /* the LFO comes in after its delay */
      if (v->ldly < ONE30) {
        v->ldly += t[T_LFO_DELAY] ? attack_step(tms(t[T_LFO_DELAY] * 127 / 100)) : ONE30;
        if (v->ldly > ONE30) v->ldly = ONE30;
      }
      int32_t lfo = (int32_t)(((int64_t)S[s].lfo * (v->ldly >> 15)) >> 15);
      int ctl = S[s].mod > S[s].at ? S[s].mod : S[s].at;
      int32_t touch = clampi(P->at_sens, 0, 100) * ctl / 127; /* 0..100 */

      /* pitch: glide, bender, modulation, key follow, ranges */
      if (P->glide) {
        v->pitch = v->target + (int32_t)(((int64_t)(v->pitch - v->target) * coef_block(tms(P->glide * 110 / 100))) >> 16);
        if (v->pitch - v->target < 4 && v->pitch - v->target > -4) v->pitch = v->target;
      } else v->pitch = v->target;
      int32_t bq = (int32_t)S[s].bend * clampi(P->bend, 0, 100) / 100 * 3072 / 8192; /* 100: an octave */
      int32_t b1 = P->b1 == 0 ? 0 : P->b1 == 1 ? bq : bq * 5 / 2, b2 = P->b2 == 0 ? 0 : P->b2 == 1 ? bq : bq * 5 / 2;
      int32_t mod = ((t[T_MOD_LFO] * t[T_MOD_LFO] * 12 * 256 / 10000) * lfo >> 15) + ((t[T_MOD_ENV1] * t[T_MOD_ENV1] * 24 * 256 / 10000) * e1 >> 15);
      int32_t m1 = t[T_VCO1_MOD] == 0 ? mod : t[T_VCO1_MOD] == 2 ? -mod : 0, m2 = t[T_VCO2_MOD] == 0 ? mod : t[T_VCO2_MOD] == 2 ? -mod : 0;
      int32_t vib = P->at_sel == 0 ? (touch * 256 / 100) * S[s].lfo2 >> 15 : 0;
      int32_t mt = master_tune * 256 / 100;
      int32_t base = v->pitch, kfb = 60 * 256 + (base - 60 * 256) * t[T_KF] / 100;
      int32_t p1 = (t[T_KF_SEL] == 0 ? kfb : base) + (t[T_R1] - 2) * 12 * 256 + m1 + b1 + vib + mt;
      int32_t p2 = (t[T_KF_SEL] == 2 ? kfb : base) + (t[T_R2] - 2) * 12 * 256 + (t[T_FINE] - 50) * 256 / 100 + m2 + b2 + vib + mt;
      uint32_t inc1 = inc_q8(p1), inc2 = inc_q8(p2);
      uint32_t rcp1 = 0xFFFFFFFFu / ((inc1 >> 16) | 1u), rcp2 = 0xFFFFFFFFu / ((inc2 >> 16) | 1u);
      /* pulse width: PW, and PWM from ENV-1 or LFO-1 */
      int32_t src = t[T_PWM_SEL] ? (lfo + Q15) >> 1 : e1;
      if (t[T_PWM_POL]) src = -src;
      int32_t duty = 32768 - t[T_PW] * 295 - (t[T_PWM] * 295 * src >> 15); /* of 65536 */
      duty = clampi(duty, 1500, 64000);
      uint32_t pw = (uint32_t)duty << 16;
      /* cross-mod: VCO-2 bends VCO-1's frequency (up to 4 x), by hand and from ENV-1 */
      int32_t xm = clampi(t[T_XMOD] + (t[T_XMOD_ENV] * e1 >> 15), 0, 100);
      int32_t xmq = xm * xm * 16384 / 10000; /* Q12 */
      if (t[T_XMOD_POL]) xmq = -xmq;
      int32_t g1 = clampi((100 - t[T_MIX]) * 2, 0, 100) * 200, g2 = clampi(t[T_MIX] * 2, 0, 100) * 200; /* Q15 */
      int w1 = t[T_W1], w2 = t[T_W2], sync = t[T_SYNC];
      /* the high-pass */
      int32_t hpa = onepole_a(t[T_HPF] ? 20 + t[T_HPF] * t[T_HPF] / 5 : 12); /* (12 Hz: the pulse's DC goes) */
      /* the low-pass: cutoff from FREQ, the envelope, LFO-1, the key and the aftertouch */
      int32_t es = t[T_ENV_SEL] ? e2 : e1;
      if (t[T_ENV_POL]) es = -es;
      int32_t ci = t[T_FREQ] * 10 + (t[T_VENV] * 8 * es >> 15) + (t[T_VLFO] * 3 * lfo >> 15) + (key - 60) * t[T_VKEY] * 9 / 100 +
                   (P->at_sel == 1 ? touch * 3 : 0);
      uint32_t g = SVF_G[clampi(ci, 0, 1023)];
      uint32_t kq1 = 121110u, kq2 = 50135u - (uint32_t)clampi(t[T_RES], 0, 100) * 491u;
      uint32_t den1 = 65536u + (uint32_t)(((uint64_t)g * (g + kq1)) >> 16), den2 = 65536u + (uint32_t)(((uint64_t)g * (g + kq2)) >> 16);
      int32_t a11 = (int32_t)(0xFFFFFFFFu / den1), a12 = (int32_t)(((int64_t)g * a11) >> 16), a13 = (int32_t)(((int64_t)g * a12) >> 16);
      int32_t a21 = (int32_t)(0xFFFFFFFFu / den2), a22 = (int32_t)(((int64_t)g * a21) >> 16), a23 = (int32_t)(((int64_t)g * a22) >> 16);
      /* the VCA: LEVEL, ENV-2, the LFO's tremolo */
      int32_t trem = Q15 - (t[T_VCA_LFO] * ((lfo + Q15) >> 1) / 100);
      int32_t amp = (int32_t)(((int64_t)e2 * t[T_VCA] * 328 >> 15) * trem >> 15);
      /* the section's place: balance (DUAL, SPLIT), stereo (SEPARATE: upper left, lower right) */
      int32_t bal = km == KM_WHOLE ? Q15 : clampi((s ? 100 - EDP.balance : EDP.balance) * 2, 0, 100) * 327;
      amp = (int32_t)((int64_t)amp * bal >> 15);
      int32_t gl = Q15, gr = Q15;
      if (EDP.stereo) gl = s ? 9000 : Q15, gr = s ? Q15 : 9000;
      int32_t a0 = v->amp, da = (amp - a0) / (int32_t)n;
      v->amp = amp;

      for (uint32_t i = 0; i < n; i++) {
        /* VCO-2 */
        uint32_t q2 = v->ph2;
        int32_t o2, o1, bb;
        switch (w2) {
          case 0: o2 = (q2 < 0x80000000u ? (int32_t)(q2 >> 15) : (int32_t)((0u - q2) >> 15)) - 32768; break;
          case 1: o2 = (int32_t)(q2 >> 16) - 32768 - blep(q2, inc2, rcp2); break;
          case 2:
            bb = blep(q2, inc2, rcp2);
            o2 = (q2 < pw ? 26000 : -26000) + ((bb - blep(q2 - pw, inc2, rcp2)) * 26000 >> 15);
            break;
          default: o2 = noise(); break;
        }
        uint32_t i1 = inc1;
        if (xmq) i1 = (uint32_t)((int64_t)inc1 + (((int64_t)inc1 * o2 >> 15) * xmq >> 12));
        uint32_t q1 = v->ph1;
        switch (w1) {
          case 0: o1 = (q1 < 0x80000000u ? (int32_t)(q1 >> 15) : (int32_t)((0u - q1) >> 15)) - 32768; break;
          case 1: o1 = (int32_t)(q1 >> 16) - 32768 - blep(q1, i1, rcp1); break;
          case 2:
            bb = blep(q1, i1, rcp1);
            o1 = (q1 < pw ? 26000 : -26000) + ((bb - blep(q1 - pw, i1, rcp1)) * 26000 >> 15);
            break;
          default:
            bb = blep(q1, i1, rcp1);
            o1 = (q1 < 0x80000000u ? 26000 : -26000) + ((bb - blep(q1 - 0x80000000u, i1, rcp1)) * 26000 >> 15);
            break;
        }
        uint32_t n1 = q1 + i1, n2 = q2 + inc2;
        if (sync == 1 && n1 < q1) n2 = 0; /* VCO-1 restarts VCO-2 */
        else if (sync == 2 && n2 < q2) n1 = 0;
        v->ph1 = n1, v->ph2 = n2;
        /* mixer, high-pass, low-pass */
        int32_t x = ((o1 * g1) >> 15) + ((o2 * g2) >> 15);
        v->hp += ((x - v->hp) * hpa) >> 15, x -= v->hp;
        x = sat15(x);
        int32_t v0 = x * 64, v3 = v0 - v->ic2;
        int32_t v1 = (int32_t)(((int64_t)a11 * v->ic1 + (int64_t)a12 * v3) >> 16);
        int32_t vv2 = v->ic2 + (int32_t)(((int64_t)a12 * v->ic1 + (int64_t)a13 * v3) >> 16);
        v->ic1 = clampi(2 * v1 - v->ic1, -134217728, 134217728), v->ic2 = clampi(2 * vv2 - v->ic2, -134217728, 134217728);
        v3 = vv2 - v->ic4;
        v1 = (int32_t)(((int64_t)a21 * v->ic3 + (int64_t)a22 * v3) >> 16);
        int32_t y = v->ic4 + (int32_t)(((int64_t)a22 * v->ic3 + (int64_t)a23 * v3) >> 16);
        v->ic3 = clampi(2 * v1 - v->ic3, -134217728, 134217728), v->ic4 = clampi(2 * y - v->ic4, -134217728, 134217728);
        y = sat15(y >> 6);
        y = (int32_t)(((int64_t)y * (a0 + da * (int32_t)i)) >> 15);
        L[b0 + i] += (y * gl) >> 15, R[b0 + i] += (y * gr) >> 15;
      }
    }
  }
  /* the click, the master */
  for (uint32_t i = 0; i < frames; i++) {
    if (click_env > 64) {
      click_ph += click_inc;
      int32_t c = (int32_t)(((int64_t)sine(click_ph) * click_env >> 15) * click_amp >> 15);
      click_env -= click_env >> 7;
      L[i] += c, R[i] += c;
    }
    out[2 * i] += sat15(L[i] * 3 / 8);
    out[2 * i + 1] += sat15(R[i] * 3 / 8);
  }
}
