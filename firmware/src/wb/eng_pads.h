/* SPDX-License-Identifier: GPL-3.0-only */
/* PADS: six voices of a 'super' saw stack (1, 2, 3, 5 or 7 detuned saws spread left and right),
 * a low-pass per side, a swell envelope, and DISTANCE: the pad moves away (darker and quieter).
 *
 *   PP_SPREAD    detune of the stack, up to +-30 cents
 *   PP_VOICES    1 2 3 5 7 saws a voice
 *   PP_CUTOFF    low-pass cutoff        PP_RESO  its resonance
 *   PP_ATTACK    swell time             PP_RELEASE  release time
 *   PP_DISTANCE  lowers the cutoff and the level together
 *   PP_SHAPE     blends the saws toward a triangle 'glass' tone
 * Integers only. Per frame and voice: up to 7 saws, two SVFs, a few multiplies. Coefficients and the
 * envelope move once a block, the envelope is interpolated across the block. */
#pragma once
#include "dsp.h"
#include "types.h"

#define PADS_NV 6u
#define PADS_NSAW 7u
typedef struct {
    uint8_t note, gate, nsaw, active;
    uint32_t age;
    uint32_t ph[PADS_NSAW], inc[PADS_NSAW];
    int32_t env20;                 /* Q20: 1048576 = full */
    int32_t amp;                   /* Q15 target (velocity * 0.18) */
    int32_t f_now;                 /* slewed SVF f */
    svf_t fl, fr;
    uint8_t shape;
} pads_voice_t;
static pads_voice_t pads_v[PADS_NV];
static uint32_t pads_ages;

static const uint8_t PADS_COUNT[5] = {1, 2, 3, 5, 7};
static const int8_t PADS_OFF[PADS_NSAW] = {0, -3, 3, -2, 2, -1, 1};   /* center first, then pairs; odd = left, even = right */
static const uint16_t PADS_NORM[PADS_NSAW + 1] = {0, 32767, 32767, 16384, 16384, 10922, 10922, 8192};  /* 1/(saws a side) */

static void pads_init(void)
{
    memset(pads_v, 0, sizeof pads_v);
    pads_ages = 0;
}

static void pads_start(pads_voice_t *v, uint8_t note, uint8_t vel, const uint8_t *p)
{
    uint32_t k, base = note_inc(note), spread = ((uint32_t)p[PP_SPREAD] * 30u) >> 8;   /* cents, 0..29 */
    v->note = note;
    v->gate = 1;
    v->active = 1;
    v->age = ++pads_ages;
    v->nsaw = PADS_COUNT[(p[PP_VOICES] * 5u) >> 8];
    v->shape = p[PP_SHAPE];
    v->amp = (vel_q15(vel) * 5898) >> 15;      /* 0.18 of full scale at vel 127 */
    for (k = 0; k < PADS_NSAW; k++) {
        int32_t c = ((int32_t)PADS_OFF[k] * (int32_t)spread) / 3;
        v->inc[k] = inc_cents(base, c);
        if (!v->active || v->env20 == 0)
            v->ph[k] = rnd_isr();              /* free phases: no stacked-saw click */
    }
    v->f_now = svf_f(p[PP_CUTOFF]);
}

static void pads_note_on(uint8_t note, uint8_t vel, const uint8_t *p, uint8_t slice)
{
    uint32_t i, pick = 0, best = 0xFFFFFFFFu;
    (void)slice;
    for (i = 0; i < PADS_NV; i++) {            /* same note, else free, else quietest release, else oldest */
        pads_voice_t *v = &pads_v[i];
        uint32_t score;
        if (v->active && v->note == note) { score = 0; }
        else if (!v->active) score = 1;
        else if (!v->gate) score = 0x10000000u + (uint32_t)(v->env20 >> 4);
        else score = 0x80000000u + v->age;
        if (score < best) { best = score; pick = i; }
    }
    {
        pads_voice_t *v = &pads_v[pick];
        uint32_t keep = v->active && v->env20 > 0;
        int32_t env = v->env20;
        pads_start(v, note, vel, p);
        v->env20 = keep ? env : 0;             /* a stolen voice swells from where it was */
    }
}

static void pads_note_off(uint8_t note)
{
    uint32_t i;
    for (i = 0; i < PADS_NV; i++)
        if (pads_v[i].active && pads_v[i].note == note)
            pads_v[i].gate = 0;
}

static void pads_all_off(void)
{
    uint32_t i;
    for (i = 0; i < PADS_NV; i++)
        pads_v[i].gate = 0;
}

static void pads_render(int32_t *out, uint32_t n)
{
    uint32_t i, vi, cut, dist;
    int32_t f_tgt, q, lvl_q15, att, rel_k;
    cut = P[T_PADS][PP_CUTOFF];
    dist = P[T_PADS][PP_DISTANCE];
    cut = (cut * (256u - dist)) >> 8;
    if (cut < 16u) cut = 16u;
    f_tgt = svf_f(cut);
    q = svf_q(P[T_PADS][PP_RESO]);
    lvl_q15 = 32767 - (int32_t)dist * 96;      /* 255 -> 0.25 */
    att = env_coef(P[T_PADS][PP_ATTACK]);      /* Q20 units a frame: 32767 = 32 frames, 1 = 23 s */
    rel_k = dec_shift(P[T_PADS][PP_RELEASE]);  /* env -= env >> k a block */
    for (vi = 0; vi < PADS_NV; vi++) {
        pads_voice_t *v = &pads_v[vi];
        int32_t e0, e1, de, e, normL, normR, amp, f, xs;
        uint32_t k, ns, k0;
        if (!v->active)
            continue;
        /* ---- once a block: the envelope */
        e0 = v->env20;
        if (v->gate) {
            e1 = e0 + att * (int32_t)n;
            if (e1 > 1048576) e1 = 1048576;
        } else {
            e1 = e0 - (e0 >> rel_k) - 64;      /* at least 64 a block: a slow shift never stalls above zero */
            if (e1 < 64) { e1 = 0; v->active = 0; }
        }
        v->env20 = e1;
        if (!e0 && !e1)
            continue;
        de = (e1 - e0) / (int32_t)n;           /* n is never 0 here */
        e = e0;
        v->f_now = slew(v->f_now, f_tgt, 800);
        f = v->f_now;
        ns = v->nsaw;
        k0 = ns == 2u ? 1u : 0u;               /* two saws: one a side, no center */
        normL = PADS_NORM[ns == 2u ? 1u : (ns + 1u) >> 1];   /* saws a side: center + odd (left) / even (right) */
        normR = normL;
        amp = (v->amp * lvl_q15) >> 15;
        xs = (int32_t)v->shape << 7;           /* 0..32640 */
        /* ---- per frame */
        for (i = 0; i < n; i++) {
            int32_t sl = 0, sr = 0, lp, bp, hp, g;
            for (k = k0; k < k0 + ns; k++) {
                int32_t s;
                v->ph[k] += v->inc[k];
                s = saw_q15(v->ph[k]);
                if (xs)
                    s += (((tri_q15(v->ph[k]) - s) * xs) >> 15);
                if (k == 0) { sl += s; sr += s; }
                else if (k & 1u) sl += s;
                else sr += s;
            }
            sl = (sl * normL) >> 15;
            sr = (sr * normR) >> 15;
            svf_run(&v->fl, sl, f, q, &lp, &bp, &hp);
            sl = SAT15(lp);
            svf_run(&v->fr, sr, f, q, &lp, &bp, &hp);
            sr = SAT15(lp);
            e += de;
            g = ((e >> 5) * amp) >> 15;        /* Q15 */
            out[2u * i] += (sl * g) >> 15;
            out[2u * i + 1u] += (sr * g) >> 15;
        }
    }
}
