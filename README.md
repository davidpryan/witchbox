# WitchBox

**A six-track groovebox firmware for the M-VAVE FM-1, for Burial-style 2-step and melodic dubstep.**

Ghost vocals, a sliding sub, dusty chords, drums that never sit on the grid, rain. Everything runs through
dust, tape, echo and a plate. Generators write patterns you can edit. A whole piece is one button.

| Track | What it is |
| --- | --- |
| **GHOST** | A chop sampler on a 3 s tape (16 slices, pitch and formant apart, stretch artifacts, dropouts) and a formant singer. The tape sings a ghost choir at boot; CATCH records the master into it. |
| **SUB** | A sine/triangle sub with slides and an 808-style decay, a reese layer, a tempo-synced wobble. |
| **PADS** | A super-saw pad (1..7 detuned saws a voice), a swell envelope, and DISTANCE: the pad moves away. |
| **KEYS** | 4-operator FM: e-piano, bell, pluck, glass, organ, marimba, DX bass, soft. |
| **DRUMS** | An 8-lane synthesized 2-step kit: kick, snare, rim, hats, a click, a shaker, a crash. Three kit flavours. |
| **WEATHER** | Rain with droplets, vinyl crackle, hiss, a drone, and the tape scrubbed as a field recording. |

**LOOSE** is the point: every track and every drum lane has a drift (a slow random walk, never jitter),
there is a 2-step swing template, a late-snare knob, hand-placed micro timing, and the kick on beat one
stays anchored so MIDI clock still locks. A FEEL switch flips between 2-STEP (130–140 BPM, garage shuffle)
and HALF (snare on 3, the reese comes in on the drop).

**The master chain** is fixed and always on: DUST (bits, rate, hiss) → TAPE (wow, flutter, saturation) →
ECHO (tempo-synced, throwable, holdable) → SPACE (a plate with pre-delay) → DUCK (keyed from the kick,
per-track amounts) → GLUE (a compressor, the drop filter, width, a limiter).

**Generators** (GEN page, one per track, four knobs each, AUTO recasts every bar):
SÉANCE (a chord walk), HAUNT (Markov vocal phrases with gaps), HEX (a Turing machine), EUCLID (hits per
lane from the feel template), MUTATE (a few steps change; undo remembers), CONTOUR (a shaped melody; on SUB
it follows the pads), ANSWER (GHOST replies to KEYS). COVEN is a 6×6 grid where a track that fires can
trigger, mute, duck, transpose, reseed or throw another. SUMMON writes a whole piece in a mood (NIGHT,
RAIN, CHURCH, TUNNEL): patterns A–D and a song with INTRO / BUILD / DROP / BREAK.

**Macros** on the black keys of HOME: DROP, BUILD (four bars, then it drops by itself), BREAK, FILL,
THROW (held), CATCH, SONG mode.

> **Status: builds on the host, runs the demo script, renders audio. Not yet run on an FM-1.** The firmware
> is built in CI with JieLi's toolchain, but nobody has flashed it yet. It stands on the
> [Dinghy](https://github.com/zvenson/dinghy) platform (Felucca / SLOOP), which is proven on the hardware:
> the USB rescue, the updater, MIDI, audio and the screen are theirs. Flashing any firmware is at your own
> risk; read [docs/PLATFORM-RULES.md](docs/PLATFORM-RULES.md) first.

Concept and design notes: [docs/CONCEPT.md](docs/CONCEPT.md). The manual: [docs/MANUAL.md](docs/MANUAL.md).

## Build

### On your computer, no FM-1 needed

```sh
tests/run_host.sh                     # -> build/host/witchbox.wav and the screens as PNG
python3 tools/wavcheck.py build/host/witchbox.wav
```

`tests/host.c` is a fake FM-1: the same `witchbox.c` runs on it, driven by a script
([tests/demo.txt](tests/demo.txt): it summons a piece, plays it, visits every page, fires the macros).
Write your own script to try an idea; `shot` lines take screenshots.

### For the FM-1

The chip (JieLi AC791N, pi32v2) needs JieLi's clang, Linux x86-64 only (on macOS or Windows the build runs
it in Docker), and three files of the public AC79 SDK:

```sh
pip3 install Pillow mido python-rtmidi
tools/get_toolchain.sh                # JieLi's clang, to ~/.jieli/toolchain
tools/get_sdk.sh                      # the three SDK files, to ~/fw-AC79_AIoT_SDK (hashes checked)
./build.sh                            # -> build/witchbox.fwsc (fails on what would brick an FM-1)
python3 tools/fm1_install.py build/witchbox.fwsc    # over USB; asks before writing
```

### GitHub Actions

Every push builds the host render (and checks it is not silent or clipping) and then the `.fwsc` package
with the real toolchain; both are workflow artifacts. A tag `vX.Y` makes a release with the package attached.
The toolchain and the SDK files are cached between runs.

## Layout

```
firmware/src/witchbox.c      the firmware: includes the platform, then wb/, then app_audio / app_main
firmware/src/wb/dsp.h        fixed-point toolbox (sine, filters, envelopes, noise, pitch)
firmware/src/wb/types.h      tracks, params, steps, patterns, song, globals: the data model
firmware/src/wb/eng_*.h      the six engines
firmware/src/wb/fx.h         the master chain
firmware/src/wb/seq.h        the sequencer and LOOSE (audio ISR)
firmware/src/wb/gen.h        the generators, COVEN, SUMMON (main loop)
firmware/src/wb/project.h    projects in flash, preferences
firmware/src/wb/ui.h         pages, knobs, keys, drawing, MIDI
firmware/src/platform.c      Dinghy's wiring of the Felucca / SLOOP platform (boot, USB, updater, audio ISR)
firmware/src/*.c, hal/       the platform, unchanged
tests/host.c                 the fake FM-1; tests/demo.txt the demo script
tools/                       build.py, the package maker, the installer, the toolchain and SDK fetchers
docs/                        the concept, the manual, the platform's API and rules
```

## The chip's rules

Integers only (no FPU, no libgcc), no 64-bit division, 5.8 ms per 256 frames in the audio interrupt,
96 KB of RAM plus a 336 KB pool, 568 KB of code. `tests/check_sources.sh` greps for floats;
`./build.sh` fails on the rest. Details: [docs/PLATFORM-RULES.md](docs/PLATFORM-RULES.md).

## License

GPL-3.0-only. The platform is Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
([Felucca](https://github.com/hugelton/Felucca)), with [SLOOP](https://github.com/isod89/sloop-fm1)'s
changes (isod89), wired up by [Dinghy](https://github.com/zvenson/dinghy) (zvenson). WitchBox (everything
under `firmware/src/wb/`, `witchbox.c`, the scripts and docs that are new here) is 2026 WitchBox
contributors. See [LICENSING.md](LICENSING.md). Not made or endorsed by M-VAVE.
