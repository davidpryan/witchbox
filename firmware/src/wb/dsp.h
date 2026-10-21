/* SPDX-License-Identifier: GPL-3.0-only */
/* WitchBox DSP toolbox: fixed point only (the chip has no FPU and no libgcc).
 *
 * Conventions
 *   Q15   a level: 32767 = 1.0 (int32_t holding -32768..32767)
 *   Q12   a gain: 4096 = 1.0
 *   phase a 32-bit accumulator; one cycle = 2^32. inc = f * 2^32 / 44100
 *   FS    44100, blocks of WB_SUB (32) frames: coefficients move once a block, samples every frame
 * Nothing here divides 64-bit values. Divisions by a variable are guarded or avoided. */
#pragma once
#include <stdint.h>

#define WB_FS 44100u
#define WB_SUB 32u                           /* the sub-block: the engines and the FX render this many frames */

#define Q15 32768
#define Q15_1 32767
#define Q12 4096
#define MUL15(a, b) (((int32_t)(a) * (int32_t)(b)) >> 15)
#define MUL12(a, b) (((int32_t)(a) * (int32_t)(b)) >> 12)
#define CLAMP(v, lo, hi) ((v) < (lo) ? (lo) : (v) > (hi) ? (hi) : (v))
#define SAT15(v) CLAMP((v), -32767, 32767)
#define ABS(v) ((v) < 0 ? -(v) : (v))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define LEN(a) (sizeof(a) / sizeof((a)[0]))

#ifdef DINGHY_HOST
#define WB_POOL                              /* big buffers: plain statics on the host */
#else
#define WB_POOL __attribute__((section(".pool")))
#endif

/* ---- random: xorshift32, one generator for the sound (ISR) and one for the generators (main loop) */
static uint32_t wb_rs_isr = 0x9E3779B9u, wb_rs_main = 0x2545F491u;
static inline uint32_t rnd_isr(void)
{
    uint32_t x = wb_rs_isr;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return wb_rs_isr = x;
}
static inline uint32_t rnd_main(void)
{
    uint32_t x = wb_rs_main;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return wb_rs_main = x;
}
static inline void rnd_seed(uint32_t s) { wb_rs_main = s ? s : 1u; }
#define RND_PCT(r) ((r) % 100u)              /* 0..99 */
#define RND15(r) ((int32_t)((r) >> 17))      /* 0..32767 */
#define RNDS15(r) ((int32_t)((r) >> 16) - 32768)   /* -32768..32767 */

/* ---- sine: a full cycle over 2^32 -> -32767..32767 (parabola refined, ~0.1 % THD) */
static inline int32_t sin_q15(uint32_t ph)
{
    int32_t x = (int32_t)ph >> 16, ax = x < 0 ? -x : x, y, ay;
    y = (x * (32768 - ax)) >> 13;
    ay = y < 0 ? -y : y;
    y += ((((y * ay) >> 15) - y) * 7373) >> 15;
    return y > 32767 ? 32767 : y < -32767 ? -32767 : y;
}
static inline int32_t tri_q15(uint32_t ph)   /* -32767..32767 */
{
    int32_t x = (int32_t)(ph >> 16) - 32768; /* -32768..32767 */
    x = x < 0 ? -x : x;                      /* 0..32768 */
    return SAT15(2 * x - 32768);
}
static inline int32_t saw_q15(uint32_t ph) { int32_t v = (int32_t)(ph >> 16) - 32768; return v < -32767 ? -32767 : v; }
static inline int32_t sqr_q15(uint32_t ph, uint32_t pw) { return ph < pw ? 32767 : -32767; }   /* pw: 2^31 = 50 % */

/* ---- pitch: phase increment of MIDI note n (0..127), with a fine offset in cents (-1200..1200) */
static const uint32_t WB_INC_C9[12] = {       /* MIDI 120..131; lower octaves shift right */
    815363807u, 863847862u, 915214929u, 969636441u, 1027294024u, 1088380105u,
    1153098554u, 1221665363u, 1294309365u, 1371273005u, 1452813141u, 1539201906u};
static const uint16_t WB_CENT64[65] = {       /* 2^(k/64) * 32768, k = 0..64 (one semitone in 64 steps) */
    32768, 33124, 33484, 33847, 34214, 34586, 34961, 35340, 35724, 36112, 36503, 36899, 37300, 37704, 38113, 38527,
    38945, 39367, 39794, 40226, 40662, 41103, 41549, 42000, 42455, 42916, 43381, 43852, 44327, 44808, 45294, 45786,
    46282, 46784, 47292, 47805, 48324, 48848, 49378, 49913, 50455, 51002, 51555, 52114, 52679, 53251, 53828, 54412,
    55002, 55599, 56202, 56811, 57427, 58050, 58680, 59316, 59959, 60610, 61267, 61931, 62603, 63282, 63968, 64662, 65363};
static inline uint32_t note_inc(int32_t note)
{
    if (note < 0) note = 0;
    if (note > 127) note = 127;
    return WB_INC_C9[note % 12] >> (10 - note / 12);
}
/* inc scaled by 2^(cents/1200); cents -3200..3200 */
static inline uint32_t inc_cents(uint32_t inc, int32_t cents)
{
    int32_t semis, k;
    uint32_t r;
    if (cents == 0) return inc;
    semis = cents / 100;                      /* toward zero */
    k = ((cents - semis * 100) * 64) / 100;   /* -63..63 */
    if (k < 0) { k += 64; semis -= 1; }
    r = (uint32_t)(((uint64_t)inc * WB_CENT64[k]) >> 15);
    if (semis > 0) r = (uint32_t)(((uint64_t)r * WB_CENT64[64]) >> 15), semis--;   /* (unrolled would be silly) */
    while (semis > 0) { r = (uint32_t)(((uint64_t)r * 34716u) >> 15); semis--; }     /* 2^(1/12) * 32768 = 34716 */
    while (semis < 0) { r = (uint32_t)(((uint64_t)r * 30929u) >> 15); semis++; }     /* 2^(-1/12) * 32768 = 30929 */
    return r;
}
static inline uint32_t inc_semis(uint32_t inc, int32_t semis)   /* exact octaves by shifting, the rest by table */
{
    while (semis >= 12) { inc <<= 1; semis -= 12; }
    while (semis <= -12) { inc >>= 1; semis += 12; }
    while (semis > 0) { inc = (uint32_t)(((uint64_t)inc * 34716u) >> 15); semis--; }
    while (semis < 0) { inc = (uint32_t)(((uint64_t)inc * 30929u) >> 15); semis++; }
    return inc;
}

/* ---- envelopes: one-pole glides. coef is the per-frame approach, Q15; time params 0..255 map to coefs */
static const uint16_t WB_ENV_COEF[32] = {     /* per-frame Q15 coefficient for 32 times, ~1 ms .. ~8 s */
    32767, 11800, 6400, 3700, 2200, 1400, 900, 600, 400, 280, 200, 140, 100, 72, 52, 38,
    28, 20, 15, 11, 8, 6, 5, 4, 3, 2, 2, 1, 1, 1, 1, 1};
static inline int32_t env_coef(uint32_t p255) { return WB_ENV_COEF[(p255 >> 3) & 31u]; }
/* decay as a multiplier per frame (Q15), for exponential tails: 0..255 -> ~5 ms .. ~6 s */
static const uint16_t WB_DEC_MUL[32] = {
    32000, 32400, 32600, 32690, 32720, 32740, 32750, 32756, 32759, 32761, 32762, 32763, 32764, 32764, 32765, 32765,
    32765, 32766, 32766, 32766, 32766, 32766, 32766, 32766, 32767, 32767, 32767, 32767, 32767, 32767, 32767, 32767};
static inline int32_t dec_mul(uint32_t p255) { return WB_DEC_MUL[(p255 >> 3) & 31u]; }

/* exponential decay level: a block-rate shift counter is cheaper and plenty: lvl -= lvl >> k every WB_SUB frames.
 * k from a time param: 0..255 -> k 1..13 (~12 ms .. ~30 s to -60 dB) */
static inline int32_t dec_shift(uint32_t p255) { return 1 + (int32_t)((p255 * 12u) >> 8); }

/* ---- filters */
/* one-pole low-pass: y += (x - y) * k >> 15. k from a cutoff in Hz: k = 1 - e^(-2 pi fc / fs) ~ 2 pi fc / fs for small */
static inline int32_t lp1_k(uint32_t hz) { uint32_t k = (hz * 4669u) >> 10; return k > 32767u ? 32767 : (int32_t)k; }  /* 2pi/44100*2^15*2^10 = 4669 */
typedef struct { int32_t y; } lp1_t;
static inline int32_t lp1(lp1_t *f, int32_t x, int32_t k) { f->y += ((x - f->y) * k) >> 15; return f->y; }
typedef struct { int32_t y, xp; } hp1_t;
static inline int32_t hp1(hp1_t *f, int32_t x, int32_t k)   /* k as lp1_k: y = x - lp(x) */
{
    f->y += ((x - f->y) * k) >> 15;
    return x - f->y;
}

/* Chamberlin state-variable filter, Q15. f = 2 sin(pi fc / fs) in Q15 (table below, fc up to ~8 kHz);
 * q = damping, Q15 (32767 = no resonance, 2000 = screaming). Stable while f <= ~0.5 * Q15 and q >= ~f/2. */
typedef struct { int32_t lp, bp; } svf_t;
static inline void svf_run(svf_t *s, int32_t x, int32_t f, int32_t q, int32_t *lp, int32_t *bp, int32_t *hp)
{
    int32_t h;
    s->lp += (f * s->bp) >> 15;
    h = x - s->lp - ((q * s->bp) >> 15);
    s->bp += (f * h) >> 15;
    s->lp = CLAMP(s->lp, -65536, 65535);     /* a resonant filter fed a square can run away: keep it finite */
    s->bp = CLAMP(s->bp, -65536, 65535);
    *lp = s->lp; *bp = s->bp; *hp = h;
}
/* f for a cutoff param 0..255 on an exponential scale 40 Hz .. 8 kHz, Q15 */
static const uint16_t WB_SVF_F[64] = {
    186, 204, 223, 244, 267, 293, 321, 351, 384, 421, 461, 505, 553, 605, 663, 726,
    795, 870, 953, 1044, 1143, 1251, 1370, 1500, 1642, 1798, 1968, 2155, 2359, 2582, 2826, 3093,
    3385, 3704, 4053, 4434, 4850, 5305, 5801, 6343, 6935, 7580, 8284, 9050, 9885, 10793, 11780, 12852,
    14014, 15274, 16637, 18109, 19696, 21404, 23237, 25200, 27295, 29524, 31887, 32767, 32767, 32767, 32767, 32767};
static inline int32_t svf_f(uint32_t p255) { return WB_SVF_F[(p255 >> 2) & 63u]; }
static inline int32_t svf_f_hz(uint32_t hz)   /* 2 sin(pi hz / fs) ~ 2 pi hz / fs for hz < 4k; clamp at 1.0 */
{
    uint32_t f = (hz * 4669u) >> 9;            /* 2 * 2pi/fs * 2^15 */
    return f > 32767u ? 32767 : (int32_t)f;
}
static inline int32_t svf_q(uint32_t res255)   /* resonance param -> damping: 0 = none (q 32767), 255 = ~Q 16 */
{
    int32_t q = 32767 - (int32_t)res255 * 120;
    return q < 2048 ? 2048 : q;
}

/* ---- waveshaping */
static inline int32_t softclip(int32_t x)     /* x in ~Q15, a cubic that stays under 32767 */
{
    if (x > 49151) return 32767;
    if (x < -49151) return -32767;
    {
        int32_t x2 = (x * x) >> 15;            /* Q15 */
        int32_t x3 = (x2 * x) >> 15;
        int32_t y = x - (x3 / 7);              /* roughly x - x^3/3 scaled so 1.5 -> 1.0 */
        return SAT15(y);
    }
}
static inline int32_t drive(int32_t x, int32_t gain_q12) { return softclip((x * gain_q12) >> 12); }

/* ---- a Q15 level from a velocity 0..127 (a gentle curve: 64 -> 0.6) */
static inline int32_t vel_q15(uint32_t vel)
{
    uint32_t v = vel > 127u ? 127u : vel;
    return (int32_t)(8000u + (v * v * 24767u) / 16129u);
}

/* ---- noise, two colours */
static inline int32_t white_isr(void) { return RNDS15(rnd_isr()) >> 1; }   /* -16384..16383 */
typedef struct { int32_t b0, b1, b2; } pink_t;
static inline int32_t pink(pink_t *p)        /* Paul Kellet's economy pink, Q15 in / out */
{
    int32_t w = white_isr();
    p->b0 = (p->b0 * 32604 + w * 164) >> 15;   /* 0.99765 / 0.0990460 */
    p->b1 = (p->b1 * 31631 + w * 982) >> 15;   /* 0.96300 / 0.2965164 */
    p->b2 = (p->b2 * 18677 + w * 3414) >> 15;  /* 0.57000 / 1.0526913 */
    return SAT15(p->b0 + p->b1 + p->b2 + (w >> 3));
}

/* ---- tempo helpers: frames per quarter note from BPM, no 64-bit division */
static inline uint32_t frames_per_beat(uint32_t bpm) { return bpm ? (WB_FS * 60u) / bpm : 20000u; }

/* ---- block fades: a level that walks toward a target by at most `step` per call (no zipper noise) */
static inline int32_t slew(int32_t now, int32_t target, int32_t step)
{
    if (now < target) { now += step; if (now > target) now = target; }
    else if (now > target) { now -= step; if (now < target) now = target; }
    return now;
}

/* ---- integer helpers */
static inline int32_t isqrt32(uint32_t v)     /* floor(sqrt(v)) */
{
    uint32_t r = 0, b = 1u << 30;
    while (b > v) b >>= 2;
    while (b) {
        if (v >= r + b) { v -= r + b; r = (r >> 1) + b; }
        else r >>= 1;
        b >>= 2;
    }
    return (int32_t)r;
}
/* a small decimal formatter (host and firmware; the platform's fmt_int is firmware-only) */
static void wb_itoa(char *b, int32_t v)
{
    char t[12];
    int n = 0, i, neg = v < 0;
    uint32_t u = (uint32_t)(neg ? -v : v);
    do { t[n++] = (char)('0' + u % 10u); u /= 10u; } while (u);
    if (neg) t[n++] = '-';
    for (i = 0; i < n; i++) b[i] = t[n - 1 - i];
    b[n] = 0;
}
static void wb_strcpy(char *d, const char *s, uint32_t max)
{
    uint32_t i = 0;
    while (s[i] && i + 1u < max) { d[i] = s[i]; i++; }
    d[i] = 0;
}
static uint32_t wb_strlen(const char *s) { uint32_t n = 0; while (s[n]) n++; return n; }
