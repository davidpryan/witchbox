/* SPDX-License-Identifier: GPL-3.0-only */
/* WitchBox master FX: the fixed chain every track goes through.
 *
 *   track: level, pan, FILT, DUCK, then sends (DUST wet/dry, ECHO, SPACE), the dry to the bus
 *   bus:   DUST return -> TAPE -> + ECHO return + SPACE return -> GLUE -> MASTER LP -> WIDTH -> limiter
 *
 * ECHO and SPACE run at half rate (22.05 kHz): their inputs are averaged over two frames, their outputs
 * linearly interpolated on the way back. Everything is Q15 audio, Q12 gains, block-rate coefficients.
 * Pool: the echo line (66 KB), the reverb (22 KB), the tape line (2 KB). Integers only. */
#pragma once
#include "dsp.h"
#include "types.h"

/* ---- buffers */
#define FX_ECHO_LEN 33075u                      /* 1.5 s at 22.05 kHz */
static int16_t fx_echo_buf[FX_ECHO_LEN] WB_POOL;
#define FX_TAPE_LEN 512u                        /* 11.6 ms: wow/flutter up to ~5.8 ms deep */
static int16_t fx_tape_buf[2][FX_TAPE_LEN] WB_POOL;
#define FX_PRE_LEN 2648u                        /* 120 ms pre-delay at half rate */
static int16_t fx_pre_buf[FX_PRE_LEN] WB_POOL;
#define FX_NCOMB 4
static const uint16_t FX_COMB_LEN[2][FX_NCOMB] = {{673, 809, 937, 1097}, {696, 838, 968, 1134}};
static int16_t fx_comb_buf[2][3516 + 120] WB_POOL;   /* L 3516, R 3636 samples, laid end to end per side */
static const uint16_t FX_AP_LEN[2][2] = {{225, 341}, {241, 367}};
static int16_t fx_ap_buf[2][608] WB_POOL;

/* ---- state */
typedef struct { int32_t yl, yr; } fx_tfilt_t;   /* a one-pole per channel */
static fx_tfilt_t fx_tf[WB_NTRACK];
static uint32_t fx_throw_cnt[WB_NTRACK];        /* frames of throw left */
static uint32_t fx_hold;                        /* echo at 100 % feedback */
static uint32_t fx_hr_phase;                    /* 0/1: which frame of the half-rate pair */
static int32_t fx_hr_acc_e, fx_hr_acc_s;        /* the pair's send sum for ECHO and SPACE */
static int32_t fx_echo_cur_l, fx_echo_prev_l, fx_echo_cur_r, fx_echo_prev_r;
static int32_t fx_sp_cur_l, fx_sp_prev_l, fx_sp_cur_r, fx_sp_prev_r;
static uint32_t fx_echo_live, fx_sp_live;     /* half-rate steps until the lines are surely silent */
/* dust */
static uint32_t fx_dust_hold_n;
static int32_t fx_dust_hl, fx_dust_hr;
static lp1_t fx_hiss_lp;
static uint32_t fx_hum_ph;
/* tape */
static uint32_t fx_tape_w, fx_wow_ph, fx_flut_ph;
static int32_t fx_tape_dly_q8;                  /* the delay now, samples * 256 */
static lp1_t fx_tape_hl, fx_tape_hr;
/* echo */
static uint32_t fx_echo_w, fx_echo_dly, fx_echo_new, fx_echo_xf;
static lp1_t fx_echo_lp;
/* space */
static uint32_t fx_pre_w;
static uint32_t fx_comb_w[2][FX_NCOMB], fx_comb_len[2][FX_NCOMB];
static int32_t fx_comb_f[2][FX_NCOMB];
static uint32_t fx_ap_w[2][2];
/* duck, glue, master */
static int32_t fx_duck_env = 32767;             /* Q15: 32767 = no ducking */
static int32_t fx_duck_hi = 32767 << 8;         /* the same, 8 bits finer, for the release */
static int32_t fx_glue_gain = 32767, fx_glue_peak;
static lp1_t fx_mlp_l, fx_mlp_r;
/* block-rate per-track gains (Q12 / Q15), recomputed in fx_process */
static int32_t fx_gl[WB_NTRACK], fx_gr[WB_NTRACK], fx_fk[WB_NTRACK], fx_fmode[WB_NTRACK];
static int32_t fx_sdust[WB_NTRACK], fx_secho[WB_NTRACK], fx_sspace[WB_NTRACK], fx_sduck[WB_NTRACK];

/* the output limiter: straight up to 24576 (-2.5 dBFS), a soft knee from there to 32767 at 1.5x full scale */
static inline int32_t fx_limit(int32_t x)
{
    int32_t a = x < 0 ? -x : x, o;
    if (a <= 24576) return x;
    if (a >= 49152) return x < 0 ? -32767 : 32767;
    o = a - 24576;                              /* 0..24576 -> 0..8191: o - o^2 / (3 * 24576), scaled */
    o = o - ((o * o) >> 13) / 9;                /* at o = 24576: 24576 - 73728/9 = 16384 */
    o = o >> 1;                                 /* 0..8192 */
    o += 24576;
    if (o > 32767) o = 32767;
    return x < 0 ? -o : o;
}

static void fx_init(void)
{
    uint32_t s, c, a = 0;
    fx_duck_env = 32767;
    fx_glue_gain = 32767;
    fx_echo_dly = fx_echo_new = 11025;
    fx_echo_xf = 0;
    for (s = 0; s < 2u; s++) {
        for (c = 0; c < FX_NCOMB; c++) {
            fx_comb_len[s][c] = FX_COMB_LEN[s][c];
            fx_comb_w[s][c] = 0;
            fx_comb_f[s][c] = 0;
        }
        fx_ap_w[s][0] = fx_ap_w[s][1] = 0;
    }
    (void)a;
}

static void fx_duck_trigger(uint32_t vel)
{
    int32_t depth = (int32_t)FXP[FX_DUCK_DEPTH] * 128;              /* 0..32640 */
    int32_t v = vel_q15(vel);                                        /* 8000..32767 */
    int32_t floor_ = 32767 - ((depth * v) >> 15);
    if (floor_ < fx_duck_env) {
        fx_duck_env = floor_;
        fx_duck_hi = floor_ << 8;
    }
}
static void fx_throw(uint32_t track)
{
    if (track < WB_NTRACK)
        fx_throw_cnt[track] = wb_fpb + 2048u;
}
static void fx_throw_hold(uint32_t on) { fx_hold = on ? 1u : 0u; }
static void fx_catch_start(void)
{
    wb_tape_wpos = 0;
    wb_tape_rec = 1;
}

/* the echo's delay for FX_ECHO_TIME, in half-rate samples */
static uint32_t fx_echo_target(void)
{
    static const uint8_t NUM[8] = {1, 1, 3, 1, 3, 2, 2, 3}, DEN[8] = {4, 2, 4, 1, 2, 1, 3, 1};
    uint32_t i = (FXP[FX_ECHO_TIME] * 8u) >> 8, fpb = wb_fpb, d;
    d = (fpb * NUM[i]) / DEN[i] / 2u;           /* 2/3: a quarter triplet (3/16 reads the same as 1/8 dotted) */
    if (d < 16u) d = 16u;
    if (d >= FX_ECHO_LEN - 1u) d = FX_ECHO_LEN - 2u;
    return d;
}

/* one half-rate step of the echo: input mono Q15 -> cur L/R */
static inline void fx_echo_step(int32_t in)
{
    int32_t a, b, outm, fb, tone_k;
    uint32_t w = fx_echo_w, ra, rb;
    ra = (w + FX_ECHO_LEN - fx_echo_dly) % FX_ECHO_LEN;
    a = fx_echo_buf[ra];
    if (fx_echo_xf) {                           /* crossfading to the new time */
        rb = (w + FX_ECHO_LEN - fx_echo_new) % FX_ECHO_LEN;
        b = fx_echo_buf[rb];
        outm = a + (((b - a) * (int32_t)(2048u - fx_echo_xf)) >> 11);
        if (--fx_echo_xf == 0u)
            fx_echo_dly = fx_echo_new;
    } else
        outm = a;
    tone_k = 1200 + (int32_t)FXP[FX_ECHO_TONE] * 120;       /* ~1.3 kHz .. ~14 kHz at half rate */
    if (tone_k > 32767) tone_k = 32767;
    fb = lp1(&fx_echo_lp, outm, tone_k);
    fb = (fb * (fx_hold ? 32767 : (int32_t)FXP[FX_ECHO_FB] * 122)) >> 15;   /* up to 0.95 */
    fb = softclip(in + fb);
    fx_echo_buf[w] = (int16_t)fb;
    fx_echo_w = (w + 1u) % FX_ECHO_LEN;
    fx_echo_cur_l = outm;
    rb = (w + FX_ECHO_LEN - (fx_echo_dly >> 1)) % FX_ECHO_LEN;   /* R: a tap at half the time, ping-pong-ish */
    fx_echo_cur_r = (outm + (int32_t)fx_echo_buf[rb]) >> 1;
}

/* one half-rate step of the reverb: input mono Q15 -> cur L/R */
static inline void fx_space_step(int32_t in, int32_t g, int32_t damp_k, uint32_t pre)
{
    uint32_t s, c, r, off;
    int32_t x;
    fx_pre_buf[fx_pre_w] = (int16_t)SAT15(in);
    r = (fx_pre_w + FX_PRE_LEN - pre) % FX_PRE_LEN;
    x = fx_pre_buf[r] >> 1;
    fx_pre_w = (fx_pre_w + 1u) % FX_PRE_LEN;
    for (s = 0; s < 2u; s++) {
        int32_t sum = 0, y;
        off = 0;
        for (c = 0; c < FX_NCOMB; c++) {
            uint32_t base = FX_COMB_LEN[s][c], w = fx_comb_w[s][c], rd;
            int16_t *buf = &fx_comb_buf[s][off];
            rd = w + base - fx_comb_len[s][c];
            if (rd >= base) rd -= base;
            y = buf[rd];
            fx_comb_f[s][c] += ((y - fx_comb_f[s][c]) * damp_k) >> 15;
            buf[w] = (int16_t)SAT15(x + ((fx_comb_f[s][c] * g) >> 15));
            fx_comb_w[s][c] = w + 1u >= base ? 0u : w + 1u;
            sum += y;
            off += base;
        }
        sum >>= 2;
        off = 0;
        for (c = 0; c < 2u; c++) {              /* two series all-passes, g = 0.5 */
            uint32_t len = FX_AP_LEN[s][c], w = fx_ap_w[s][c];
            int16_t *buf = &fx_ap_buf[s][off];
            int32_t d = buf[w], o = d - (sum >> 1);
            buf[w] = (int16_t)SAT15(sum + (o >> 1));
            fx_ap_w[s][c] = w + 1u >= len ? 0u : w + 1u;
            sum = o;
            off += len;
        }
        if (s == 0) fx_sp_cur_l = SAT15(sum);
        else fx_sp_cur_r = SAT15(sum);
    }
}

static void fx_process(int32_t tb[WB_NTRACK][2 * WB_SUB], int32_t *out, uint32_t n)
{
    uint32_t t, i;
    /* ---- block rate: gains, coefficients */
    int32_t dust_shift = ((int32_t)FXP[FX_DUST_BITS] * 10) / 255;            /* 0..10 bits thrown away */
    uint32_t dust_hold = 1u + ((uint32_t)FXP[FX_DUST_RATE] * 10u) / 255u;    /* 1..11 samples held */
    int32_t hiss = (int32_t)FXP[FX_DUST_HISS] * 24;                           /* Q15 level, up to ~0.19 */
    int32_t wow_depth = (int32_t)FXP[FX_TAPE_WOW];                            /* 0..255 -> 0..~5.8 ms */
    int32_t sat_g = 4096 + ((int32_t)FXP[FX_TAPE_SAT] * 4096 * 3) / 255;     /* Q12 drive 1..4 */
    int32_t tape_on = wow_depth || FXP[FX_TAPE_SAT];
    int32_t sp_g = 22937 + ((int32_t)FXP[FX_SPACE_SIZE] * 7863) / 255;       /* comb feedback 0.70..0.94 */
    int32_t sp_damp = 32767 - (int32_t)FXP[FX_SPACE_DAMP] * 120;             /* 32767 bright .. 2167 dark */
    uint32_t sp_pre = ((uint32_t)FXP[FX_SPACE_PRE] * (FX_PRE_LEN - 2u)) / 255u;
    int32_t glue_r_q8 = 256 + ((int32_t)FXP[FX_GLUE] * 7 * 256) / 255;      /* ratio 1..8 in Q8 */
    int32_t mlp_p = FXP[FX_MASTER_LP], mlp_k = 32767, mlp_on = mlp_p < 254;
    int32_t width = (int32_t)FXP[FX_WIDTH] * 32;                              /* Q12: 128 -> 4096 */
    int32_t tape_tgt_q8, tape_step_q8, duck_k;
    uint32_t c, s;
    if (sp_damp < 2167) sp_damp = 2167;
    if (mlp_on) {
        uint32_t hz = (60u << (mlp_p >> 5)) * (32u + (mlp_p & 31u)) >> 5;
        mlp_k = lp1_k(hz);
    }
    /* the reverb's comb lengths follow SIZE, one sample a block */
    for (s = 0; s < 2u; s++)
        for (c = 0; c < FX_NCOMB; c++) {
            uint32_t base = FX_COMB_LEN[s][c], want = (base * (96u + ((uint32_t)FXP[FX_SPACE_SIZE] * 160u) / 255u)) >> 8;
            if (want < 8u) want = 8u;
            if (want > base) want = base;
            if (fx_comb_len[s][c] < want) fx_comb_len[s][c]++;
            else if (fx_comb_len[s][c] > want) fx_comb_len[s][c]--;
        }
    /* the echo time: start a crossfade when the tempo or the division moved */
    {
        uint32_t want = fx_echo_target();
        if (!fx_echo_xf && want != fx_echo_dly) {
            fx_echo_new = want;
            fx_echo_xf = 2048u;
        }
    }
    /* duck release: 50 ms .. 1.5 s time constant, as a block-rate coefficient */
    {
        uint32_t tau = 2205u + ((uint32_t)FXP[FX_DUCK_REL] * 63945u) / 255u;   /* frames */
        duck_k = (int32_t)((1048576u * (WB_SUB / 32u)) / tau);              /* 32768 * 32 / tau */
        if (duck_k < 4) duck_k = 4;
        {
            int32_t d = (((32767 << 8) - fx_duck_hi) * duck_k) >> 15;
            if (!d && fx_duck_hi < (32767 << 8)) d = 1;    /* always gets all the way back */
            fx_duck_hi += d;
            if (fx_duck_hi > (32767 << 8)) fx_duck_hi = 32767 << 8;
            fx_duck_env = fx_duck_hi >> 8;
        }
        wb_pos.duck = fx_duck_env;
    }
    /* tape wow + flutter: the delay this block ends at, then a per-frame step */
    {
        int32_t lfo = (sin_q15(fx_wow_ph) * 3 + sin_q15(fx_flut_ph)) >> 2;    /* -32767..32767 */
        fx_wow_ph += 68173u * n;                                             /* 0.7 Hz */
        fx_flut_ph += 779143u * n;                                           /* 8 Hz */
        tape_tgt_q8 = wow_depth * 256 + ((wow_depth * lfo) >> 7);           /* 0 .. 2 * depth samples, Q8 */
        if (tape_tgt_q8 < 0) tape_tgt_q8 = 0;
        if (tape_tgt_q8 > (int32_t)(FX_TAPE_LEN - 4u) * 256) tape_tgt_q8 = (int32_t)(FX_TAPE_LEN - 4u) * 256;
        tape_step_q8 = n ? (tape_tgt_q8 - fx_tape_dly_q8) / (int32_t)n : 0;
    }
    for (t = 0; t < WB_NTRACK; t++) {
        int32_t lvl = P[t][PC_LEVEL], pan = P[t][PC_PAN], f = P[t][PC_FILT], g = (lvl * lvl * 4096) / 65025;   /* Q12, 255 = 1.0 */
        fx_gl[t] = pan <= 128 ? g : (g * (255 - pan)) / 127;
        fx_gr[t] = pan >= 128 ? g : (g * pan) / 128;
        if (f == 128) { fx_fmode[t] = 0; fx_fk[t] = 32767; }
        else if (f < 128) { fx_fmode[t] = 1; fx_fk[t] = lp1_k((60u << (f >> 4)) * (16u + (f & 15u)) >> 4); }
        else { uint32_t q = (uint32_t)f - 129u; fx_fmode[t] = 2; fx_fk[t] = lp1_k((20u << (q >> 4)) * (16u + (q & 15u)) >> 4); }
        fx_sdust[t] = (int32_t)P[t][PC_DUST] * 16;          /* Q12 */
        fx_secho[t] = (int32_t)P[t][PC_ECHO] * 16;
        fx_sspace[t] = (int32_t)P[t][PC_SPACE] * 16;
        if (fx_throw_cnt[t]) {
            uint32_t k = fx_throw_cnt[t];
            int32_t full = k >= 2048u ? 4096 : (int32_t)(k * 2);
            if (fx_secho[t] < full) fx_secho[t] = full;
            if (fx_sspace[t] < full) fx_sspace[t] = full;
            fx_throw_cnt[t] = k > n ? k - n : 0u;
        }
        fx_sduck[t] = 32767 - (((32767 - fx_duck_env) * (int32_t)P[t][PC_DUCK]) >> 8);   /* Q15 gain */
    }
    /* ---- frames */
    for (i = 0; i < n; i++) {
        int32_t bl = 0, br = 0, dl = 0, dr = 0, se = 0, ss = 0, l, r, m, sd;
        for (t = 0; t < WB_NTRACK; t++) {
            int32_t xl = tb[t][2u * i], xr = tb[t][2u * i + 1u], pk;
            if (!xl && !xr && !fx_tf[t].yl && !fx_tf[t].yr)
                continue;                        /* a silent track costs nothing */
            xl = (xl * fx_gl[t]) >> 12;
            xr = (xr * fx_gr[t]) >> 12;
            if (fx_fmode[t] == 1) {
                fx_tf[t].yl += ((xl - fx_tf[t].yl) * fx_fk[t]) >> 15; xl = fx_tf[t].yl;
                fx_tf[t].yr += ((xr - fx_tf[t].yr) * fx_fk[t]) >> 15; xr = fx_tf[t].yr;
            } else if (fx_fmode[t] == 2) {
                fx_tf[t].yl += ((xl - fx_tf[t].yl) * fx_fk[t]) >> 15; xl -= fx_tf[t].yl;
                fx_tf[t].yr += ((xr - fx_tf[t].yr) * fx_fk[t]) >> 15; xr -= fx_tf[t].yr;
            }
            xl = (xl * fx_sduck[t]) >> 15;
            xr = (xr * fx_sduck[t]) >> 15;
            pk = ABS(xl) > ABS(xr) ? ABS(xl) : ABS(xr);
            if ((uint32_t)pk > wb_pos.meter[t]) wb_pos.meter[t] = (uint32_t)(pk > 32767 ? 32767 : pk);
            sd = fx_sdust[t];
            dl += (xl * sd) >> 12;  dr += (xr * sd) >> 12;
            bl += (xl * (4096 - sd)) >> 12;  br += (xr * (4096 - sd)) >> 12;
            m = (xl + xr) >> 1;
            se += (m * fx_secho[t]) >> 12;
            ss += (m * fx_sspace[t]) >> 12;
        }
        /* DUST return */
        if (dl || dr || hiss) {
            if (++fx_dust_hold_n >= dust_hold) {
                fx_dust_hold_n = 0;
                if (dust_shift) {
                    int32_t half = 1 << (dust_shift - 1);
                    fx_dust_hl = ((dl + half) >> dust_shift) << dust_shift;
                    fx_dust_hr = ((dr + half) >> dust_shift) << dust_shift;
                } else { fx_dust_hl = dl; fx_dust_hr = dr; }
            }
            bl += fx_dust_hl;
            br += fx_dust_hr;
            if (hiss) {
                int32_t nz = lp1(&fx_hiss_lp, white_isr(), 12000), hum;
                fx_hum_ph += 5843420u;           /* 60 Hz */
                hum = (sin_q15(fx_hum_ph) * (hiss >> 3)) >> 15;
                nz = ((nz * hiss) >> 15) + hum;
                bl += nz;
                br += nz;
            }
        }
        /* TAPE: wow/flutter as a modulated short delay, then saturation and a little top loss */
        if (tape_on) {
            uint32_t w = fx_tape_w, d, fr, r0, r1;
            int32_t a, b;
            fx_tape_dly_q8 += tape_step_q8;
            if (fx_tape_dly_q8 < 0) fx_tape_dly_q8 = 0;
            d = (uint32_t)fx_tape_dly_q8 >> 8;
            fr = (uint32_t)fx_tape_dly_q8 & 255u;
            fx_tape_buf[0][w] = (int16_t)SAT15(bl);
            fx_tape_buf[1][w] = (int16_t)SAT15(br);
            r0 = (w + FX_TAPE_LEN - d) & (FX_TAPE_LEN - 1u);
            r1 = (r0 + FX_TAPE_LEN - 1u) & (FX_TAPE_LEN - 1u);
            a = fx_tape_buf[0][r0]; b = fx_tape_buf[0][r1];
            bl = a + (((b - a) * (int32_t)fr) >> 8);
            a = fx_tape_buf[1][r0]; b = fx_tape_buf[1][r1];
            br = a + (((b - a) * (int32_t)fr) >> 8);
            fx_tape_w = (w + 1u) & (FX_TAPE_LEN - 1u);
            if (FXP[FX_TAPE_SAT]) {
                bl = drive(bl, sat_g);
                br = drive(br, sat_g);
                bl = lp1(&fx_tape_hl, bl, 30000);    /* a touch of head loss */
                br = lp1(&fx_tape_hr, br, 30000);
            }
        }
        /* ECHO and SPACE at half rate: average two frames in, interpolate out */
        fx_hr_acc_e += se;
        fx_hr_acc_s += ss;
        if (fx_hr_phase) {
            fx_echo_prev_l = fx_echo_cur_l; fx_echo_prev_r = fx_echo_cur_r;
            fx_sp_prev_l = fx_sp_cur_l; fx_sp_prev_r = fx_sp_cur_r;
            if (fx_hr_acc_e || fx_hold)
                fx_echo_live = FX_ECHO_LEN * 8u;     /* the line holds sound for a while after any input */
            if (fx_echo_live) {
                fx_echo_live--;
                fx_echo_step(SAT15(fx_hr_acc_e >> 1));
            } else
                fx_echo_cur_l = fx_echo_cur_r = 0;
            if (fx_hr_acc_s)
                fx_sp_live = 22050u * 8u;
            if (fx_sp_live) {
                fx_sp_live--;
                fx_space_step(SAT15(fx_hr_acc_s >> 1), sp_g, sp_damp, sp_pre);
            } else
                fx_sp_cur_l = fx_sp_cur_r = 0;
            fx_hr_acc_e = fx_hr_acc_s = 0;
            l = bl + fx_echo_cur_l + fx_sp_cur_l;
            r = br + fx_echo_cur_r + fx_sp_cur_r;
            fx_hr_phase = 0;
        } else {
            l = bl + ((fx_echo_cur_l + fx_echo_prev_l) >> 1) + ((fx_sp_cur_l + fx_sp_prev_l) >> 1);
            r = br + ((fx_echo_cur_r + fx_echo_prev_r) >> 1) + ((fx_sp_cur_r + fx_sp_prev_r) >> 1);
            fx_hr_phase = 1;
        }
        /* GLUE: feed-forward compressor: the peak is read here, the gain computed at block rate (below) */
        {
            int32_t pk = ABS(l) > ABS(r) ? ABS(l) : ABS(r);
            if (pk > fx_glue_peak) fx_glue_peak = pk;
        }
        l = (l * fx_glue_gain) >> 15;
        r = (r * fx_glue_gain) >> 15;
        if (mlp_on) {
            l = lp1(&fx_mlp_l, l, mlp_k);
            r = lp1(&fx_mlp_r, r, mlp_k);
        }
        if (width != 4096) {
            int32_t mid = (l + r) >> 1, side = ((l - r) >> 1) * width >> 12;
            l = mid + side;
            r = mid - side;
        }
        l = fx_limit(l);
        r = fx_limit(r);
        out[2u * i] += l;
        out[2u * i + 1u] += r;
        if (wb_tape_rec && !fx_hr_phase) {       /* CATCH: the master, mono, at half rate */
            uint32_t wp = wb_tape_wpos;
            if (wp < WB_TAPE_LEN) {
                wb_tape[wp] = (int16_t)((l + r) >> 1);
                wb_tape_wpos = wp + 1u;
            } else
                wb_tape_rec = 0;
        }
    }
    /* ---- GLUE gain computer, once a block: threshold -6 dBFS, ratio from FX_GLUE, 5 ms attack, 150 ms release */
    {
        int32_t thr = 16384, want = 32767, pk = fx_glue_peak;
        fx_glue_peak = 0;
        if (pk > thr && glue_r_q8 > 256) {
            int32_t over = pk - thr, target = thr + ((over * 256) / glue_r_q8);
            want = (int32_t)(((uint32_t)target << 15) / (uint32_t)pk);    /* pk > thr > 0 */
            if (want > 32767) want = 32767;
        }
        if (want < fx_glue_gain) fx_glue_gain += ((want - fx_glue_gain) * 4800) >> 15;   /* ~5 ms */
        else fx_glue_gain += ((want - fx_glue_gain) * 160) >> 15;                         /* ~150 ms */
        if (fx_glue_gain > 32767) fx_glue_gain = 32767;
        if (fx_glue_gain < 1024) fx_glue_gain = 1024;
    }
}
