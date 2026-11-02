/* SPDX-License-Identifier: GPL-3.0-only */
/* Host test for ZEUS-80 (tests/run.sh). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "zs.h"
#include "sq.h"
#include "tones.h"

int32_t sine(uint32_t phase) {
  int32_t x = (int32_t)phase >> 16, ax = x < 0 ? -x : x, y, ay;
  y = (x * (32768 - ax)) >> 13;
  ay = y < 0 ? -y : y;
  y += ((((y * ay) >> 15) - y) * 7373) >> 15;
  return y > 32767 ? 32767 : y < -32767 ? -32767 : y;
}
static const uint32_t INC_C9[12] = {815363807u, 863847862u, 915214929u, 969636441u, 1027294024u, 1088380105u,
                                    1153098554u, 1221665363u, 1294309365u, 1371273005u, 1452813141u, 1539201906u};
uint32_t note_inc(int note) {
  if (note < 0) note = 0;
  if (note > 127) note = 127;
  return INC_C9[note % 12] >> (10 - note / 12);
}
static uint32_t rs = 0x1234567u;
uint32_t random32(void) { return rs ^= rs << 13, rs ^= rs >> 17, rs ^= rs << 5; }
static int midi_out;
void midi_send(uint8_t st, uint8_t d1, uint8_t d2) { (void)st, (void)d1, (void)d2, midi_out++; }

#define MAXF (44100 * 200)
static int16_t buf[MAXF * 2];
static uint32_t pos;
static int fail, maxv;

static void render(uint32_t frames) {
  static int32_t blk[128];
  while (frames) {
    uint32_t n = frames > 64 ? 64 : frames;
    memset(blk, 0, sizeof blk);
    sq_tick(n);
    z_render(blk, n);
    int v = 0;
    for (int i = 0; i < VOICES; i++) v += z_voice_sect[i] != 0;
    if (v > maxv) maxv = v;
    for (uint32_t i = 0; i < 2 * n; i++) {
      if (blk[i] > 32767 || blk[i] < -32767) fail = 1;
      if (pos * 2 + i < MAXF * 2) buf[pos * 2 + i] = (int16_t)blk[i];
    }
    pos += n, frames -= n;
  }
}
static double save(const char *dir, const char *name) {
  char path[600];
  snprintf(path, sizeof path, "%s/%s.wav", dir, name);
  FILE *f = fopen(path, "wb");
  uint32_t frames = pos < MAXF ? pos : MAXF, data = frames * 4, riff = 36 + data, rate = 44100, br = rate * 4, fmt = 16;
  uint16_t pcm = 1, ch = 2, al = 4, bits = 16;
  fwrite("RIFF", 1, 4, f), fwrite(&riff, 4, 1, f), fwrite("WAVEfmt ", 1, 8, f), fwrite(&fmt, 4, 1, f);
  fwrite(&pcm, 2, 1, f), fwrite(&ch, 2, 1, f), fwrite(&rate, 4, 1, f), fwrite(&br, 4, 1, f);
  fwrite(&al, 2, 1, f), fwrite(&bits, 2, 1, f), fwrite("data", 1, 4, f), fwrite(&data, 4, 1, f);
  fwrite(buf, 2, frames * 2, f);
  fclose(f);
  double sq = 0;
  int peak = 0, clip = 0;
  for (uint32_t i = 0; i < frames * 2; i++) {
    int v = abs(buf[i]);
    sq += (double)v * v, peak = v > peak ? v : peak, clip += v >= 32700;
  }
  printf("%-14s %5.1f s  peak %6d rms %6.0f clip %.3f%%\n", name, frames / 44100.0, peak, sqrt(sq / (frames * 2)), 100.0 * clip / (frames * 2));
  pos = 0;
  return sqrt(sq / (frames * 2));
}

static tone_t TN[64];
static void load_patch(int n) {
  volatile uint8_t *e = (volatile uint8_t *)&EDP;
  for (int i = 0; i < PATCH_BYTES; i++) e[i] = FACTORY_PATCH[n][i];
  for (int s = 0; s < 2; s++)
    for (int i = 0; i < NTP; i++) EDT[s].p[i] = FACTORY_TONE[EDP.s[s].tone][i];
}
static void kb(int note, int vel) { z_midi((uint8_t)(vel ? 0x90 : 0x80), (uint8_t)note, (uint8_t)vel); }

int main(int argc, char **argv) {
  const char *dir = argc > 1 ? argv[1] : ".";
  (void)TN;
  z_init();
  sq_init();
  /* ---- every patch: a chord held, then a short phrase */
  double rms[64];
  for (int p = 0; p < 64; p++) {
    load_patch(p);
    static const uint8_t C[4] = {48, 55, 60, 64};
    for (int i = 0; i < 4; i++) kb(C[i], 100);
    render(44100);
    for (int i = 0; i < 4; i++) kb(C[i], 0);
    render(22050);
    static const uint8_t M[6] = {60, 62, 64, 67, 64, 72};
    for (int i = 0; i < 6; i++) kb(M[i], 70 + i * 10), render(5000), kb(M[i], 0), render(1000);
    render(30000);
    z_panic(), render(64);
    char name[32];
    snprintf(name, sizeof name, "p%d%d", p / 8 + 1, p % 8 + 1);
    if (p % 16 == 15) {
      snprintf(name, sizeof name, "1-patches-%d-%d", (p - 15) / 8 + 1, p / 8 + 1);
      save(dir, name);
    }
  }
  /* loudness of each, alone */
  int quiet = 0;
  for (int p = 0; p < 64; p++) {
    load_patch(p);
    kb(60, 100), kb(64, 100), render(30000), kb(60, 0), kb(64, 0), render(20000), z_panic(), render(64);
    double s = 0;
    for (uint32_t i = 0; i < pos * 2; i++) s += (double)buf[i] * buf[i];
    rms[p] = sqrt(s / (pos * 2 + 1));
    pos = 0;
    if (rms[p] < 400) quiet++, printf("quiet: %d%d %s rms %.0f\n", p / 8 + 1, p % 8 + 1, PATCH_NAME[p], rms[p]);
    if (rms[p] > 9000) printf("loud: %d%d %s rms %.0f\n", p / 8 + 1, p % 8 + 1, PATCH_NAME[p], rms[p]);
  }
  /* ---- CPU: all eight modules, the heaviest kind (xmod, sync, pulse), 10 s */
  load_patch(0);
  EDT[0].p[T_W1] = 2, EDT[0].p[T_W2] = 2, EDT[0].p[T_XMOD] = 50, EDT[0].p[T_SYNC] = 1, EDT[0].p[T_HPF] = 30, EDT[0].p[T_RES] = 60;
  for (int i = 0; i < 8; i++) kb(48 + i * 3, 100);
  clock_t t0 = clock();
  render(44100 * 10);
  double ms = (clock() - t0) * 1000.0 / CLOCKS_PER_SEC;
  pos = 0, z_panic(), render(64);
  printf("CPU: 8 modules for 10 s: %.0f ms (host), up to %d modules\n", ms, maxv);
  if (maxv != 8) fail = 1;
  /* ---- the modes: SPLIT, DUAL, SOLO with glide, UNISON 1 and 2, hold */
  load_patch(50); /* 73 BASS|BRS: split */
  kb(36, 100), kb(64, 100), kb(67, 100), render(30000);
  int up = 0, lo = 0;
  for (int i = 0; i < VOICES; i++) up += z_voice_sect[i] == 1, lo += z_voice_sect[i] == 2;
  printf("split: upper %d lower %d (want 2, 1)  ", up, lo);
  if (up != 2 || lo != 1) fail = 1;
  kb(36, 0), kb(64, 0), kb(67, 0), render(30000), z_panic(), render(64);
  load_patch(48); /* 71 BRS+STR: dual */
  kb(60, 100), render(10000);
  up = lo = 0;
  for (int i = 0; i < VOICES; i++) up += z_voice_sect[i] == 1, lo += z_voice_sect[i] == 2;
  printf("dual: upper %d lower %d (want 1, 1)  ", up, lo);
  if (up != 1 || lo != 1) fail = 1;
  kb(60, 0), render(40000), z_panic(), render(64);
  load_patch(35); /* 54 PORTA LD: solo, glide */
  for (int i = 0; i < 4; i++) kb(60 + i * 5, 100), render(8000);
  for (int i = 3; i >= 0; i--) kb(60 + i * 5, 0), render(8000);
  render(20000);
  load_patch(33); /* 52 SAW LEAD: unison 1 */
  kb(57, 100), render(10000);
  up = 0;
  for (int i = 0; i < VOICES; i++) up += z_voice_sect[i] == 1;
  printf("unison 1: %d modules (want 8)  ", up);
  if (up != 8) fail = 1;
  kb(60, 100), render(10000), kb(60, 0), render(10000), kb(57, 0), render(30000);
  load_patch(0), EDP.s[0].assign = AS_UNI2;
  kb(48, 100), kb(55, 100), kb(60, 100), render(20000);
  up = 0;
  for (int i = 0; i < VOICES; i++) up += z_voice_sect[i] == 1;
  printf("unison 2, 3 keys: %d modules (want 6: 2 a key)\n", up);
  if (up != 6) fail = 1;
  kb(48, 0), kb(55, 0), kb(60, 0), render(30000);
  load_patch(0), EDP.s[0].hold = HOLD_ON;
  kb(60, 100), render(5000), kb(60, 0), render(44100);
  up = 0;
  for (int i = 0; i < VOICES; i++) up += z_voice_sect[i] == 1;
  kb(64, 100), render(30000), kb(64, 0), render(5000);
  int up2 = 0;
  for (int i = 0; i < VOICES; i++) up2 += z_voice_sect[i] == 1;
  printf("hold on: held %d, after a new key %d   ", up, up2);
  if (up != 1) fail = 1;
  z_panic(), render(44100);
  /* pitch bend and aftertouch */
  load_patch(33);
  kb(60, 100), render(10000);
  for (int b = 8192; b <= 16383; b += 1024) z_midi(0xE0, (uint8_t)(b & 127), (uint8_t)(b >> 7)), render(2000);
  z_midi(0xE0, 0, 64), z_midi(0xD0, 127, 0), render(44100), z_midi(0xD0, 0, 0), kb(60, 0), render(20000);
  save(dir, "2-modes");
  /* ---- pattern A on 73, 16 s */
  load_patch(50);
  maxv = 0;
  sq_cmd(OP_PLAY, 1, 0, 0), render(44100 * 16), sq_cmd(OP_PLAY, 0, 0, 0), render(44100);
  printf("sequencer: up to %d modules  ", maxv);
  save(dir, "3-sequencer");
  /* ---- the keyboard's arpeggiator, every mode, on 22 PWM STR */
  load_patch(9);
  ARP[DST_KBD] = (arp_t){AM_UP, 2, 2, 90, 0, 100, 0, 0};
  sq_cmd(OP_KEY_ON, DST_KBD, 60, 100), sq_cmd(OP_KEY_ON, DST_KBD, 63, 100), sq_cmd(OP_KEY_ON, DST_KBD, 67, 100);
  for (int m = 1; m < AM_COUNT; m++) ARP[DST_KBD].mode = (uint8_t)m, render(44100);
  sq_cmd(OP_KEY_OFF, DST_KBD, 60, 0), sq_cmd(OP_KEY_OFF, DST_KBD, 63, 0), sq_cmd(OP_KEY_OFF, DST_KBD, 67, 0), render(22050);
  ARP[DST_KBD].mode = 0;
  save(dir, "4-arpeggios");
  /* ---- a track to MIDI out */
  midi_out = 0;
  TRK[4].part = DST_EXT + 1;
  GLOB.pattern = 1, render(64);
  sq_cmd(OP_TARGET, 4, 0, 0), sq_cmd(OP_SREC, 60, 1, 0), sq_cmd(OP_PLAY, 1, 0, 0), render(44100), sq_cmd(OP_PLAY, 0, 0, 0), render(64);
  printf("midi out: %d messages  ", midi_out);
  if (midi_out < 2) fail = 1;
  /* ---- fuzz */
  srand(5);
  for (int b = 0; b < 40000; b++) {
    int r = rand() % 16;
    if (r < 6) z_midi((uint8_t)((0x80 + 16 * (rand() % 7)) | (rand() % 4)), (uint8_t)(rand() % 128), (uint8_t)(rand() % 128));
    else if (r < 9) {
      int s = rand() & 1, i = rand() % NTP;
      EDT[s].p[i] = (uint8_t)(rand() % (FACTORY_TONE[0][i] * 0 + 101));
    } else if (r == 9) ((volatile uint8_t *)&EDP)[rand() % PATCH_BYTES] = (uint8_t)(rand() % 128);
    else if (r == 10) load_patch(rand() % 64);
    else if (r == 11) sq_cmd((uint8_t)(rand() % 9), (uint8_t)(rand() % 20), (uint8_t)(rand() % 128), (uint8_t)(rand() % 128));
    else if (r == 12) ARP[rand() % ARPS] = (arp_t){(uint8_t)(rand() % AM_COUNT), (uint8_t)(rand() % DIVS), (uint8_t)(1 + rand() % 4), (uint8_t)(4 + rand() % 120), 30, 80, (uint8_t)(rand() & 1), 0};
    else if (r == 13) TRK[rand() % TRACKS].part = (uint8_t)(rand() % DESTS);
    else if (rand() % 300 == 0) z_panic();
    master_tune = (int8_t)(rand() % 101 - 50);
    render(1 + rand() % 200);
    pos = 0;
  }
  printf("fuzz: %s\n", fail ? "FAILED" : "ok");
  return fail;
}
