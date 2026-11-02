#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""ZEUS-80's factory memory: 64 tones and 64 patches, numbered 11..88 (bank 1-8, number 1-8) -> src/tones.h

A tone is 48 parameters (0..100, or a choice), in the order the panel shows them, four to a group.
A patch is the key mode, the split point, the balance, the stereo mode, and for the upper and the lower section:
the tone, the assign mode, unison detune, octave shift, glide, hold, LFO-2 rate, aftertouch sensitivity, bender range,
VCO-1 and VCO-2 bend, aftertouch destination. All sounds are newly made here."""
from pathlib import Path

OUT = Path(__file__).resolve().parents[1] / "src" / "tones.h"

# name (8 chars on the LCD), max, choices
P = [
  ("LFO RATE", 100, None), ("LFO DLY", 100, None), ("LFO WAVE", 4, ["TRI", "SW+", "SW-", "SQR", "RND"]), ("VCA LFO", 100, None),
  ("MOD LFO", 100, None), ("MOD ENV1", 100, None), ("VCO1 MOD", 2, ["NRM", "OFF", "INV"]), ("VCO2 MOD", 2, ["NRM", "OFF", "INV"]),
  ("PW", 100, None), ("PWM", 100, None), ("PWM SEL", 1, ["EN1", "LFO"]), ("PWM POL", 1, ["NRM", "INV"]),
  ("VCO1 WAV", 3, ["TRI", "SAW", "PLS", "SQR"]), ("VCO1 RNG", 4, ["32'", "16'", " 8'", " 4'", " 2'"]), ("KEY FLW", 100, None), ("KF SEL", 2, ["VC1", "OFF", "VC2"]),
  ("XMOD MAN", 100, None), ("XMOD EN1", 100, None), ("XMOD POL", 1, ["NRM", "INV"]), ("SYNC", 2, ["OFF", "1>2", "2>1"]),
  ("VCO2 WAV", 3, ["TRI", "SAW", "PLS", "NOI"]), ("VCO2 RNG", 4, ["32'", "16'", " 8'", " 4'", " 2'"]), ("VCO2 FIN", 100, None), ("MIXER", 100, None),
  ("VCF FREQ", 100, None), ("VCF RES", 100, None), ("HPF FREQ", 100, None), ("VCF KEY", 100, None),
  ("ENV SEL", 1, ["EN1", "EN2"]), ("ENV POL", 1, ["NRM", "INV"]), ("VCF ENV", 100, None), ("VCF LFO", 100, None),
  ("E1 ATK", 100, None), ("E1 DCY", 100, None), ("E1 SUS", 100, None), ("E1 REL", 100, None),
  ("E2 ATK", 100, None), ("E2 DCY", 100, None), ("E2 SUS", 100, None), ("E2 REL", 100, None),
  ("E1 KEY", 100, None), ("E2 KEY", 100, None), ("E1 DYN", 1, ["OFF", "ON"]), ("E2 DYN", 1, ["OFF", "ON"]),
  ("VCA LEVL", 100, None), ("DYN LEVL", 100, None), ("ENV RSET", 1, ["OFF", "ON"]), ("", 0, None),
]
GROUPS = ["LFO-1", "VCO MOD", "PWM", "VCO-1", "X-MOD", "VCO-2", "VCF", "VCF MOD", "ENV-1", "ENV-2", "ENV KEY", "VCA"]
KEYS = ["lfo_rate", "lfo_delay", "lfo_wave", "vca_lfo", "mod_lfo", "mod_env1", "vco1_mod", "vco2_mod", "pw", "pwm", "pwm_sel",
        "pwm_pol", "w1", "r1", "kf", "kf_sel", "xmod", "xmod_env", "xmod_pol", "sync", "w2", "r2", "fine", "mix", "freq", "res",
        "hpf", "vkey", "env_sel", "env_pol", "venv", "vlfo", "a1", "d1", "s1", "r1t", "a2", "d2", "s2", "r2t", "k1", "k2",
        "dyn1", "dyn2", "vca", "dyn", "reset", "spare"]
assert len(P) == 48 == len(KEYS)
DEF = dict(lfo_rate=50, lfo_delay=0, lfo_wave=0, vca_lfo=0, mod_lfo=0, mod_env1=0, vco1_mod=0, vco2_mod=0, pw=0, pwm=0, pwm_sel=1,
           pwm_pol=0, w1=1, r1=2, kf=100, kf_sel=1, xmod=0, xmod_env=0, xmod_pol=0, sync=0, w2=1, r2=2, fine=50, mix=50, freq=60,
           res=0, hpf=0, vkey=50, env_sel=0, env_pol=0, venv=30, vlfo=0, a1=0, d1=40, s1=30, r1t=30, a2=0, d2=40, s2=100, r2t=30,
           k1=0, k2=0, dyn1=0, dyn2=1, vca=80, dyn=40, reset=0, spare=0)

TONES = []


def t(name, **kw):
  d = dict(DEF)
  for k in kw:
    assert k in d, k
  d.update(kw)
  TONES.append((name, [d[k] for k in KEYS]))


# ---- bank 1: brass
t("JP BRASS", w1=1, w2=1, fine=54, freq=30, res=10, venv=58, a1=12, d1=55, s1=45, r1t=35, a2=8, d2=50, s2=90, r2t=35, vkey=60, dyn1=1)
t("SOFT BRS", fine=53, freq=26, venv=45, a1=30, d1=60, s1=50, r1t=45, a2=25, s2=95, r2t=45)
t("BRS STAB", fine=55, freq=34, res=15, venv=60, a1=0, d1=35, s1=20, r1t=20, a2=0, d2=45, s2=0, r2t=25, dyn1=1)
t("HORNS", w2=2, pw=30, fine=52, mix=40, freq=32, venv=48, a1=18, d1=50, s1=55, a2=14, s2=100, r2t=38)
t("SYN BRS2", r2=1, fine=50, mix=45, freq=24, res=30, venv=62, a1=6, d1=58, s1=35, a2=4, s2=85, r2t=30, dyn1=1)
t("BRASS PW", w1=2, w2=2, pw=25, pwm=30, lfo_rate=38, fine=54, freq=36, venv=44, a1=15, d1=50, s1=50, a2=10, r2t=40)
t("FAT BRAS", r2=1, fine=48, mix=55, freq=28, res=20, venv=60, a1=10, d1=60, s1=40, a2=6, r2t=40, mod_env1=4, vco2_mod=1)
t("SWEL BRS", fine=54, freq=18, venv=60, a1=70, d1=70, s1=70, r1t=50, a2=55, s2=100, r2t=55)
# ---- bank 2: strings
t("STRINGS", fine=56, freq=52, res=5, hpf=20, venv=10, a2=40, d2=50, s2=100, r2t=55, mod_lfo=10, lfo_rate=55, lfo_delay=40)
t("PWM STR", w1=2, w2=2, pw=20, pwm=55, lfo_rate=36, fine=53, freq=50, hpf=25, venv=8, a2=45, r2t=58)
t("SLOW STR", fine=57, freq=44, hpf=18, venv=20, a1=80, d1=80, s1=80, a2=78, r2t=70, mod_lfo=8, lfo_delay=60)
t("OCT STR", r2=3, fine=51, mix=40, freq=55, hpf=22, venv=8, a2=40, r2t=55)
t("HI STR", r1=3, r2=3, fine=55, freq=62, hpf=40, venv=6, a2=35, r2t=50, mod_lfo=12, lfo_rate=60, lfo_delay=30)
t("ENSEMBLE", w2=2, pw=10, pwm=60, lfo_rate=45, fine=58, freq=48, hpf=20, venv=12, a2=30, r2t=60, mod_lfo=14)
t("STR SWEL", fine=56, freq=30, venv=50, a1=85, d1=90, s1=70, a2=60, r2t=60, hpf=15)
t("CELLOS", r1=1, r2=1, fine=55, freq=40, res=10, venv=20, a1=20, a2=30, r2t=45, mod_lfo=12, lfo_delay=50, lfo_rate=52)
# ---- bank 3: pads
t("WARM PAD", w1=2, w2=1, pw=15, pwm=40, lfo_rate=30, fine=55, freq=38, res=15, venv=20, a1=50, d1=70, s1=60, a2=50, r2t=65)
t("SWEEP PD", fine=54, freq=20, res=55, venv=55, a1=90, d1=95, s1=40, r1t=80, a2=55, r2t=75, vlfo=10, lfo_rate=20)
t("GLASS PD", w1=0, w2=2, pw=40, pwm=45, r2=3, mix=55, freq=58, res=30, hpf=35, venv=15, a2=45, r2t=70)
t("CHOIR", w1=2, w2=2, pw=35, pwm=50, lfo_rate=42, fine=56, freq=36, res=35, hpf=30, venv=10, a2=55, r2t=60, mod_lfo=8)
t("SPACE PD", r2=3, fine=53, freq=40, res=45, venv=35, a1=70, d1=90, s1=50, a2=65, r2t=85, vlfo=25, lfo_rate=12, lfo_wave=1)
t("DARK PAD", r1=1, fine=55, freq=22, res=25, venv=15, a2=60, r2t=70, mix=55)
t("SHIMMER", w1=2, w2=0, r2=4, pw=30, pwm=60, lfo_rate=58, mix=45, freq=64, res=20, hpf=45, a2=50, r2t=80, vca_lfo=10)
t("VOX PAD", w1=3, w2=2, pw=25, pwm=40, fine=55, freq=34, res=55, venv=12, a2=50, r2t=60, mod_lfo=10, lfo_delay=50)
# ---- bank 4: bass
t("SYN BASS", r1=1, r2=1, fine=52, freq=22, res=35, venv=55, a1=0, d1=38, s1=10, r1t=15, a2=0, d2=50, s2=70, r2t=12, dyn1=1)
t("SQR BASS", w1=3, w2=2, r1=1, r2=0, pw=30, freq=26, res=20, venv=45, d1=40, s1=15, s2=80, r2t=10)
t("PLUCK BS", r1=1, r2=1, fine=51, freq=16, res=45, venv=62, d1=30, s1=0, r1t=20, d2=45, s2=0, r2t=20, dyn1=1)
t("SUB BASS", w1=0, w2=3, r1=0, r2=1, pw=0, mix=40, freq=30, venv=15, s2=100, r2t=12)
t("ACID BS", w1=1, mix=0, r1=1, freq=14, res=78, venv=65, d1=32, s1=5, r1t=18, d2=40, s2=60, r2t=10, dyn1=1)
t("SYNC BS", r1=1, r2=1, sync=1, mod_env1=30, vco1_mod=1, freq=40, res=15, venv=35, d1=35, s1=10, s2=80, r2t=12)
t("FUNK BS", w1=2, w2=2, r1=1, r2=1, pw=50, fine=50, freq=20, res=40, venv=58, d1=26, s1=0, d2=40, s2=50, r2t=10, dyn1=1)
t("XMOD BS", w1=1, w2=0, r1=1, r2=1, xmod=18, xmod_env=30, freq=30, res=25, venv=40, d1=35, s1=10, s2=75, r2t=12)
# ---- bank 5: leads
t("SYNC LD", sync=1, r2=2, mod_env1=45, vco1_mod=1, mix=100, freq=60, res=10, venv=20, a1=0, d1=60, s1=30, s2=100, r2t=25, mod_lfo=8, lfo_delay=50, lfo_rate=60)
t("SAW LEAD", fine=53, freq=50, res=25, venv=35, d1=45, s1=40, s2=100, r2t=25, mod_lfo=10, lfo_delay=55, lfo_rate=62)
t("SQR LEAD", w1=3, w2=2, pw=10, mix=30, freq=55, res=10, venv=25, d1=50, s1=50, r2t=25, mod_lfo=10, lfo_delay=45, lfo_rate=60)
t("PORTA LD", fine=55, freq=46, res=30, venv=40, d1=55, s1=35, r2t=30, mod_lfo=8, lfo_delay=60, lfo_rate=60)
t("OCT LEAD", r2=3, fine=50, mix=45, freq=55, res=20, venv=30, d1=50, s1=40, r2t=28)
t("VIBE LD", w1=0, w2=2, pw=20, mix=60, freq=58, res=15, venv=20, mod_lfo=18, lfo_rate=58, r2t=30)
t("SOLO PWM", w1=2, w2=2, pw=30, pwm=50, lfo_rate=30, r2=1, mix=35, freq=44, res=35, venv=40, d1=50, s1=30, r2t=25)
t("SCREAM", sync=1, mod_env1=60, vco1_mod=1, xmod=20, mix=80, freq=48, res=60, venv=45, d1=60, s1=40, r2t=30, mod_lfo=12, lfo_rate=64, lfo_delay=40)
# ---- bank 6: keys
t("POLY KEY", fine=53, freq=40, res=10, venv=50, d1=45, s1=10, r1t=30, d2=60, s2=40, r2t=35, dyn1=1, k1=40, k2=40)
t("E.PIANO", w1=0, w2=2, pw=40, r2=3, mix=35, freq=36, venv=35, d1=50, s1=0, d2=70, s2=0, r2t=45, k2=60, dyn1=1, xmod=8)
t("CLAV", w1=2, w2=2, pw=60, r2=3, freq=34, res=30, venv=55, d1=25, s1=0, d2=40, s2=20, r2t=15, dyn1=1, hpf=30)
t("HARPSICH", w1=2, w2=1, pw=70, r2=3, fine=52, freq=58, res=15, hpf=40, venv=40, d1=40, s1=0, d2=55, s2=0, r2t=30, k2=50)
t("ORGAN", w1=3, w2=2, pw=0, r2=3, mix=45, freq=60, venv=0, a2=0, d2=10, s2=100, r2t=5, vca=75)
t("PLUCK", vca=100, fine=52, freq=30, res=35, venv=55, d1=30, s1=0, d2=40, s2=0, r2t=30, dyn1=1, k1=50, k2=50)
t("MARIMBA", vca=100, w1=0, w2=0, r2=4, mix=30, freq=50, venv=20, d1=20, s1=0, d2=28, s2=0, r2t=25, k2=60, xmod=12)
t("BRIGHT K", fine=55, freq=56, res=20, hpf=30, venv=40, d1=50, s1=20, d2=60, s2=30, r2t=40, dyn1=1)
# ---- bank 7: bells, cross-mod
t("BELLS", w1=0, w2=0, r1=3, r2=2, xmod=45, mix=0, freq=80, venv=0, a2=0, d2=75, s2=0, r2t=70, k2=40)
t("XMOD BEL", w1=0, w2=1, r1=2, r2=3, xmod=60, xmod_env=30, mix=0, freq=70, res=10, venv=10, d1=60, s1=0, d2=80, s2=0, r2t=75)
t("CHIMES", w1=0, w2=2, r1=4, r2=2, xmod=35, pw=30, mix=20, freq=75, hpf=40, d2=65, s2=0, r2t=60)
t("METAL", w1=1, w2=2, r1=2, r2=4, xmod=70, xmod_env=40, mix=10, freq=55, res=40, venv=30, d1=50, s1=0, d2=60, s2=10, r2t=50)
t("GLOCK", w1=0, w2=0, r1=4, r2=4, xmod=25, fine=56, mix=30, freq=85, d2=50, s2=0, r2t=50)
t("TUBULAR", w1=0, w2=1, r1=2, r2=2, xmod=40, xmod_env=20, mix=10, freq=60, res=20, venv=20, d1=70, s1=0, d2=85, s2=0, r2t=80)
t("CRYSTAL", w1=0, w2=2, r1=3, r2=4, pw=45, pwm=30, xmod=30, mix=40, freq=78, hpf=50, d2=70, s2=10, r2t=80, vca_lfo=8)
t("GAMELAN", w1=2, w2=0, r1=2, r2=3, pw=20, xmod=55, mix=5, freq=58, res=30, venv=25, d1=55, s1=0, d2=70, s2=0, r2t=65)
# ---- bank 8: effects
t("NOISE SW", w2=3, mix=100, freq=20, res=70, venv=70, a1=60, d1=90, s1=0, r1t=80, a2=30, r2t=70)
t("WIND", w2=3, mix=100, freq=40, res=80, vlfo=40, lfo_rate=10, lfo_wave=4, venv=0, a2=60, r2t=80)
t("LASER", w1=1, mix=0, r1=4, mod_env1=90, env_pol=1, vco1_mod=0, freq=70, res=40, venv=30, d1=40, s1=0, d2=50, s2=0, r2t=40)
t("S&H BLIP", w1=2, w2=2, r1=3, lfo_wave=4, lfo_rate=70, mod_lfo=40, freq=50, res=60, vlfo=40, s2=100, r2t=20)
t("SIREN", w1=0, mix=0, mod_lfo=60, lfo_rate=40, lfo_wave=0, freq=70, r2t=30)
t("THUNDER", w2=3, mix=100, r2=0, freq=10, res=40, venv=50, a1=10, d1=90, s1=0, d2=95, s2=0, r2t=90, vlfo=20, lfo_wave=4, lfo_rate=30)
t("COMPUTER", w1=3, w2=3, r1=3, lfo_wave=4, lfo_rate=80, mod_lfo=50, sync=1, freq=60, res=30, s2=100, r2t=10)
t("RISE", r2=2, fine=56, mod_env1=50, a1=90, d1=100, s1=100, freq=20, venv=70, res=40, a2=50, r2t=60)
assert len(TONES) == 64, len(TONES)

# ---- patches: key mode W/D/S, split, balance, stereo; then [upper, lower] sections
# section: tone, assign (0 SOLO 1 UNI1 2 UNI2 3 POLY1 4 POLY2), detune, oct (0..4, 2 = 0), glide, hold (0 OFF 1 ON 2 MIDI),
#          lfo2, at_sens, bend, b1, b2 (0 OFF 1 NRM 2 WIDE), at_sel (0 VCO 1 VCF)
NAMES = ["KEY MODE", "SPLIT PT", "BALANCE", "STEREO"]


def sec(tone, assign=3, detune=20, oct=2, glide=0, hold=2, lfo2=55, at=40, bend=17, b1=1, b2=1, at_sel=0):
  return [tone, assign, detune, oct, glide, hold, lfo2, at, bend, b1, b2, at_sel]


def tn(n):  # 11..88 -> 0..63
  return (n // 10 - 1) * 8 + n % 10 - 1


PATCHES = []


def pt(name, km, up, lo=None, split=60, bal=50, stereo=0):
  lo = lo or sec(up[0])
  PATCHES.append((name, [km, split, bal, stereo] + up + lo))


for n in [11, 12, 13, 14, 15, 16, 17, 18, 21, 22, 23, 24, 25, 26, 27, 28, 31, 32, 33, 34, 35, 36, 37, 38]:
  pt(TONES[tn(n)][0], 0, sec(tn(n)))
for n in [41, 42, 43, 44, 45, 46, 47, 48]:  # bass: mono, a little glide on some
  pt(TONES[tn(n)][0], 0, sec(tn(n), assign=0 if n in (45, 46) else 4, glide=12 if n == 45 else 0))
pt("SYNC LD", 0, sec(tn(51), assign=0, glide=8, at_sel=0))
pt("SAW LEAD", 0, sec(tn(52), assign=1, detune=30, glide=5))
pt("SQR LEAD", 0, sec(tn(53), assign=0, glide=10))
pt("PORTA LD", 0, sec(tn(54), assign=0, glide=35))
pt("OCT LEAD", 0, sec(tn(55), assign=0, glide=6))
pt("VIBE LD", 0, sec(tn(56), assign=4, glide=0, at_sel=0))
pt("SOLO PWM", 0, sec(tn(57), assign=0, glide=15))
pt("SCREAM", 0, sec(tn(58), assign=1, detune=45, glide=10, at_sel=1))
for n in [61, 62, 63, 64, 65, 66, 67, 68]:
  pt(TONES[tn(n)][0], 0, sec(tn(n)))
pt("BRS+STR", 1, sec(tn(11)), sec(tn(21)), bal=55)
pt("PAD+BELL", 1, sec(tn(31)), sec(tn(71)), bal=60, stereo=1)
pt("BASS|BRS", 2, sec(tn(11)), sec(tn(41), assign=0), split=55)
pt("BASS|STR", 2, sec(tn(21)), sec(tn(43), assign=4), split=55)
pt("STR+OCT", 1, sec(tn(21)), sec(tn(24), oct=3), bal=45, stereo=1)
pt("BASS|LD", 2, sec(tn(52), assign=0, glide=8), sec(tn(41), assign=0), split=60)
pt("CHOIR+PD", 1, sec(tn(34)), sec(tn(36)), bal=50, stereo=1)
pt("BASS|EP", 2, sec(tn(62)), sec(tn(47), assign=0), split=55)
for n in [71, 72, 74, 77, 81, 82, 84, 88]:
  pt(TONES[tn(n)][0], 0, sec(tn(n), hold=2))
assert len(PATCHES) == 64, len(PATCHES)


def cstr(s):
  return '"' + s.replace('"', '\\"') + '"'


L = ["/* SPDX-License-Identifier: GPL-3.0-only */", "/* Generated by tools/gen_tones.py: ZEUS-80's factory tones and patches */",
     "#ifndef TONES_H", "#define TONES_H", "#include <stdint.h>", "",
     f"#define NTP {len(P)}", f"#define NTG {len(GROUPS)}",
     "static const char *const TP_NAME[NTP] = {" + ", ".join(cstr(p[0]) for p in P) + "};",
     "static const uint8_t TP_MAX[NTP] = {" + ", ".join(str(p[1]) for p in P) + "};"]
for i, p in enumerate(P):
  if p[2]:
    L.append(f"static const char *const TP_CH{i}[] = {{" + ", ".join(cstr(c) for c in p[2]) + "};")
L.append("static const char *const *const TP_CHOICE[NTP] = {" + ", ".join(f"TP_CH{i}" if p[2] else "0" for i, p in enumerate(P)) + "};")
L.append("static const char *const TG_NAME[NTG] = {" + ", ".join(cstr(g) for g in GROUPS) + "};")
L.append("static const char *const TONE_NAME[64] = {" + ", ".join(cstr(n) for n, _ in TONES) + "};")
L.append("static const uint8_t FACTORY_TONE[64][NTP] = {")
for n, v in TONES:
  for i, x in enumerate(v):
    assert 0 <= x <= max(P[i][1], 0), (n, KEYS[i], x)
  L.append("  {" + ", ".join(str(x) for x in v) + "}, /* " + n + " */")
L.append("};")
L.append("static const char *const PATCH_NAME[64] = {" + ", ".join(cstr(n) for n, _ in PATCHES) + "};")
L.append("static const uint8_t FACTORY_PATCH[64][28] = {")
for n, v in PATCHES:
  assert len(v) == 28
  L.append("  {" + ", ".join(str(x) for x in v) + "}, /* " + n + " */")
L.append("};")
L.append("#endif")
OUT.write_text("\n".join(L) + "\n")
print(OUT, len(TONES), "tones", len(PATCHES), "patches")
