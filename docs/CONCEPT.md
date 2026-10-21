# M-VAVE FM-1: open firmware survey and WitchBox concept

Date: 2026-10-21. The device is the **M-VAVE FM-1** (often misspelled "M-Wave"), a ~$60–80 pocket 6-op FM synth. Since mid-2026 it has become the most hacked cheap synth on the market: the community hub lists well over a hundred firmwares, and on 17 Oct 2026 M-VAVE itself released an official open firmware + SDK ("FM1 Open").

---

## 1. The hardware you are writing for

| Item | Fact | Source |
|---|---|---|
| SoC | JieLi AC791N (pi32v2 ISA), dual core | AL-255/FM-1-RE, aroum hub |
| Clock | up to 320 MHz (AC7911B datasheet) | datasheet via search |
| RAM | ~578 KB on-chip SRAM (secondary source, unverified) | Chinese comparison article |
| FPU | datasheet says FPU; Dinghy's RULES.md says **no FPU, no libgcc** in practice. Plan on fixed-point DSP. | zvenson/dinghy |
| Flash | 1 MB SPI, XIP at 0x02000000, single bank (UBOOT / App / VM partitions). Code space is tight: Ghoulbox reported only 4.5→23.8 KB free. | aroum hub, FM-1-RE, Ghoulbox |
| Sample storage | ~500 KiB usable (PIGEON); official firmware gives ~19.5 s across 8 slots; Ravel uses a 120 KB capture tape | PIGEON, m-vave/fm1_open, Ravel |
| Audio | 44.1 kHz stereo out, 16-bit; USB audio in at 44.1/48 k (SLOOP platform); 2 DAC channels | Dinghy, SLOOP |
| Display | 240×240 RGB565 LCD | aroum hub |
| Input | 27 capacitive keys (16 white / 11 black) with velocity + aftertouch (via "Velo"), 4 knobs, 7 encoders, 14 buttons, MASTER knob | Dinghy, MONK-1 |
| MIDI | USB MIDI class, TRS MIDI in on some firmwares, BLE MIDI in stock | SLOOP, fm1_open |
| Dual core | Second pi32v2 core usable for voice rendering (Melodee findings, Jangada mailbox layout, SLOOP 2.7, X0X) | X0X README |
| Recovery | Hold OCT- at power-on → USB rescue (Felucca platform); two crash loops → auto-rescue; RP2040 "Transporter" dongle for hard bricks | aroum hub, Dinghy |
| Stock engine | Dexed/msfa-derived 6-op FM | FM-1-RE |

Important caveat from FM-1-RE: the update protocol was reverse-engineered and is not a *proven* recovery path on the single-bank layout. Community rescue modes exist in the Felucca platform, but every project says "flash at your own risk".

---

## 2. Firmware landscape (what exists, Oct 2026)

### 2.1 Platforms you can build on

| Base | What it gives you | License | Hardware-proven? | Notes |
|---|---|---|---|---|
| **Felucca** (Hügelton / Leo Kuroshita) | Full platform: audio ISR, USB, update loader, web installer, storage, 14 engines (ANALOG, FM6, PHASE, LOFI, SAMPLE, VOICE, TRIO, WHEEL, GRAIN, PHYS, NOISE, SLICE, DRUM), sequencer, FX | GPL-3.0 (engines MIT) | Yes, the reference | Maintainer stepped back; ~80% of community firmwares fork this |
| **SLOOP** (isod89) | Felucca + 4-track groovebox, 64-step p-lock sequencer, dual-core (2.7), USB audio in, DUST/DUCK/FILT master, 27 punch-in FX, web editor | GPL-3.0 | Yes (beta) | Second-largest family |
| **Dinghy** (zvenson) | Minimal skeleton on SLOOP's platform: `app_audio()` stereo callback, screen primitives, keys/knobs by name, USB MIDI, 256-byte settings, crash screen, host-mode renderer, build.sh with Docker on macOS, RAM-overflow checks | GPL-3.0 | Yes | **Best starting point for an original-engine firmware** |
| **FM1 Open** (M-VAVE official) | 8-track groovebox, DX7 (128 voices, SysEx in/out), 808 drums, sampler (8 slots, ~19.5 s), 303/PSG/pluck, browser Workbench + flasher | App GPLv3 + SDK linking exception; **engine is a binary blob** | Yes | Windows-only toolchain; opaque DSP; good for "stock-plus", bad for custom DSP |
| **Lunar Modulator** (ip2k) | Mutable Instruments Plaits-style macro engines, 8-track Movy-style sequencer, mod rack, WASM simulator | MIT (+GPL switch) | **No** (never run on hardware) | Research quality, beautiful but unproven |
| **Optimist** (w0ts) | Modular builder that assembles firmware images to flash/RAM budgets | — | WIP | Interesting for later |
| **OpenIFW**, **fm1-emu**, **fm1-emulator** | WASM host platform, browser AC791N emulator (Joakim Eriksson), Rust desktop emulator | various | n/a | Test without hardware |

### 2.2 Firmwares with pieces relevant to this concept

| Firmware | Why it matters here | Status |
|---|---|---|
| **X0X** (Charles Vestal) | 909/808/two Open303s, **BB Gen 8-slice breakbeat re-arranger**, shared reverb + tape delay, master compressor, parts with independent lengths, graceful quality degradation under CPU load, knob recording | Beta, solid |
| **Ravel FM-1** (De-Pattern) | **120 KB always-listening capture tape**, resample bars on the downbeat, µ-law half-rate "dirty" mode, MLR playheads, Beads-style grain delay (½ s, 12-bit), WOBBLE wow/flutter delay, 12-BIT reverb, 8×8 influence grid | Builds, **not yet run on hardware** |
| **PIGEON** (The Shrimp Blimp) | Sampler on 500 KiB flash, **IMA ADPCM / mono MP3 storage**, USB recording at 22.05 kHz mono, 32 chops per song with per-chop pitch/reverse/env, browser sample tool | Hardware checklist complete |
| **Loom FM-1** (Co-oscillator) | GRAIN + **SCRUB tape-platter engine**, **sidechain ducking from drum track**, tape stop / freeze / reverse / harmonizer performance FX, chord progression generator | v1.5.1, few users |
| **DUB-1** (sbegueria, fork of AMB-1) | Generative dub: weighted random-walk chord engine stacked in thirds, **tape echo with push/hold "throw"**, **Dattorro plate**, kick that ducks everything, **vinyl crackle bed**, 8-bar auto-arrangement | Working, hardware |
| **GHOULBOX** (Jason Persinger) | **TAPE fx: wow, flutter, saturation, hiss**; CRSH bit/rate reduction; HALL reverb that reuses the delay buffer (no extra RAM); ambience layer (wind/crackle); DREAM generative composer | v2.3.2, hardware |
| **Jangada** (zednaked) | Master DUST/DUCK/FILT/TAPE/HUM, 3 reverbs incl. plate, 4-slot mod matrix, dual-core measurements | hardware |
| **MONK-1** (Charles Vestal) | **FOF formant vocal synth**, 12 characters, 5 vowels on Catmull-Rom splines, head size/breath, 12 KB per voice after optimisation, half-rate int16 echo | hardware, emulator |
| **Felucca VOICE / FM6 / SLICE** | formant vowel oscillator, Dexed 6-op with .syx import, slice-per-key sampler | hardware |
| **Groove OS** (Peter Gombos, $29, closed) | VA with "super" mode (7 detuned copies), 8 tracks, per-step chance, punch-in FX (dive, lift, wash). Shows the demand for a groovebox UI; not forkable | commercial |
| **Baud Girl FM-1+VA** (closed) | Stock ergonomics overhaul + VA engine | commercial/free, closed |
| **BREAK//174**, **zp12**, **April OS**, **SLICE64-FM** | Jungle amen generator, 12-bit sampling drum machine, 32-slot sampling groovebox, 64-slice sampler | mixed |
| **fm1-x0x-rmx**, **Skiff**, **TRIBE**, **Hortator** | generic groovebox / drum machine alternatives | mixed |

### 2.3 What nobody has built yet

- A firmware whose **timing engine** is the point: off-grid, human, 2-step swing per lane. Every sequencer in the ecosystem is grid-first with "humanize" bolted on.
- A **vocal-chop-first** sampler: pitch and formant decoupled per slice, gated/dropout tails, reverb throw per step. PIGEON and SLICE64 are generic choppers; MONK-1 sings but doesn't chop.
- **Weather as an instrument**: rain, crackle, hiss and field loops as a mixed, sequenced, ducked track rather than an on/off ambience toggle.
- A dubstep **drop macro**: half-time switch, sub in, filter open, duck on, one gesture.

That gap is the concept.

---

## 3. Concept: WitchBox

> A six-track groovebox for Burial-style 2-step and melodic dubstep: ghost vocals, sub, dusty chords, swung drums that never sit on the grid, and weather.

### 3.1 Design principles

1. **Opinionated, not generic.** Fixed track roles like X0X, not "14 engines on any track". Fewer engines means code fits in the ~1 MB flash and the UI can be purpose-built.
2. **Time is the instrument.** The sequencer's "LOOSE" system (per-lane drift, swing, late snares) is the headline feature, not a sub-menu.
3. **Everything goes through dust.** The master chain (DUST → TAPE → SPACE → DUCK → GLUE) is always on; the dry sound is the exception.
4. **Two feels, one box.** A FEEL switch: `2-STEP` (Burial, 130–140 BPM, garage shuffle) and `HALF` (melodic dubstep, 140–150 BPM, snare on 3, half-time drop).
5. **Generate, then edit.** Every generator writes ordinary steps, so a happy accident can be frozen and hand-tuned.
6. **Build on the proven platform.** Fork **Dinghy** (SLOOP platform layer: audio ISR, USB rescue, installer, USB audio in, dual core). GPL-3.0. Reuse DSP from X0X, Ravel, Loom, DUB-1, Ghoulbox, MONK-1 and Felucca where licenses allow (all GPL-3.0 compatible).

### 3.2 Track layout

| # | Track | Engine | Burial/melodic-dubstep role |
|---|---|---|---|
| 1 | **GHOST** | Chop sampler + formant | Pitched-up/down R&B vocal fragments. Modes: `CHOP` (ADPCM user sample, auto-sliced by transient or grid, one slice per key, per-slice semitone ±12, **formant shift decoupled from pitch**, reverse, gate length, "DROPOUT" chance that mutes mid-slice, stretch-grain artifact knob), `SING` (MONK-1 FOF vowels driven by the pattern, for synthetic "oooh" lines when you have no sample). Per-step reverb throw. |
| 2 | **SUB** | Sine/triangle sub + optional REESE layer | Pure slide-able sub with 808-style decay and glide. `REESE` adds two detuned saws → lowpass for melodic-dubstep drops. Tempo-synced WOBBLE LFO with rate on a knob (1/1 … 1/32, triplets) and "RATE-ON-KEY" per step. |
| 3 | **PADS** | 2-osc VA (super mode up to 7 copies) + FM6 (Dexed) pads | Dusty minor/sus chords. Fixed "SWELL" envelope, built-in lowpass with "DISTANCE" macro (LP + reverb send + width), chord memory from black keys (4-note voicings, moves as a shape like DUB-1). Primary duck target. |
| 4 | **KEYS** | FM6 (Dexed, .syx import) + VA pluck | E-piano, bell, pluck leads, piano stabs. Arp with octave range. Secondary duck target. |
| 5 | **DRUMS** | 8-lane kit, synthesised + ADPCM one-shots | KICK, SNARE A (clap-layered), SNARE B (rim/ghost), HAT C, HAT O, FOUND 1–2 (clicks, lighter, shaker, bottle), CRASH/RIDE. BB-Gen style **break re-arranger** on an ADPCM loop as an alternate mode. |
| 6 | **WEATHER** | Generative texture bed | RAIN (filtered noise + Poisson droplet impulses + distant rumble), CRACKLE (vinyl, DUB-1 style), HISS (tape), FIELD (user loop at 11 kHz µ-law, slow-scrubbed like Loom SCRUB), DRONE (sine cluster). Density, tone, stereo width. Ducked lightly. Always running unless muted. |

Voice budget: ~12 voices total, matching Ghoulbox/Groove-OS experience on this chip. Core 1 renders FM6 + VA (PADS, KEYS, REESE); core 0 does sampler, formant, drums, weather, FX and UI.

### 3.3 The LOOSE timing system (the signature)

Burial famously arranged drums by ear in a wave editor with no grid. Emulate the result, not the method:

- **Per-lane DRIFT** (0–100): random timing offset per hit, drawn from a slow-moving random walk so it feels like a player, not jitter. Snare lanes default late (+8 to +20 ms), hats default early.
- **Per-track SWING** with the 2-step template (push the "and" of 2 and 4, not every 16th).
- **LATE SNARE**: dedicated knob on the DRUMS page for the 2-step "dragging" snare feel.
- **MICRO** per step (±23 ticks at 96 PPQ) for hand-placed ghost notes.
- **STEP 1 anchor**: the kick on beat 1 is never drifted so the loop still locks to MIDI clock.
- **FLAM/DOUBLE**: ghost double-snare with velocity ratio.
- Velocity randomisation per lane, with a "crackle" velocity floor so quiet hits turn into clicks.

Sequencer: 16-step pages, 1–64 steps per track, independent lengths per track (polymetric like X0X/Ravel), per-step p-locks on any knob, chance, ratchet, conditions (fill, 1:4, not-first). HALF-TIME toggle re-maps the drum template without re-entering notes.

### 3.4 Master chain (fixed order)

```
tracks ─▶ per-track sends ─▶ DUST ─▶ TAPE ─▶ ECHO ─▶ SPACE ─▶ DUCK ─▶ GLUE ─▶ out
```

| Stage | Behaviour | Borrow from |
|---|---|---|
| **DUST** | Bit-depth 16→6, sample-rate 44.1k→6k with proper aliasing, hiss floor, "VINYL" (crackle + 60 Hz hum + gentle LP) | Ghoulbox CRSH, Jangada DUST/HUM |
| **TAPE** | Wow (0.5–2 Hz), flutter (6–12 Hz), saturation, head-bump EQ, hiss | Ghoulbox TAPE, Ravel WOBBLE |
| **ECHO** | Tempo-synced tape delay (dotted 1/8 default), feedback with LP in the loop, **THROW** (momentary send, hold = infinite feedback), half-rate int16 buffer to save RAM | DUB-1 push/hold, MONK-1 half-rate echo |
| **SPACE** | Dattorro plate with pre-delay up to 120 ms, "DISTANCE" damping, optional 12-BIT mode, freeze. Reuses the echo buffer tail like Ghoulbox HALL to save RAM. | DUB-1, Ravel, Ghoulbox |
| **DUCK** | Sidechain envelope keyed from KICK lane (or external MIDI note), per-track amount (PADS 100 %, SUB 60 %, WEATHER 30 %, GHOST 0 % by default), attack/release, "PUMP" curve | Loom, Jangada DUCK, X0X comp |
| **GLUE** | Master compressor + soft limiter + optional half-time filter sweep macro | X0X |

### 3.5 Song and drop macros

- Patterns A–H per track; **sections** INTRO / BUILD / DROP / BREAK / OUTRO chained into a 32-row arrangement.
- **DROP macro** (one key): HALF-TIME on, SUB unmute, REESE on, PADS filter open, DUCK on, SPACE pre-delay up, GLUE harder.
- **BUILD macro**: snare-roll generator (ratchet density ramps over N bars), noise riser into SPACE, ECHO feedback ramps, filter closes.
- **BREAK macro**: drums mute except FOUND lanes, WEATHER density up, GHOST reverb throw.
- 2-STEP feel ignores DROP by default (Burial tracks don't "drop"); it uses BREAK/RETURN instead.

### 3.6 Generative sequencing

Every track has a GEN page. The rule everywhere: generators write into the normal step pattern, so anything they produce can be frozen, edited by hand, or undone. Nothing is a black box that only plays live.

| Generator | Track(s) | What it does | Borrowed from |
|---|---|---|---|
| **SÉANCE** (chord walk) | PADS, KEYS | Weighted random walk over scale degrees, biased toward the tonic and toward minor/sus voicings; chords stacked in thirds, voicings move as a shape so changes slide. DRIFT sets how often it moves, GRAVITY how hard it pulls home. Writes one chord per bar (or per step with HOLD). | DUB-1 |
| **HAUNT** (vocal phrase Markov) | GHOST | Builds a first-order Markov chain from the slices you have already placed and generates new phrases from it. GAP inserts rests (the Burial silence-between-fragments feel), ECHO repeats the last fragment pitched ±2 semitones, STRAY picks a slice outside the chain. Learns from edits, so hand-placing three good slices steers it. | new |
| **HEX** (locked random) | all | Turing-machine style shift register per track: a looping random sequence of notes/triggers with a LOCK knob (0 = always new, 100 = frozen). Lengths 2–32. Works for drum lanes, sub notes and FOUND clicks. | Music Thing Turing Machine idea |
| **EUCLID** | DRUMS (per lane), WEATHER | Euclidean hits per lane with ROTATE and a DENSITY slope across the pattern. FOUND lanes default to sparse, rotated Euclids so clicks never land with the kick. | DUB-1, PIGEON |
| **MUTATE** | all | Per bar, change N steps (velocity, micro-timing, slice, note) with a MEMORY depth so it can walk back to the last state you liked. SHIFT+MUTATE = dice (full reroll), SHIFT+UNDO = revert. | Groove OS dice, Ghoulbox EVOL |
| **COVEN** (influence grid) | all | 6×6 matrix: a track can TRIG, MUTE, DUCK, TRANSPOSE or RESEED another. Default spells: KICK → DUCK PADS; SNARE B → TRIG a GHOST slice at chance 20 %; GHOST rest → WEATHER density up. | Ravel 8×8 grid |
| **CONTOUR** (melody) | KEYS, SUB | Scale-constrained melody with a chosen shape (RISE, FALL, ARC, VALE, FLAT) and a RANGE in semitones; note lengths from a rhythm pool (dotted 8ths, offbeat 16ths). SUB mode follows the SÉANCE roots with a slide chance. | new |
| **ANSWER** (call and response) | GHOST ↔ KEYS | After a KEYS phrase, GHOST replies in the next bar with the same contour on random slices, pitched to the scale. Phrase length and REPLY delay set per pair. | new |
| **SUMMON** | whole project | Generates a full piece in a key and mood (NIGHT, RAIN, CHURCH, TUNNEL): seeds SÉANCE, CONTOUR, EUCLID, HAUNT and WEATHER, builds patterns A–D and an 8-section arrangement with BUILD/DROP/BREAK already placed. One encoder turn = new piece, HOLD = keep drums and reroll the rest. | Ghoulbox DREAM |
| **WEATHER drift** | WEATHER | Density and tone follow slow Perlin-style noise and respond to the arrangement section (thick in BREAK, thin in DROP), with an occasional thunder/rumble event at low chance. | new |

Generative options plug into the LOOSE system rather than bypass it: anything a generator writes still gets per-lane drift, swing and late-snare treatment, so generated patterns feel played rather than computed.

**UI**: GEN is a page per track with four knobs (the generator's two main parameters, DENSITY, and SEED). The ARP button doubles as GEN run/stop for the selected track; SHIFT+ARP freezes the current output into the pattern. A global SEED is shown on HOME and saved with the project so a piece can be recreated.

### 3.7 Sampling workflow

- Sources: USB audio in (22.05 kHz mono, like PIGEON) or browser upload tool (any file → mono IMA ADPCM at 22.05 k; vocal slots optionally 32 k).
- Budget on ~500 KiB flash at 22.05 kHz ADPCM (~11 KB/s): ~45 s total. Default split: GHOST 20 s (2 songs × 32 chops), DRUMS 15 s (one-shots + one break), FIELD 10 s.
- **CAPTURE TAPE** (from Ravel): a 120 KB RAM ring buffer always listening to the master; `CATCH` freezes it into a GHOST slot so you can resample your own pads into vocal-like chops.
- No live time-stretch (CPU). Instead a `STRETCH` knob fakes the artifact with 20–40 ms grain repeat, which is closer to the early-2000s software sound anyway.

### 3.8 UI

- Keys: 16 white = steps / drum lanes / chord notes depending on layer; 11 black = track select, pattern select, scene launch.
- Knobs: 4 per page, layered by holding a section button (SLOOP model). Encoders: SELECT (page), ALGORITHM (track), PRESETS (pattern).
- Pages: HOME (6-row mixer with rain animation and duck meter), GHOST, SUB, PADS, KEYS, DRUMS (lane grid with drift readout), WEATHER, FX, LOOSE, GEN (per track), COVEN, SONG, SETUP.
- Look: near-black background, desaturated amber/teal text, rain streaks on idle, VT-style mono font. Screen-reader hooks like DUB-1 and PIGEON.
- MIDI: clock in/out, per-track channel, CC map for all knobs, DUCK key from external note.

### 3.9 Memory and CPU sketch (fixed-point, int16 buffers)

| Block | RAM |
|---|---|
| Capture tape | 120 KB |
| ECHO (1 s stereo, 22.05 k, int16, shared with SPACE tail) | ~88 KB |
| SPACE plate state | ~40 KB |
| GHOST voices (4 × 12 KB formant, slice playback streams from flash XIP) | ~50 KB |
| FM6 (4 voices) + VA (6 voices) | ~40 KB |
| Drums, weather, UI, framebuffer strip, stack | remainder |

Total under ~400 KB, leaving headroom against the unverified ~578 KB figure. Flash: must drop every engine not listed above; measure against Ghoulbox's 23.8 KB headroom early and keep the DX7 voice bank small (32 voices, user-loadable).

### 3.10 Build plan

| Phase | Deliverable | Reuse |
|---|---|---|
| 0 | Fork Dinghy; host renderer + WASM build (OpenIFW / fm1-emu); dual-core job register from Melodee/Jangada; CI with reference renders | Dinghy, SLOOP 2.7 |
| 1 | DRUMS + SUB + LOOSE sequencer + DUCK + GLUE. This alone should already sound like a 2-step sketch. | X0X drums/comp, Loom duck |
| 2 | GHOST chop sampler, ADPCM storage + browser upload, formant shift, SING mode | PIGEON pipeline, Felucca SLICE, MONK-1 FOF |
| 3 | PADS + KEYS (VA super, FM6), DUST/TAPE/ECHO/SPACE | Felucca FM6/ANALOG, Ghoulbox TAPE, DUB-1 plate/echo |
| 4 | WEATHER generator, capture tape, song mode, drop/build/break macros | Ravel tape, DUB-1 crackle/arrangement |
| 4b | Generative layer: HEX, EUCLID, MUTATE first (cheap), then SÉANCE/CONTOUR/HAUNT/ANSWER, COVEN grid, SUMMON last | DUB-1 chord walk, Ghoulbox DREAM, Ravel grid |
| 5 | Hardware soak on two units, rescue path verified (OCT- boot, crash-loop auto-rescue), web installer with "return to V15", manual | Felucca installer |

### 3.11 Risks

- **Bricking**: single-bank flash, update protocol not a proven recovery. Mitigate: build on the Felucca platform's rescue mode, keep an RP2040 Transporter dongle on the bench, test the installer's stock-restore path first.
- **CPU**: plate reverb + 4 formant voices + 10 FM/VA voices may exceed the budget at 44.1 k. Mitigate: half-rate FX buffers, X0X-style graceful degradation, dual core from day one.
- **Flash size**: 1 MB total with code, DX7 bank and ~500 KiB of samples. Mitigate: no unused engines, ADPCM only, sample budget enforced by the upload tool.
- **No FPU in practice**: all DSP in Q15/Q31. Borrow from projects that already run on hardware rather than porting float code from Mutable.
- **Sample licensing**: ship only CC0 one-shots and synthesised drums; vocals are user-supplied.
- **Felucca maintainer stepped back**: platform fixes may need to come from the SLOOP/Dinghy side.

---

## Sources

- [Synthtopia: Alt firmware options for the M-Wave FM-1](https://www.synthtopia.com/content/2026/10/04/alt-firmware-options-for-the-m-wave-fm-1-pocket-synthesizer/)
- [Synthtopia: M-VAVE officially opens FM-1 firmware with Workbench and SDK](https://www.synthtopia.com/content/2026/10/20/m-vave-officially-opens-fm-1-firmware-to-developers-with-new-workbench-sdk/)
- [Synth Anatomy: FM-1 review](https://synthanatomy.com/2026/07/m-vave-fm-1-review-low-budget-pocket-fm-ynthesizer-with-iconic-sounds.html)
- [Synth Anatomy: FM-1 Open](https://synthanatomy.com/2026/10/m-vave-fm-1-open-the-green-light-for-custom-firmware-and-community-hacks.html)
- [Synth Anatomy: Groove OS](https://synthanatomy.com/2026/10/groove-os-turns-the-m-vave-fm-1-into-an-8-track-groovebox.html)
- [aroum/fm1-custom-fw community hub](https://github.com/aroum/fm1-custom-fw)
- [m-vave/fm1_open (official)](https://github.com/m-vave/fm1_open)
- [AL-255/FM-1-RE reverse engineering](https://github.com/AL-255/FM-1-RE)
- [Hacker News thread on the reverse engineering](https://news.ycombinator.com/item?id=49994065)
- [hugelton/Felucca](https://github.com/hugelton/Felucca) and [Engines wiki](https://github.com/hugelton/Felucca/wiki/Engines)
- [isod89/sloop-fm1](https://github.com/isod89/sloop-fm1)
- [zvenson/dinghy](https://github.com/zvenson/dinghy)
- [ip2k/lunar-modulator](https://github.com/ip2k/mvave-fm1-open-firmware)
- [charlesvestal/fm1-x0x](https://github.com/charlesvestal/fm1-x0x), [fm1-monk](https://github.com/charlesvestal/fm1-monk), [FoMni](https://charlesvestal.github.io/fm1-fomni/emu/)
- [De-Pattern/Ravel_FM1](https://github.com/De-Pattern/Ravel_FM1)
- [TheShrimpBlimp/pigeon](https://github.com/TheShrimpBlimp/pigeon)
- [Co-oscillator/Loom-FM-1](https://github.com/Co-oscillator/Loom-FM-1)
- [sbegueria/fm1-dub (DUB-1)](https://github.com/sbegueria/fm1-dub)
- [jasonpersinger/ghoulbox-fm1-dungeon-synth](https://github.com/jasonpersinger/ghoulbox-fm1-dungeon-synth)
- [zednaked/jangada](https://github.com/zednaked/jangada)
- [AntonioAltea/fmel-1](https://github.com/AntonioAltea/fmel-1)
- [FM1 Firmware Hub comparison](https://fm1firmwarehub.online/), [fm1-editor.com firmware list](https://fm1-editor.com/firmware/)
- [Synthtopia: FoMni Omnichord firmware](https://www.synthtopia.com/?p=155230), [Matrixsynth](https://www.matrixsynth.com/2026/10/new-firmware-turns-fm-1-into-omnichord.html)
- [Piano & Synth Magazine: FM-1 custom firmware collection](https://pianoandsynth.com/m-vave-fm-1-custom-firmware-collection/)
- [AC7911B datasheet (LCSC)](https://atta.szlcsc.com/upload/public/pdf/source/20241209/7B2C0C261932781E1CA5530337098F4C.pdf), [AC791N vs AC792N comparison (CSDN)](https://gitcode.csdn.net/69e9857554b52172bc6bb671.html)
