/* SPDX-License-Identifier: GPL-3.0-only */
/* WitchBox WEATHER: the bed. No notes; layers mixed by the track's params, a slow random walk on the
 * density, and events from COVEN (gust, crackle burst, density up / down for a bar).
 *
 *   RAIN     pink/white noise through a one-pole, a slow amplitude walk, Poisson droplets (short pings)
 *   CRACKLE  sparse impulses through a low-pass, a trace of 50 Hz hum
 *   HISS     high-passed white noise
 *   DRONE    three sines on G.root, slowly beating
 *   FIELD    the ghost tape scrubbed at 1/4 .. 1/2 speed, reversing now and then, low-passed
 *   TONE tilts the whole bed, WIDTH decorrelates L / R, DENSITY scales the droplets and the crackle. */
#pragma once
#include "dsp.h"
#include "types.h"

#define WE_NDROP 4
typedef struct { uint32_t ph, inc; int32_t amp, amul; } we_drop_t;

static struct {
    pink_t pinkl, pinkr;
    lp1_t rainl, rainr, tonel, toner, crack, field, rumble;
    hp1_t hissl, hissr;
    int32_t rain_walk, rain_target;      /* Q15 slow amplitude walk */
    we_drop_t drop[WE_NDROP];
    uint32_t drop_thresh;                /* per-frame spawn threshold on a 16-bit random */
    uint32_t crack_thresh;
    uint32_t hum_ph;
    uint32_t dr_ph[3], dr_inc[3];
    uint32_t fpos;                       /* tape position Q16 */
    int32_t fspeed;                      /* tape samples per frame Q16, signed */
    int32_t gust;                        /* Q15 swell level, decays */
    int32_t gust_target;
    int32_t crack_burst;                 /* frames left of the burst */
    int32_t dens_off;                    /* -1, 0, +1 */
    int32_t dens_frames;                 /* frames left for the density offset */
    uint32_t blocks;
} we;

static void weather_init(void)
{
    uint32_t i;
    memset(&we, 0, sizeof we);
    we.rain_walk = 20000; we.rain_target = 20000;
    we.fspeed = 12000;
    for (i = 0; i < 3u; i++) we.dr_ph[i] = i * 0x40000000u;
}

static void weather_event(uint32_t kind)
{
    switch (kind) {
    case 0: we.gust_target = 32767; break;
    case 1: we.crack_burst = (int32_t)(WB_FS * 3u / 10u); break;
    case 2: we.dens_off = 1; we.dens_frames = (int32_t)(wb_fpb * 4u); break;
    case 3: we.dens_off = -1; we.dens_frames = (int32_t)(wb_fpb * 4u); break;
    default: break;
    }
}

static void weather_render(int32_t *out, uint32_t n)
{
    int32_t rain = P[T_WEATHER][PW_RAIN] * 128, crackle = P[T_WEATHER][PW_CRACKLE] * 128;
    int32_t hiss = P[T_WEATHER][PW_HISS] * 128, drone = P[T_WEATHER][PW_DRONE] * 128;
    int32_t field = P[T_WEATHER][PW_FIELD] * 128, width = P[T_WEATHER][PW_WIDTH] * 128;
    uint32_t tone = P[T_WEATHER][PW_TONE], density = P[T_WEATHER][PW_DENSITY];
    int32_t tonek, raink, i, k;
    int32_t dens;
    uint32_t root_note;

    /* ---- block rate */
    we.blocks++;
    dens = (int32_t)density + we.dens_off * 90;
    dens = CLAMP(dens, 0, 255);
    if (we.dens_frames > 0) { we.dens_frames -= (int32_t)n; if (we.dens_frames <= 0) we.dens_off = 0; }
    we.gust = slew(we.gust, we.gust_target, we.gust_target ? 60 : 12);
    if (we.gust >= 32767) we.gust_target = 0;
    if (we.crack_burst > 0) we.crack_burst -= (int32_t)n;
    /* the rain walk: a new target every ~1.5 s, approached slowly */
    if ((we.blocks & 2047u) == 0) we.rain_target = 12000 + (int32_t)(rnd_isr() % 16000u);
    we.rain_walk = slew(we.rain_walk, we.rain_target, 3);
    /* droplets: ~2 .. 40 a second by density, each a ping between 2 and 6 kHz */
    we.drop_thresh = 3u + ((uint32_t)dens * 60u >> 8);               /* per-frame chance * 65536 / 44100 */
    we.crack_thresh = (uint32_t)(8u + ((uint32_t)dens * 80u >> 8)) * (we.crack_burst > 0 ? 8u : 1u);
    tonek = lp1_k(500u + ((tone * tone) >> 2));                        /* 500 Hz .. ~16 kHz */
    raink = lp1_k(1500u + (tone * 20u));
    root_note = 36u + (G.root % 12u);
    {
        uint32_t base = note_inc((int32_t)root_note);
        we.dr_inc[0] = base;
        we.dr_inc[1] = base + 29000u;                                   /* ~ +0.3 Hz */
        we.dr_inc[2] = (base << 1) - 19000u;                            /* the octave, -0.2 Hz */
    }
    /* the field scrub: now and then a new speed, now and then a reversal */
    if ((we.blocks & 511u) == 0) {
        int32_t sp = 8192 + (int32_t)(rnd_isr() % 8192u);
        we.fspeed = (rnd_isr() & 7u) == 0 ? -sp : (we.fspeed < 0 ? -sp : sp);
    }
    for (k = 0; k < WE_NDROP; k++) {
        if (we.drop[k].amp > 100) continue;
        if ((rnd_isr() & 0xFFFFu) < we.drop_thresh * n) {               /* spawn once a block at most */
            uint32_t hz = 2000u + (rnd_isr() % 4000u);
            we.drop[k].ph = 0;
            we.drop[k].inc = hz * 97391u;
            we.drop[k].amp = 32767;
            we.drop[k].amul = 32768 - 743 / (15 + (int32_t)(rnd_isr() % 25u));
            break;
        }
    }

    /* ---- frames */
    for (i = 0; i < (int32_t)n; i++) {
        int32_t pl = pink(&we.pinkl), pr = pink(&we.pinkr), wl = white_isr(), wr = white_isr();
        int32_t l = 0, r = 0, mono = 0, s, lvl;
        /* rain */
        lvl = (rain * we.rain_walk) >> 16;
        lvl = lvl + ((lvl * we.gust) >> 15);                              /* the gust up to 2x */
        l += (lp1(&we.rainl, pl, raink) * lvl) >> 15;
        r += (lp1(&we.rainr, pr, raink) * lvl) >> 15;
        if (we.gust) mono += (lp1(&we.rumble, wl, 200) * ((we.gust * rain) >> 15)) >> 13;   /* a rumble under it */
        for (k = 0; k < WE_NDROP; k++)
            if (we.drop[k].amp > 100) {
                we.drop[k].ph += we.drop[k].inc;
                s = (sin_q15(we.drop[k].ph) * we.drop[k].amp) >> 15;
                mono += (s * lvl) >> 16;
                we.drop[k].amp = (we.drop[k].amp * we.drop[k].amul) >> 15;
            }
        /* crackle */
        {
            uint32_t rr = rnd_isr();
            int32_t imp = (rr & 0xFFFFu) < we.crack_thresh ? (int32_t)((rr >> 17) & 0x7FFFu) - 8192 : 0;
            we.hum_ph += 50u * 97391u;
            s = lp1(&we.crack, imp << 1, 9000) + (sin_q15(we.hum_ph) >> 8);
            mono += (s * crackle) >> 15;
        }
        /* hiss */
        l += (hp1(&we.hissl, wl, 6000) * hiss) >> 17;
        r += (hp1(&we.hissr, wr, 6000) * hiss) >> 17;
        /* drone */
        if (drone) {
            s = 0;
            for (k = 0; k < 3; k++) { we.dr_ph[k] += we.dr_inc[k]; s += sin_q15(we.dr_ph[k]); }
            mono += ((s / 3) * drone) >> 17;
        }
        /* field: the tape scrubbed */
        if (field) {
            uint32_t idx;
            int32_t t0, t1, frac;
            we.fpos = (uint32_t)((int32_t)we.fpos + we.fspeed);
            if (we.fpos >= (WB_TAPE_LEN - 1u) << 16) {
                we.fpos = we.fspeed < 0 ? ((WB_TAPE_LEN - 2u) << 16) : 0;
            }
            idx = we.fpos >> 16;
            frac = (int32_t)(we.fpos & 0xFFFFu);
            t0 = wb_tape[idx];
            t1 = wb_tape[idx + 1u];
            s = t0 + (((t1 - t0) * frac) >> 16);
            mono += (lp1(&we.field, s, 2000) * field) >> 16;
        }
        /* tone, width, out */
        l += mono; r += mono;
        l = lp1(&we.tonel, l, tonek);
        r = lp1(&we.toner, r, tonek);
        {
            int32_t mid = (l + r) >> 1;
            l = mid + (((l - mid) * width) >> 15);
            r = mid + (((r - mid) * width) >> 15);
        }
        out[2 * i] += SAT15(l);
        out[2 * i + 1] += SAT15(r);
    }
}
