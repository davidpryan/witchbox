#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Sanity-check a host render: not silent, not stuck at the rails, a level in a plausible range.
   tools/wavcheck.py build/host/witchbox.wav   (exit 1 on a problem)"""
import array, sys, wave

path = sys.argv[1] if len(sys.argv) > 1 else "build/host/witchbox.wav"
w = wave.open(path, "rb")
n = w.getnframes()
a = array.array("h", w.readframes(n))
w.close()
if sys.byteorder != "little":
    a.byteswap()
if n < 44100:
    sys.exit(f"{path}: too short ({n} frames)")
peak = max(abs(v) for v in a)
clipped = sum(1 for v in a if abs(v) >= 32700)
sq = 0
for v in a[::4]:
    sq += v * v
rms = int((sq / max(1, len(a) // 4)) ** 0.5)
# a quiet second anywhere? (per second RMS)
secs = []
for s in range(n // 44100):
    seg = a[s * 88200:(s + 1) * 88200]
    ss = 0
    for v in seg[::8]:
        ss += v * v
    secs.append(int((ss / max(1, len(seg) // 8)) ** 0.5))
print(f"{path}: {n} frames, {n / 44100:.1f} s, peak {peak}, rms {rms}, clipped samples {clipped}")
print("per second rms:", " ".join(str(v) for v in secs))
problems = []
if rms < 150:
    problems.append("too quiet")
if clipped > n // 200:
    problems.append("clipping more than 0.5 % of the time")
if max(secs[2:]) < 200:
    problems.append("nothing audible after the first two seconds")
if problems:
    sys.exit("PROBLEMS: " + ", ".join(problems))
print("ok")
