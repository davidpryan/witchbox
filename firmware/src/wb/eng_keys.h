/* SPDX-License-Identifier: GPL-3.0-only */
/* KEYS: four voices of 4-operator phase-modulation FM, eight patches, three algorithms.
 *
 *   PK_PATCH     E-PIANO BELL PLUCK GLASS ORGAN MARIMBA DX-BASS SOFT
 *   PK_RATIO     offsets the modulators' ratio (128 = as the patch)
 *   PK_INDEX     scales the modulation depth (128 = as the patch)
 *   PK_DECAY     scales the decays (128 = as the patch)
 *   PK_FEEDBACK  operator 4 feeds itself
 *   PK_BRIGHT    a low-pass on the voice
 *   PK_RELEASE   release time after note-off
 *   PK_DETUNE    +-cents on operator 2 (128 = none): the chorus of an e-piano
 * Algorithms: 0 stack 4->3->2->1, 1 pairs (4->3) + (2->1), 2 fan (2, 3, 4) -> 1.
 * Integers only. Per frame and voice: four sines, a one-pole. Envelopes move once a block and are
 * interpolated across it. The modulation phase offset is (mod * idx16) << 4 in uint32: it wraps, which is
 * what a phase wants; mod * idx16 itself stays under 2^31. */
#pragma once
#include "dsp.h"
#include "types.h"

#define KEYS_NV 4u
typedef struct {
    uint16_t ratio[4];             /* Q8: 256 = 1.0 */
    uint8_t level[4];              /* carriers: output level; modulators: index */
    uint8_t decay[4];              /* 0 fast .. 255 held */
    uint8_t alg;
} keys_patch_t;
static const keys_patch_t KEYS_PATCH[8] = {
    /* ratio 1..4            level              decay              alg */
    {{256, 256, 256, 3584}, {220, 110, 90, 70},  {200, 150, 190, 60},  1},   /* E-PIANO: body pair + tine pair (14:1) */
    {{256, 896, 256, 1792}, {200, 120, 110, 80}, {230, 220, 220, 180}, 1},   /* BELL: 3.5 and 7 ratios, long */
    {{256, 256, 512, 768},  {230, 150, 100, 60}, {160, 90, 70, 50},    0},   /* PLUCK: stack, fast modulators */
    {{256, 1280, 256, 512}, {180, 80, 120, 70},  {220, 200, 220, 190}, 1},   /* GLASS: a 5th harmonic shimmer */
    {{256, 512, 768, 128},  {200, 60, 50, 40},   {255, 255, 255, 255}, 2},   /* ORGAN: fan, held */
    {{256, 1024, 256, 2560}, {230, 170, 80, 60}, {120, 40, 110, 30},   1},   /* MARIMBA: a 4:1 strike, very fast */
    {{256, 256, 256, 256},  {240, 190, 100, 50}, {210, 90, 120, 80},   0},   /* DX-BASS: stack of unisons */
    {{256, 256, 256, 512},  {200, 50, 110, 40},  {230, 230, 230, 230}, 1},   /* SOFT: low indices, long */
};
static const char *const KEYS_PATCH_NAME[8] = {"E-PIANO", "BELL", "PLUCK", "GLASS", "ORGAN", "MARIMBA", "DX-BASS", "SOFT"};

typedef struct {
    uint8_t note, gate, active, alg;
    uint32_t age;
    uint32_t ph[4], inc[4];
    int32_t lvl[4];                /* envelope level per operator, Q15 */
    int32_t idx16[4];              /* modulation index per operator (phase units / 16 per Q15 unit) */
    uint8_t k[4];                  /* decay shift per operator */
    uint8_t rel_k;
    int32_t fb16;                  /* op 4 feedback */
    int32_t amp;                   /* Q15 */
    int32_t prev4;                 /* op 4's last output, for feedback */
    lp1_t lp;
} keys_voice_t;
static keys_voice_t keys_v[KEYS_NV];
static uint32_t keys_ages;

static void keys_init(void)
{
    memset(keys_v, 0, sizeof keys_v);
    keys_ages = 0;
}

static void keys_note_on(uint8_t note, uint8_t vel, const uint8_t *p, uint8_t slice)
{
    uint32_t i, pick = 0, best = 0xFFFFFFFFu, base, pi, op;
    const keys_patch_t *pt;
    keys_voice_t *v;
    int32_t roff, iscale, dscale, vscale, det;
    (void)slice;
    for (i = 0; i < KEYS_NV; i++) {
        keys_voice_t *w = &keys_v[i];
        uint32_t score = w->active && w->note == note ? 0u : !w->active ? 1u
                       : !w->gate ? 0x10000000u + (uint32_t)w->lvl[0] : 0x80000000u + w->age;
        if (score < best) { best = score; pick = i; }
    }
    v = &keys_v[pick];
    pi = (p[PK_PATCH] * 8u) >> 8;
    pt = &KEYS_PATCH[pi];
    base = note_inc(note);
    roff = ((int32_t)p[PK_RATIO] - 128) * 2;             /* Q8: +-1.0 on the modulators */
    iscale = (int32_t)p[PK_INDEX] + 1;                   /* /128 */
    dscale = (int32_t)p[PK_DECAY] + 64;                  /* /192 */
    vscale = (int32_t)vel + 64;                          /* /128: harder is brighter */
    det = ((int32_t)p[PK_DETUNE] - 128) / 4;             /* cents */
    v->note = note;
    v->gate = 1;
    v->active = 1;
    v->age = ++keys_ages;
    v->alg = pt->alg;
    v->amp = (vel_q15(vel) * 8192) >> 15;                /* 0.25 of full scale at vel 127 */
    v->fb16 = (int32_t)p[PK_FEEDBACK] * 20;
    v->rel_k = (uint8_t)dec_shift(p[PK_RELEASE]);
    v->prev4 = 0;
    v->lp.y = 0;
    for (op = 0; op < 4u; op++) {
        int32_t ratio = pt->ratio[op], carrier, d;
        carrier = op == 0 || (pt->alg == 1 && op == 2);
        if (!carrier) {
            ratio += roff;
            if (ratio < 32) ratio = 32;
        }
        v->inc[op] = (uint32_t)(((uint64_t)base * (uint32_t)ratio) >> 8);
        if (op == 1 && det)
            v->inc[op] = inc_cents(v->inc[op], det);
        v->ph[op] = 0;
        v->lvl[op] = (int32_t)pt->level[op] * 128;       /* Q15 */
        if (carrier) {
            v->idx16[op] = 0;
        } else {
            int32_t ix = (int32_t)pt->level[op] * 48;    /* up to ~9 rad */
            ix = (ix * iscale) >> 7;
            ix = (ix * vscale) >> 7;
            if (ix > 16000) ix = 16000;
            v->idx16[op] = ix;
            v->lvl[op] = 32767;                          /* a modulator's envelope scales its index */
        }
        d = ((int32_t)pt->decay[op] * dscale) / 192;
        if (d > 255) d = 255;
        v->k[op] = (uint8_t)dec_shift((uint32_t)d);
    }
}

static void keys_note_off(uint8_t note)
{
    uint32_t i;
    for (i = 0; i < KEYS_NV; i++)
        if (keys_v[i].active && keys_v[i].note == note)
            keys_v[i].gate = 0;
}

static void keys_all_off(void)
{
    uint32_t i;
    for (i = 0; i < KEYS_NV; i++)
        keys_v[i].gate = 0;
}

static void keys_render(int32_t *out, uint32_t n)
{
    uint32_t i, vi, op;
    int32_t bk;
    {
        uint32_t hz = 300u + (uint32_t)P[T_KEYS][PK_BRIGHT] * 60u;
        bk = lp1_k(hz);
    }
    for (vi = 0; vi < KEYS_NV; vi++) {
        keys_voice_t *v = &keys_v[vi];
        int32_t l0[4], l1[4], dl[4], l[4], alive = 0, amp, fb16, carriers;
        uint32_t ph0, ph1, ph2, ph3, in0, in1, in2, in3;
        if (!v->active)
            continue;
        /* ---- once a block: the four envelopes */
        for (op = 0; op < 4u; op++) {
            int32_t e = v->lvl[op], k = v->gate ? v->k[op] : v->rel_k;
            l0[op] = e;
            e -= (e >> k) + 1;                  /* at least 1 a block: a slow shift never stalls above zero */
            if (e < 4) e = 0;
            v->lvl[op] = e;
            l1[op] = e;
            dl[op] = (e - l0[op]) / (int32_t)n;
            l[op] = l0[op];
            alive |= e | l0[op];
        }
        if (!alive) { v->active = 0; continue; }
        if (!v->lvl[0] && !(v->alg == 1 && v->lvl[2])) { v->active = 0; continue; }   /* carriers gone */
        amp = v->amp;
        fb16 = v->fb16;
        carriers = v->alg == 1 ? 2 : 1;
        ph0 = v->ph[0]; ph1 = v->ph[1]; ph2 = v->ph[2]; ph3 = v->ph[3];
        in0 = v->inc[0]; in1 = v->inc[1]; in2 = v->inc[2]; in3 = v->inc[3];
        /* ---- per frame */
        for (i = 0; i < n; i++) {
            int32_t o4, o3, o2, o1, m, y;
            uint32_t off;
            l[0] += dl[0]; l[1] += dl[1]; l[2] += dl[2]; l[3] += dl[3];
            ph0 += in0; ph1 += in1; ph2 += in2; ph3 += in3;
            /* op 4: feeds itself */
            off = (uint32_t)(v->prev4 * fb16) << 4;
            o4 = (sin_q15(ph3 + off) * l[3]) >> 15;
            v->prev4 = o4;
            m = (o4 * v->idx16[3]) ;                      /* < 2^31: 32767 * 16000 */
            if (v->alg == 0) {                            /* 4 -> 3 -> 2 -> 1 */
                o3 = (sin_q15(ph2 + ((uint32_t)m << 4)) * l[2]) >> 15;
                m = o3 * v->idx16[2];
                o2 = (sin_q15(ph1 + ((uint32_t)m << 4)) * l[1]) >> 15;
                m = o2 * v->idx16[1];
                o1 = (sin_q15(ph0 + ((uint32_t)m << 4)) * l[0]) >> 15;
                y = o1;
            } else if (v->alg == 1) {                     /* (4 -> 3) + (2 -> 1) */
                o3 = (sin_q15(ph2 + ((uint32_t)m << 4)) * l[2]) >> 15;
                o2 = (sin_q15(ph1) * l[1]) >> 15;
                m = o2 * v->idx16[1];
                o1 = (sin_q15(ph0 + ((uint32_t)m << 4)) * l[0]) >> 15;
                y = (o1 + o3) >> 1;
            } else {                                      /* (2, 3, 4) -> 1 */
                o3 = (sin_q15(ph2) * l[2]) >> 15;
                o2 = (sin_q15(ph1) * l[1]) >> 15;
                m = (o2 * v->idx16[1]) + (o3 * v->idx16[2]) + m;   /* each < 2^29: the sum fits */
                o1 = (sin_q15(ph0 + ((uint32_t)m << 4)) * l[0]) >> 15;
                y = o1;
            }
            y = (y * amp) >> 15;
            y = lp1(&v->lp, y, bk);
            out[2u * i] += y;
            out[2u * i + 1u] += y;
        }
        (void)carriers;
        v->ph[0] = ph0; v->ph[1] = ph1; v->ph[2] = ph2; v->ph[3] = ph3;
        (void)l1;
    }
}
