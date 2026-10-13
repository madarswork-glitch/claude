#!/bin/sh
# Instructions per frame of a simulator binary (valgrind/callgrind), setup excluded.
# Device frame time on the ESP32-S3 is roughly 4 ms + 10.6 ms per million, fitted
# against six Basics presets with published timings (within ~2 ms on all six).
ir() { valgrind --tool=callgrind --callgrind-out-file=/dev/null "$1" --frames "$2" 2>&1 | grep -o "Collected : [0-9]*" | grep -o "[0-9]*"; }
a=$(ir "$1" 10); b=$(ir "$1" 30)
python3 -c "n=($b-$a)/20; print(f'{n/1e6:.2f} M instr/frame  ~{4+10.6*n/1e6:.0f} ms on device')"
