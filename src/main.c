/* SPDX-License-Identifier: GPL-3.0-only */
/* ZEUS-80: an eight-voice analogue-style polysynth module for the M-VAVE FM-1, after the 1984 Roland MKS-80 (README.md
 * has the map), with an arpeggiator and a six-track step sequencer as LAX-32's (sq.h, seq.c). The screen is the
 * module's front: its 16-character backlit dot-matrix LCD, the BANK and NUMBER buttons, the UPPER, LOWER, TONE and
 * PATCH lights, the eight modules; below, the four knobs' parameters or, on the sequencer's pages, the six tracks.
 *
 *   SELECT   the patch 11..88            PRESETS  the bank (editing: the parameter group; sequencer: the step)
 *   ALGORITHM  the tone of the section   SEL  UPPER / LOWER          keys  play (as the key mode says)
 *   EDIT     edit the tone (again: back) HOME  edit the patch (again: back)
 *   ENV      ENV-1 / ENV-2               LFO  LFO-1                   FX  VCF / VCF MOD
 *   K1-K4    the group's four parameters (playing: VCF FREQ, RES, ENV, E2 RELEASE)
 *   SAVE     write the patch and its tones (and keep everything)
 *   ARP  the arpeggiator   SEQ  STEP, FEEL, TRACK   GLO  tempo, swing, pattern, click   PLAY  REC  as LAX-32
 *   MIDI in: channel 1 the keyboard (key mode), 2 the upper section, 3 the lower */
#include "fm1.h"
#include "zs.h"
#include "sq.h"
#include "font5x8.h"
#include "tones.h"

static int32_t clampi(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }
static char *put(char *d, const char *s) {
  while (*s) *d++ = *s++;
  *d = 0;
  return d;
}
static char *put_int(char *d, int32_t v) {
  char b[12];
  format_int(b, v);
  return put(d, b);
}
static char *put_2(char *d, int v) {
  *d++ = (char)('0' + v / 10 % 10), *d++ = (char)('0' + v % 10), *d = 0;
  return d;
}
static char *put_pad(char *d, const char *s, int w) {
  int i = 0;
  for (; s[i] && i < w; i++) *d++ = s[i];
  for (; i < w; i++) *d++ = ' ';
  *d = 0;
  return d;
}
static char *put_note(char *d, int n) {
  static const char *const NN[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
  return put_int(put(d, NN[n % 12]), n / 12 - 1);
}
static int str_eq(const char *a, const char *b) {
  while (*a && *a == *b) a++, b++;
  return *a == *b;
}
static char *put_num(char *d, int n) { return put_2(d, (n / 8 + 1) * 10 + n % 8 + 1); } /* 0..63 -> 11..88 */

/* ---------------------------------------------------------------- colours: a black rack, a backlit LCD, red LEDs */

#define C_CASE RGB(20, 20, 22)
#define C_PANEL RGB(40, 40, 44)
#define C_TEXT RGB(225, 225, 230)
#define C_DIM RGB(120, 120, 128)
#define C_FAINT RGB(58, 58, 64)
#define C_LCD_BG RGB(140, 180, 70)
#define C_LCD_OFF RGB(128, 166, 64)
#define C_LCD_ON RGB(24, 40, 14)
#define C_LED RGB(255, 36, 28)
#define C_LED_OFF RGB(70, 18, 16)
#define C_SEL RGB(240, 150, 40)
#define C_UP RGB(255, 170, 50)
#define C_LO RGB(110, 220, 120)
#define C_STEP RGB(140, 180, 70)
#define C_STEP_ACC RGB(214, 240, 90)
#define C_STEP_MAYBE RGB(84, 110, 30)
#define C_STEP_MUTE RGB(70, 80, 50)

/* ---------------------------------------------------------------- the LCD: 16 characters of 5 x 7 dots */

#define LCD_N 16
#define LCD_X 11
#define LCD_Y 29
#define DOT_W 2
#define DOT_H 3
#define CH_W 14
static char lcd_shown[LCD_N + 1];

static void lcd_char(int i, char c) {
  int x0 = LCD_X + i * CH_W;
  unsigned u = (unsigned char)c;
  const uint8_t *g = FONT58[(u < 32 || u > 127 ? '?' : u) - 32];
  for (int r = 0; r < 8; r++)
    for (int k = 0; k < 5; k++) rect(x0 + k * DOT_W, LCD_Y + r * DOT_H, DOT_W, DOT_H - 1, (g[r] >> (4 - k) & 1u) ? C_LCD_ON : C_LCD_OFF);
}
static void lcd_frame(void) {
  rect(0, 18, SCREEN_W, 44, C_CASE);
  rect(4, 21, SCREEN_W - 8, 38, RGB(8, 8, 8));
  rect(6, 23, SCREEN_W - 12, 34, C_LCD_BG);
  for (int i = 0; i < LCD_N; i++) lcd_shown[i] = 0;
}
static void lcd_show(const char *s) {
  int end = 0;
  for (int i = 0; i < LCD_N; i++) {
    if (!s[i]) end = 1;
    char c = end ? ' ' : s[i];
    if (c != lcd_shown[i]) lcd_shown[i] = c, lcd_char(i, c);
  }
}

/* ---------------------------------------------------------------- memory: 64 tones, 64 patches */

static tone_t TONES[64];
static patch_t PATCHES[64];
static uint8_t cur_patch, sect;

static void tone_into(int s, int t) {
  for (int i = 0; i < NTP; i++) EDT[s].p[i] = TONES[t & 63].p[i];
}
static void load_patch(int n) {
  cur_patch = (uint8_t)(n & 63);
  const uint8_t *b = (const uint8_t *)&PATCHES[cur_patch];
  volatile uint8_t *e = (volatile uint8_t *)&EDP;
  z_panic();
  for (int i = 0; i < PATCH_BYTES; i++) e[i] = b[i];
  tone_into(0, EDP.s[0].tone), tone_into(1, EDP.s[1].tone);
}

/* the patch's parameters: four groups of four (the last three for the section edited) */
#define NPG 4
static const char *const PG_GROUP[NPG] = {"KEY MODE", "VOICE", "GLIDE", "BENDER"};
static const char *const PP_NAME[NPG][4] = {{"KEY MODE", "SPLIT PT", "BALANCE", "STEREO"},
                                            {"TONE", "ASSIGN", "DETUNE", "OCT SHFT"},
                                            {"GLIDE", "HOLD", "LFO2 RTE", "TOUCH"},
                                            {"BENDER", "VCO1 BND", "VCO2 BND", "TOUCH TO"}};
static const uint8_t PP_MAX[NPG][4] = {{2, 108, 100, 1}, {63, 4, 100, 4}, {100, 2, 100, 100}, {100, 2, 2, 1}};
static const char *const CH_KM[] = {"WHL", "DUL", "SPL"}, *const CH_ST[] = {"MIX", "SEP"}, *const CH_AS[] = {"SOL", "UN1", "UN2", "PL1", "PL2"},
                  *const CH_OCT[] = {" -2", " -1", "  0", " +1", " +2"}, *const CH_HOLD[] = {"OFF", " ON", "MID"},
                  *const CH_BEND[] = {"OFF", "NRM", "WID"}, *const CH_AT[] = {"VCO", "VCF"};
static int pp_offset(int g, int k) { return g == 0 ? k : 4 + sect * 12 + (g - 1) * 4 + k; }

/* a value as the LCD shows it: three characters */
static void fmt_val(char *d, int v, int max, const char *const *choice, int kind) {
  if (choice) put_pad(d, choice[clampi(v, 0, max)], 3);
  else if (kind == 1) put(put_num(put(d, " "), v), "");
  else if (kind == 2) {
    char b[8];
    put_note(b, v);
    if (b[2] == 0) d = put(d, " ");
    put(d, b);
  } else {
    char b[8];
    put_int(b, v);
    int l = 0;
    while (b[l]) l++;
    for (; l < 3; l++) *d++ = ' ';
    put(d, b);
  }
}
static const char *const *pp_choice(int g, int k) {
  static const char *const *const C[NPG][4] = {{CH_KM, 0, 0, CH_ST}, {0, CH_AS, 0, CH_OCT}, {0, CH_HOLD, 0, 0}, {0, CH_BEND, CH_BEND, CH_AT}};
  return C[g][k];
}
static int pp_kind(int g, int k) { return g == 1 && k == 0 ? 1 : g == 0 && k == 1 ? 2 : 0; }

/* ---------------------------------------------------------------- the panel's state */

enum { PG_PLAY, PG_TONE, PG_PATCH, PG_ARP, PG_ARP2, PG_STEP, PG_FEEL, PG_TRACK, PG_GLOBAL, PG_COUNT };
static const char *const PAGE_NAME[PG_COUNT] = {"PLAY", "TONE", "PATCH", "ARP", "ARP 2", "STEP", "FEEL", "TRACK", "GLOBAL"};
static const char *const PAGE_KNOBS[PG_COUNT] = {"", "", "", "ARP mode rate oct", "ARP2 swg chc hld", "STEP pch oct len",
                                                 "FEEL chc rat nud", "TRACK to len sz", "GLO bpm sw ptn ck"};
static const uint8_t PAGE_BTN[PG_COUNT] = {BTN_COUNT, BTN_EDIT, BTN_HOME, BTN_ARP, BTN_ARP, BTN_SEQ, BTN_SEQ, BTN_SEQ, BTN_GLO};
static uint8_t page, tgroup = 6, pgroup, trk, cur, rec;
static int8_t oct;
static uint8_t key_note[KEY_COUNT], key_dst[KEY_COUNT], keys_down, srec_gesture, default_note = 60;
static char msg[24];
static uint32_t msg_until, saved_t, midi_light_t, midi_seen_last, cur_moved_t, edit_t;
static uint8_t edit_cleared;

static int seq_view(void) { return page >= PG_STEP; }
static int tlen(int t) { return clampi(TLEN[sq_pat][t], 1, STEPS); }
static char dst_char(int d) { return d == DST_KBD ? 'K' : d == DST_UPPER ? 'U' : d == DST_LOWER ? 'L' : 'M'; }
static char *put_dst(char *d, int dst) {
  if (dst == DST_KBD) return put(d, "KEYBOARD");
  if (dst == DST_UPPER) return put(d, "UPPER");
  if (dst == DST_LOWER) return put(d, "LOWER");
  return put_int(put(d, "MIDI CH "), dst - DST_EXT + 1);
}
static int arp_of(void) { /* the arpeggiator the ARP pages set: the keys', or the track's */
  if (seq_view()) return TRK[trk].part < ARPS ? TRK[trk].part : -1;
  return DST_KBD;
}

static void lcd_msg(const char *s, uint32_t now) {
  put_pad(msg, s, LCD_N);
  msg_until = now + 1600;
}
static void lcd_value(const char *name, const char *was, const char *now_v, uint32_t t) { /* "VCF FREQ  66> 75" */
  char b[24], *d = put_pad(b, name, 9);
  d = put(d, was);
  *d++ = '>';
  put(d, now_v);
  lcd_msg(b, t);
}

static void show_tone(uint32_t now) {
  char b[24], *d = put(b, sect ? "LOWER TONE " : "UPPER TONE ");
  put_num(d, EDP.s[sect].tone);
  lcd_msg(b, now);
}

/* ---------------------------------------------------------------- the knobs */

static const uint8_t PLAY_KNOBS[4] = {T_FREQ, T_RES, T_VENV, T_R2T};

static void tone_knob(int i, int e, uint32_t now) {
  int max = TP_MAX[i], a = e < 0 ? -e : e;
  if (!max) return;
  int v = EDT[sect].p[i];
  v = clampi(v + (max > 10 && a > 1 ? e * 3 : e > 0 ? 1 : -1), 0, max);
  EDT[sect].p[i] = (uint8_t)v;
  char w[8], n[8];
  fmt_val(w, TONES[EDP.s[sect].tone].p[i], max, TP_CHOICE[i], 0);
  fmt_val(n, v, max, TP_CHOICE[i], 0);
  lcd_value(TP_NAME[i], w, n, now);
}
static void patch_knob(int g, int k, int e, uint32_t now) {
  int o = pp_offset(g, k), max = PP_MAX[g][k], a = e < 0 ? -e : e, lo = g == 0 && k == 1 ? 21 : 0;
  volatile uint8_t *b = (volatile uint8_t *)&EDP;
  int v = clampi(b[o] + (max > 10 && a > 1 ? e * 3 : e > 0 ? 1 : -1), lo, max);
  b[o] = (uint8_t)v;
  if (g == 1 && k == 0) tone_into(sect, v); /* a new tone for the section */
  if (g == 0 && k == 0) z_panic();          /* the modules go to the sections anew */
  char w[8], n[8];
  fmt_val(w, ((const uint8_t *)&PATCHES[cur_patch])[o], max, pp_choice(g, k), pp_kind(g, k));
  fmt_val(n, v, max, pp_choice(g, k), pp_kind(g, k));
  lcd_value(PP_NAME[g][k], w, n, now);
}

static int chance_index(const step_t *s) { return s->cond ? 99 + s->cond : 100 - clampi(s->chance, 1, 100); }
static void target(void) { sq_cmd(OP_TARGET, trk, cur, 0); }
static char *put_len(char *d, int len) {
  if (len <= 15) return put(put_int(d, len * 100 / 16), "%");
  if (len == 16) return put(d, "1 STEP");
  return put(put_int(d, len - 15), " STEPS");
}

static void seq_knob(int k, int e, uint32_t now) {
  int ai = arp_of();
  volatile arp_t *R = &ARP[ai < 0 ? 0 : ai];
  const step_t *st = &STEP[sq_pat][trk][cur];
  int d = e > 0 ? 1 : -1, a = e < 0 ? -e : e;
  char b[24], *s = b;
  if ((page == PG_ARP || page == PG_ARP2) && ai < 0) {
    lcd_msg("NO ARP FOR MIDI", now);
    return;
  }
  switch (page * 4 + k) {
    case PG_ARP * 4 + 0:
      R->mode = (uint8_t)clampi(R->mode + d, 0, AM_COUNT - 1);
      if (!R->mode) sq_cmd(OP_ARP_STOP, (uint8_t)ai, 0, 0);
      put(put(s, "ARP MODE "), AM_NAME[R->mode]);
      break;
    case PG_ARP * 4 + 1: R->rate = (uint8_t)clampi(R->rate + d, 0, DIVS - 1), put(put(s, "ARP RATE "), DIV_NAME[R->rate]); break;
    case PG_ARP * 4 + 2: R->oct = (uint8_t)clampi(R->oct + d, 1, 4), put_int(put(s, "ARP OCTAVES "), R->oct); break;
    case PG_ARP * 4 + 3: R->gate = (uint8_t)clampi(R->gate + d * (a > 1 ? 4 * a : 2), 4, 127), put(put_int(put(s, "ARP GATE "), R->gate * 100 / 128), "%"); break;
    case PG_ARP2 * 4 + 0: R->swing = (uint8_t)clampi(R->swing + d * (a > 1 ? 3 * a : 1), 0, 100), put_int(put(s, "ARP SWING "), R->swing); break;
    case PG_ARP2 * 4 + 1: R->chance = (uint8_t)clampi(R->chance + d * (a > 1 ? 3 * a : 1), 1, 100), put(put_int(put(s, "ARP CHANCE "), R->chance), "%"); break;
    case PG_ARP2 * 4 + 2: R->hold = e > 0, put(s, R->hold ? "ARP HOLD ON" : "ARP HOLD OFF"); break;
    case PG_ARP2 * 4 + 3: R->order = e > 0, put(s, R->order ? "ARP ORDER PLAYED" : "ARP ORDER LOW-HI"); break;
    case PG_STEP * 4 + 0:
    case PG_STEP * 4 + 1:
      target(), sq_cmd(OP_SET, k ? SET_OCT : SET_PITCH, (uint8_t)(64 + d), default_note);
      put(put_2(put(s, "STEP "), cur + 1), k ? " OCTAVE" : " PITCH");
      break;
    case PG_STEP * 4 + 2: {
      int v = clampi(st->len + d, 1, LEN_MAX);
      target(), sq_cmd(OP_SET, SET_LEN, (uint8_t)v, 0);
      put_len(put(s, "LENGTH "), v);
      break;
    }
    case PG_STEP * 4 + 3: {
      int v = clampi(st->vel + d * (a > 1 ? 4 * a : 1), 1, 127);
      target(), sq_cmd(OP_SET, SET_VEL, (uint8_t)v, 0);
      put_int(put(s, "VELOCITY "), v);
      break;
    }
    case PG_FEEL * 4 + 0: {
      int i = clampi(chance_index(st) + d * (a > 1 ? 3 * a : 1), 0, 99 + TC_COUNT - 1);
      int chance = i < 100 ? 100 - i : 100, cond = i < 100 ? 0 : i - 99;
      target(), sq_cmd(OP_SET, SET_CHANCE, (uint8_t)chance, (uint8_t)cond);
      if (cond) put(put(s, "CONDITION "), TC_NAME[cond]);
      else put(put_int(put(s, "CHANCE "), chance), "%");
      break;
    }
    case PG_FEEL * 4 + 1: {
      int v = clampi(((st->flags & SF_FLAM) ? 5 : (st->flags & SF_RATCH) + 1) + d, 1, 5);
      target(), sq_cmd(OP_SET, SET_RATCH, (uint8_t)v, 0);
      if (v == 5) put(s, "FLAM");
      else put_int(put(s, "RATCHET x"), v);
      break;
    }
    case PG_FEEL * 4 + 2: {
      int v = clampi(st->nudge + d, -32, 31);
      target(), sq_cmd(OP_SET, SET_NUDGE, (uint8_t)(v + 32), 0);
      put(put_int(put(s, v > 0 ? "NUDGE +" : "NUDGE "), v), "/64");
      break;
    }
    case PG_FEEL * 4 + 3: target(), sq_cmd(OP_SET, SET_ACCENT, e > 0, 0), put(s, e > 0 ? "ACCENT ON" : "ACCENT OFF"); break;
    case PG_TRACK * 4 + 0: {
      int v = clampi(TRK[trk].part + d, 0, DESTS - 1);
      TRK[trk].part = (uint8_t)v;
      if (rec) sq_cmd(OP_REC, 1, trk, 0);
      put_dst(put(put(put(s, "T"), (char[2]){(char)('1' + trk), 0}), " "), v);
      break;
    }
    case PG_TRACK * 4 + 1: {
      int v = clampi(tlen(trk) + d, 1, STEPS);
      TLEN[sq_pat][trk] = (uint8_t)v;
      if (cur >= v) cur = (uint8_t)(v - 1);
      put_int(put(s, "LENGTH "), v);
      break;
    }
    case PG_TRACK * 4 + 2: TRK[trk].div = (uint8_t)clampi(TRK[trk].div + d, 0, DIVS - 1), put(put(s, "STEP SIZE "), DIV_NAME[TRK[trk].div]); break;
    case PG_TRACK * 4 + 3: TRK[trk].dir = (uint8_t)clampi(TRK[trk].dir + d, 0, 3), put(put(s, "DIRECTION "), DIR_NAME[TRK[trk].dir]); break;
    case PG_GLOBAL * 4 + 0: GLOB.bpm = (uint8_t)clampi(GLOB.bpm + d * (a > 1 ? 4 * a : 1), 40, 240), put_int(put(s, "TEMPO "), GLOB.bpm); break;
    case PG_GLOBAL * 4 + 1: GLOB.swing = (uint8_t)clampi(GLOB.swing + d * (a > 1 ? 3 * a : 1), 0, 100), put_int(put(s, "SWING "), GLOB.swing); break;
    case PG_GLOBAL * 4 + 2:
      GLOB.pattern = (uint8_t)clampi(GLOB.pattern + d, 0, PATTERNS - 1);
      s = put(s, "PATTERN ");
      *s++ = (char)('A' + GLOB.pattern), *s = 0;
      if (sq_playing && GLOB.pattern != sq_pat) put(s, " NEXT");
      break;
    case PG_GLOBAL * 4 + 3: {
      static const char *const CN[3] = {"OFF", "REC", "ON"};
      GLOB.click = (uint8_t)clampi(GLOB.click + d, 0, 2), put(put(s, "CLICK "), CN[GLOB.click]);
      break;
    }
    default: return;
  }
  lcd_msg(b, now);
}

static void knob(int k, int e, uint32_t now) {
  if (page == PG_PLAY) tone_knob(PLAY_KNOBS[k], e, now);
  else if (page == PG_TONE) tone_knob(tgroup * 4 + k, e, now);
  else if (page == PG_PATCH) patch_knob(pgroup, k, e, now);
  else seq_knob(k, e, now);
}

/* ---------------------------------------------------------------- the front: buttons and lights */

#define BTN_Y0 64
#define LIST_Y 120
#define FOOT_Y 222

static void draw_title(void) {
  rect(0, 0, SCREEN_W, 18, C_CASE);
  text(6, 1, "ZEUS-80", C_TEXT, FONT_SMALL);
  text(SCREEN_W - 6 - text_width("8 VOICE ANALOG", FONT_SMALL), 1, "8 VOICE ANALOG", C_DIM, FONT_SMALL);
}
static void draw_pad(int x, int y, int lit, char c) {
  rect(x, y, 20, 16, lit ? C_LED : C_PANEL);
  char b[2] = {c, 0};
  text(x + 6, y, b, lit ? RGB(255, 235, 230) : C_DIM, FONT_SMALL);
}
static void draw_light(int x, int y, const char *label, uint16_t c) {
  rect(x, y + 4, 8, 8, c);
  text(x + 11, y, label, C_DIM, FONT_SMALL);
}
static uint32_t panel_shown = ~0u;
static void draw_panel(void) { /* BANK, NUMBER; UPPER LOWER TONE PATCH */
  uint32_t h = (uint32_t)cur_patch | (uint32_t)sect << 6 | (uint32_t)EDP.key_mode << 7 | (uint32_t)(page == PG_TONE) << 9 | (uint32_t)(page == PG_PATCH) << 10;
  if (h == panel_shown) return;
  panel_shown = h;
  rect(0, BTN_Y0, SCREEN_W, LIST_Y - BTN_Y0, C_CASE);
  text(4, BTN_Y0 + 2, "BANK", C_DIM, FONT_SMALL), text(4, BTN_Y0 + 20, "NUMBER", C_DIM, FONT_SMALL);
  for (int i = 0; i < 8; i++) {
    draw_pad(56 + i * 23, BTN_Y0 + 2, cur_patch / 8 == i, (char)('1' + i));
    draw_pad(56 + i * 23, BTN_Y0 + 20, cur_patch % 8 == i, (char)('1' + i));
  }
  int y = BTN_Y0 + 38, km = EDP.key_mode;
  uint16_t lu = sect == 0 ? C_LED : km != KM_WHOLE ? C_LED_OFF : C_FAINT, ll = sect == 1 ? C_LED : km != KM_WHOLE ? C_LED_OFF : C_FAINT;
  if (km == KM_WHOLE) lu = ll = sect == 0 ? C_LED : C_LED_OFF; /* WHOLE: both, as on the module */
  draw_light(4, y, "UPPER", lu), draw_light(64, y, "LOWER", ll);
  draw_light(124, y, "TONE", page == PG_TONE ? C_LED : C_LED_OFF), draw_light(176, y, "PATCH", page == PG_PATCH ? C_LED : C_LED_OFF);
}

/* the eight modules, and the four knobs' parameters */
static uint8_t mod_shown[VOICES];
static char row_shown[6][40];
static void draw_text_row(int r, const char *s, uint16_t c) {
  if (str_eq(s, row_shown[r])) return;
  put(row_shown[r], s);
  rect(0, LIST_Y + 16 + r * 16, SCREEN_W, 16, C_CASE);
  text(4, LIST_Y + 16 + r * 16, s, c, FONT_SMALL);
}
static void draw_param_row(int r, const char *name, int v, int stored, int max, const char *const *choice, int kind) {
  char b[40], *d = put_pad(b, name, 9), vs[8];
  fmt_val(vs, v, max, choice, kind);
  d = put(d, vs);
  *d++ = v != stored ? '*' : ' ', *d = 0;
  char key[48];
  put_int(put(put(key, b), "|"), max * 1000 + stored);
  if (str_eq(key, row_shown[r])) return;
  put(row_shown[r], key);
  int y = LIST_Y + 16 + r * 16;
  rect(0, y, SCREEN_W, 16, C_CASE);
  text(4, y, b, v != stored ? C_TEXT : C_DIM, FONT_SMALL);
  if (max > 0) {
    int x0 = 118, w = 116;
    rect(x0, y + 6, w, 4, C_FAINT);
    int lo = kind == 2 ? 21 : 0;
    rect(x0, y + 6, (v - lo) * w / (max - lo), 4, C_SEL);
    rect(x0 + (stored - lo) * w / (max - lo), y + 3, 2, 10, C_TEXT);
  }
}
static void knobs_screen(void) {
  char h[40], *d;
  for (int i = 0; i < VOICES; i++)
    if (mod_shown[i] != z_voice_sect[i] + 1) {
      mod_shown[i] = (uint8_t)(z_voice_sect[i] + 1);
      rect(56 + i * 23, LIST_Y + 4, 20, 8, z_voice_sect[i] == 1 ? C_UP : z_voice_sect[i] == 2 ? C_LO : C_FAINT);
    }
  int t = EDP.s[sect].tone;
  if (page == PG_PATCH) {
    put(put(h, PG_GROUP[pgroup]), pgroup ? (sect ? "  (LOWER)" : "  (UPPER)") : "");
    draw_text_row(0, h, C_SEL);
    for (int k = 0; k < 4; k++) {
      int o = pp_offset(pgroup, k);
      draw_param_row(k + 1, PP_NAME[pgroup][k], ((volatile uint8_t *)&EDP)[o], ((const uint8_t *)&PATCHES[cur_patch])[o], PP_MAX[pgroup][k], pp_choice(pgroup, k),
                     pp_kind(pgroup, k));
    }
    return;
  }
  if (page == PG_TONE) {
    d = put(put_num(put(h, sect ? "LOWER " : "UPPER "), t), " ");
    put(put(put(d, TG_NAME[tgroup]), " "), "");
    draw_text_row(0, h, C_SEL);
    for (int k = 0; k < 4; k++) {
      int i = tgroup * 4 + k;
      draw_param_row(k + 1, TP_NAME[i], EDT[sect].p[i], TONES[t].p[i], TP_MAX[i], TP_CHOICE[i], 0);
    }
    return;
  }
  /* PLAY, ARP: the patch and the section's tone, the quick knobs */
  d = put(put_num(put(h, sect ? "LOWER " : "UPPER "), t), " ");
  put(d, TONE_NAME[t]);
  draw_text_row(0, h, sect ? C_LO : C_UP);
  if (page == PG_ARP || page == PG_ARP2) {
    volatile arp_t *R = &ARP[DST_KBD];
    char b[40];
    put(put(put(put(b, "ARP "), AM_NAME[R->mode]), "  RATE "), DIV_NAME[R->rate]);
    draw_text_row(1, b, C_TEXT);
    put(put_int(put(put_int(put(b, "OCTAVES "), R->oct), "  GATE "), R->gate * 100 / 128), "%");
    draw_text_row(2, b, C_DIM);
    put(put(put_int(put(put_int(put(b, "SWING "), R->swing), "  CHANCE "), R->chance), "%"), "");
    draw_text_row(3, b, C_DIM);
    put(put(put(b, R->hold ? "HOLD ON" : "HOLD OFF"), "  ORDER "), R->order ? "PLAYED" : "LOW-HI");
    draw_text_row(4, b, C_DIM);
    return;
  }
  for (int k = 0; k < 4; k++) {
    int i = PLAY_KNOBS[k];
    draw_param_row(k + 1, TP_NAME[i], EDT[sect].p[i], TONES[t].p[i], TP_MAX[i], TP_CHOICE[i], 0);
  }
}

static void draw_foot(uint32_t now) {
  rect(0, FOOT_Y, SCREEN_W, SCREEN_H - FOOT_Y, C_CASE);
  char b[40], *d = put(put(b, PAGE_NAME[page]), " ");
  text(4, FOOT_Y + 2, b, C_SEL, FONT_SMALL);
  int x = 4 + text_width(b, FONT_SMALL);
  if (sq_playing) x = text(x, FOOT_Y + 2, "PLAY ", RGB(120, 230, 90), FONT_SMALL);
  if (rec) x = text(x, FOOT_Y + 2, "REC ", C_LED, FONT_SMALL);
  d = b;
  *d++ = (char)('A' + sq_pat);
  if (GLOB.pattern != sq_pat) *d++ = '>', *d++ = (char)('A' + GLOB.pattern);
  *d++ = ' ', *d = 0;
  put_int(d, GLOB.bpm);
  x = text(x, FOOT_Y + 2, b, C_DIM, FONT_SMALL);
  if (now - saved_t < 1500 && saved_t) text(x + 4, FOOT_Y + 2, "SAVED", C_TEXT, FONT_SMALL);
  text(SCREEN_W - 6 - text_width("MIDI", FONT_SMALL) - 16, FOOT_Y + 2, "MIDI", C_DIM, FONT_SMALL);
}
static void draw_midi_led(int on) { rect(SCREEN_W - 16, FOOT_Y + 6, 10, 8, on ? C_LED : C_LED_OFF); }

/* ---------------------------------------------------------------- the sequencer's front (as LAX-32's) */

#define GRID_X 24
#define CELL 13
#define ROW_H 16
#define GRID_Y LIST_Y
static uint8_t cell_shown[TRACKS][16], label_shown[TRACKS];
static char info_shown[3][32];

static int cell_x(int i) { return GRID_X + i * CELL + i / 4; }
static int view_page(uint32_t now) {
  if (sq_playing && now - cur_moved_t > 2500) return sq_pos[trk] / 16;
  return cur / 16;
}
static void draw_label(int t) {
  int y = GRID_Y + t * ROW_H, sel = t == trk;
  char b[2] = {(char)('1' + t), 0};
  rect(0, y, GRID_X - 1, ROW_H, C_CASE);
  text(2, y, b, sel ? C_SEL : TRK[t].mute ? C_FAINT : C_DIM, FONT_SMALL);
  b[0] = TRK[t].mute ? 'M' : dst_char(TRK[t].part);
  text(12, y, b, TRK[t].mute ? C_LED : C_FAINT, FONT_SMALL);
}
static void draw_cell(int t, int i, uint8_t code) {
  int x = cell_x(i), y = GRID_Y + t * ROW_H + 2;
  static const uint16_t COL[7] = {C_CASE, C_PANEL, C_STEP, C_STEP_ACC, C_STEP_MAYBE, C_STEP_MUTE, C_TEXT};
  uint16_t c = COL[code & 7];
  if (code & 16) {
    rect(x, y, CELL - 1, ROW_H - 4, C_SEL);
    rect(x + 2, y + 2, CELL - 5, ROW_H - 8, c);
  } else rect(x, y, CELL - 1, ROW_H - 4, c);
  if ((code & 8) && (code & 7) != 6) rect(x + 3, y + ROW_H - 7, CELL - 7, 2, C_TEXT);
}
static void draw_info(int line, const char *s) {
  if (str_eq(s, info_shown[line])) return;
  rect(0, BTN_Y0 + 2 + line * 17, SCREEN_W, 16, C_CASE);
  text(4, BTN_Y0 + 2 + line * 17, s, line ? C_DIM : C_TEXT, FONT_SMALL);
  put(info_shown[line], s);
}
static void seq_screen(uint32_t now, int all) {
  int pg = view_page(now);
  if (all) {
    for (int t = 0; t < TRACKS; t++) {
      label_shown[t] = 255;
      for (int i = 0; i < 16; i++) cell_shown[t][i] = 255;
    }
    for (int l = 0; l < 3; l++) info_shown[l][0] = 1, info_shown[l][1] = 0;
  }
  for (int t = 0; t < TRACKS; t++) {
    uint8_t lb = (uint8_t)((t == trk) | TRK[t].mute << 1 | TRK[t].part << 2);
    if (lb != label_shown[t]) label_shown[t] = lb, draw_label(t);
    int len = tlen(t);
    for (int i = 0; i < 16; i++) {
      int s = pg * 16 + i;
      const step_t *st = &STEP[sq_pat][t][s];
      uint8_t code;
      if (s >= len) code = 0;
      else if (!st->n) code = 1;
      else if (TRK[t].mute) code = 5;
      else if (st->flags & SF_ACCENT) code = 3;
      else if (st->chance < 100 || st->cond) code = 4;
      else code = 2;
      if (sq_playing && sq_pos[t] == s && s < len) code = (uint8_t)(code >= 2 ? code | 8 : 6);
      if (t == trk && s == cur) code |= 16;
      if (code != cell_shown[t][i]) cell_shown[t][i] = code, draw_cell(t, i, code);
    }
  }
  char l0[40], l1[40], l2[40], *d;
  const step_t *st = &STEP[sq_pat][trk][cur];
  int len = tlen(trk);
  if (page == PG_TRACK) {
    put_dst(put(put(put(l0, "TRACK "), (char[2]){(char)('1' + trk), 0}), " TO "), TRK[trk].part);
    put(put(put(put(put_int(put(l1, "LENGTH "), len), "  STEP "), DIV_NAME[TRK[trk].div]), "  "), DIR_NAME[TRK[trk].dir]);
    put(put_int(put(put_int(put(l2, "PAGE "), view_page(now) + 1), "/"), (len + 15) / 16), TRK[trk].mute ? "  MUTED (LFO)" : "");
  } else if (page == PG_GLOBAL) {
    put_int(put(put_int(put(l0, "TEMPO "), GLOB.bpm), "  SWING "), GLOB.swing);
    static const char *const CN[3] = {"OFF", "REC", "ON"};
    d = put(l1, "PATTERN ");
    *d++ = (char)('A' + sq_pat), *d = 0;
    if (GLOB.pattern != sq_pat) *d++ = '>', *d++ = (char)('A' + GLOB.pattern), *d = 0;
    put(put(d, "  CLICK "), CN[GLOB.click]);
    put(l2, sq_fill ? "FILL" : "hold SEL: FILL");
  } else {
    d = put(put_2(put(l0, "STEP "), cur + 1), "  ");
    if (!st->n) put(d, "---");
    else
      for (int k = 0; k < st->n; k++) d = put(put_note(d, st->note[k]), " ");
    if (!st->n) l1[0] = l2[0] = 0;
    else {
      put(put_int(put(put_len(put(l1, "LEN "), st->len), "  VEL "), st->vel), (st->flags & SF_ACCENT) ? "  ACC" : "");
      d = st->cond ? put(put(l2, "IF "), TC_NAME[st->cond]) : put(put_int(l2, st->chance), "%");
      d = (st->flags & SF_FLAM) ? put(d, "  FLAM") : put_int(put(d, "  x"), (st->flags & SF_RATCH) + 1);
      put_int(put(d, st->nudge > 0 ? "  NUDGE +" : "  NUDGE "), st->nudge);
    }
  }
  draw_info(0, l0), draw_info(1, l1), draw_info(2, l2);
}

/* ---------------------------------------------------------------- keeping it all */

#define STORE_MAGIC 0x5A455531u /* "ZEU1" */
typedef struct {
  uint32_t magic;
  uint8_t patch, spare[3];
  tone_t tones[64];
  uint8_t patches[64][PATCH_BYTES];
  uint8_t trk[TRACKS][4], arp[ARPS][8], glob[4], tlen[PATTERNS][TRACKS];
  uint16_t nsteps;
} store_t;
typedef struct {
  uint16_t at;
  uint8_t b[sizeof(step_t)];
} srec_t;
#define SREC_MAX ((SETTINGS_MAX - sizeof(store_t)) / sizeof(srec_t))
static union {
  store_t st;
  uint8_t raw[SETTINGS_MAX];
} sv POOL;

static void factory(void) {
  for (int t = 0; t < 64; t++)
    for (int i = 0; i < NTP; i++) TONES[t].p[i] = FACTORY_TONE[t][i];
  for (int p = 0; p < 64; p++) {
    uint8_t *b = (uint8_t *)&PATCHES[p];
    for (int i = 0; i < PATCH_BYTES; i++) b[i] = FACTORY_PATCH[p][i];
  }
}

static int load_all(void) { /* the patch to start on */
  if (settings_load(sv.raw, SETTINGS_MAX) || sv.st.magic != STORE_MAGIC) return 50; /* 73: BASS|BRS, the patch pattern A is made for */
  for (int t = 0; t < 64; t++)
    for (int i = 0; i < NTP; i++) TONES[t].p[i] = (uint8_t)clampi(sv.st.tones[t].p[i], 0, TP_MAX[i] ? TP_MAX[i] : 0);
  for (int p = 0; p < 64; p++) {
    uint8_t *b = (uint8_t *)&PATCHES[p];
    for (int i = 0; i < PATCH_BYTES; i++) b[i] = sv.st.patches[p][i];
    PATCHES[p].key_mode %= 3, PATCHES[p].split = (uint8_t)clampi(PATCHES[p].split, 21, 108), PATCHES[p].balance = (uint8_t)clampi(PATCHES[p].balance, 0, 100);
    PATCHES[p].stereo &= 1;
    for (int s = 0; s < 2; s++) {
      sect_t *x = &PATCHES[p].s[s];
      x->tone &= 63, x->assign %= 5, x->oct %= 5, x->hold %= 3, x->b1 %= 3, x->b2 %= 3, x->at_sel &= 1;
      x->detune = (uint8_t)clampi(x->detune, 0, 100), x->glide = (uint8_t)clampi(x->glide, 0, 100), x->lfo2 = (uint8_t)clampi(x->lfo2, 0, 100);
      x->at_sens = (uint8_t)clampi(x->at_sens, 0, 100), x->bend = (uint8_t)clampi(x->bend, 0, 100);
    }
  }
  for (int t = 0; t < TRACKS; t++)
    TRK[t] = (track_t){(uint8_t)(sv.st.trk[t][0] % DESTS), (uint8_t)(sv.st.trk[t][1] % DIVS), (uint8_t)(sv.st.trk[t][2] & 3), (uint8_t)(sv.st.trk[t][3] & 1)};
  for (int p = 0; p < ARPS; p++) {
    const uint8_t *a = sv.st.arp[p];
    ARP[p] = (arp_t){(uint8_t)(a[0] % AM_COUNT), (uint8_t)(a[1] % DIVS), (uint8_t)clampi(a[2], 1, 4), (uint8_t)clampi(a[3], 4, 127),
                     (uint8_t)clampi(a[4], 0, 100), (uint8_t)clampi(a[5], 1, 100), (uint8_t)(a[6] & 1), (uint8_t)(a[7] & 1)};
  }
  GLOB.bpm = (uint8_t)clampi(sv.st.glob[0], 40, 240), GLOB.swing = (uint8_t)clampi(sv.st.glob[1], 0, 100);
  GLOB.click = (uint8_t)clampi(sv.st.glob[2], 0, 2), GLOB.pattern = sv.st.glob[3] & 3;
  for (int p = 0; p < PATTERNS; p++)
    for (int t = 0; t < TRACKS; t++) {
      TLEN[p][t] = (uint8_t)clampi(sv.st.tlen[p][t], 1, STEPS);
      for (int s = 0; s < STEPS; s++) STEP[p][t][s].n = 0;
    }
  const srec_t *r = (const srec_t *)(sv.raw + sizeof(store_t));
  for (uint32_t i = 0; i < sv.st.nsteps && i < SREC_MAX; i++) {
    step_t *s = &STEP[r[i].at >> 9 & 3][(r[i].at >> 6 & 7) % TRACKS][r[i].at & 63];
    const uint8_t *b = r[i].b;
    *s = (step_t){{b[0] & 127, b[1] & 127, b[2] & 127, b[3] & 127}, (uint8_t)(b[4] > 4 ? 4 : b[4]), (uint8_t)clampi(b[5], 1, 127),
                  (uint8_t)clampi(b[6], 1, LEN_MAX), (uint8_t)clampi(b[7], 1, 100), (uint8_t)(b[8] < TC_COUNT ? b[8] : 0),
                  (uint8_t)(b[9] & 15), (int8_t)clampi((int8_t)b[10], -32, 31), 0};
  }
  return sv.st.patch & 63;
}

static int save_all(void) { /* steps that did not fit */
  store_t *st = &sv.st;
  st->magic = STORE_MAGIC, st->patch = cur_patch;
  for (int t = 0; t < 64; t++) st->tones[t] = TONES[t];
  for (int p = 0; p < 64; p++) {
    const uint8_t *b = (const uint8_t *)&PATCHES[p];
    for (int i = 0; i < PATCH_BYTES; i++) st->patches[p][i] = b[i];
  }
  for (int p = 0; p < ARPS; p++) {
    const volatile arp_t *a = &ARP[p];
    uint8_t *b = st->arp[p];
    b[0] = a->mode, b[1] = a->rate, b[2] = a->oct, b[3] = a->gate, b[4] = a->swing, b[5] = a->chance, b[6] = a->hold, b[7] = a->order;
  }
  for (int t = 0; t < TRACKS; t++) st->trk[t][0] = TRK[t].part, st->trk[t][1] = TRK[t].div, st->trk[t][2] = TRK[t].dir, st->trk[t][3] = TRK[t].mute;
  st->glob[0] = GLOB.bpm, st->glob[1] = GLOB.swing, st->glob[2] = GLOB.click, st->glob[3] = GLOB.pattern;
  srec_t *r = (srec_t *)(sv.raw + sizeof(store_t));
  uint32_t n = 0;
  int lost = 0;
  for (int p = 0; p < PATTERNS; p++)
    for (int t = 0; t < TRACKS; t++) {
      st->tlen[p][t] = TLEN[p][t];
      for (int s = 0; s < STEPS; s++) {
        const step_t *x = &STEP[p][t][s];
        if (!x->n) continue;
        if (n >= SREC_MAX) {
          lost++;
          continue;
        }
        r[n].at = (uint16_t)(p << 9 | t << 6 | s);
        const uint8_t *b = (const uint8_t *)x;
        for (uint32_t i = 0; i < sizeof(step_t); i++) r[n].b[i] = b[i];
        n++;
      }
    }
  st->nsteps = (uint16_t)n;
  settings_save(sv.raw, SETTINGS_MAX);
  return lost;
}

/* WRITE: the edited patch and its tones into memory, then all of it into flash */
static int write_all(void) {
  for (int s = 0; s < 2; s++) {
    int t = EDP.s[s].tone;
    if (s == 1 && EDP.key_mode == KM_WHOLE && EDP.s[1].tone == EDP.s[0].tone) break;
    for (int i = 0; i < NTP; i++) TONES[t].p[i] = EDT[s].p[i];
  }
  uint8_t *b = (uint8_t *)&PATCHES[cur_patch];
  const volatile uint8_t *e = (const volatile uint8_t *)&EDP;
  for (int i = 0; i < PATCH_BYTES; i++) b[i] = e[i];
  return save_all();
}

/* ---------------------------------------------------------------- setup */

void setup(void) {
  z_init();
  sq_init();
  factory();
  load_patch(load_all());
  led_backlight(1);
  clear(C_CASE);
  draw_title();
  lcd_frame();
  lcd_msg("ZEUS-80 Ver 0.1", 0);
  msg_until = 1800;
}

/* ---------------------------------------------------------------- loop */

static uint32_t sh_foot = ~0u, led_b = ~0u, led_k = ~0u;
static int8_t led_shown = -1, view_shown = -1;

static void set_page(int p, uint32_t now) {
  page = (uint8_t)p;
  char b[24], *d;
  if (p == PG_TONE) d = put(b, "[ EDIT TONE "), put(put_num(d, EDP.s[sect].tone), " ]");
  else if (p == PG_PATCH) d = put(b, "[EDIT PATCH "), put(put_num(d, cur_patch), " ]");
  else if (p == PG_PLAY) put(b, "");
  else put(b, PAGE_KNOBS[p]);
  if (b[0]) lcd_msg(b, now);
  else msg_until = 0;
}

static void key_down(int k) {
  int dst = seq_view() ? TRK[trk].part : DST_KBD;
  int n = clampi(KEY_FIRST_NOTE + k + oct * 12, 0, 127);
  key_note[k] = (uint8_t)(n + 1), key_dst[k] = (uint8_t)dst;
  sq_cmd(OP_KEY_ON, (uint8_t)dst, (uint8_t)n, 100);
  if (seq_view() && rec && !sq_playing) {
    target();
    sq_cmd(OP_SREC, (uint8_t)n, keys_down == 0, 0);
    srec_gesture = 1;
  }
  default_note = (uint8_t)n;
  keys_down++;
}
static void key_up(int k) {
  if (!key_note[k]) return;
  sq_cmd(OP_KEY_OFF, key_dst[k], (uint8_t)(key_note[k] - 1), 0);
  key_note[k] = 0;
  if (keys_down) keys_down--;
  if (!keys_down && srec_gesture) srec_gesture = 0, cur = (uint8_t)((cur + 1) % tlen(trk)), cur_moved_t = millis();
}

void loop(void) {
  uint32_t now = millis();
  int32_t e;
  char b[24];

  /* ---- buttons */
  if (button_pressed(BTN_PLAY)) sq_cmd(OP_PLAY, !sq_playing, 0, 0), lcd_msg(sq_playing ? "STOP" : "PLAY", now), cur_moved_t = 0;
  if (button_pressed(BTN_REC)) {
    rec = !rec;
    sq_cmd(OP_REC, rec, trk, 0);
    if (rec && !seq_view()) set_page(PG_STEP, now);
    lcd_msg(rec ? (sq_playing ? "RECORD: LIVE" : "RECORD: STEP") : "RECORD OFF", now);
  }
  if (button_pressed(BTN_SAVE)) {
    int lost = write_all();
    saved_t = now ? now : 1;
    if (lost) put(put_int(put(b, "FULL: "), lost), " LOST");
    else put(put_num(put(b, "[ WRITE  "), cur_patch), " OK ]");
    lcd_msg(b, now);
  }
  if (button_pressed(BTN_EDIT) && !seq_view()) set_page(page == PG_TONE ? PG_PLAY : PG_TONE, now);
  if (button_pressed(BTN_HOME)) set_page(page == PG_PATCH ? PG_PLAY : PG_PATCH, now);
  if (!seq_view()) {
    int in_tone = page == PG_TONE;
    if (button_pressed(BTN_ENV)) tgroup = in_tone && tgroup == 8 ? 9 : 8, set_page(PG_TONE, now);
    if (button_pressed(BTN_LFO)) tgroup = 0, set_page(PG_TONE, now);
    if (button_pressed(BTN_FX)) tgroup = in_tone && tgroup == 6 ? 7 : 6, set_page(PG_TONE, now);
    if (button_pressed(BTN_SEL)) sect = !sect, show_tone(now);
  }
  if (button_pressed(BTN_ARP)) set_page(page == PG_ARP ? PG_ARP2 : PG_ARP, now);
  if (button_pressed(BTN_SEQ)) set_page(page == PG_STEP ? PG_FEEL : page == PG_FEEL ? PG_TRACK : PG_STEP, now);
  if (button_pressed(BTN_GLO)) set_page(PG_GLOBAL, now);
  if (button_pressed(BTN_OCT_DOWN) || button_pressed(BTN_OCT_UP)) {
    if (button_held(BTN_OCT_DOWN) && button_held(BTN_OCT_UP)) oct = 0;
    else oct = (int8_t)clampi(oct + (button_pressed(BTN_OCT_UP) ? 1 : -1), -3, 3);
    put_int(put(b, "OCTAVE "), oct);
    lcd_msg(b, now);
  }
  if (seq_view()) {
    if (button_pressed(BTN_EDIT)) {
      target(), sq_cmd(OP_CLEAR, 0, 0, 0);
      put(put_2(put(b, "STEP "), cur + 1), " ERASED");
      lcd_msg(b, now);
      edit_t = now, edit_cleared = 0;
    }
    if (button_held(BTN_EDIT) && !edit_cleared && now - edit_t > 1000) {
      target(), sq_cmd(OP_CLEAR, 1, 0, 0), edit_cleared = 1;
      put(put(put(b, "TRACK "), (char[2]){(char)('1' + trk), 0}), " CLEARED");
      lcd_msg(b, now);
    }
    if (button_pressed(BTN_SEL) && rec && !sq_playing) cur = (uint8_t)((cur + 1) % tlen(trk)), cur_moved_t = now, lcd_msg("REST", now);
    if (button_pressed(BTN_LFO)) {
      TRK[trk].mute = !TRK[trk].mute;
      put(put(put(b, "TRACK "), (char[2]){(char)('1' + trk), 0}), TRK[trk].mute ? " MUTED" : " ON");
      lcd_msg(b, now);
    }
  }
  sq_fill = (uint8_t)(seq_view() && sq_playing && button_held(BTN_SEL));

  /* ---- encoders */
  if ((e = encoder(ENC_SELECT))) {
    if (seq_view()) {
      trk = (uint8_t)((trk + TRACKS * 8 + e) % TRACKS);
      if (cur >= tlen(trk)) cur = (uint8_t)(tlen(trk) - 1);
      if (rec) sq_cmd(OP_REC, 1, trk, 0);
      put_dst(put(put(put(b, "TRACK "), (char[2]){(char)('1' + trk), 0}), " "), TRK[trk].part);
      lcd_msg(b, now);
    } else {
      load_patch((cur_patch + 64 * 8 + e) & 63);
      put(put(put_num(put(b, "PATCH "), cur_patch), " "), "");
      put_pad(b + 9, PATCH_NAME[cur_patch], 7);
      lcd_msg(b, now);
    }
  }
  if ((e = encoder(ENC_ALGORITHM))) {
    int t = (EDP.s[sect].tone + 64 * 8 + e) & 63;
    EDP.s[sect].tone = (uint8_t)t;
    if (EDP.key_mode == KM_WHOLE && sect == 0) EDP.s[1].tone = (uint8_t)t, tone_into(1, t);
    tone_into(sect, t);
    char *d = put_num(put(b, sect ? "LOWER " : "UPPER "), t);
    put_pad(put(d, " "), TONE_NAME[t], 7);
    lcd_msg(b, now);
  }
  if ((e = encoder(ENC_PRESETS))) {
    if (seq_view()) {
      if (sq_playing && now - cur_moved_t > 2500) cur = (uint8_t)(view_page(now) * 16 + cur % 16);
      cur = (uint8_t)clampi(cur + e, 0, tlen(trk) - 1), cur_moved_t = now;
    } else if (page == PG_TONE) {
      tgroup = (uint8_t)clampi(tgroup + (e > 0 ? 1 : -1), 0, NTG - 1);
      put(put(b, "TONE: "), TG_NAME[tgroup]);
      lcd_msg(b, now);
    } else if (page == PG_PATCH) {
      pgroup = (uint8_t)clampi(pgroup + (e > 0 ? 1 : -1), 0, NPG - 1);
      put(put(b, "PATCH: "), PG_GROUP[pgroup]);
      lcd_msg(b, now);
    } else { /* a bank up or down */
      load_patch((cur_patch + 64 + (e > 0 ? 8 : -8)) & 63);
      put_pad(put(put_num(put(b, "PATCH "), cur_patch), " "), PATCH_NAME[cur_patch], 7);
      lcd_msg(b, now);
    }
  }
  for (int k = 0; k < 4; k++)
    if ((e = encoder(ENC_KNOB1 + k))) knob(k, e, now);

  /* ---- keys */
  for (int k = 0; k < KEY_COUNT; k++) {
    if (key_pressed(k)) key_down(k);
    if (key_released(k)) key_up(k);
  }
  /* ---- MIDI in: notes to an arpeggiator or a track recording, the rest to the synth */
  uint8_t st, d1, d2;
  while (midi_read(&st, &d1, &d2)) {
    if (st == 0xFA || st == 0xFB) sq_cmd(OP_PLAY, 1, 0, 0);
    else if (st == 0xFC) sq_cmd(OP_PLAY, 0, 0, 0);
    if (st < 0x80 || st >= 0xF0) continue;
    int c = st & 15, kind = st & 0xF0;
    if ((kind == 0x90 || kind == 0x80) && c < ARPS && (ARP[c].mode || (rec && sq_playing && c == TRK[trk].part))) {
      if (kind == 0x90 && d2) sq_cmd(OP_KEY_ON, (uint8_t)c, d1, d2);
      else sq_cmd(OP_KEY_OFF, (uint8_t)c, d1, 0);
      z_midi_seen = z_midi_seen + 1;
    } else z_midi(st, d1, d2);
  }
  if (z_midi_seen != midi_seen_last) midi_seen_last = z_midi_seen, midi_light_t = now;
  int led = now - midi_light_t < 60;

  /* ---- LEDs */
  uint32_t lk = 0, lb = PAGE_BTN[page] < BTN_COUNT ? 1u << PAGE_BTN[page] : 0;
  for (int k = 0; k < KEY_COUNT; k++)
    if (key_held(k)) lk |= 1u << k;
  if (sq_playing && (sq_pos[0] & 3) < 2) lb |= 1u << BTN_PLAY;
  if (rec && (sq_playing || (now / 400) & 1)) lb |= 1u << BTN_REC;
  if (arp_of() >= 0 && ARP[arp_of()].mode) lb |= 1u << BTN_ARP;
  if (seq_view() && TRK[trk].mute) lb |= 1u << BTN_LFO;
  if (!seq_view() && sect) lb |= 1u << BTN_SEL;
  if (sq_fill) lb |= 1u << BTN_SEL;
  if (lk != led_k) {
    for (int i = 0; i < KEY_COUNT; i++)
      if ((lk ^ led_k) >> i & 1u) led_key(i, lk >> i & 1u);
    led_k = lk;
  }
  if (lb != led_b) {
    for (int i = 0; i < BTN_COUNT; i++)
      if ((lb ^ led_b) >> i & 1u) led_button(i, lb >> i & 1u);
    led_b = lb;
  }

  /* ---- the LCD: a message, else as the module shows it */
  if (now < msg_until) lcd_show(msg);
  else if (page == PG_TONE) {
    char *d = put(b, "[ EDIT TONE ");
    put(put_num(d, EDP.s[sect].tone), " ]");
    lcd_show(b);
  } else if (page == PG_PATCH) {
    char *d = put(b, "[EDIT PATCH ");
    put(put_num(d, cur_patch), " ]");
    lcd_show(b);
  } else if (seq_view()) { /* "A 120 T1 05 PLAY" */
    char *d = b;
    *d++ = (char)('A' + sq_pat), *d++ = ' ';
    d = put(put_int(d, GLOB.bpm), GLOB.bpm < 100 ? "  T" : " T");
    *d++ = (char)('1' + trk), *d++ = ' ';
    d = put(put_2(d, (sq_playing ? sq_pos[trk] : cur) + 1), " ");
    put(d, rec ? (sq_playing ? "REC" : "STEP") : sq_playing ? "PLAY" : "STOP");
    lcd_show(b);
  } else { /* "1 INT 11 W 11 11": channel, memory, patch, key mode, lower and upper tones */
    static const char KM[3] = {'W', 'D', 'S'};
    char *d = put(b, "1 INT ");
    d = put(put_num(d, cur_patch), " ");
    *d++ = KM[EDP.key_mode % 3], *d++ = ' ';
    put_num(put(put_num(d, EDP.s[1].tone), " "), EDP.s[0].tone);
    lcd_show(b);
  }

  /* ---- below it */
  int sv_now = seq_view(), all = sv_now != view_shown;
  if (all) {
    view_shown = (int8_t)sv_now;
    rect(0, BTN_Y0, SCREEN_W, FOOT_Y - BTN_Y0, C_CASE);
    panel_shown = ~0u;
    for (int i = 0; i < 6; i++) row_shown[i][0] = 1, row_shown[i][1] = 0;
    for (int i = 0; i < VOICES; i++) mod_shown[i] = 255;
    if (!sv_now) text(4, LIST_Y, "MODULE", C_DIM, FONT_SMALL);
  }
  if (sv_now) seq_screen(now, all);
  else draw_panel(), knobs_screen();
  uint32_t h = (uint32_t)page | (uint32_t)(now - saved_t < 1500) << 4 | (uint32_t)sq_playing << 5 | (uint32_t)rec << 6 | (uint32_t)sq_pat << 7 |
               (uint32_t)GLOB.pattern << 9 | (uint32_t)GLOB.bpm << 11;
  if (h != sh_foot) sh_foot = h, draw_foot(now), led_shown = -1;
  if (led != led_shown) led_shown = (int8_t)led, draw_midi_led(led);
}

/* ---------------------------------------------------------------- the sound */

void audio(int32_t *out, uint32_t frames) {
  for (uint32_t i = 0; i < frames; i += 64) {
    uint32_t n = frames - i < 64 ? frames - i : 64;
    sq_tick(n);
    z_render(out + 2 * i, n);
  }
}
