/* SPDX-License-Identifier: GPL-3.0-only */
/* WitchBox GHOST: the vocal track. Two modes by PG_MODE.
 *
 *   CHOP (0..127)   four voices play slices of the ghost tape (wb_tape, 22.05 kHz mono). Pitch from the note
 *                   (60 = unity) and PG_PITCH (+-12 st). When PG_FORMANT is off centre or PG_STRETCH is on the
 *                   slice is read through two overlapping 20 ms grains: the grains play at the formant ratio,
 *                   the playhead that spawns them moves at the pitch ratio (slowed by STRETCH: the early-2000s
 *                   time-stretch artefact). PG_GATE cuts the slice short, PG_DROPOUT mutes it for 30..120 ms
 *                   somewhere in the middle, at a chance: the gap.
 *   SING (128..255) a formant singer, two voices: a glottal pulse with vibrato and jitter plus breath through
 *                   three resonators from a vowel table (A E I O U by PG_VOWEL), PG_FORMANT = head size,
 *                   portamento between notes.
 *
 * ghost_boot_choir() sings sixteen phrases into the tape at boot, so CHOP has vocal material out of the box;
 * ghost_reslice() finds sixteen slice boundaries in whatever the tape holds (transients, else equal parts). */
#pragma once
#include "dsp.h"
#include "types.h"

/* ---- the singer (used live at 44.1 kHz and by the boot choir at 22.05 kHz) */
typedef struct {
    uint32_t ph, inc, tinc, einc, vph;
    int32_t amp, target, astep, rstep;   /* Q15 envelope, attack / release steps per frame */
    int32_t y1[3], y2[3];                /* the resonators */
    int32_t a1[3], a2[3], g[3], fg[3];
    uint32_t key;                        /* the vowel/formant/rate the coefficients were made for */
    uint32_t age;
    uint8_t note, active;
} sg_voice_t;

static const uint16_t SG_F[5][3] = {{650, 1080, 2650}, {400, 1700, 2600}, {290, 1870, 2800}, {400, 800, 2600}, {350, 600, 2700}};
static const uint16_t SG_G[5][3] = {{32767, 16400, 14600}, {32767, 6500, 8200}, {32767, 5800, 4100}, {32767, 10300, 8200}, {32767, 3300, 4600}};
static const uint16_t SG_BW[3] = {80, 90, 120};

/* vowel 0..255 across A E I O U, formant 0..255 = head size (128 = 1.0, +-1 octave), half = 22.05 kHz */
static void sg_set_formants(sg_voice_t *v, uint32_t vowel, uint32_t formant, uint32_t half)
{
    uint32_t key = vowel | formant << 8 | half << 16, pos = (vowel * 4u), i = pos >> 8, fr = pos & 255u, k;
    int32_t cents = (((int32_t)formant - 128) * 1200) / 128;
    if (v->key == key) return;
    v->key = key;
    if (i > 3u) { i = 3u; fr = 255u; }
    for (k = 0; k < 3u; k++) {
        uint32_t f = SG_F[i][k] + (((int32_t)SG_F[i + 1][k] - (int32_t)SG_F[i][k]) * (int32_t)fr >> 8);
        int32_t gain = SG_G[i][k] + (((int32_t)SG_G[i + 1][k] - (int32_t)SG_G[i][k]) * (int32_t)fr >> 8);
        int32_t r, c, bw = SG_BW[k];
        f = inc_cents(f << 8, cents) >> 8;
        if (f > 5000u) f = 5000u;
        r = 32768 - (half ? (bw * 4669) / 1000 : (bw * 2334) / 1000);
        c = sin_q15((f * (half ? 194783u : 97391u)) + 0x40000000u);     /* cos w */
        v->a1[k] = (r * c) >> 15;                                       /* 2 r cos w, Q14 */
        v->a2[k] = -((r * r) >> 15);                                    /* -r^2, Q15 */
        v->g[k] = 32768 - r;
        v->fg[k] = gain;
    }
}

static void sg_block(sg_voice_t *v, uint32_t vibdepth)    /* once a block: glide, vibrato, jitter -> einc */
{
    int32_t lfo;
    uint32_t inc;
    if (!v->active) return;
    v->inc += ((int32_t)v->tinc - (int32_t)v->inc) >> 3;                /* portamento */
    v->vph += 5u * 97391u * WB_SUB;                                     /* 5 Hz, a block at a time */
    lfo = sin_q15(v->vph);
    inc = v->inc;
    inc = (uint32_t)((int32_t)inc + (((int32_t)(inc >> 7) * lfo >> 15) * (int32_t)vibdepth >> 8));   /* up to +-0.8 % */
    inc = (uint32_t)((int32_t)inc + ((int32_t)(inc >> 9) * (RNDS15(rnd_isr()) >> 8) >> 7));           /* jitter */
    v->einc = inc;
    v->age += WB_SUB;
}

static inline int32_t sg_frame(sg_voice_t *v, int32_t breath)
{
    uint32_t x;
    int32_t src, out = 0, k;
    v->ph += v->einc;
    x = v->ph >> 16;                                                     /* 0..65535 */
    src = (int32_t)(((65535u - x) * (65535u - x)) >> 17) - 10923;       /* a decaying pulse, no DC */
    src += (white_isr() * breath) >> 9;
    for (k = 0; k < 3; k++) {
        int32_t y = (src * v->g[k]) >> 15;
        y += (int32_t)(((int64_t)v->a1[k] * v->y1[k]) >> 14);
        y += (int32_t)(((int64_t)v->a2[k] * v->y2[k]) >> 15);
        y = CLAMP(y, -(1 << 20), 1 << 20);
        v->y2[k] = v->y1[k];
        v->y1[k] = y;
        out += (y * v->fg[k]) >> 16;
    }
    out += (white_isr() * breath) >> 12;                                 /* a little unfiltered breath */
    v->amp = slew(v->amp, v->target, v->target > v->amp ? v->astep : v->rstep);
    return (out * v->amp) >> 15;
}

/* ---- the chop voices */
#define GH_NV 4
typedef struct {
    uint8_t active, note, granular, muted;
    uint32_t start, end;                 /* tape samples */
    uint32_t pos;                        /* the playhead, Q16 tape samples */
    int32_t rate, grate;                 /* playhead and grain rates, Q16 tape samples a frame */
    struct { uint32_t pos; int32_t age; uint8_t on; } gr[2];
    int32_t spawn, glen, half, wstep;    /* grain scheduling, triangular window */
    int32_t amp, target, level;          /* Q15 fade, the velocity level */
    int32_t gate;                        /* frames left before the cut, -1 = none */
    int32_t drop_at, drop_len;           /* the gap */
    uint32_t age;
} gh_voice_t;

static gh_voice_t gh[GH_NV];
static sg_voice_t gs[2];
static uint32_t gh_ages;

static inline int32_t gh_read(uint32_t pos, uint32_t end)
{
    uint32_t idx = pos >> 16;
    int32_t t0, t1;
    if (idx + 1u >= end || idx + 1u >= WB_TAPE_LEN) return 0;
    t0 = wb_tape[idx];
    t1 = wb_tape[idx + 1u];
    return t0 + (((t1 - t0) * (int32_t)(pos & 0xFFFFu)) >> 16);
}

static void ghost_init(void)
{
    uint32_t i;
    memset(gh, 0, sizeof gh);
    memset(gs, 0, sizeof gs);
    for (i = 0; i < 17u; i++) wb_tape_slice[i] = (WB_TAPE_LEN * i) / 16u;
}

static void ghost_all_off(void)
{
    uint32_t i;
    for (i = 0; i < GH_NV; i++) gh[i].target = 0;
    for (i = 0; i < 2u; i++) gs[i].target = 0;
}

static void ghost_note_on(uint8_t note, uint8_t vel, const uint8_t *p, uint8_t slice)
{
    int32_t lvl = vel_q15(vel);
    if (p[PG_MODE] >= 128u) {                                            /* SING */
        uint32_t i, pick = 0, best = 0xFFFFFFFFu;
        for (i = 0; i < 2u; i++) {
            uint32_t score = gs[i].active && gs[i].note == note ? 0u : gs[i].active ? 0x80000000u - gs[i].age : (uint32_t)gs[i].amp;
            if (score < best) { best = score; pick = i; }
        }
        {
            sg_voice_t *v = &gs[pick];
            v->tinc = note_inc(note);
            if (!v->active || v->amp < 500) { v->inc = v->einc = v->tinc; v->ph = 0; }
            v->active = 1;
            v->note = note;
            v->age = 0;
            v->target = (lvl * 13000) >> 15;
            v->astep = 25;
            v->rstep = 4;
        }
        return;
    }
    {
        uint32_t i, pick = 0, best = 0xFFFFFFFFu, s = slice & 15u, start, end, formant = p[PG_FORMANT], stretch = p[PG_STRETCH];
        int32_t cents = (note - 60) * 100 + (((int32_t)p[PG_PITCH] - 128) * 1200) / 128, rate;
        gh_voice_t *v;
        for (i = 0; i < GH_NV; i++) {
            uint32_t score = !gh[i].active ? 0u : 0x80000000u - gh[i].age;
            if (score < best) { best = score; pick = i; }
        }
        v = &gh[pick];
        start = wb_tape_slice[s];
        end = wb_tape_slice[s + 1u];
        if (end > WB_TAPE_LEN) end = WB_TAPE_LEN;
        if (end <= start + 64u) { start = 0; end = WB_TAPE_LEN; }
        rate = (int32_t)(inc_cents(32768u << 4, cents) >> 4);
        rate = CLAMP(rate, 1024, 32768 * 8);
        v->active = 1;
        v->note = note;
        v->muted = 0;
        v->start = start;
        v->end = end;
        v->pos = start << 16;
        v->rate = (rate * 256) / (256 + (int32_t)stretch);
        v->grate = (int32_t)(inc_cents((uint32_t)rate << 4, (((int32_t)formant - 128) * 1200) / 128) >> 4);
        v->granular = (formant != 128u || stretch != 0u);
        v->glen = 882; v->half = 441; v->wstep = 74;
        v->spawn = 0;
        v->gr[0].on = v->gr[1].on = 0;
        v->amp = 0;
        v->level = (lvl * 16000) >> 15;
        v->target = v->level;
        v->age = 0;
        gh_ages++;
        {
            uint32_t frames = ((end - start) << 12) / (uint32_t)(v->rate >> 4 ? v->rate >> 4 : 1);
            v->gate = p[PG_GATE] ? (int32_t)((frames * p[PG_GATE]) >> 8) : -1;
            if (RND_PCT(rnd_isr()) < p[PG_DROPOUT] * 100u / 255u) {
                v->drop_at = (int32_t)((frames * (20u + rnd_isr() % 50u)) / 100u);
                v->drop_len = 1323 + (int32_t)(rnd_isr() % 3969u);
            } else {
                v->drop_at = -1;
                v->drop_len = 0;
            }
        }
    }
}

static void ghost_note_off(uint8_t note)
{
    uint32_t i;
    for (i = 0; i < GH_NV; i++)
        if (gh[i].active && gh[i].note == note) gh[i].target = 0;
    for (i = 0; i < 2u; i++)
        if (gs[i].active && gs[i].note == note) gs[i].target = 0;
}

static void ghost_render(int32_t *out, uint32_t n)
{
    uint32_t i, k;
    /* ---- SING */
    {
        uint32_t vowel = P[T_GHOST][PG_VOWEL], formant = P[T_GHOST][PG_FORMANT];
        int32_t breath = P[T_GHOST][PG_BREATH];
        for (k = 0; k < 2u; k++) {
            sg_voice_t *v = &gs[k];
            if (!v->active) continue;
            sg_set_formants(v, vowel, formant, 0);
            sg_block(v, 128);
            for (i = 0; i < n; i++) {
                int32_t s = sg_frame(v, breath);
                out[2 * i] += s;
                out[2 * i + 1] += s;
            }
            if (!v->target && v->amp == 0) v->active = 0;
        }
    }
    /* ---- CHOP */
    for (k = 0; k < GH_NV; k++) {
        gh_voice_t *v = &gh[k];
        uint32_t end;
        if (!v->active) continue;
        end = v->end;
        for (i = 0; i < n; i++) {
            int32_t s = 0, pan;
            if (v->gate > 0 && --v->gate == 0) v->target = 0;
            if (v->drop_at >= 0) {
                if (v->drop_at == 0) { v->muted = 1; v->drop_at = -1; }
                else v->drop_at--;
            }
            if (v->muted) {
                if (--v->drop_len <= 0) v->muted = 0;
            }
            if (!v->granular) {
                s = gh_read(v->pos, end);
                v->pos += (uint32_t)v->rate;
                if ((v->pos >> 16) + 1u >= end) v->target = 0;
            } else {
                int32_t g;
                if (--v->spawn <= 0 && (v->pos >> 16) + 1u < end) {
                    uint32_t slot = v->gr[0].on && !v->gr[1].on ? 1u : v->gr[0].on && v->gr[1].on ? (v->gr[0].age > v->gr[1].age ? 0u : 1u) : 0u;
                    v->gr[slot].on = 1;
                    v->gr[slot].age = 0;
                    v->gr[slot].pos = v->pos;
                    v->spawn = v->half;
                }
                for (g = 0; g < 2; g++)
                    if (v->gr[g].on) {
                        int32_t age = v->gr[g].age, w = age < v->half ? age * v->wstep : (v->glen - age) * v->wstep;
                        s += (gh_read(v->gr[g].pos, end) * w) >> 15;
                        v->gr[g].pos += (uint32_t)v->grate;
                        if (++v->gr[g].age >= v->glen) v->gr[g].on = 0;
                    }
                v->pos += (uint32_t)v->rate;
                if ((v->pos >> 16) + 1u >= end && !v->gr[0].on && !v->gr[1].on) v->target = 0;
            }
            v->amp = slew(v->amp, v->muted ? 0 : v->target, 150);
            s = (s * v->amp) >> 15;
            pan = (int32_t)k * 2000 - 3000;                               /* the four voices spread a little */
            out[2 * i] += (s * (4096 - pan)) >> 12;
            out[2 * i + 1] += (s * (4096 + pan)) >> 12;
        }
        v->age += n;
        if (!v->target && v->amp == 0) v->active = 0;
    }
}

/* ---- slices: sixteen boundaries from the tape's transients; what the transients do not give, the biggest
 * gaps are split in halves (main loop) */
static void ghost_reslice(void)
{
    enum { BL = 128, NB = WB_TAPE_LEN / BL };                            /* 516 blocks of 2.9 ms */
    static uint16_t env[NB];
    static uint8_t used[NB];
    uint32_t b, i, k, n = 1, mean = 0;
    uint32_t picks[17];
    for (b = 0; b < NB; b++) {
        uint32_t sum = 0;
        for (i = 0; i < BL; i++) { int32_t s = wb_tape[b * BL + i]; sum += (uint32_t)(s < 0 ? -s : s); }
        env[b] = (uint16_t)(sum >> 7);
        mean += env[b];
    }
    mean /= NB;
    memset(used, 0, sizeof used);
    picks[0] = 0;
    for (i = 0; i < 16u; i++) used[i] = 1;
    while (n < 16u) {                                                    /* the strongest rise first */
        uint32_t bestb = 0;
        int32_t best = (int32_t)(mean >> 3) + 20;
        for (b = 3; b < NB - 4u; b++) {
            int32_t hi, lo, score;
            if (used[b]) continue;
            hi = MAX(env[b + 1], MAX(env[b + 2], env[b + 3]));
            lo = MIN(env[b - 1], MIN(env[b - 2], env[b - 3]));
            score = hi - lo;
            if (score > best) { best = score; bestb = b; }
        }
        if (!bestb) break;
        picks[n++] = bestb;
        for (b = bestb > 16u ? bestb - 16u : 0; b < bestb + 16u && b < NB; b++) used[b] = 1;   /* at least 46 ms apart */
    }
    for (i = 1; i < n; i++)                                              /* sort ascending (16 items at most) */
        for (k = i; k > 0 && picks[k] < picks[k - 1]; k--) { uint32_t t = picks[k]; picks[k] = picks[k - 1]; picks[k - 1] = t; }
    picks[n] = NB;
    while (n < 16u) {                                                    /* split the biggest gap */
        uint32_t big = 0, gap = 0;
        for (i = 0; i < n; i++)
            if (picks[i + 1] - picks[i] > gap) { gap = picks[i + 1] - picks[i]; big = i; }
        for (i = n + 1; i > big + 1u; i--) picks[i] = picks[i - 1];
        picks[big + 1] = picks[big] + gap / 2u;
        n++;
    }
    for (i = 0; i < 16u; i++) wb_tape_slice[i] = (picks[i] > 1u ? picks[i] - 1u : 0u) * BL;   /* a block early: the attack */
    wb_tape_slice[16] = WB_TAPE_LEN;
}

/* ---- the boot choir: sixteen sung phrases into the tape at 22.05 kHz (main loop, before the audio uses it) */
static void ghost_boot_choir(void)
{
    static const uint8_t NOTE[16] = {57, 60, 64, 62, 67, 69, 64, 60, 55, 59, 62, 65, 69, 72, 67, 64};
    static const uint8_t VOWEL[16] = {0, 192, 255, 64, 0, 128, 192, 0, 255, 192, 64, 0, 192, 255, 128, 0};
    static const uint16_t LENMS[16] = {130, 180, 140, 230, 110, 160, 190, 130, 170, 100, 180, 150, 120, 210, 130, 160};
    sg_voice_t v;
    uint32_t pos = 0, ph;
    memset(&v, 0, sizeof v);
    for (ph = 0; ph < 16u; ph++) {
        uint32_t len = (LENMS[ph] * WB_TAPE_RATE) / 1000u, gap = 500u + (ph * 37u) % 300u, i;
        wb_tape_slice[ph] = pos;
        if (pos + len + gap > WB_TAPE_LEN) break;
        v.active = 1;
        v.key = 0;
        v.tinc = note_inc(NOTE[ph]) << 1;                                /* half the rate: twice the increment */
        v.inc = ph ? v.inc : v.tinc;
        v.einc = v.inc;
        v.target = 26000;
        v.astep = 48; v.rstep = 64;                                      /* ~25 ms / ~18 ms at 22.05 kHz: the gap is silent */
        v.amp = 0;
        v.age = 0;
        sg_set_formants(&v, VOWEL[ph], 128 + (ph & 3u) * 6u, 1);
        for (i = 0; i < len + gap; i++) {
            int32_t s;
            if ((i & (WB_SUB - 1u)) == 0) sg_block(&v, 160);
            if (i == len) v.target = 0;
            s = sg_frame(&v, 70);
            s = softclip(s);
            wb_tape[pos + i] = (int16_t)SAT15(s);
        }
        pos += len + gap;
    }
    for (; pos < WB_TAPE_LEN; pos++) wb_tape[pos] = 0;
    for (; ph < 16u; ph++) wb_tape_slice[ph] = wb_tape_slice[ph ? ph - 1u : 0];
    wb_tape_slice[16] = WB_TAPE_LEN;
}
