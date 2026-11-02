/* SPDX-License-Identifier: GPL-3.0-only */
/* ZEUS-80's arpeggiator and six-track sequencer (sq.h), run from audio() a chunk of up to 64 frames at a time.
 * Modelled on Skiff's (github.com/illuminus5/skiff, GPL-3.0): its arpeggio modes and their order (arp_pick), the
 * step's chance, trig conditions, ratchet, flam and nudge, the directions, the swing, live recording into the nearest
 * step (overdub, chords of up to 4, the length as held) and step recording. */
#include "fm1.h"
#include "sq.h"

const char *const TC_NAME[TC_COUNT] = {"--", "FILL", "!FILL", "1:2", "2:2", "1:3", "2:3", "3:3", "1:4", "2:4", "3:4", "4:4"};
const char *const DIR_NAME[4] = {"FWD", "RVRS", "PING", "RAND"};
const char *const DIV_NAME[DIVS] = {"1/4", "1/8", "1/16", "1/32", "8T", "16T", "1/2", "1/1"};
const char *const AM_NAME[AM_COUNT] = {"OFF", "UP", "DN", "UPDN", "RND", "ORD", "REPEAT", "DNUP",
                                       "UP+8", "CONV", "DIVG", "PINKY", "THUMB", "WALK", "CHORD"};

step_t STEP[PATTERNS][TRACKS][STEPS] POOL;
volatile uint8_t TLEN[PATTERNS][TRACKS];
volatile track_t TRK[TRACKS];
volatile arp_t ARP[ARPS];
volatile glob_t GLOB = {120, 0, CLICK_REC, 0};
volatile uint8_t sq_fill, sq_playing, sq_rec, sq_pat;
volatile uint8_t sq_pos[TRACKS], sq_lit[TRACKS];
volatile uint32_t sq_edits;

static int32_t clampi(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }
static uint32_t rs = 0x2545F491u;
static uint32_t rnd(void) { return rs ^= rs << 13, rs ^= rs >> 17, rs ^= rs << 5; }

/* ---------------------------------------------------------------- the commands from loop() */

#define QN 256
static volatile uint32_t q[QN];
static volatile uint32_t q_wr, q_rd;
void sq_cmd(uint8_t op, uint8_t a, uint8_t b, uint8_t c) {
  if (q_wr - q_rd >= QN) return;
  q[q_wr % QN] = (uint32_t)op << 24 | (uint32_t)a << 16 | (uint32_t)b << 8 | c;
  RING_PUBLISH();
  q_wr = q_wr + 1;
}

/* ---------------------------------------------------------------- time */

/* samples x 256 in a step of each DIV_NAME */
static uint32_t div_q8(int d) {
  uint32_t beat = 2646000u * 256u / (uint32_t)clampi(GLOB.bpm, 40, 240);
  static const uint8_t MUL[DIVS] = {1, 1, 1, 1, 1, 1, 2, 4}, DV[DIVS] = {1, 2, 4, 8, 3, 6, 1, 1};
  d = d < 0 || d >= DIVS ? 2 : d;
  return beat / DV[d] * MUL[d];
}
/* a step's length: the swing makes the even steps longer and the odd ones shorter (at most 40 % of a step) */
static uint32_t swung(uint32_t base, uint32_t count, int swing) {
  uint32_t sw = base / 250u * (uint32_t)clampi(swing, 0, 100);
  return (count & 1u) ? base - sw : base + sw;
}

/* ---------------------------------------------------------------- notes out, and their ends */

static void on(int d, int note, int vel) {
  if (d < DST_EXT) z_note(d == DST_KBD ? 2 : d - 1, note, vel);
  else midi_send((uint8_t)(0x90 | ((d - DST_EXT) & 15)), (uint8_t)note, (uint8_t)vel);
}
static void off(int d, int note) {
  if (d < DST_EXT) z_note(d == DST_KBD ? 2 : d - 1, note, 0);
  else midi_send((uint8_t)(0x80 | ((d - DST_EXT) & 15)), (uint8_t)note, 0);
}

#define OFFS 48
#define SRC_ARP 7
static struct {
  int32_t rem; /* samples left */
  uint8_t part, note, src, used;
} OFF[OFFS];

/* a note that ends by itself after len samples (src: the track, or SRC_ARP) */
static void play(int part, int note, int vel, int32_t len, int src) {
  int f = -1, m = 0;
  for (int i = 0; i < OFFS; i++) {
    if (OFF[i].used && OFF[i].part == part && OFF[i].note == note) { /* again: it starts over */
      off(part, note);
      f = i;
      break;
    }
    if (!OFF[i].used && f < 0) f = i;
    if (OFF[i].used && OFF[i].rem < OFF[m].rem) m = i;
  }
  if (f < 0) off(OFF[m].part, OFF[m].note), f = m;
  OFF[f].rem = len < 1 ? 1 : len, OFF[f].part = (uint8_t)part, OFF[f].note = (uint8_t)note, OFF[f].src = (uint8_t)src, OFF[f].used = 1;
  on(part, note, vel);
  if (src < TRACKS) sq_lit[src] = 0;
}
static void offs_tick(uint32_t frames) {
  for (int i = 0; i < OFFS; i++)
    if (OFF[i].used && (OFF[i].rem -= (int32_t)frames) <= 0) off(OFF[i].part, OFF[i].note), OFF[i].used = 0;
}
static void offs_end(int src) { /* src -1: every track's (not the arpeggiator's) */
  for (int i = 0; i < OFFS; i++)
    if (OFF[i].used && (src < 0 ? OFF[i].src != SRC_ARP : OFF[i].src == src)) off(OFF[i].part, OFF[i].note), OFF[i].used = 0;
}

/* ---------------------------------------------------------------- the tracks */

static struct {
  uint32_t pos, cur_len, count; /* q8 samples into the step; its length; steps since the start */
  uint8_t cur, nxt, up, pre;    /* the step playing, the one after it, PING's way, nxt already played early */
} T[TRACKS];
static uint32_t mpos, mstep; /* the bar's clock: 1/16 steps since PLAY */

/* the hits to come: a step's notes, after a delay (nudge, ratchets, flam) */
#define HITS 32
static struct {
  int32_t delay, len;
  uint8_t note[4];
  uint8_t n, vel, track, used;
} H[HITS];

static void hit(int tr, const step_t *s, int32_t delay, int vel, int32_t len, uint8_t skip) {
  for (int i = 0; i < HITS; i++)
    if (!H[i].used) {
      H[i].delay = delay, H[i].len = len, H[i].vel = (uint8_t)vel, H[i].track = (uint8_t)tr, H[i].n = 0;
      for (int k = 0; k < s->n && k < 4; k++)
        if (!(skip >> k & 1u)) H[i].note[H[i].n++] = s->note[k];
      H[i].used = H[i].n > 0;
      return;
    }
}

static uint32_t tl(int tr) { return TLEN[sq_pat][tr] ? (TLEN[sq_pat][tr] > STEPS ? STEPS : TLEN[sq_pat][tr]) : 1u; }
static uint32_t next_step(int tr, uint32_t s) {
  uint32_t len = tl(tr);
  if (s >= len) s = len - 1;
  switch (TRK[tr].dir) {
    case DIR_RVRS: return s ? s - 1 : len - 1;
    case DIR_PING:
      if (len < 2) return 0;
      if (T[tr].up && s + 1 >= len) T[tr].up = 0;
      else if (!T[tr].up && s == 0) T[tr].up = 1;
      return T[tr].up ? s + 1 : s - 1;
    case DIR_RAND: return rnd() % len;
    default: return (s + 1) % len;
  }
}

static int cond_ok(int c, uint32_t pass) {
  if (c == TC_FILL) return sq_fill;
  if (c == TC_NFILL) return !sq_fill;
  if (c >= TC_1_2 && c <= TC_4_4) {
    static const uint8_t A[] = {1, 2, 1, 2, 3, 1, 2, 3, 4}, B[] = {2, 2, 3, 3, 3, 4, 4, 4, 4};
    return pass % B[c - TC_1_2] == A[c - TC_1_2] - 1u;
  }
  return 1;
}

/* the notes recorded live into a step not reached yet: not played again when it is (it sounded from the key) */
static uint8_t skip_step[TRACKS] = {255, 255, 255, 255, 255, 255}, skip_mask[TRACKS];

/* step s of track tr starts in delay q8 samples (count: its place since the start; step_len its length) */
static void trigger(int tr, uint32_t s, uint32_t count, uint32_t step_len, int32_t delay8) {
  const step_t *st = &STEP[sq_pat][tr][s];
  uint8_t skip = 0;
  if (skip_step[tr] == s) skip = skip_mask[tr], skip_step[tr] = 255;
  if (!st->n || TRK[tr].mute) return;
  uint32_t len = tl(tr);
  if (!cond_ok(st->cond, count / (len ? len : 1))) return;
  if (st->chance < 100 && rnd() % 100u >= st->chance) return;
  int32_t sl = (int32_t)(step_len >> 8); /* samples */
  int32_t nl = st->len <= 15 ? sl * st->len / 16 : st->len == 16 ? sl : sl * (st->len - 15);
  int vel = (st->flags & SF_ACCENT) ? 127 : st->vel;
  int32_t d = (delay8 + st->nudge * (int32_t)(step_len >> 6)) >> 8;
  if (d < 0) d = 0;
  if (st->flags & SF_FLAM) {
    int32_t g = sl / 2 < 661 ? sl / 2 : 661;
    hit(tr, st, d, vel * 5 / 8, g, skip);
    hit(tr, st, d + g, vel, nl, 0);
    return;
  }
  int r = (st->flags & SF_RATCH) + 1;
  if (r > 1 && nl > sl / r) nl = sl / r;
  for (int k = 0; k < r; k++) hit(tr, st, d + sl * k / r, vel, nl, k ? 0 : skip);
}

/* track tr goes into step s now */
static void enter(int tr, uint32_t s) {
  uint32_t len = tl(tr);
  if (s >= len) s = 0;
  T[tr].cur = (uint8_t)s;
  sq_pos[tr] = (uint8_t)s;
  uint32_t base = div_q8(TRK[tr].div);
  T[tr].cur_len = swung(base, T[tr].count, GLOB.swing);
  if (!T[tr].pre) trigger(tr, s, T[tr].count, T[tr].cur_len, 0);
  T[tr].pre = 0;
  T[tr].nxt = (uint8_t)next_step(tr, s);
  /* the next one early (a negative nudge) */
  const step_t *nx = &STEP[sq_pat][tr][T[tr].nxt];
  if (nx->n && nx->nudge < 0) {
    trigger(tr, T[tr].nxt, T[tr].count + 1, swung(base, T[tr].count + 1, GLOB.swing), (int32_t)T[tr].cur_len);
    T[tr].pre = 1;
  }
}

static void start_tracks(void) {
  for (int tr = 0; tr < TRACKS; tr++) {
    uint32_t len = tl(tr);
    T[tr].pos = 0, T[tr].count = 0, T[tr].up = 1, T[tr].pre = 0;
    uint32_t s = TRK[tr].dir == DIR_RVRS ? len - 1 : TRK[tr].dir == DIR_RAND ? rnd() % len : 0;
    enter(tr, s);
  }
  mpos = 0;
}

/* ---------------------------------------------------------------- live recording */

static uint8_t rec_track;
static uint32_t now_s; /* samples since power-on */
#define RHELD 8
static struct {
  uint8_t note, step, used;
  uint32_t t0;
} RH[RHELD];

static void edited(void) { sq_edits = sq_edits + 1; }

static step_t *add_note(step_t *s, int note, int vel) { /* overdub: a chord of up to 4; full: the last one replaced */
  if (!s->n) *s = (step_t){{0}, 0, (uint8_t)vel, 14, 100, 0, 0, 0, 0};
  for (int k = 0; k < s->n; k++)
    if (s->note[k] == note) return s;
  if (s->n < 4) s->note[s->n++] = (uint8_t)note;
  else s->note[3] = (uint8_t)note;
  s->vel = (uint8_t)vel;
  return s;
}

static void rec_on(int part, int note, int vel) {
  int tr = rec_track;
  if (!sq_playing || !sq_rec || TRK[tr].part != part) return;
  /* the nearest step: the one playing, or the next past its middle */
  uint32_t s = T[tr].cur;
  int ahead = T[tr].pos > T[tr].cur_len / 2;
  if (ahead) s = T[tr].nxt;
  step_t *st = add_note(&STEP[sq_pat][tr][s], note, vel);
  edited();
  if (ahead) { /* it sounds now: not again when its step comes */
    if (skip_step[tr] != s) skip_step[tr] = (uint8_t)s, skip_mask[tr] = 0;
    for (int k = 0; k < st->n; k++)
      if (st->note[k] == note) skip_mask[tr] |= (uint8_t)(1u << k);
  }
  for (int i = 0; i < RHELD; i++)
    if (!RH[i].used) {
      RH[i].used = 1, RH[i].note = (uint8_t)note, RH[i].step = (uint8_t)s, RH[i].t0 = now_s;
      break;
    }
}
static void rec_off(int part, int note) {
  int tr = rec_track;
  for (int i = 0; i < RHELD; i++)
    if (RH[i].used && RH[i].note == note) {
      RH[i].used = 0;
      if (!sq_playing || !sq_rec || TRK[tr].part != part) return;
      uint32_t L = div_q8(TRK[tr].div) >> 8, held = now_s - RH[i].t0, code;
      if (held * 16u < L * 15u) code = (uint32_t)clampi((int32_t)((held * 16u + L / 2u) / L), 1, 15);
      else if (held < L + L / 2u) code = 16;
      else code = (uint32_t)clampi((int32_t)((held + L / 2u) / L) + 15, 17, LEN_MAX);
      STEP[sq_pat][tr][RH[i].step].len = (uint8_t)code;
      edited();
      return;
    }
}

/* ---------------------------------------------------------------- the arpeggiator */

static struct {
  uint8_t held[16], nheld, phys, last;
  uint8_t note[4], nn; /* sounding */
  uint8_t walk;
  uint32_t idx;
  int32_t pos, off;
  uint8_t fire;
} A[ARPS];
static uint8_t direct[DESTS][16]; /* notes played straight (no arpeggio): bit per note */

static void arp_silence(int p) {
  for (int k = 0; k < A[p].nn; k++) off(p, A[p].note[k]), rec_off(p, A[p].note[k]);
  A[p].nn = 0;
}

static void arp_add(int p, int note) {
  if (ARP[p].hold && A[p].phys == 0) A[p].nheld = 0; /* a new chord */
  A[p].phys++;
  A[p].last = (uint8_t)note;
  for (int i = 0; i < A[p].nheld; i++)
    if (A[p].held[i] == note) return;
  if (A[p].nheld < 16) A[p].held[A[p].nheld++] = (uint8_t)note;
  if (A[p].nheld == 1) A[p].fire = 1, A[p].idx = 0xFFFFFFFFu;
}
static void arp_remove(int p, int note) {
  if (A[p].phys) A[p].phys--;
  if (ARP[p].hold) return;
  for (int i = 0; i < A[p].nheld; i++)
    if (A[p].held[i] == note) {
      for (int j = i; j + 1 < A[p].nheld; j++) A[p].held[j] = A[p].held[j + 1];
      A[p].nheld--;
      break;
    }
}

/* the notes over OCT octaves, low to high (or as played) */
static uint32_t arp_list(int p, uint8_t *list, uint32_t *cnt) {
  uint32_t n = A[p].nheld, len = 0;
  uint8_t b[16];
  for (uint32_t i = 0; i < n; i++) b[i] = A[p].held[i];
  if (!ARP[p].order && ARP[p].mode != AM_ORD)
    for (uint32_t i = 1; i < n; i++)
      for (uint32_t j = i; j > 0 && b[j - 1] > b[j]; j--) {
        uint8_t x = b[j];
        b[j] = b[j - 1], b[j - 1] = x;
      }
  *cnt = n;
  int oct = clampi(ARP[p].oct, 1, 4);
  for (int o = 0; o < oct; o++)
    for (uint32_t i = 0; i < n && len < 64; i++) list[len++] = (uint8_t)clampi(b[i] + 12 * o, 0, 127);
  if (n && ARP[p].mode == AM_REPEAT) list[0] = A[p].last, len = 1;
  return len;
}

/* which of the list step idx plays (Skiff's arp_pick) */
static uint32_t arp_pick(int p, uint8_t *list, uint32_t len) {
  uint32_t i = A[p].idx, m = ARP[p].mode, cyc, k, w;
  if (m == AM_UP8 && len < 64u) list[len] = (uint8_t)clampi(list[0] + 12 * clampi(ARP[p].oct, 1, 4), 0, 127), len++;
  cyc = len > 1u ? 2u * len - 2u : 1u;
  k = i % cyc;
  switch (m) {
    case AM_DN: return len - 1u - i % len;
    case AM_UPDN: return k < len ? k : cyc - k;
    case AM_DNUP: return k < len ? len - 1u - k : k - (len - 1u);
    case AM_RND: return rnd() % len;
    case AM_CONV:
    case AM_DIVG:
      k = i % len;
      if (m == AM_DIVG) k = len - 1u - k;
      return (k & 1u) ? len - 1u - k / 2u : k / 2u;
    case AM_PINKY: return len < 2u ? 0u : (k & 1u) ? len - 1u : k / 2u;
    case AM_THUMB: return len < 2u ? 0u : (k & 1u) ? 1u + k / 2u : 0u;
    case AM_WALK:
      w = A[p].walk;
      if (!i) w = 0;
      else if (w >= len) w = len - 1u;
      else if (len > 1u) w = !w ? 1u : w == len - 1u ? w - 1u : (rnd() >> 7) & 1u ? w + 1u : w - 1u;
      A[p].walk = (uint8_t)w;
      return w;
    default: return i % len;
  }
}

static void arp_fire(int p, int32_t period) {
  uint8_t list[65];
  uint32_t cnt, len = arp_list(p, list, &cnt);
  arp_silence(p);
  if (!len) return;
  A[p].idx++;
  if (rnd() % 100u >= (uint32_t)clampi(ARP[p].chance, 1, 100)) return;
  A[p].off = period * clampi(ARP[p].gate, 1, 127) / 128;
  if (A[p].off < 64) A[p].off = 64;
  if (ARP[p].mode == AM_CHORD) { /* the first 4 together, an octave up each step through OCT */
    int o = (int)(A[p].idx % (uint32_t)clampi(ARP[p].oct, 1, 4));
    for (uint32_t i = 0; i < cnt && i < 4; i++) A[p].note[A[p].nn++] = (uint8_t)clampi(list[i] + 12 * o, 0, 127);
  } else {
    A[p].note[A[p].nn++] = list[arp_pick(p, list, len)];
  }
  for (int k = 0; k < A[p].nn; k++) on(p, A[p].note[k], 100), rec_on(p, A[p].note[k], 100);
}

static void arp_tick(uint32_t frames) {
  for (int p = 0; p < ARPS; p++) {
    if (A[p].nn) {
      A[p].off -= (int32_t)frames;
      if (A[p].off <= 0) arp_silence(p);
    }
    if (!ARP[p].hold && !A[p].phys) A[p].nheld = 0;
    if (!ARP[p].mode || !A[p].nheld) {
      if (A[p].nn && !A[p].nheld) arp_silence(p);
      A[p].pos = 0;
      continue;
    }
    int32_t period = (int32_t)(div_q8(ARP[p].rate) >> 8);
    int32_t sw = period / 250 * clampi(ARP[p].swing, 0, 100);
    A[p].pos += (int32_t)frames;
    if (A[p].fire || A[p].pos >= period + ((A[p].idx & 1u) ? sw : -sw)) A[p].fire = 0, A[p].pos = 0, arp_fire(p, period);
  }
}

/* ---------------------------------------------------------------- the commands */

static uint8_t tgt_track, tgt_step;

static void key_on(int p, int note, int vel) {
  if (p < 0 || p >= DESTS) return;
  if (p < ARPS && ARP[p].mode) {
    arp_add(p, note);
    return;
  }
  direct[p][note >> 3] |= (uint8_t)(1u << (note & 7));
  on(p, note, vel);
  rec_on(p, note, vel);
}
static void key_off(int p, int note) {
  if (p < 0 || p >= DESTS) return;
  if (direct[p][note >> 3] >> (note & 7) & 1u) {
    direct[p][note >> 3] &= (uint8_t)~(1u << (note & 7));
    off(p, note);
    rec_off(p, note);
  }
  if (p < ARPS) arp_remove(p, note);
}

static void set(int field, int b, int c) {
  step_t *s = &STEP[sq_pat][tgt_track][tgt_step];
  int def = 60;
  switch (field) {
    case SET_PITCH:
    case SET_OCT: {
      int d = (b - 64) * (field == SET_OCT ? 12 : 1);
      if (!s->n) {
        add_note(s, c ? c : def, 100);
        if (field == SET_PITCH) break;
      }
      int lo = 127, hi = 0;
      for (int k = 0; k < s->n; k++) lo = s->note[k] < lo ? s->note[k] : lo, hi = s->note[k] > hi ? s->note[k] : hi;
      if (lo + d < 0 || hi + d > 127) break;
      for (int k = 0; k < s->n; k++) s->note[k] = (uint8_t)(s->note[k] + d);
      break;
    }
    case SET_LEN: s->len = (uint8_t)clampi(b, 1, LEN_MAX); break;
    case SET_VEL: s->vel = (uint8_t)clampi(b, 1, 127); break;
    case SET_CHANCE: s->chance = (uint8_t)clampi(b, 1, 100), s->cond = (uint8_t)(c < TC_COUNT ? c : 0); break;
    case SET_RATCH:
      s->flags = (uint8_t)((s->flags & ~(SF_RATCH | SF_FLAM)) | (b >= 5 ? SF_FLAM : (uint8_t)clampi(b - 1, 0, 3)));
      break;
    case SET_NUDGE: s->nudge = (int8_t)clampi(b - 32, -32, 31); break;
    case SET_ACCENT: s->flags = (uint8_t)(b ? s->flags | SF_ACCENT : s->flags & ~SF_ACCENT); break;
  }
  edited();
}

static void stop_all(void) {
  sq_playing = 0;
  offs_end(-1);
  for (int i = 0; i < HITS; i++) H[i].used = 0;
  for (int i = 0; i < RHELD; i++) RH[i].used = 0;
  for (int tr = 0; tr < TRACKS; tr++) skip_step[tr] = 255;
}

static void command(uint32_t m) {
  int op = (int)(m >> 24), a = (int)(m >> 16 & 255), b = (int)(m >> 8 & 255), c = (int)(m & 255);
  switch (op) {
    case OP_KEY_ON: key_on(a, b & 127, c ? c : 100); break;
    case OP_KEY_OFF: key_off(a, b & 127); break;
    case OP_PLAY:
      if (a && !sq_playing) {
        sq_pat = (uint8_t)(GLOB.pattern & 3);
        sq_playing = 1, mstep = 0;
        start_tracks();
      } else if (!a && sq_playing) stop_all();
      break;
    case OP_REC:
      sq_rec = (uint8_t)(a != 0), rec_track = (uint8_t)(b % TRACKS);
      for (int i = 0; i < RHELD; i++) RH[i].used = 0;
      break;
    case OP_TARGET: tgt_track = (uint8_t)(a % TRACKS), tgt_step = (uint8_t)(b % STEPS); break;
    case OP_SREC: {
      step_t *s = &STEP[sq_pat][tgt_track][tgt_step];
      if (b) s->n = 0;
      add_note(s, a & 127, 100);
      edited();
      break;
    }
    case OP_SET: set(a, b, c); break;
    case OP_CLEAR:
      if (a) {
        for (int s = 0; s < STEPS; s++) STEP[sq_pat][tgt_track][s].n = 0;
        offs_end(tgt_track);
      } else STEP[sq_pat][tgt_track][tgt_step].n = 0;
      edited();
      break;
    case OP_ARP_STOP:
      if (a < ARPS) A[a].nheld = 0, A[a].phys = 0, arp_silence(a);
      break;
  }
}

/* ---------------------------------------------------------------- audio() */

void sq_tick(uint32_t frames) {
  while (q_rd != q_wr) {
    uint32_t m = q[q_rd % QN];
    q_rd = q_rd + 1;
    command(m);
  }
  now_s += frames;
  offs_tick(frames);
  for (int tr = 0; tr < TRACKS; tr++)
    if (sq_lit[tr] < 255) sq_lit[tr]++;

  /* a pattern asked for while stopped: at once */
  if (!sq_playing) sq_pat = (uint8_t)(GLOB.pattern & 3);
  for (int tr = 0; tr < TRACKS; tr++) /* a muted track goes quiet at once */
    if (TRK[tr].mute) offs_end(tr);

  if (sq_playing) {
    /* the bar's clock: the metronome, a new pattern on the bar */
    uint32_t m16 = div_q8(2);
    mpos += frames << 8;
    while (mpos >= m16) {
      mpos -= m16;
      mstep++;
      if (!(mstep & 15u) && (GLOB.pattern & 3) != sq_pat) {
        offs_end(-1);
        sq_pat = (uint8_t)(GLOB.pattern & 3);
        start_tracks();
        mpos = 0;
      }
    }
    int click_now = GLOB.click == CLICK_ON || (GLOB.click == CLICK_REC && sq_rec);
    static uint32_t clicked = 0xFFFFFFFFu;
    if (click_now && !(mstep & 3u) && clicked != mstep) clicked = mstep, z_click(!(mstep & 15u));
    if (!click_now || (mstep & 3u)) clicked = 0xFFFFFFFFu;
    /* the tracks */
    for (int tr = 0; tr < TRACKS; tr++) {
      T[tr].pos += frames << 8;
      while (T[tr].pos >= T[tr].cur_len) {
        T[tr].pos -= T[tr].cur_len;
        T[tr].count++;
        enter(tr, T[tr].nxt);
      }
    }
  }

  /* the hits due */
  for (int i = 0; i < HITS; i++) {
    if (!H[i].used) continue;
    if (H[i].delay > 0) {
      H[i].delay -= (int32_t)frames;
      if (H[i].delay > 0) continue;
    }
    int part = TRK[H[i].track].part;
    if (sq_playing && !TRK[H[i].track].mute)
      for (int k = 0; k < H[i].n; k++) play(part, H[i].note[k], H[i].vel, H[i].len, H[i].track);
    H[i].used = 0;
  }
  arp_tick(frames);
}

/* ---------------------------------------------------------------- as shipped */

static void put_step(int p, int tr, int s, const uint8_t *notes, int n, int vel, int len) {
  step_t *st = &STEP[p][tr][s];
  *st = (step_t){{0}, 0, (uint8_t)vel, (uint8_t)len, 100, 0, 0, 0, 0};
  for (int k = 0; k < n; k++) st->note[k] = notes[k];
  st->n = (uint8_t)n;
}

void sq_init(void) {
  static const uint8_t DST[TRACKS] = {DST_UPPER, DST_LOWER, DST_KBD, DST_KBD, DST_EXT + 0, DST_EXT + 9};
  for (int tr = 0; tr < TRACKS; tr++) TRK[tr] = (track_t){DST[tr], 2, DIR_FWD, 0};
  for (int p = 0; p < ARPS; p++) ARP[p] = (arp_t){AM_OFF, 2, 1, 96, 0, 100, 0, 0};
  for (int p = 0; p < PATTERNS; p++)
    for (int tr = 0; tr < TRACKS; tr++) {
      TLEN[p][tr] = 16;
      for (int s = 0; s < STEPS; s++) STEP[p][tr][s].n = 0;
    }
  /* pattern A, for a split patch (73 BASS|BRS): four bars in D minor, brass stabs on the upper section (track 1),
   * a sequenced bass on the lower (2) */
  static const uint8_t CH[4][4] = {{62, 65, 69, 74}, {58, 62, 65, 70}, {60, 64, 67, 72}, {57, 61, 64, 69}}; /* Dm Bb C A */
  static const uint8_t ROOT[4] = {38, 34, 36, 33};
  TLEN[0][0] = 64, TLEN[0][1] = 64;
  for (int b = 0; b < 4; b++) {
    put_step(0, 0, b * 16, CH[b], 4, 104, 16 + 2);   /* 3 steps */
    put_step(0, 0, b * 16 + 6, CH[b], 4, 84, 10);
    put_step(0, 0, b * 16 + 10, CH[b], 4, 96, 16 + 3);
    for (int i = 0; i < 16; i++) {
      static const int8_t BS[16] = {0, 0, 12, 0, 0, 10, 0, 12, 0, 0, 12, 0, 7, 0, 12, 10};
      static const uint8_t ON[16] = {1, 0, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 0, 1, 1};
      if (!ON[i]) continue;
      uint8_t n = (uint8_t)(ROOT[b] + BS[i]);
      put_step(0, 1, b * 16 + i, &n, 1, (i & 3) ? 84 : 112, 10);
    }
  }
  STEP[0][1][15].chance = 60, STEP[0][1][31].chance = 60; /* a ghost note, now and then */
  STEP[0][1][63].flags = 1, STEP[0][1][63].cond = TC_2_2; /* a roll into every second turn */
}
