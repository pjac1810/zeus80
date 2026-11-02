#!/bin/sh
# Host test of ZEUS-80: every patch, the modes, the sequencer and the arpeggiator to build/render-out/*.wav, the CPU
# a full chord takes, then a fuzz of MIDI, tone and patch values and sequencer commands (UBSan).
set -e
cd "$(dirname "$0")/.."
mkdir -p build/render-out
${CC:-cc} -O1 -g -fsanitize=undefined -fno-sanitize-recover -Wall -Wextra -Isrc -Iplatform \
  tests/render.c src/synth.c src/seq.c -lm -o build/render
build/render build/render-out
