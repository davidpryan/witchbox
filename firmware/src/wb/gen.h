/* SPDX-License-Identifier: GPL-3.0-only */
/* WitchBox generators (main loop). Every generator writes ordinary steps into the pattern being edited,
 * so what it makes can be frozen, edited by hand and undone. Nothing here runs in the ISR.
 *
 *   SEANCE   chord walk over scale degrees           PADS KEYS
 *   HAUNT    Markov vocal phrases with gaps          GHOST
 *   HEX      locked random (a Turing machine)        any track, any lane
 *   EUCLID   Euclidean hits per lane                 DRUMS
 *   MUTATE   change a few steps, with undo           any
 *   CONTOUR  shaped scale melody / sub line          KEYS SUB (GHOST too)
 *   ANSWER   GHOST replies to KEYS                   GHOST
 *   SUMMON   a whole piece in a mood                 everything
 * Each track has a generator, four knobs (A, B, DENSITY, SEED) and an AUTO flag (recast every bar). */
#pragma once
#include "types.h"

enum { GEN_OFF = 0, GEN_SEANCE, GEN_HAUNT, GEN_HEX, GEN_EUCLID, GEN_MUTATE, GEN_CONTOUR, GEN_ANSWER, WB_NGEN };
static const char *const WB_GEN_NAME[WB_NGEN] = {"OFF", "SEANCE", "HAUNT", "HEX", "EUCLID", "MUTATE", "CONTOUR", "ANSWER"};
static const char *const WB_GEN_KNOB[WB_NGEN][2] = {
    {"-", "-"}, {"DRIFT", "GRAVITY"}, {"GAP", "ECHO"}, {"LOCK", "LENGTH"}, {"ROTATE", "SLOPE"},
    {"FLIP", "WANDER"}, {"SHAPE", "RANGE"}, {"DELAY", "SPREAD"}};
static const char *const WB_MOOD_NAME[4] = {"NIGHT", "RAIN", "CHURCH", "TUNNEL"};

typedef struct {
    uint8_t kind, auto_run, k[4];     /* k: A, B, DENSITY, SEED */
    uint8_t lane;                     /* DRUMS: which lane HEX / MUTATE work on (0xFF = all) */
    uint8_t pad;
    uint16_t hex;                     /* HEX shift register */
    int16_t degree;                   /* SEANCE: where the walk is */
    uint8_t last_slice;               /* HAUNT */
} wb_gen_t;
static wb_gen_t GEN[WB_NTRACK];
static uint32_t gen_last_bar;         /* for AUTO */
static wb_pattern_t gen_undo WB_POOL; /* MUTATE's memory: the pattern before the last cast */
static uint8_t gen_undo_valid;

/* ---- helpers */
static inline wb_pattern_t *gen_pat(void) { return &PAT[G.pattern < WB_NPAT ? G.pattern : 0]; }
static inline uint32_t gen_rnd(uint32_t n) { return n ? rnd_main() % n : 0; }   /* 0..n-1 */
static inline int gen_pct(uint32_t pct) { return RND_PCT(rnd_main()) < pct; }
static void gen_seed_from(uint32_t t)
{
    uint32_t s = (uint32_t)G.seed[0] | (uint32_t)G.seed[1] << 8 | (uint32_t)G.seed[2] << 16 | (uint32_t)G.seed[3] << 24;
    rnd_seed(s ^ (uint32_t)GEN[t].k[3] * 2654435761u ^ (t + 1u) * 40503u ^ wb_pos.bar * 7919u);
}
static void gen_clear_track(wb_trackpat_t *tp, uint32_t len)
{
    uint32_t i;
    for (i = 0; i < WB_NSTEP; i++) { tp->s[i].note = 0; tp->s[i].vel = 0; tp->s[i].pl = 0xFF; tp->s[i].chance = 100; tp->s[i].flags = 0; tp->s[i].micro = 0; tp->s[i].slice = 0; tp->s[i].len = 4; }
    tp->len = (uint8_t)(len ? len : 16u);
}
static void gen_clear_lane(wb_lanepat_t *lp, uint32_t len)
{
    uint32_t i;
    for (i = 0; i < WB_NSTEP; i++) { lp->s[i].note = 0; lp->s[i].vel = 0; lp->s[i].pl = 0xFF; lp->s[i].chance = 100; lp->s[i].flags = 0; lp->s[i].micro = 0; lp->s[i].slice = 0; lp->s[i].len = 4; }
    lp->len = (uint8_t)(len ? len : 16u);
}
static void gen_set(wb_step_t *s, uint32_t note, uint32_t vel, uint32_t len, uint32_t slice)
{
    s->note = (uint8_t)note; s->vel = (uint8_t)vel; s->len = (uint8_t)len; s->slice = (uint8_t)slice;
    s->chance = 100; s->flags = 0; s->micro = 0; s->pl = 0xFF;
}
static uint32_t scale_count(void)
{
    uint32_t i, c = 0, bits = WB_SCALES[G.scale % WB_NSCALE].bits;
    for (i = 0; i < 12u; i++) c += (bits >> i) & 1u;
    return c ? c : 1u;
}
/* the chord shape that stacks thirds on scale degree d (minor / major / sus by what the scale gives) */
static uint32_t gen_shape_for(int32_t d, uint32_t seventh)
{
    int32_t r = scale_note(G.root, G.scale, 4, d), third = scale_note(G.root, G.scale, 4, d + 2) - r,
            fifth = scale_note(G.root, G.scale, 4, d + 4) - r, sev = scale_note(G.root, G.scale, 4, d + 6) - r;
    if (fifth == 7 && third == 3) return seventh ? (sev == 10 ? 3u : 1u) : 1u;
    if (fifth == 7 && third == 4) return seventh ? (sev == 11 ? 4u : 2u) : 2u;
    if (fifth == 6) return 12u;          /* diminished: a quartal stand-in reads dark */
    if (fifth == 8) return 13u;
    return 8u;                            /* a bare fifth */
}

/* ---- SEANCE: a weighted random walk over degrees, voicings that move as a shape */
static void gen_seance(uint32_t t)
{
    wb_trackpat_t *tp = &gen_pat()->t[t];
    wb_gen_t *g = &GEN[t];
    uint32_t drift = g->k[0], gravity = g->k[1], per_bar = g->k[2] < 86u ? 1u : g->k[2] < 171u ? 2u : 4u, bar, k;
    int32_t cnt = (int32_t)scale_count(), base_oct = t == T_PADS ? 3 : 4, prev_note = -1;
    gen_seed_from(t);
    gen_clear_track(tp, 64);
    tp->div = 0;
    for (bar = 0; bar < 4u; bar++)
        for (k = 0; k < per_bar; k++) {
            int32_t d = g->degree, note, step = (int32_t)(bar * 16u + k * (16u / per_bar)), len = (int32_t)(64u / per_bar);
            if (gen_pct(30u + drift * 60u / 255u)) {    /* move */
                if (d != 0 && gen_pct(gravity * 70u / 255u)) d = 0;   /* home */
                else {
                    static const int8_t MOVES[8] = {-1, 1, -2, 2, 3, -3, 4, -4};
                    d += MOVES[gen_rnd(gen_pct(60u) ? 2u : 8u)];
                }
                if (d > cnt) d -= cnt;
                if (d < -cnt) d += cnt;
            }
            g->degree = (int16_t)d;
            note = scale_note(G.root, G.scale, base_oct, d);
            if (prev_note >= 0) {                        /* voice leading: stay within a fifth of the last root */
                while (note - prev_note > 7) note -= 12;
                while (prev_note - note > 7) note += 12;
            }
            note = CLAMP(note, 24, 96);
            prev_note = note;
            gen_set(&tp->s[step], (uint32_t)note, 80u + gen_rnd(30u), (uint32_t)len, gen_shape_for(d, t == T_KEYS || gen_pct(40u)));
            if (gen_pct(drift * 25u / 255u)) tp->s[step].flags |= 1u << 7;   /* a throw now and then */
        }
}

/* ---- HAUNT: a first-order Markov chain over the slices already placed; gaps and echoes */
static void gen_haunt(uint32_t t)
{
    wb_trackpat_t *tp = &gen_pat()->t[t];
    wb_gen_t *g = &GEN[t];
    static uint8_t trans[16][16];
    uint32_t i, j, gap = g->k[0], echo = g->k[1], density = g->k[2], n = 0, len = tp->len ? tp->len : 32u;
    int32_t last = -1;
    static const int8_t OFFS[8] = {0, 0, 3, -2, 7, -5, 5, 12};   /* pitch offsets, in the key's colour */
    gen_seed_from(t);
    memset(trans, 0, sizeof trans);
    for (i = 0; i < len; i++) {                      /* learn from what is there */
        if (!tp->s[i].note) continue;
        if (last >= 0) trans[last][tp->s[i].slice & 15u]++;
        last = (int32_t)(tp->s[i].slice & 15u);
        n++;
    }
    if (n < 2u)                                       /* nothing to learn from: a mild default chain */
        for (i = 0; i < 16u; i++) { trans[i][(i + 1u) & 15u] = 3; trans[i][(i + 5u) & 15u] = 2; trans[i][i] = 1; }
    gen_clear_track(tp, len);
    last = g->last_slice & 15;
    for (i = 0; i < len;) {
        uint32_t phrase = 2u + gen_rnd(3u), k;
        if (!gen_pct(20u + density * 70u / 255u)) { i += 2u; continue; }   /* silence */
        for (k = 0; k < phrase && i < len; k++) {
            uint32_t next, sum = 0, pick, acc = 0;
            if (gen_pct(echo * 60u / 255u) && last >= 0) next = (uint32_t)last;   /* echo the last fragment */
            else if (gen_pct(10u)) next = gen_rnd(16u);                           /* stray */
            else {
                for (j = 0; j < 16u; j++) sum += trans[last][j];
                if (!sum) next = gen_rnd(16u);
                else {
                    pick = gen_rnd(sum);
                    next = 15u;
                    for (j = 0; j < 16u; j++) { acc += trans[last][j]; if (pick < acc) { next = j; break; } }
                }
            }
            gen_set(&tp->s[i], (uint32_t)(60 + OFFS[gen_rnd(8u)] + (next == (uint32_t)last && gen_pct(50u) ? (gen_pct(50u) ? 2 : -2) : 0)),
                    70u + gen_rnd(40u), 4u + gen_rnd(2u) * 4u, next);
            last = (int32_t)next;
            i += 1u + (gen_pct(50u) ? 1u : 0u);
        }
        i += 1u + gen_rnd(1u + gap * 6u / 255u);     /* the gap after a phrase */
        g->last_slice = (uint8_t)last;
    }
}

/* ---- HEX: a Turing machine. One register a track; LOCK keeps it, LENGTH loops it. */
static uint32_t gen_hex_clock(wb_gen_t *g, uint32_t lock, uint32_t len)
{
    uint32_t out = g->hex & 1u, back = out;
    if (!gen_pct(lock * 100u / 255u)) back ^= 1u;    /* unlocked: the bit may flip */
    g->hex = (uint16_t)((g->hex >> 1) | (back << (len - 1u)));
    return out;
}
static void gen_hex(uint32_t t)
{
    wb_gen_t *g = &GEN[t];
    uint32_t lock = g->k[0], len = 2u + g->k[1] * 14u / 255u, density = g->k[2], i, lane = g->lane;
    int32_t cnt = (int32_t)scale_count();
    gen_seed_from(t);
    if (!g->hex) g->hex = (uint16_t)(rnd_main() | 1u);
    if (t == T_DRUMS) {
        uint32_t l0 = lane == 0xFFu ? 0u : lane, l1 = lane == 0xFFu ? WB_NLANE : lane + 1u, l;
        for (l = l0; l < l1; l++) {
            wb_lanepat_t *lp = &gen_pat()->lane[l];
            gen_clear_lane(lp, 16);
            for (i = 0; i < 16u; i++) {
                uint32_t bit = gen_hex_clock(g, lock, len), v = g->hex & 0xFu;
                if (bit && v * 16u < density + 16u)
                    gen_set(&lp->s[i], 1, 60u + (g->hex >> 4 & 63u), 4, 0);
            }
        }
        return;
    }
    {
        wb_trackpat_t *tp = &gen_pat()->t[t];
        uint32_t steps = tp->len ? tp->len : 16u;
        gen_clear_track(tp, steps);
        for (i = 0; i < steps; i++) {
            uint32_t bit = gen_hex_clock(g, lock, len), v = g->hex & 0xFu;
            if (bit && v * 16u < density + 16u) {
                int32_t deg = (int32_t)((g->hex >> 4) & 7u) % cnt, note;
                if (t == T_GHOST) note = 60 + (int32_t)((g->hex >> 8) & 7u) - 3;
                else note = scale_note(G.root, G.scale, t == T_SUB ? 2 : t == T_PADS ? 3 : 4, deg);
                gen_set(&tp->s[i], (uint32_t)CLAMP(note, 24, 100), 64u + (g->hex >> 9 & 63u), t == T_PADS ? 16u : 4u,
                        t == T_GHOST ? (g->hex >> 11 & 15u) : (t == T_PADS ? 1u + (g->hex >> 12 & 3u) : 0u));
            }
        }
    }
}

/* ---- EUCLID: hits per lane from a feel template, rotated and sloped */
static inline int euclid_hit(uint32_t i, uint32_t k, uint32_t n) { return n && ((i * k) % n) < k; }
static void gen_euclid(uint32_t t)
{
    static const uint8_t HITS_2STEP[WB_NLANE] = {3, 2, 3, 10, 2, 5, 7, 1};
    static const uint8_t ROT_2STEP[WB_NLANE] = {0, 4, 7, 1, 3, 5, 2, 0};
    static const uint8_t HITS_HALF[WB_NLANE] = {2, 1, 2, 8, 1, 4, 6, 1};
    static const uint8_t ROT_HALF[WB_NLANE] = {0, 8, 10, 1, 5, 3, 2, 0};
    wb_gen_t *g = &GEN[t];
    uint32_t rot = g->k[0] * 16u / 256u, slope = g->k[1], density = g->k[2], l, i;
    (void)t;
    gen_seed_from(T_DRUMS);
    for (l = 0; l < WB_NLANE; l++) {
        wb_lanepat_t *lp = &gen_pat()->lane[l];
        uint32_t base = G.feel ? HITS_HALF[l] : HITS_2STEP[l], r = (G.feel ? ROT_HALF[l] : ROT_2STEP[l]) + (l == L_KICK ? 0u : rot);
        uint32_t k = (base * (64u + density * 3u / 4u)) / 128u;   /* density 128 = the template */
        if (l == L_SNARE || l == L_KICK) k = base;                /* the backbone keeps its count */
        if (k > 16u) k = 16u;
        if (l == L_CRASH && density < 200u) k = 0;
        gen_clear_lane(lp, 16);
        for (i = 0; i < 16u; i++) {
            uint32_t j = (i + 16u - (r % 16u)) % 16u, vel;
            if (!euclid_hit(j, k, 16u)) continue;
            vel = i % 4u == 0u ? 100u + gen_rnd(27u) : 60u + gen_rnd(40u);
            if (l == L_HATC || l == L_FOUND1 || l == L_FOUND2) vel = 40u + gen_rnd(50u) + (i % 4u == 2u ? 20u : 0u);
            if (slope > 128u && i >= 8u) vel = vel * (128u + (slope - 128u)) / 256u + vel / 2u;   /* louder second half */
            if (slope < 128u && i >= 8u) vel = vel * (slope + 64u) / 192u;                       /* quieter */
            gen_set(&lp->s[i], 1, CLAMP(vel, 1u, 127u), 4, 0);
            if (l == L_FOUND1 || l == L_FOUND2) lp->s[i].chance = (uint8_t)(50u + gen_rnd(50u));
            if (l == L_HATO) lp->s[i].chance = 70;
            if (l == L_KICK && !G.feel && i == 10u) lp->s[i].micro = 2;   /* the 2-step kick that drags */
        }
    }
}

/* ---- MUTATE: change N steps; remember the pattern before */
static void gen_mutate(uint32_t t)
{
    wb_gen_t *g = &GEN[t];
    uint32_t flip = g->k[0], wander = g->k[1], n = 1u + g->k[2] * 7u / 255u, i;
    gen_seed_from(t);
    memcpy(&gen_undo, gen_pat(), sizeof gen_undo);
    gen_undo_valid = 1;
    for (i = 0; i < n; i++) {
        if (t == T_DRUMS) {
            uint32_t l = g->lane == 0xFFu ? gen_rnd(WB_NLANE) : g->lane;
            wb_lanepat_t *lp = &gen_pat()->lane[l];
            uint32_t len = lp->len ? lp->len : 16u, st = gen_rnd(len);
            wb_step_t *s = &lp->s[st];
            if (gen_pct(flip * 100u / 255u)) { if (s->note) s->note = 0; else gen_set(s, 1, 50u + gen_rnd(60u), 4, 0); }
            else if (s->note) {
                s->vel = (uint8_t)CLAMP((int32_t)s->vel + (int32_t)gen_rnd(41u) - 20, 1, 127);
                s->micro = (int8_t)CLAMP((int32_t)s->micro + (int32_t)gen_rnd(7u) - 3, -12, 12);
                if (gen_pct(wander * 50u / 255u)) s->flags = (uint8_t)((s->flags & ~3u) | (gen_pct(70u) ? 0u : 1u + gen_rnd(2u)));
            }
        } else {
            wb_trackpat_t *tp = &gen_pat()->t[t];
            uint32_t len = tp->len ? tp->len : 16u, st = gen_rnd(len);
            wb_step_t *s = &tp->s[st];
            if (gen_pct(flip * 100u / 255u)) {
                if (s->note) s->note = 0;
                else gen_set(s, (uint32_t)CLAMP(t == T_GHOST ? 60 : scale_note(G.root, G.scale, t == T_SUB ? 2 : 4, (int32_t)gen_rnd(7u)), 24, 100),
                             60u + gen_rnd(50u), 4, t == T_GHOST ? gen_rnd(16u) : 0u);
            } else if (s->note) {
                s->vel = (uint8_t)CLAMP((int32_t)s->vel + (int32_t)gen_rnd(41u) - 20, 1, 127);
                s->micro = (int8_t)CLAMP((int32_t)s->micro + (int32_t)gen_rnd(7u) - 3, -12, 12);
                if (gen_pct(wander * 100u / 255u)) {
                    if (t == T_GHOST) s->slice = (uint8_t)gen_rnd(16u);
                    else {                                   /* a scale neighbour */
                        int32_t nn = (int32_t)s->note + (gen_pct(50u) ? 1 : -1), guard = 12;
                        while (!in_scale(G.root, G.scale, nn) && guard--) nn += nn > (int32_t)s->note ? 1 : -1;
                        s->note = (uint8_t)CLAMP(nn, 24, 100);
                    }
                }
            }
        }
    }
}
static void gen_undo_cast(void)
{
    if (!gen_undo_valid) return;
    memcpy(gen_pat(), &gen_undo, sizeof gen_undo);
    gen_undo_valid = 0;
}

/* ---- CONTOUR: a shaped melody from a rhythm pool; on SUB it follows the PADS roots */
static void gen_contour(uint32_t t)
{
    wb_trackpat_t *tp = &gen_pat()->t[t];
    wb_gen_t *g = &GEN[t];
    uint32_t shape = g->k[0] * 5u / 256u, range = 2u + g->k[1] * 12u / 255u, n = 2u + g->k[2] * 14u / 255u, i, k;
    static const uint8_t POOL[16] = {0, 3, 6, 7, 10, 11, 14, 15, 2, 4, 8, 12, 1, 5, 9, 13};   /* offbeat-leaning order */
    uint8_t pos[16], used[16] = {0};
    gen_seed_from(t);
    gen_clear_track(tp, 16);
    tp->div = 0;
    if (t == T_SUB) {                                    /* follow the pads: the root of each bar's chord, slides */
        wb_trackpat_t *pads = &gen_pat()->t[T_PADS];
        uint32_t slide = g->k[0];
        int32_t root = -1;
        gen_clear_track(tp, 64);
        for (i = 0; i < 64u; i++) {
            if (pads->s[i].note) root = (int32_t)pads->s[i].note;
            if (root < 0) root = scale_note(G.root, G.scale, 3, 0);
            if (i % 16u == 0u || (i % 16u == 10u && gen_pct(n * 6u)) || (i % 16u == 6u && gen_pct(n * 3u))) {
                int32_t nn = root;
                while (nn > 48) nn -= 12;
                while (nn < 29) nn += 12;
                gen_set(&tp->s[i], (uint32_t)nn, 100u + gen_rnd(27u), i % 16u == 0u ? 24u : 8u, 0);
                if (gen_pct(slide * 100u / 255u)) tp->s[i].flags |= 1u << 5;
            }
        }
        return;
    }
    for (i = 0, k = 0; i < 16u && k < n; i++)            /* positions from the pool, a little random */
        if (gen_pct(70u)) { pos[k++] = POOL[i]; used[POOL[i]] = 1; }
    for (i = 0; i < 16u && k < n; i++) if (!used[POOL[i]]) pos[k++] = POOL[i];
    for (i = 0; i < k; i++) {                            /* sort by position */
        uint32_t j;
        for (j = i + 1u; j < k; j++) if (pos[j] < pos[i]) { uint8_t tmp = pos[i]; pos[i] = pos[j]; pos[j] = tmp; }
    }
    for (i = 0; i < k; i++) {
        int32_t x = k > 1u ? (int32_t)(i * 255u / (k - 1u)) : 128, c;   /* 0..255 along the phrase */
        switch (shape) {
        case 0: c = x; break;                                  /* RISE */
        case 1: c = 255 - x; break;                            /* FALL */
        case 2: c = x < 128 ? x * 2 : (255 - x) * 2; break;    /* ARC */
        case 3: c = x < 128 ? 255 - x * 2 : (x - 128) * 2; break;   /* VALE */
        default: c = 128; break;                               /* FLAT */
        }
        {
            int32_t deg = (c * (int32_t)range) / 255 + (int32_t)gen_rnd(3u) - 1, note;
            note = scale_note(G.root, G.scale, t == T_GHOST ? 5 : 4, deg);
            if (t == T_GHOST) note = 60 + (note - scale_note(G.root, G.scale, 5, 0));
            gen_set(&tp->s[pos[i]], (uint32_t)CLAMP(note, 24, 100), 70u + gen_rnd(50u), gen_pct(30u) ? 8u : 4u,
                    t == T_GHOST ? gen_rnd(16u) : 0u);
        }
    }
}

/* ---- ANSWER: GHOST replies to the KEYS phrase in the next bar, same contour, random slices */
static void gen_answer(uint32_t t)
{
    wb_trackpat_t *keys = &gen_pat()->t[T_KEYS], *gh = &gen_pat()->t[T_GHOST];
    wb_gen_t *g = &GEN[t];
    uint32_t delay = g->k[0] < 128u ? 16u : 32u, spread = g->k[1], n = 1u + g->k[2] * 7u / 255u, i, k = 0, cnt = scale_count();
    int32_t first = -1;
    (void)cnt;
    gen_seed_from(t);
    gen_clear_track(gh, delay == 16u ? 32u : 48u);
    for (i = 0; i < 16u && k < n; i++) {
        const wb_step_t *s = &keys->s[i];
        int32_t rel;
        if (!s->note) continue;
        if (first < 0) first = (int32_t)s->note;
        rel = ((int32_t)s->note - first) * (int32_t)(64u + spread * 191u / 255u) / 128;   /* the contour, scaled */
        gen_set(&gh->s[delay + i], (uint32_t)CLAMP(60 + rel, 48, 72), s->vel, s->len ? s->len : 4u, gen_rnd(16u));
        if (gen_pct(20u)) gh->s[delay + i].flags |= 1u << 7;
        k++;
    }
}

/* ---- the dispatcher */
static void gen_cast(uint32_t t)
{
    switch (GEN[t].kind) {
    case GEN_SEANCE: if (t == T_PADS || t == T_KEYS) gen_seance(t); break;
    case GEN_HAUNT: if (t == T_GHOST) gen_haunt(t); break;
    case GEN_HEX: if (t != T_WEATHER) gen_hex(t); break;
    case GEN_EUCLID: if (t == T_DRUMS) gen_euclid(t); break;
    case GEN_MUTATE: if (t != T_WEATHER) gen_mutate(t); break;
    case GEN_CONTOUR: if (t == T_KEYS || t == T_SUB || t == T_GHOST) gen_contour(t); break;
    case GEN_ANSWER: if (t == T_GHOST) gen_answer(t); break;
    default: break;
    }
}
/* which generators a track offers */
static int gen_allowed(uint32_t t, uint32_t kind)
{
    switch (kind) {
    case GEN_OFF: return 1;
    case GEN_SEANCE: return t == T_PADS || t == T_KEYS;
    case GEN_HAUNT: return t == T_GHOST;
    case GEN_HEX: return t != T_WEATHER;
    case GEN_EUCLID: return t == T_DRUMS;
    case GEN_MUTATE: return t != T_WEATHER;
    case GEN_CONTOUR: return t == T_KEYS || t == T_SUB || t == T_GHOST;
    case GEN_ANSWER: return t == T_GHOST;
    default: return 0;
    }
}
/* AUTO: recast every bar, from the main loop */
static void gen_auto_tick(void)
{
    uint32_t t, bar = wb_pos.bar;
    if (!wb_pos.playing || bar == gen_last_bar) return;
    gen_last_bar = bar;
    for (t = 0; t < WB_NTRACK; t++)
        if (GEN[t].auto_run && GEN[t].kind) gen_cast(t);
}

/* ---- WEATHER drift: density and tone wander, and follow the section */
static int32_t gen_weather_base = -1, gen_weather_walk;
static void gen_weather_drift(void)
{
    int32_t target, d;
    if (gen_weather_base < 0) gen_weather_base = P[T_WEATHER][PW_DENSITY];
    gen_weather_walk += (int32_t)gen_rnd(9u) - 4;
    gen_weather_walk = CLAMP(gen_weather_walk, -40, 40);
    target = gen_weather_base + gen_weather_walk / 2;
    if (wb_pos.playing) {
        if (wb_pos.section == SEC_BREAK) target += 40;
        if (wb_pos.section == SEC_DROP) target -= 30;
    }
    target = CLAMP(target, 0, 255);
    d = target - (int32_t)P[T_WEATHER][PW_DENSITY];
    if (d) P[T_WEATHER][PW_DENSITY] = (uint8_t)((int32_t)P[T_WEATHER][PW_DENSITY] + (d > 0 ? 1 : -1));
    if (gen_pct(1u)) weather_event(gen_pct(50u) ? 0u : 1u);   /* now and then: a gust or a crackle */
}

/* ---- SUMMON: a whole piece */
static void gen_summon(uint32_t mood, uint32_t keep_drums)
{
    static const uint8_t MOOD_ROOT[4] = {9, 2, 4, 7};        /* A, D, E, G */
    static const uint8_t MOOD_SCALE[4] = {0, 1, 2, 5};       /* minor, dorian, phrygian, minor pent */
    static const uint16_t MOOD_BPM[4] = {134, 130, 138, 142};
    uint32_t p, t, save_pat = G.pattern;
    wb_gen_t keep[WB_NTRACK];
    memcpy(keep, GEN, sizeof keep);
    G.root = MOOD_ROOT[mood & 3u];
    G.scale = MOOD_SCALE[mood & 3u];
    G.bpm = MOOD_BPM[mood & 3u];
    G.swing = (uint8_t)(mood == 1u ? 62 : 56);
    G.loose = (uint8_t)(mood == 2u ? 35 : 55);
    G.feel = mood == 3u ? 1 : 0;
    /* sound: a sketch per mood */
    P[T_PADS][PP_CUTOFF] = (uint8_t)(mood == 2u ? 150 : 90); P[T_PADS][PP_DISTANCE] = (uint8_t)(mood == 1u ? 170 : 110);
    P[T_PADS][PP_VOICES] = (uint8_t)(mood == 3u ? 255 : 150); P[T_PADS][PP_ATTACK] = 120; P[T_PADS][PP_RELEASE] = 170;
    P[T_PADS][PC_SPACE] = 150; P[T_PADS][PC_ECHO] = 60; P[T_PADS][PC_DUST] = (uint8_t)(mood == 1u ? 140 : 90);
    P[T_SUB][PS_DECAY] = 140; P[T_SUB][PS_REESE] = (uint8_t)(mood == 3u ? 160 : 0); P[T_SUB][PS_CUTOFF] = 110;
    P[T_SUB][PS_WOBBLE_RATE] = (uint8_t)(mood == 3u ? 130 : 0); P[T_SUB][PS_WOBBLE_DEPTH] = 150;
    P[T_KEYS][PK_PATCH] = (uint8_t)(mood == 2u ? 40 : mood == 1u ? 90 : 10); P[T_KEYS][PC_SPACE] = 170; P[T_KEYS][PC_ECHO] = 110;
    P[T_GHOST][PG_MODE] = 40; P[T_GHOST][PG_FORMANT] = (uint8_t)(mood == 1u ? 150 : 110); P[T_GHOST][PG_DROPOUT] = 25;
    P[T_GHOST][PG_STRETCH] = (uint8_t)(mood == 0u ? 60 : 20); P[T_GHOST][PC_SPACE] = 200; P[T_GHOST][PC_ECHO] = 120;
    P[T_DRUMS][PD_KIT] = (uint8_t)(mood == 3u ? 200 : mood == 2u ? 128 : 30); P[T_DRUMS][PD_LATE_SNARE] = (uint8_t)(G.feel ? 20 : 70);
    P[T_DRUMS][PC_DUST] = (uint8_t)(mood == 1u ? 160 : 100); P[T_DRUMS][PC_SPACE] = 50;
    P[T_WEATHER][PW_RAIN] = (uint8_t)(mood == 1u ? 200 : mood == 0u ? 90 : 40); P[T_WEATHER][PW_CRACKLE] = (uint8_t)(mood == 0u ? 160 : 80);
    P[T_WEATHER][PW_HISS] = 60; P[T_WEATHER][PW_DRONE] = (uint8_t)(mood == 2u ? 150 : mood == 3u ? 100 : 30);
    P[T_WEATHER][PW_DENSITY] = 128; P[T_WEATHER][PW_FIELD] = 40;
    gen_weather_base = 128;
    FXP[FX_DUST_BITS] = (uint8_t)(mood == 1u ? 120 : 70); FXP[FX_DUST_RATE] = 60; FXP[FX_DUST_HISS] = 50;
    FXP[FX_TAPE_WOW] = (uint8_t)(mood == 0u ? 120 : 70); FXP[FX_TAPE_SAT] = 90;
    FXP[FX_ECHO_TIME] = 2; FXP[FX_ECHO_FB] = 150; FXP[FX_ECHO_TONE] = 110;
    FXP[FX_SPACE_SIZE] = (uint8_t)(mood == 2u ? 230 : 160); FXP[FX_SPACE_DAMP] = 120; FXP[FX_SPACE_PRE] = 60;
    FXP[FX_DUCK_REL] = 110; FXP[FX_DUCK_DEPTH] = 180; FXP[FX_GLUE] = 120; FXP[FX_MASTER_LP] = 255; FXP[FX_WIDTH] = 160;
    for (t = 0; t < WB_NTRACK; t++) { P[t][PC_LEVEL] = 200; P[t][PC_DUCK] = 0; P[t][PC_FILT] = 128; }
    P[T_PADS][PC_DUCK] = 220; P[T_KEYS][PC_DUCK] = 120; P[T_WEATHER][PC_DUCK] = 80; P[T_SUB][PC_DUCK] = 140;
    P[T_WEATHER][PC_LEVEL] = 150; P[T_SUB][PC_LEVEL] = 230; P[T_GHOST][PC_LEVEL] = 190;
    /* patterns A..D: intro, main, drop, break */
    for (p = 0; p < 4u; p++) {
        G.pattern = (uint8_t)p;
        G.seed[0] = (uint8_t)rnd_main(); G.seed[1] = (uint8_t)(rnd_main() >> 8);
        GEN[T_PADS].kind = GEN_SEANCE; GEN[T_PADS].k[0] = (uint8_t)(p == 2u ? 160 : 90); GEN[T_PADS].k[1] = 140; GEN[T_PADS].k[2] = (uint8_t)(p == 2u ? 120 : 40);
        GEN[T_PADS].degree = 0;
        gen_cast(T_PADS);
        GEN[T_SUB].kind = GEN_CONTOUR; GEN[T_SUB].k[0] = 90; GEN[T_SUB].k[2] = (uint8_t)(p == 2u ? 200 : 60);
        gen_cast(T_SUB);
        GEN[T_KEYS].kind = GEN_CONTOUR; GEN[T_KEYS].k[0] = (uint8_t)(p * 60u); GEN[T_KEYS].k[1] = 120; GEN[T_KEYS].k[2] = (uint8_t)(p == 3u ? 140 : p == 0u ? 40 : 90);
        gen_cast(T_KEYS);
        if (!keep_drums) {
            GEN[T_DRUMS].kind = GEN_EUCLID; GEN[T_DRUMS].k[0] = (uint8_t)(p * 40u); GEN[T_DRUMS].k[1] = 128; GEN[T_DRUMS].k[2] = (uint8_t)(p == 2u ? 200 : p == 3u ? 60 : 128);
            gen_cast(T_DRUMS);
            if (p == 0u || p == 3u) { gen_pat()->lane[L_KICK].len = 16; memset(gen_pat()->lane[L_KICK].s, 0, sizeof gen_pat()->lane[L_KICK].s); }   /* no kick in intro / break */
        }
        GEN[T_GHOST].kind = p == 1u ? GEN_ANSWER : GEN_HAUNT; GEN[T_GHOST].k[0] = (uint8_t)(p == 3u ? 200 : 120); GEN[T_GHOST].k[1] = 100; GEN[T_GHOST].k[2] = (uint8_t)(p == 0u ? 50 : 130);
        gen_cast(T_GHOST);
    }
    G.pattern = (uint8_t)(save_pat < 4u ? save_pat : 1u);
    /* the song */
    SONG[0].pat = 0; SONG[0].section = SEC_INTRO; SONG[0].bars = 4; SONG[0].mute = 0;
    SONG[1].pat = 1; SONG[1].section = SEC_BUILD; SONG[1].bars = 4; SONG[1].mute = 0;
    SONG[2].pat = 2; SONG[2].section = SEC_DROP; SONG[2].bars = 8; SONG[2].mute = 0;
    SONG[3].pat = 3; SONG[3].section = SEC_BREAK; SONG[3].bars = 4; SONG[3].mute = 0;
    SONG[4].pat = 2; SONG[4].section = SEC_DROP; SONG[4].bars = 8; SONG[4].mute = 0;
    SONG[5].pat = 0; SONG[5].section = SEC_OUTRO; SONG[5].bars = 4; SONG[5].mute = 0;
    SONG[6].pat = 0xFF;
    for (t = 0; t < WB_NTRACK; t++) { GEN[t].auto_run = 0; }
    (void)keep;
}
static void gen_init(void)
{
    uint32_t t;
    for (t = 0; t < WB_NTRACK; t++) {
        GEN[t].kind = GEN_OFF;
        GEN[t].k[0] = 128; GEN[t].k[1] = 128; GEN[t].k[2] = 128; GEN[t].k[3] = (uint8_t)t;
        GEN[t].lane = 0xFF;
        GEN[t].hex = (uint16_t)(0xACE1u + t * 977u);
    }
}
