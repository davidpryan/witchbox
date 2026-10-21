/* SPDX-License-Identifier: GPL-3.0-only */
/* WitchBox DRUMS: an eight-lane 2-step kit, fully synthesized, one voice a lane.
 *
 *   KICK   sine with a fast exponential pitch sweep, a click, soft clipped
 *   SNARE  tuned body + high-passed noise snap + a quiet three-burst clap layer
 *   RIM    a band-passed noise tick + a 1 kHz ping
 *   HAT    six inharmonic squares, high-passed, short (closed) / long (open, choked by closed)
 *   CLICK  a 2 ms burst through a resonant band-pass at a pitch that wanders around PD_FOUND_PITCH
 *   SHAKE  noise with an AM envelope through a band-pass near 5 kHz
 *   CRASH  long bright noise, wide
 *
 * Amplitudes are Q24 that decay by a Q15 multiplier a frame: tau in ms -> mul = 32768 - 743 / ms.
 * PD_KIT: 0 DUSTY (darker, lower, softer), 1 PUNCH (tighter), 2 STEEL (brighter). */
#pragma once
#include "dsp.h"
#include "types.h"

#define DR_Q24 16777216
#define DR_MUL(ms) (32768 - 743 / ((ms) < 1 ? 1 : (ms)))      /* a per-frame decay for a time constant in ms */
#define DR_A15(a) ((a) >> 9)                                 /* Q24 -> Q15 */
static inline int32_t dr_dec(int32_t a, int32_t mul) { return (int32_t)(((int64_t)a * mul) >> 15); }

typedef struct {
    int32_t amp, amul;           /* main envelope (Q24, per-frame multiplier) */
    int32_t amp2, amul2;         /* a second envelope (snap, click, open ring) */
    uint32_t ph, inc;            /* an oscillator */
    int32_t sweep, smul;         /* kick: extra inc that decays */
    uint32_t ph2[6], inc2[6];    /* hat cluster / clap bursts */
    uint32_t age;                /* frames since the hit */
    int32_t gl, gr;              /* pan gains Q12 */
    int32_t level;               /* velocity level Q15 */
    svf_t svf;
    hp1_t hp;
    lp1_t lp;
    int32_t f, q, hpk;           /* filter coefficients for the hit */
    int32_t extra;               /* lane specific: clap burst index, crash seed, ... */
    uint8_t active;
} dr_lane_t;

static dr_lane_t dr[WB_NLANE];
static uint8_t dr_kit;

static const uint32_t DR_HAT_RATIO[6] = {    /* Q12 ratios of an 808-ish cluster */
    4096, 5497, 5043, 6771, 7996, 8698};

static void drums_init(void)
{
    uint32_t i;
    memset(dr, 0, sizeof dr);
    for (i = 0; i < WB_NLANE; i++) { dr[i].gl = 4096; dr[i].gr = 4096; }
    dr[L_HATC].gl = 3600; dr[L_HATC].gr = 4096;
    dr[L_HATO].gl = 3600; dr[L_HATO].gr = 4096;
    dr[L_FOUND1].gl = 4096; dr[L_FOUND1].gr = 3400;
    dr[L_FOUND2].gl = 4096; dr[L_FOUND2].gr = 3000;
    dr[L_CRASH].gl = 4096; dr[L_CRASH].gr = 4096;
}

static void drums_all_off(void)
{
    uint32_t i;
    for (i = 0; i < WB_NLANE; i++) { dr[i].active = 0; dr[i].amp = 0; dr[i].amp2 = 0; }
}

/* the kit scales: a time (ms) and a brightness (a cutoff in Hz) */
static inline int32_t dr_kit_ms(int32_t ms) { return dr_kit == 1 ? (ms * 3) >> 2 : dr_kit == 0 ? (ms * 5) >> 2 : ms; }
static inline uint32_t dr_kit_hz(uint32_t hz) { return dr_kit == 2 ? (hz * 3) >> 1 : dr_kit == 0 ? (hz * 3) >> 2 : hz; }

static void drums_hit(uint8_t lane, uint8_t vel, const uint8_t *p)
{
    dr_lane_t *l;
    int32_t lvl;
    uint32_t i;
    if (lane >= WB_NLANE) return;
    l = &dr[lane];
    dr_kit = (uint8_t)((p[PD_KIT] * 3u) >> 8);
    lvl = vel_q15(vel);
    l->age = 0;
    l->active = 1;
    l->amp = DR_Q24;
    l->amp2 = DR_Q24;
    l->ph = 0;
    l->extra = 0;
    memset(&l->svf, 0, sizeof l->svf);
    switch (lane) {
    case L_KICK: {
        uint32_t base = 35u + ((p[PD_KICK_TUNE] * 45u) >> 8);           /* 35..80 Hz */
        base = dr_kit == 0 ? (base * 7) >> 3 : base;
        l->inc = base * 97391u;                                           /* Hz * 2^32 / 44100 */
        l->sweep = (int32_t)(l->inc * 3u);                                /* from 4x down */
        l->smul = DR_MUL(dr_kit == 1 ? 12 : 22);
        l->amul = DR_MUL(dr_kit_ms(40 + ((p[PD_KICK_DECAY] * 300) >> 8)));
        l->amul2 = DR_MUL(1);                                             /* the click */
        l->level = (lvl * 23000) >> 15;
        break; }
    case L_SNARE: {
        uint32_t tone = 170u + ((p[PD_SNARE_TONE] * 90u) >> 8);          /* 170..260 Hz */
        l->inc = tone * 97391u;
        l->sweep = (int32_t)(l->inc >> 1);
        l->smul = DR_MUL(8);
        l->amul = DR_MUL(dr_kit_ms(28));                                  /* the body */
        l->amul2 = DR_MUL(dr_kit_ms(25 + ((p[PD_SNARE_SNAP] * 110) >> 8)));   /* the snap */
        l->hpk = lp1_k(dr_kit_hz(900));
        l->f = svf_f_hz(dr_kit_hz(1400)); l->q = svf_q(120);
        l->level = (lvl * 14500) >> 15;
        for (i = 0; i < 3u; i++) l->ph2[i] = 0;
        break; }
    case L_RIM:
        l->inc = 1000u * 97391u;
        l->amul = DR_MUL(4);
        l->amul2 = DR_MUL(3);
        l->f = svf_f_hz(dr_kit_hz(2200)); l->q = svf_q(170);
        l->level = (lvl * 9000) >> 15;
        break;
    case L_HATC:
    case L_HATO: {
        uint32_t basehz = dr_kit_hz(420);
        uint32_t ms = lane == L_HATC ? 12u + ((p[PD_HAT_DECAY] * 70u) >> 8) : 150u + ((p[PD_HAT_DECAY] * 700u) >> 8);
        for (i = 0; i < 6u; i++) { l->inc2[i] = (uint32_t)(((uint64_t)(basehz * 97391u) * DR_HAT_RATIO[i]) >> 12); l->ph2[i] = i * 0x2A000000u; }
        l->amul = DR_MUL(dr_kit_ms((int32_t)ms));
        l->hpk = lp1_k(dr_kit_hz(2200));
        l->level = (lvl * (dr_kit == 2 ? 9000 : 7000)) >> 15;
        if (lane == L_HATC && dr[L_HATO].active) dr[L_HATO].amul = DR_MUL(3);   /* choke */
        break; }
    case L_FOUND1: {
        uint32_t hz = 1200u + ((p[PD_FOUND_PITCH] * 4000u) >> 8);
        hz += (rnd_isr() % (hz >> 1)) - (hz >> 2);                        /* each one a little different */
        l->f = svf_f_hz(hz); l->q = svf_q(215);
        l->amul = DR_MUL(1);                                              /* 2 ms burst; the resonance rings */
        l->amul2 = DR_MUL(18);
        l->level = (lvl * 6500) >> 15;
        break; }
    case L_FOUND2:
        l->f = svf_f_hz(5000); l->q = svf_q(90);
        l->amul = DR_MUL(dr_kit_ms(45));
        l->level = (lvl * 7000) >> 15;
        l->amp = 0;                                                       /* AM: rises first */
        break;
    case L_CRASH:
        l->amul = DR_MUL(dr_kit_ms(900));
        l->hpk = lp1_k(dr_kit_hz(1500));
        l->level = (lvl * 7500) >> 15;
        break;
    default: break;
    }
}

static void drums_render(int32_t *out, uint32_t n)
{
    uint32_t i, lane;
    for (lane = 0; lane < WB_NLANE; lane++) {
        dr_lane_t *l = &dr[lane];
        int32_t gl = l->gl, gr = l->gr, lvl = l->level;
        if (!l->active) continue;
        switch (lane) {
        case L_KICK:
            for (i = 0; i < n; i++) {
                int32_t s, a15 = DR_A15(l->amp);
                l->ph += l->inc + (uint32_t)l->sweep;
                l->sweep = dr_dec(l->sweep, l->smul);
                s = (sin_q15(l->ph) * a15) >> 15;
                s += (white_isr() * DR_A15(l->amp2)) >> 16;               /* the click */
                s = softclip((s * 5) >> 2);
                s = (s * lvl) >> 15;
                out[2 * i] += (s * gl) >> 12;
                out[2 * i + 1] += (s * gr) >> 12;
                l->amp = dr_dec(l->amp, l->amul);
                l->amp2 = dr_dec(l->amp2, l->amul2);
            }
            if (DR_A15(l->amp) < 8) l->active = 0;
            break;
        case L_SNARE:
            for (i = 0; i < n; i++) {
                int32_t s, nz, lp, bp, hp, clap = 0, a15 = DR_A15(l->amp), a2 = DR_A15(l->amp2);
                uint32_t age = l->age + i;
                l->ph += l->inc + (uint32_t)l->sweep;
                l->sweep = dr_dec(l->sweep, l->smul);
                s = (tri_q15(l->ph) * a15) >> 15;
                nz = white_isr();
                s += (hp1(&l->hp, nz, l->hpk) * a2) >> 14;
                /* the clap layer: three bursts 10 ms apart through the band-pass, then a short tail */
                if (age < 1323u) {
                    uint32_t ph = age % 441u;                                  /* 10 ms */
                    int32_t w = (int32_t)(441u - ph) * 74;                      /* 32634 .. 0 */
                    clap = (nz * w) >> 15;
                } else if (age < 4000u)
                    clap = (nz * (int32_t)(4000u - age)) / 2677;              /* a tail (constant divisor) */
                svf_run(&l->svf, clap, l->f, l->q, &lp, &bp, &hp);
                s += bp >> 2;
                s = (s * lvl) >> 15;
                s = softclip(s);
                out[2 * i] += (s * gl) >> 12;
                out[2 * i + 1] += (s * gr) >> 12;
                l->amp = dr_dec(l->amp, l->amul);
                l->amp2 = dr_dec(l->amp2, l->amul2);
            }
            if (DR_A15(l->amp) < 8 && DR_A15(l->amp2) < 8 && l->age > 4000u) l->active = 0;
            break;
        case L_RIM:
            for (i = 0; i < n; i++) {
                int32_t s, lp, bp, hp;
                l->ph += l->inc;
                svf_run(&l->svf, (white_isr() * DR_A15(l->amp2)) >> 15, l->f, l->q, &lp, &bp, &hp);
                s = bp + ((sin_q15(l->ph) * DR_A15(l->amp)) >> 15);
                s = (s * lvl) >> 15;
                out[2 * i] += (s * gl) >> 12;
                out[2 * i + 1] += (s * gr) >> 12;
                l->amp = dr_dec(l->amp, l->amul);
                l->amp2 = dr_dec(l->amp2, l->amul2);
            }
            if (DR_A15(l->amp) < 8 && l->age > 2000u) l->active = 0;
            break;
        case L_HATC:
        case L_HATO:
            for (i = 0; i < n; i++) {
                int32_t s = 0, k;
                for (k = 0; k < 6; k++) {
                    l->ph2[k] += l->inc2[k];
                    s += (int32_t)(l->ph2[k] >> 31) ? 5000 : -5000;
                }
                s ^= (int32_t)((l->ph2[0] ^ l->ph2[3]) >> 31) ? 3000 : 0;
                s = hp1(&l->hp, s, l->hpk);
                s = (s * DR_A15(l->amp)) >> 15;
                s = (s * lvl) >> 15;
                out[2 * i] += (s * gl) >> 12;
                out[2 * i + 1] += (s * gr) >> 12;
                l->amp = dr_dec(l->amp, l->amul);
            }
            if (DR_A15(l->amp) < 8) l->active = 0;
            break;
        case L_FOUND1:
            for (i = 0; i < n; i++) {
                int32_t s, lp, bp, hp;
                svf_run(&l->svf, (white_isr() * DR_A15(l->amp)) >> 14, l->f, l->q, &lp, &bp, &hp);
                s = (bp * DR_A15(l->amp2)) >> 15;
                s = (s * lvl) >> 15;
                out[2 * i] += (s * gl) >> 12;
                out[2 * i + 1] += (s * gr) >> 12;
                l->amp = dr_dec(l->amp, l->amul);
                l->amp2 = dr_dec(l->amp2, l->amul2);
            }
            if (DR_A15(l->amp2) < 8) l->active = 0;
            break;
        case L_FOUND2:
            for (i = 0; i < n; i++) {
                int32_t s, lp, bp, hp;
                uint32_t age = l->age + i;
                if (age < 441u) l->amp = (int32_t)(age * (DR_Q24 / 441u));    /* 10 ms rise */
                else l->amp = dr_dec(l->amp, l->amul);
                svf_run(&l->svf, white_isr(), l->f, l->q, &lp, &bp, &hp);
                s = (bp * DR_A15(l->amp)) >> 15;
                s = (s * lvl) >> 15;
                out[2 * i] += (s * gl) >> 12;
                out[2 * i + 1] += (s * gr) >> 12;
            }
            if (l->age > 441u && DR_A15(l->amp) < 8) l->active = 0;
            break;
        case L_CRASH:
            for (i = 0; i < n; i++) {
                int32_t a = white_isr(), b = white_isr(), m, s1, s2, a15 = DR_A15(l->amp);
                m = hp1(&l->hp, a, l->hpk);
                s1 = (((m + (b >> 1)) * a15) >> 15) * lvl >> 15;
                s2 = (((m - (b >> 1)) * a15) >> 15) * lvl >> 15;
                out[2 * i] += (s1 * gl) >> 12;
                out[2 * i + 1] += (s2 * gr) >> 12;
                l->amp = dr_dec(l->amp, l->amul);
            }
            if (DR_A15(l->amp) < 8) l->active = 0;
            break;
        default:
            l->active = 0;
            break;
        }
        l->age += n;
    }
}
