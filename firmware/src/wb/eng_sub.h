/* SPDX-License-Identifier: GPL-3.0-only */
/* SUB: a monophonic sub bass (sine -> triangle -> soft square), 808-style decay or held, glide,
 * and a REESE layer (two detuned saws an octave up through a low-pass) with a tempo-synced WOBBLE.
 *
 *   PS_SHAPE        0 sine .. 128 triangle .. 255 soft square
 *   PS_DECAY        0 = hold while a key is down; else an exponential decay that ignores note-off
 *   PS_GLIDE        0 = none; else a pitch glide between notes (last-note priority)
 *   PS_REESE        level of the reese layer
 *   PS_CUTOFF       low-pass on the reese layer
 *   PS_WOBBLE_RATE  0 off, then 1/1 1/2 1/4 1/8 1/16 1/8T 1/32 of a beat (wb_fpb)
 *   PS_WOBBLE_DEPTH how far the wobble pulls the cutoff down
 *   PS_DRIVE        soft clip on the sum
 * Integers only. Per-frame: three oscillators, one SVF, a few multiplies. Coefficients once a block. */
#pragma once
#include "dsp.h"
#include "types.h"

#define SUB_NHELD 8u
static struct {
    uint8_t held[SUB_NHELD];
    uint32_t nheld;
    uint32_t gate;                 /* 1 while a key is down */
    uint8_t note;
    uint8_t decay;                 /* snapshot at note-on */
    uint32_t ph, rph0, rph1;       /* sub, reese a, reese b */
    int32_t inc_now, inc_tgt;      /* sub pitch (int32: a note's inc is < 2^31) */
    int32_t env;                   /* Q15 level now */
    int32_t amp;                   /* Q15 level target (velocity * 0.8) */
    uint32_t trig;                 /* a note-on waits to be applied to the envelope */
    svf_t svf;
    uint32_t lfo_ph;
    int32_t f_now;                 /* the slewed SVF f */
} sub_s;

static void sub_init(void)
{
    memset(&sub_s, 0, sizeof sub_s);
    sub_s.f_now = 2000;
}

static void sub_held_remove(uint32_t note)
{
    uint32_t i, j = 0;
    for (i = 0; i < sub_s.nheld; i++)
        if (sub_s.held[i] != note)
            sub_s.held[j++] = sub_s.held[i];
    sub_s.nheld = j;
}

static void sub_set_pitch(uint32_t note, uint32_t glide)
{
    int32_t tgt = (int32_t)(note_inc((int32_t)note) & 0x7FFFFFFFu);
    sub_s.note = (uint8_t)note;
    sub_s.inc_tgt = tgt;
    if (!glide || !sub_s.gate)
        sub_s.inc_now = tgt;
}

static void sub_note_on(uint8_t note, uint8_t vel, const uint8_t *p, uint8_t slice)
{
    (void)slice;
    if (note > 108u) note = 108u;              /* the reese sits an octave up: keep it in range */
    sub_held_remove(note);
    if (sub_s.nheld >= SUB_NHELD) {            /* full: drop the oldest */
        uint32_t i;
        for (i = 1; i < SUB_NHELD; i++) sub_s.held[i - 1] = sub_s.held[i];
        sub_s.nheld = SUB_NHELD - 1u;
    }
    sub_s.held[sub_s.nheld++] = note;
    sub_set_pitch(note, p[PS_GLIDE]);
    sub_s.gate = 1;
    sub_s.decay = p[PS_DECAY];
    sub_s.amp = (vel_q15(vel) * 26214) >> 15;  /* 0.8 of full scale at vel 127 */
    sub_s.trig = 1;
}

static void sub_note_off(uint8_t note)
{
    sub_held_remove(note);
    if (sub_s.nheld) {
        sub_set_pitch(sub_s.held[sub_s.nheld - 1u], P[T_SUB][PS_GLIDE]);
    } else {
        sub_s.gate = 0;
    }
}

static void sub_all_off(void)
{
    sub_s.nheld = 0;
    sub_s.gate = 0;
}

/* wobble divisions: frames per cycle = wb_fpb * num / den */
static const uint8_t SUB_WOB_NUM[8] = {0, 4, 2, 1, 1, 1, 1, 1};
static const uint8_t SUB_WOB_DEN[8] = {1, 1, 1, 1, 2, 4, 3, 8};

static void sub_render(int32_t *out, uint32_t n)
{
    uint32_t i, shape, cut, rate, depth, reese, drv, glide;
    int32_t x_lo, x_hi, env, amp, gk, mul, f, q, rlvl, gain, lfo, inc, rinc0, rinc1;
    uint32_t ph, rph0, rph1;
    if (!sub_s.env && !sub_s.trig && !sub_s.gate)
        return;                                /* silent: costs nothing */

    /* ---- once a block */
    shape = P[T_SUB][PS_SHAPE];
    cut = P[T_SUB][PS_CUTOFF];
    rate = P[T_SUB][PS_WOBBLE_RATE] >> 5;      /* 0..7 */
    depth = P[T_SUB][PS_WOBBLE_DEPTH];
    reese = P[T_SUB][PS_REESE];
    drv = P[T_SUB][PS_DRIVE];
    glide = P[T_SUB][PS_GLIDE];
    gk = glide ? env_coef(glide) : 32767;
    mul = dec_mul(sub_s.decay);
    if (rate) {
        uint32_t fpc = (wb_fpb * SUB_WOB_NUM[rate]) / SUB_WOB_DEN[rate];
        if (fpc < 64u) fpc = 64u;
        sub_s.lfo_ph += (0xFFFFFFFFu / fpc) * n;
        lfo = (sin_q15(sub_s.lfo_ph) + 32767) >> 1;          /* 0..32767 */
        cut = cut - ((((depth * cut) >> 8) * (uint32_t)lfo) >> 15);
    } else {
        sub_s.lfo_ph = 0xC0000000u;            /* parked at the top of the cycle */
    }
    f = svf_f(cut);
    sub_s.f_now = slew(sub_s.f_now, f, 1200);
    f = sub_s.f_now;
    q = svf_q(48);
    rlvl = (int32_t)reese * 64;                /* Q15-ish: 255 -> 0.5 */
    gain = 4096 + (int32_t)drv * 48;           /* Q12: up to ~4x */
    if (shape < 128u) { x_lo = (int32_t)shape << 8; x_hi = -1; }
    else { x_lo = -1; x_hi = (int32_t)(shape - 128u) << 8; }
    if (sub_s.trig) {
        sub_s.trig = 0;
        if (sub_s.decay) sub_s.env = sub_s.amp;  /* a decaying bass starts at the top */
    }
    env = sub_s.env;
    amp = sub_s.amp;
    inc = sub_s.inc_now;
    {
        uint32_t base = (uint32_t)inc << 1;
        rinc0 = (int32_t)inc_cents(base, -15);
        rinc1 = (int32_t)inc_cents(base, 15);
    }
    ph = sub_s.ph; rph0 = sub_s.rph0; rph1 = sub_s.rph1;

    /* ---- per frame */
    for (i = 0; i < n; i++) {
        int32_t w, s, r, lp, bp, hp, v;
        /* envelope */
        if (sub_s.decay) {
            env = (env * mul) >> 15;
            if (env < 8 && !sub_s.gate) env = 0;
        } else {
            int32_t tgt = sub_s.gate ? amp : 0;
            env += ((tgt - env) * (sub_s.gate ? 3000 : 700)) >> 15;
            if (!sub_s.gate && env < 8) env = 0;
        }
        /* glide */
        if (inc != sub_s.inc_tgt)
            inc += (int32_t)(((int64_t)(sub_s.inc_tgt - inc) * gk) >> 15);
        /* the sub: sine -> triangle -> soft square */
        ph += (uint32_t)inc;
        s = sin_q15(ph);
        if (x_lo >= 0) {
            int32_t t = tri_q15(ph);
            w = s + (((t - s) * x_lo) >> 15);
        } else {
            int32_t t = tri_q15(ph), sq = (int32_t)ph < 0 ? -22000 : 22000;
            w = t + (((sq - t) * x_hi) >> 15);
        }
        /* the reese: two detuned saws through the low-pass */
        rph0 += (uint32_t)rinc0;
        rph1 += (uint32_t)rinc1;
        r = (saw_q15(rph0) + saw_q15(rph1)) >> 1;
        svf_run(&sub_s.svf, r, f, q, &lp, &bp, &hp);
        lp = SAT15(lp);
        r = (lp * rlvl) >> 15;
        v = ((w + r) * env) >> 15;
        if (drv)
            v = softclip((v * gain) >> 12);
        out[2u * i] += v;
        out[2u * i + 1u] += v;
    }
    sub_s.env = env;
    sub_s.inc_now = inc;
    sub_s.ph = ph; sub_s.rph0 = rph0; sub_s.rph1 = rph1;
}
