# WitchBox manual

## The panel

| Control | Does |
| --- | --- |
| **ALGORITHM** encoder | selects the track (GHOST, SUB, PADS, KEYS, DRUMS, WEATHER) |
| **PRESETS** encoder | selects the pattern A..H (the change waits for the bar) |
| **SELECT** encoder | page-specific: the lane (DRUMS), the step page, the song row, the COVEN column page, tempo on HOME |
| **KNOB 1..4** | the four values shown at the bottom of the screen |
| **OCT- / OCT+** | the row of knobs on pages with rows; the octave on HOME; the generator on GEN; the step page on STEPS |
| **PLAY** | start / stop. **SEL on HOME** restarts from the top |
| **REC** | tap: record live keys into the pattern. Held: shift |
| **HOME EDIT SEQ ENV LFO FX GLO ARP SAVE** | the pages (see below) |
| **MASTER** | the volume |

## Pages

### HOME (HOME)
The mixer: six tracks with meters and levels, the duck meter, the seed. Knobs: BPM, SWING, LOOSE, the master
low-pass. White keys play the current track; HOME held + white keys 1..6 mute tracks. Black keys are the
macros: **1 DROP**, **2 BUILD**, **3 BREAK**, **4 FILL** (next bar), **5 THROW** (hold: the echo feeds back
forever), **6 CATCH** (3 s of the master into the ghost tape), **7 SONG / PATTERN mode**.

### SOUND (EDIT)
The track's 16 parameters in four rows (OCT-/OCT+): its eight sound parameters, then LEVEL, PAN, DUST, ECHO,
SPACE, DUCK, FILT (low-pass below 128, open at 128, high-pass above), SWING (per track).

| Track | Sound parameters |
| --- | --- |
| GHOST | MODE (CHOP below 128, SING above), PITCH (±12 semitones), FORMANT (apart from pitch), GATE, STRETCH, DROPOUT, VOWEL, BREATH |
| SUB | SHAPE (sine → triangle → square), DECAY (0 holds), GLIDE, REESE, CUTOFF, WOBBLE rate (0 off, then 1/1 .. 1/32), DEPTH, DRIVE |
| PADS | SPREAD, VOICES (1, 2, 3, 5, 7 saws), CUTOFF, RESO, ATTACK, RELEASE, DISTANCE, SHAPE |
| KEYS | PATCH (8), RATIO, INDEX, DECAY, FEEDBACK, BRIGHT, RELEASE, DETUNE |
| DRUMS | KIT (DUSTY, PUNCH, STEEL), KICK TUNE, KICK DECAY, SNARE TONE, SNARE SNAP, HAT DECAY, FOUND pitch, LATE SNARE |
| WEATHER | RAIN, CRACKLE, HISS, DRONE, TONE, DENSITY, WIDTH, FIELD (the tape, scrubbed) |

Keys play the track. On GHOST in CHOP mode the white keys are the 16 slices, the black keys play slice 1 at
other pitches. On DRUMS the white keys 1..8 are the lanes. On WEATHER the first four white keys are events.

### STEPS (SEQ)
The 16 steps of the current page on the white keys (OCT-/OCT+ or SELECT: pages 1..4). A white key toggles a
step. **Hold a step** and the knobs become its NOTE (or SLICE on GHOST), VEL, MICRO (±23 ticks, a 16th is 24),
CHANCE; with REC held: CONDITION (always, fill, not fill, 1:2, 2:2, 1:4, 4:4, first), RATCHET (0..3), THROW,
ACCENT. Without a step held: LENGTH (1..64 per track, per lane: polymeter), DIV (1/16, 1/8, 1/32), SHIFT
(rotate), CLEAR (turn left). Black keys 1..8 pick the pattern. On PADS and KEYS a step's chord shape is on
the step's SLICE value (NOTE, MIN, MAJ, MIN7, MAJ7, SUS4, SUS2, MIN9, 5TH, ...).

With REC on and the transport running, keys and MIDI notes are written to the nearest step.

### LOOSE (ENV)
Row 0: LOOSE (the global scale of every drift), SWING, LATE SNARE (ticks the snare and rim drag), FEEL
(2-STEP / HALF). Rows 1..4: the DRIFT of every track and every lane, 0..100. The drift is a random walk per
playhead, bounded, re-rolled on every hit; the kick on beat one is never moved; hats lean early; the snare
lanes drag by LATE SNARE. Swing pushes the off 16ths; the 2-STEP feel also pushes the "and" of 2 and 4.

### GEN (LFO)
The track's generator (OCT-/OCT+ picks one the track supports), its four knobs, **SEL casts** it once,
**ARP** toggles AUTO (recast every bar; a `*` shows on HOME), **REC+SEL undoes** the last cast.

| Generator | Knobs A, B | Tracks |
| --- | --- | --- |
| SÉANCE | DRIFT (how often the chord moves), GRAVITY (pull toward the tonic); DENSITY = chords per bar | PADS, KEYS |
| HAUNT | GAP (silence after a phrase), ECHO (repeat the last fragment, shifted); learns from the slices already placed | GHOST |
| HEX | LOCK (0 always new .. 255 frozen), LENGTH (2..16) | all but WEATHER (DRUMS: one lane or ALL, on SELECT) |
| EUCLID | ROTATE, SLOPE (the second half louder or softer); DENSITY scales the feel template | DRUMS |
| MUTATE | FLIP (chance a step toggles), WANDER (chance a note / slice moves); DENSITY = how many steps | all but WEATHER |
| CONTOUR | SHAPE (RISE, FALL, ARC, VALE, FLAT), RANGE; on SUB: SLIDE chance, and it follows the PADS roots | KEYS, SUB, GHOST |
| ANSWER | DELAY (1 or 2 bars), SPREAD (how far the reply moves) | GHOST (reads KEYS) |

SEED (knob 4) and the project seed on HOME make a cast repeatable. Everything a generator writes is ordinary
steps: edit them on STEPS.

### FX (FX)
Four rows: DUST (BITS, RATE, HISS) and TAPE (WOW, SAT); ECHO (TIME in divisions, FB, TONE); SPACE (SIZE, DAMP,
PRE-DELAY) and DUCK (RELEASE); DUCK DEPTH, GLUE, the master LOW-PASS, WIDTH.

### COVEN (GLO)
The 6×6 grid. A row is a source track (OCT-/OCT+), the knobs set the spell on four targets (SELECT flips to the
last two): TRIG (the target plays its last note / a rim, 35 %), MUTE (the target skips a step), DUCK (keys the
duck), UP / DOWN (the target's next note an octave away), SEED (re-rolls the chances), THROW. Default: DRUMS
ducks PADS.

### SUMMON (ARP)
MOOD (NIGHT, RAIN, CHURCH, TUNNEL), ROOT, SCALE, BPM. **SEL** writes the sounds, patterns A..D and the song
(INTRO A 4 bars, BUILD B 4, DROP C 8, BREAK D 4, DROP C 8, OUTRO A 4). **REC+SEL** keeps the drums and
rerolls the rest. Turn SONG mode on (HOME, black key 7) and press PLAY.

### SONG (SAVE)
SLOT 1 or 2 (knob 4). **SEL saves** the project (patterns, song, every parameter, the generators) into the
slot; **REC+SEL loads** it. The transport stops first: a save erases flash with the sound off. Rows
(SELECT): the song, each row a pattern, a section (INTRO / BUILD / DROP / BREAK / OUTRO: the weather and the
conditions follow it), and bars.

## MIDI
USB-MIDI in: channels 1..6 play the tracks (10 or 5 = drums, GM notes 36..51), CC 16..23 set the track's
eight sound parameters, start / stop. Out: MIDI clock while playing.

## The ghost tape
3 s, mono, 22.05 kHz, in RAM. At boot the formant singer sings 16 phrases into it so GHOST has chops at
once. CATCH (HOME, black key 6) records the master into it instead; the slices are re-cut on transients.
WEATHER's FIELD scrubs the same tape slowly as a found-sound bed.
