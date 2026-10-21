/* SPDX-License-Identifier: GPL-3.0-only */
/* WitchBox data model: tracks, parameters, steps, patterns, and the contract between the sequencer
 * (ISR), the engines (ISR), the FX (ISR), the generators (main loop) and the UI (main loop).
 *
 * Threads: the audio ISR renders; the main loop edits. Shared data is written by one side only:
 *   main -> ISR   P[][] (params, bytes), patterns (bytes), transport requests, live note ring
 *   ISR -> main   wb_pos (where we are), meters, duck level
 * A byte or a 32-bit word is atomic; nothing larger crosses without a sequence number. */
#pragma once
#include <stdint.h>
#include "dsp.h"

/* ---- tracks */
enum { T_GHOST = 0, T_SUB, T_PADS, T_KEYS, T_DRUMS, T_WEATHER, WB_NTRACK };
static const char *const WB_TRACK_NAME[WB_NTRACK] = {"GHOST", "SUB", "PADS", "KEYS", "DRUMS", "WEATHER"};

/* ---- drum lanes (track T_DRUMS) */
enum { L_KICK = 0, L_SNARE, L_RIM, L_HATC, L_HATO, L_FOUND1, L_FOUND2, L_CRASH, WB_NLANE };
static const char *const WB_LANE_NAME[WB_NLANE] = {"KICK", "SNARE", "RIM", "HAT", "OPEN", "CLICK", "SHAKE", "CRASH"};

/* ---- parameters: 16 bytes a track (0..255), P[track][i]. Engines read them every block.
 * The first 8 of every track are its sound; 8..15 are mix / send / sequencing. Names per engine below. */
#define WB_NPAR 16
static volatile uint8_t P[WB_NTRACK][WB_NPAR];

/* common tail of every track */
enum { PC_LEVEL = 8, PC_PAN, PC_DUST, PC_ECHO, PC_SPACE, PC_DUCK, PC_FILT, PC_SWING };
/* GHOST: chop sampler + formant singer (eng_ghost.h) */
enum { PG_MODE = 0, PG_PITCH, PG_FORMANT, PG_GATE, PG_STRETCH, PG_DROPOUT, PG_VOWEL, PG_BREATH };
/* SUB: sine/triangle sub + reese layer (eng_sub.h) */
enum { PS_SHAPE = 0, PS_DECAY, PS_GLIDE, PS_REESE, PS_CUTOFF, PS_WOBBLE_RATE, PS_WOBBLE_DEPTH, PS_DRIVE };
/* PADS: super VA + swell (eng_pads.h) */
enum { PP_SPREAD = 0, PP_VOICES, PP_CUTOFF, PP_RESO, PP_ATTACK, PP_RELEASE, PP_DISTANCE, PP_SHAPE };
/* KEYS: 4-op FM keys (eng_keys.h) */
enum { PK_PATCH = 0, PK_RATIO, PK_INDEX, PK_DECAY, PK_FEEDBACK, PK_BRIGHT, PK_RELEASE, PK_DETUNE };
/* DRUMS: kit macros (eng_drums.h) */
enum { PD_KIT = 0, PD_KICK_TUNE, PD_KICK_DECAY, PD_SNARE_TONE, PD_SNARE_SNAP, PD_HAT_DECAY, PD_FOUND_PITCH, PD_LATE_SNARE };
/* WEATHER: the bed (eng_weather.h) */
enum { PW_RAIN = 0, PW_CRACKLE, PW_HISS, PW_DRONE, PW_TONE, PW_DENSITY, PW_WIDTH, PW_FIELD };

/* ---- the master FX: 16 bytes, FXP[i] (fx.h) */
#define WB_NFX 16
static volatile uint8_t FXP[WB_NFX];
enum { FX_DUST_BITS = 0, FX_DUST_RATE, FX_DUST_HISS, FX_TAPE_WOW, FX_TAPE_SAT, FX_ECHO_TIME, FX_ECHO_FB, FX_ECHO_TONE,
       FX_SPACE_SIZE, FX_SPACE_DAMP, FX_SPACE_PRE, FX_DUCK_REL, FX_DUCK_DEPTH, FX_GLUE, FX_MASTER_LP, FX_WIDTH };

/* ---- a step. 8 bytes. Pitched tracks use all; drum lanes use vel/micro/chance/flags (a lane row). */
typedef struct {
    uint8_t note;        /* MIDI note; 0 = rest. On GHOST: pitch offset, 60 = unity */
    uint8_t vel;         /* 1..127 */
    uint8_t len;         /* gate length in 16ths * 4 (4 = one 16th), 0 = tie to the next step */
    int8_t micro;        /* -23..23 ticks hand-placed offset (96 PPQ: a 16th is 24 ticks) */
    uint8_t chance;      /* 0..100 % (0 reads as 100 for an old pattern) */
    uint8_t flags;       /* bits 0-1 ratchet (0..3 extra hits), 2-4 condition, 5 slide, 6 accent, 7 throw */
    uint8_t slice;       /* GHOST: slice 0..15 of the tape. KEYS/PADS: chord shape index (0 = single note) */
    uint8_t pl;          /* a parameter lock: high nibble = param 0..7 of the track, low nibble = value / 16; 0xFF none */
} wb_step_t;
#define ST_RATCHET(f) ((f) & 3u)
#define ST_COND(f) (((f) >> 2) & 7u)
#define ST_SLIDE(f) (((f) >> 5) & 1u)
#define ST_ACCENT(f) (((f) >> 6) & 1u)
#define ST_THROW(f) (((f) >> 7) & 1u)
enum { COND_ALWAYS = 0, COND_FILL, COND_NOT_FILL, COND_1_2, COND_2_2, COND_1_4, COND_4_4, COND_FIRST };

#define WB_NSTEP 64
#define WB_NPAT 8
typedef struct {
    wb_step_t s[WB_NSTEP];
    uint8_t len;          /* 1..64 steps */
    uint8_t div;          /* step division: 0 = 1/16, 1 = 1/8, 2 = 1/32 */
    uint8_t pad[2];
} wb_trackpat_t;
typedef struct {
    wb_step_t s[WB_NSTEP];   /* per lane row: note = 0/1 hit, vel, micro, chance, flags */
    uint8_t len;
    uint8_t pad[3];
} wb_lanepat_t;
typedef struct {
    wb_trackpat_t t[WB_NTRACK - 2];   /* GHOST SUB PADS KEYS, indexed by track (T_WEATHER has no notes, T_DRUMS has lanes) */
    wb_lanepat_t lane[WB_NLANE];
} wb_pattern_t;
static wb_pattern_t PAT[WB_NPAT] WB_POOL;    /* 8 x 4712 B in the pool */

/* ---- the song: 32 rows of a pattern per track, a section type, repeats */
enum { SEC_INTRO = 0, SEC_BUILD, SEC_DROP, SEC_BREAK, SEC_OUTRO, WB_NSEC };
static const char *const WB_SEC_NAME[WB_NSEC] = {"INTRO", "BUILD", "DROP", "BREAK", "OUTRO"};
typedef struct {
    uint8_t pat;          /* pattern 0..7 played on every track (0xFF = end of song) */
    uint8_t section;      /* SEC_... what the macros do here */
    uint8_t bars;         /* how many bars this row lasts */
    uint8_t mute;         /* track mute bits for this row */
} wb_songrow_t;
#define WB_NSONG 32
static wb_songrow_t SONG[WB_NSONG];

/* ---- the global, saved state of a project (fits the settings sector together with the UI prefs) */
typedef struct {
    uint16_t bpm;         /* 60..200 */
    uint8_t feel;         /* 0 = 2-STEP, 1 = HALF */
    uint8_t swing;        /* 0..100 global swing */
    uint8_t loose;        /* 0..100 global drift scale */
    uint8_t late_snare;   /* 0..24 ticks */
    uint8_t root, scale;  /* 0..11, scale index */
    uint8_t mute;         /* track mute bits */
    uint8_t pattern;      /* the pattern playing when not in song mode */
    uint8_t song_mode;
    uint8_t lane_mute;    /* drum lane mute bits (BREAK) */
    uint8_t seed[4];      /* the global seed shown on HOME */
    uint8_t drift[WB_NTRACK + WB_NLANE];   /* per track and per lane drift 0..100 */
    uint8_t coven[WB_NTRACK][WB_NTRACK];   /* COVEN grid: row = source, col = target, value = spell */
} wb_global_t;
static wb_global_t G;

/* ---- COVEN spells (what a source does to a target when it fires) */
enum { SPELL_NONE = 0, SPELL_TRIG, SPELL_MUTE, SPELL_DUCK, SPELL_UP, SPELL_DOWN, SPELL_RESEED, SPELL_THROW, WB_NSPELL };
static const char *const WB_SPELL_NAME[WB_NSPELL] = {"-", "TRIG", "MUTE", "DUCK", "UP", "DOWN", "SEED", "THROW"};

/* ---- scales (semitone sets over 12), used by the generators and the key layout */
typedef struct { const char *name; uint16_t bits; } wb_scale_t;
static const wb_scale_t WB_SCALES[] = {
    {"MINOR", 0x5AD},  {"DORIAN", 0x6AD}, {"PHRYG", 0x5AB}, {"HARM-M", 0x9AD}, {"MAJOR", 0xAB5},
    {"PENT-M", 0x4A9}, {"BLUES", 0x4E9},  {"LYDIAN", 0xAD5}, {"MIXO", 0x6B5}, {"CHROM", 0xFFF},
};
#define WB_NSCALE ((uint32_t)LEN(WB_SCALES))
static inline int in_scale(uint32_t root, uint32_t scale, int32_t note)
{
    uint32_t d = (uint32_t)(((note - (int32_t)root) % 12 + 12) % 12);
    return (WB_SCALES[scale % WB_NSCALE].bits >> d) & 1u;
}
/* degree k (0 = root, may be negative) of the scale, as a MIDI note around `base` */
static inline int32_t scale_note(uint32_t root, uint32_t scale, int32_t base_oct, int32_t degree)
{
    uint16_t bits = WB_SCALES[scale % WB_NSCALE].bits;
    int32_t n = 0, oct, k, count = 0, i;
    for (i = 0; i < 12; i++) count += (bits >> i) & 1;
    if (!count) count = 1;
    oct = degree >= 0 ? degree / count : -((-degree + count - 1) / count);
    k = degree - oct * count;                  /* 0..count-1 */
    for (i = 0; i < 12; i++)
        if ((bits >> i) & 1) { if (!k) { n = i; break; } k--; }
    return 12 * (base_oct + oct) + (int32_t)root + n;
}

/* ---- chord shapes for PADS / KEYS steps (semitones above the step note; 0 ends) */
static const int8_t WB_CHORD[16][4] = {
    {0, 0, 0, 0}, {0, 3, 7, 0}, {0, 4, 7, 0}, {0, 3, 7, 10}, {0, 4, 7, 11}, {0, 5, 7, 0}, {0, 2, 7, 0}, {0, 3, 7, 14},
    {0, 7, 12, 0}, {0, 3, 10, 14}, {0, 4, 11, 14}, {0, 7, 10, 14}, {0, 5, 10, 0}, {0, 3, 8, 0}, {0, 4, 9, 0}, {0, 12, 0, 0}};
static const char *const WB_CHORD_NAME[16] = {"NOTE", "MIN", "MAJ", "MIN7", "MAJ7", "SUS4", "SUS2", "MIN9",
                                              "5TH", "M7-9", "MA7-9", "7SUS", "QUART", "MIN6", "MAJ6", "OCT"};

/* ---- transport, written by the ISR, read by the UI */
typedef struct {
    volatile uint32_t playing;   /* 1 while the sequencer runs */
    volatile uint32_t tick;      /* 96 PPQ ticks since start */
    volatile uint32_t bar;       /* bars since start */
    volatile uint32_t step16;    /* the 16th within the bar, 0..15 */
    volatile uint32_t song_row;  /* in song mode */
    volatile uint32_t section;   /* SEC_... now */
    volatile int32_t duck;       /* the duck envelope, Q15, for the meter */
    volatile uint32_t meter[WB_NTRACK];   /* peak per track, Q15, decays in the UI */
    volatile uint32_t isr_us;    /* the last block's render time, microseconds (0 on the host) */
} wb_pos_t;
static wb_pos_t wb_pos;
/* tempo, set by the main loop; frames per beat and per tick for the ISR */
static volatile uint32_t wb_fpb = 20000, wb_fpt_q8 = 53333;   /* frames per beat; frames per tick * 256 */
static volatile uint32_t wb_req;      /* bit requests from the UI to the ISR: */
enum { REQ_PLAY = 1, REQ_STOP = 2, REQ_FILL = 4, REQ_DROP = 8, REQ_BUILD = 16, REQ_BREAK = 32, REQ_RESTART = 64, REQ_CATCH = 128 };

/* ---- live notes from the keys / MIDI to the ISR: a ring, one writer (main), one reader (ISR) */
typedef struct { uint8_t track, note, vel, slice; } wb_live_t;
#define WB_LIVE_N 32
static wb_live_t wb_live[WB_LIVE_N];
static volatile uint32_t wb_live_w, wb_live_r;
static inline void live_push(uint32_t track, uint32_t note, uint32_t vel, uint32_t slice)
{
    uint32_t w = wb_live_w;
    if (w - wb_live_r >= WB_LIVE_N) return;
    wb_live[w % WB_LIVE_N].track = (uint8_t)track;
    wb_live[w % WB_LIVE_N].note = (uint8_t)note;
    wb_live[w % WB_LIVE_N].vel = (uint8_t)vel;
    wb_live[w % WB_LIVE_N].slice = (uint8_t)slice;
    wb_live_w = w + 1;
}

/* ---- the ghost tape: 3 s mono at 22.05 kHz in the pool. GHOST plays slices of it; the FX fill it on CATCH;
 * eng_ghost renders a sung choir into it at boot so the firmware sings out of the box */
#define WB_TAPE_RATE 22050u
#define WB_TAPE_LEN 66150u
static int16_t wb_tape[WB_TAPE_LEN] WB_POOL;
static volatile uint32_t wb_tape_slice[17];   /* slice boundaries (samples), 16 slices; [16] = end */
static volatile uint32_t wb_tape_rec;         /* 1: the FX are writing the master into the tape (CATCH) */
static volatile uint32_t wb_tape_wpos;

/* ---- the engine contract. Every engine is a header with static state and these functions:
 *   void X_init(void)                                     once, main loop, before audio
 *   void X_note_on(uint8_t note, uint8_t vel, const uint8_t *p, uint8_t slice)   ISR; p = the 8 sound params
 *                                                         after parameter locks; slice = step's slice byte
 *   void X_note_off(uint8_t note)                         ISR
 *   void X_all_off(void)                                  ISR
 *   void X_render(int32_t *out, uint32_t n)               ISR: ADD n stereo frames (L R L R) in Q15 into out
 *   DRUMS instead: drums_hit(uint8_t lane, uint8_t vel, const uint8_t *p)
 *   WEATHER has no notes: weather_event(uint32_t kind) for COVEN spells */

/* the master FX ask the sequencer for these */
static void fx_duck_trigger(uint32_t vel);              /* the kick fired (fx.h) */
static void fx_throw(uint32_t track);                   /* a step with the throw flag (fx.h) */

/* what the parameter lock byte means, applied on a copy of the track's sound params */
static inline void plock_apply(uint8_t *p8, uint8_t pl)
{
    if (pl == 0xFFu) return;
    p8[(pl >> 4) & 7u] = (uint8_t)((pl & 15u) * 17u);
}
