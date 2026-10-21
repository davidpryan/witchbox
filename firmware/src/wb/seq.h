/* SPDX-License-Identifier: GPL-3.0-only */
/* WitchBox sequencer: runs in the audio ISR at 96 PPQ. Reads the patterns the main loop writes.
 *
 * Every step is evaluated one 16th (SEQ_LOOK ticks) before its nominal time, so a hit may land early
 * (negative micro timing, drift). Evaluation decides chance and conditions, computes the offset
 * (micro + swing + LOOSE drift + late snare), and schedules the hit (and its note-off, and ratchets)
 * in a small event queue. Each tick fires what is due. The LOOSE system is the point of this file. */
#pragma once
#include "types.h"

#define SEQ_PPQ 96u
#define SEQ_STEP 24u                   /* ticks per 16th */
#define SEQ_BAR 384u                   /* ticks per bar */
#define SEQ_LOOK 24u                   /* evaluate a step this many ticks ahead */
#define SEQ_QN 64u                     /* event queue entries */

enum { EV_FREE = 0, EV_ON, EV_OFF, EV_HIT, EV_RESEED };
typedef struct {
    uint32_t due;                      /* absolute tick */
    uint8_t kind, track, a, b;         /* a = note or lane, b = vel */
    uint8_t slice, pl, flags, pad;
} seq_ev_t;

static struct {
    uint32_t playing, tick, acc;       /* acc: frames * 256 toward the next tick */
    uint32_t bar, bar_tick;            /* bar count and tick within the bar */
    uint32_t pat;                      /* the pattern playing */
    uint32_t fill, fill_next;          /* fill active this bar / requested for the next */
    uint32_t first;                    /* first pass since start / pattern change (COND_FIRST) */
    uint32_t section, song_row, song_bars_left;
    struct {                           /* per pitched track and per drum lane: the playhead */
        uint32_t step, eval_tick;      /* next step to evaluate, its nominal tick */
        int32_t walk;                  /* LOOSE random walk, ticks * 16 */
        int32_t transpose;             /* COVEN UP/DOWN for the next hit, semitones */
        uint32_t mute_until;           /* COVEN MUTE */
        uint8_t last_note;
    } ph[WB_NTRACK + WB_NLANE];        /* 0..5 tracks, 6..13 lanes (lane i at WB_NTRACK + i) */
    seq_ev_t q[SEQ_QN];
    uint32_t qn;
    uint32_t duck_seen;
} seq;

/* ---- helpers */
static inline uint32_t seq_pat(void) { return seq.pat < WB_NPAT ? seq.pat : 0; }
static inline uint32_t step_ticks(uint32_t div) { return div == 1 ? 48u : div == 2 ? 12u : 24u; }

static void seq_push(uint32_t due, uint32_t kind, uint32_t track, uint32_t a, uint32_t b, uint32_t slice, uint32_t pl, uint32_t flags)
{
    uint32_t i;
    for (i = 0; i < SEQ_QN; i++)
        if (seq.q[i].kind == EV_FREE) {
            seq.q[i].due = due;
            seq.q[i].kind = (uint8_t)kind;
            seq.q[i].track = (uint8_t)track;
            seq.q[i].a = (uint8_t)a;
            seq.q[i].b = (uint8_t)b;
            seq.q[i].slice = (uint8_t)slice;
            seq.q[i].pl = (uint8_t)pl;
            seq.q[i].flags = (uint8_t)flags;
            return;
        }
}

/* the effective sound params of a track with a lock applied */
static void seq_params(uint32_t t, uint32_t pl, uint8_t *p8)
{
    uint32_t i;
    for (i = 0; i < 8u; i++) p8[i] = P[t][i];
    plock_apply(p8, (uint8_t)pl);
}

/* ---- engines in, by track */
static void eng_note_on(uint32_t t, uint32_t note, uint32_t vel, uint32_t slice, uint32_t pl)
{
    uint8_t p8[8];
    if (note > 127u || !vel) return;
    seq_params(t, pl, p8);
    switch (t) {
    case T_GHOST: ghost_note_on((uint8_t)note, (uint8_t)vel, p8, (uint8_t)(slice & 15u)); break;
    case T_SUB: sub_note_on((uint8_t)note, (uint8_t)vel, p8, 0); break;
    case T_PADS: case T_KEYS: {
        const int8_t *c = WB_CHORD[slice & 15u];
        uint32_t k;
        for (k = 0; k < 4u; k++) {
            int32_t n = (int32_t)note + c[k];
            if (k && !c[k]) break;
            if (n > 127) break;
            if (t == T_PADS) pads_note_on((uint8_t)n, (uint8_t)vel, p8, 0);
            else keys_note_on((uint8_t)n, (uint8_t)vel, p8, 0);
        }
        break;
    }
    default: break;
    }
}
static void eng_note_off(uint32_t t, uint32_t note, uint32_t slice)
{
    switch (t) {
    case T_GHOST: ghost_note_off((uint8_t)note); break;
    case T_SUB: sub_note_off((uint8_t)note); break;
    case T_PADS: case T_KEYS: {
        const int8_t *c = WB_CHORD[slice & 15u];
        uint32_t k;
        for (k = 0; k < 4u; k++) {
            int32_t n = (int32_t)note + c[k];
            if (k && !c[k]) break;
            if (n > 127) break;
            if (t == T_PADS) pads_note_off((uint8_t)n);
            else keys_note_off((uint8_t)n);
        }
        break;
    }
    default: break;
    }
}
static void eng_all_off(void)
{
    ghost_all_off(); sub_all_off(); pads_all_off(); keys_all_off(); drums_all_off();
}
static void eng_hit(uint32_t lane, uint32_t vel, uint32_t pl)
{
    uint8_t p8[8];
    seq_params(T_DRUMS, pl, p8);
    if (lane == L_KICK) fx_duck_trigger(vel);
    drums_hit((uint8_t)lane, (uint8_t)vel, p8);
}

/* ---- COVEN: a source fired; what does it do to the others */
static void coven_fire(uint32_t src, uint32_t vel)
{
    uint32_t dst;
    for (dst = 0; dst < WB_NTRACK; dst++) {
        uint32_t spell = G.coven[src][dst];
        if (!spell || dst == src) continue;
        switch (spell) {
        case SPELL_TRIG:
            if (RND_PCT(rnd_isr()) < 35u) {
                if (dst == T_DRUMS) seq_push(seq.tick, EV_HIT, T_DRUMS, L_RIM, vel * 3u / 4u, 0, 0xFF, 0);
                else if (dst == T_WEATHER) weather_event(1);
                else seq_push(seq.tick, EV_ON, dst, seq.ph[dst].last_note ? seq.ph[dst].last_note : 48u + G.root, vel * 3u / 4u, 0, 0xFF, 0),
                     seq_push(seq.tick + SEQ_STEP, EV_OFF, dst, seq.ph[dst].last_note ? seq.ph[dst].last_note : 48u + G.root, 0, 0, 0xFF, 0);
            }
            break;
        case SPELL_MUTE: seq.ph[dst].mute_until = seq.tick + SEQ_STEP; break;
        case SPELL_DUCK: fx_duck_trigger(vel); break;
        case SPELL_UP: seq.ph[dst].transpose = 12; break;
        case SPELL_DOWN: seq.ph[dst].transpose = -12; break;
        case SPELL_RESEED: wb_rs_isr ^= seq.tick * 2654435761u; if (!wb_rs_isr) wb_rs_isr = 1; break;
        case SPELL_THROW: fx_throw(dst); break;
        default: break;
        }
    }
}

/* ---- conditions and the LOOSE offset */
static int seq_cond(uint32_t cond)
{
    switch (cond) {
    case COND_FILL: return seq.fill;
    case COND_NOT_FILL: return !seq.fill;
    case COND_1_2: return (seq.bar & 1u) == 0u;
    case COND_2_2: return (seq.bar & 1u) == 1u;
    case COND_1_4: return (seq.bar & 3u) == 0u;
    case COND_4_4: return (seq.bar & 3u) == 3u;
    case COND_FIRST: return seq.first;
    default: return 1;
    }
}
/* the offset in ticks of a hit on playhead `ph` at bar position `pos16` (16th within the bar) */
static int32_t seq_loose(uint32_t phi, uint32_t pos16, int32_t micro, uint32_t is_lane, uint32_t lane)
{
    int32_t off = micro, amt, sw;
    uint32_t drift = G.drift[phi < WB_NTRACK + WB_NLANE ? phi : 0];
    /* swing on the off 16ths; the 2-STEP template also pushes the 'and' of 2 and 4 */
    sw = (int32_t)(is_lane ? G.swing : (uint32_t)P[phi][PC_SWING] * 100u / 255u);
    if (pos16 & 1u) off += sw * 12 / 100;
    if (!G.feel && (pos16 == 6u || pos16 == 14u)) off += sw * 4 / 100;
    /* LOOSE: a slow random walk per playhead, scaled by the track's drift and the global loose */
    amt = (int32_t)(drift * G.loose) * 12 * 16 / 10000;   /* max +-12 ticks, in 1/16 ticks */
    if (amt > 0) {
        int32_t w = seq.ph[phi].walk, kick_anchor = is_lane && lane == L_KICK && pos16 == 0u;
        w += (int32_t)(RNDS15(rnd_isr()) * (amt / 2)) >> 15;
        if (w > amt) w = amt;
        if (w < -amt) w = -amt;
        seq.ph[phi].walk = w;
        if (!kick_anchor) off += w / 16;
    }
    if (is_lane && (lane == L_SNARE || lane == L_RIM)) off += (int32_t)P[T_DRUMS][PD_LATE_SNARE] * 24 / 255;
    if (is_lane && (lane == L_HATC || lane == L_HATO)) off -= (int32_t)(drift * 2u / 100u);   /* hats lean early */
    if (off < -(int32_t)SEQ_LOOK + 1) off = -(int32_t)SEQ_LOOK + 1;
    if (off > 60) off = 60;
    return off;
}

/* ---- evaluate one step of a pitched track */
static void seq_eval_track(uint32_t t)
{
    wb_trackpat_t *tp = &PAT[seq_pat()].t[t];
    uint32_t len = tp->len ? tp->len : 16u, st = seq.ph[t].step % len, nominal = seq.ph[t].eval_tick;
    uint32_t stk = step_ticks(tp->div), pos16 = ((nominal - SEQ_LOOK) % SEQ_BAR) / SEQ_STEP;
    const wb_step_t *s = &tp->s[st];
    seq.ph[t].step = st + 1u;
    seq.ph[t].eval_tick = nominal + stk;
    if (!s->note || !s->vel) return;
    if (G.mute & (1u << t)) return;
    if (seq.tick < seq.ph[t].mute_until) return;
    if (!seq_cond(ST_COND(s->flags))) return;
    if (s->chance && s->chance < 100u && RND_PCT(rnd_isr()) >= s->chance) return;
    {
        int32_t off = seq_loose(t, pos16, s->micro, 0, 0), note = (int32_t)s->note + seq.ph[t].transpose;
        uint32_t due = nominal + (uint32_t)off, gate = s->len ? (uint32_t)s->len * 6u : stk + 1u, r = ST_RATCHET(s->flags), k;
        uint32_t vel = ST_ACCENT(s->flags) ? 127u : s->vel;
        seq.ph[t].transpose = 0;
        note = CLAMP(note, 1, 127);
        seq.ph[t].last_note = (uint8_t)note;
        for (k = 0; k <= r; k++) {
            uint32_t d = due + k * (stk / (r + 1u));
            seq_push(d, EV_ON, t, (uint32_t)note, vel, s->slice, s->pl, s->flags);
            seq_push(d + (r ? stk / (r + 1u) - 1u : gate), EV_OFF, t, (uint32_t)note, 0, s->slice, 0xFF, 0);
        }
    }
}

/* ---- evaluate one step of a drum lane */
static void seq_eval_lane(uint32_t lane)
{
    wb_lanepat_t *lp = &PAT[seq_pat()].lane[lane];
    uint32_t phi = WB_NTRACK + lane, len = lp->len ? lp->len : 16u, st = seq.ph[phi].step % len;
    uint32_t nominal = seq.ph[phi].eval_tick, pos16 = ((nominal - SEQ_LOOK) % SEQ_BAR) / SEQ_STEP;
    const wb_step_t *s = &lp->s[st];
    seq.ph[phi].step = st + 1u;
    seq.ph[phi].eval_tick = nominal + SEQ_STEP;
    if (!s->note || !s->vel) return;
    if (G.mute & (1u << T_DRUMS)) return;
    if (G.lane_mute & (1u << lane)) return;
    if (seq.tick < seq.ph[phi].mute_until) return;
    if (!seq_cond(ST_COND(s->flags))) return;
    if (s->chance && s->chance < 100u && RND_PCT(rnd_isr()) >= s->chance) return;
    {
        int32_t off = seq_loose(phi, pos16, s->micro, 1, lane);
        uint32_t due = nominal + (uint32_t)off, r = ST_RATCHET(s->flags), k;
        uint32_t vel = ST_ACCENT(s->flags) ? 127u : s->vel;
        for (k = 0; k <= r; k++)
            seq_push(due + k * (SEQ_STEP / (r + 1u)), EV_HIT, T_DRUMS, lane, vel, 0, s->pl, s->flags);
    }
}

/* ---- transport */
static void seq_reset_heads(void)
{
    uint32_t i;
    for (i = 0; i < WB_NTRACK + WB_NLANE; i++) {
        seq.ph[i].step = 0;
        seq.ph[i].eval_tick = SEQ_LOOK;
        seq.ph[i].mute_until = 0;
        seq.ph[i].transpose = 0;
    }
    for (i = 0; i < SEQ_QN; i++) seq.q[i].kind = EV_FREE;
    seq.tick = 0;
    seq.acc = 0;
    seq.bar = 0;
    seq.bar_tick = 0;
    seq.first = 1;
}
static void seq_start(void)
{
    seq_reset_heads();
    seq.fill = seq.fill_next = 0;
    if (G.song_mode) {
        seq.song_row = 0;
        seq.song_bars_left = SONG[0].pat < WB_NPAT ? (SONG[0].bars ? SONG[0].bars : 1u) : 0;
        if (SONG[0].pat < WB_NPAT) { seq.pat = SONG[0].pat; seq.section = SONG[0].section; }
    } else {
        seq.pat = G.pattern;
    }
    seq.playing = 1;
}
static void seq_stop(void)
{
    uint32_t i;
    seq.playing = 0;
    for (i = 0; i < SEQ_QN; i++) seq.q[i].kind = EV_FREE;
    eng_all_off();
}

/* a bar boundary: fills, song rows, pattern changes (they wait for the bar) */
static void seq_bar(void)
{
    seq.bar++;
    seq.fill = seq.fill_next;
    seq.fill_next = 0;
    seq.first = 0;
    if (G.song_mode) {
        if (seq.song_bars_left) seq.song_bars_left--;
        if (!seq.song_bars_left) {
            uint32_t r = seq.song_row + 1u;
            if (r >= WB_NSONG || SONG[r].pat >= WB_NPAT) r = 0;
            seq.song_row = r;
            if (SONG[r].pat < WB_NPAT) {
                seq.pat = SONG[r].pat;
                seq.section = SONG[r].section;
                seq.song_bars_left = SONG[r].bars ? SONG[r].bars : 1u;
                seq.first = 1;
            } else {
                seq_stop();
            }
        }
    } else if (seq.pat != G.pattern) {
        seq.pat = G.pattern;
        seq.first = 1;
    }
}

/* one tick: evaluate what is due a 16th ahead, fire what is due now */
static void seq_tick(void)
{
    uint32_t i;
    /* bar position (the audible grid is SEQ_LOOK behind the evaluation grid) */
    if (seq.tick >= SEQ_LOOK) {
        seq.bar_tick = (seq.tick - SEQ_LOOK) % SEQ_BAR;
        if (seq.bar_tick == 0u && seq.tick > SEQ_LOOK) seq_bar();
    }
    /* evaluate */
    for (i = 0; i < WB_NTRACK; i++)
        if (i != T_DRUMS && i != T_WEATHER)
            while (seq.ph[i].eval_tick <= seq.tick + SEQ_LOOK) seq_eval_track(i);
    for (i = 0; i < WB_NLANE; i++)
        while (seq.ph[WB_NTRACK + i].eval_tick <= seq.tick + SEQ_LOOK) seq_eval_lane(i);
    /* fire */
    for (i = 0; i < SEQ_QN; i++) {
        seq_ev_t *e = &seq.q[i];
        if (e->kind == EV_FREE || e->due > seq.tick) continue;
        switch (e->kind) {
        case EV_ON:
            eng_note_on(e->track, e->a, e->b, e->slice, e->pl);
            if (ST_THROW(e->flags)) fx_throw(e->track);
            coven_fire(e->track, e->b);
            break;
        case EV_OFF: eng_note_off(e->track, e->a, e->slice); break;
        case EV_HIT:
            eng_hit(e->a, e->b, e->pl);
            if (ST_THROW(e->flags)) fx_throw(T_DRUMS);
            if (e->a == L_KICK || e->a == L_SNARE) coven_fire(T_DRUMS, e->b);
            break;
        default: break;
        }
        e->kind = EV_FREE;
    }
    seq.tick++;
}

/* requests from the UI, once a block */
static void seq_requests(void)
{
    uint32_t r = wb_req;
    if (!r) return;
    wb_req = 0;
    if (r & REQ_STOP) seq_stop();
    if (r & REQ_PLAY) { if (!seq.playing) seq_start(); }
    if (r & REQ_RESTART) { seq_stop(); seq_start(); }
    if (r & REQ_FILL) seq.fill_next = 1;
    if (r & REQ_DROP) seq.section = SEC_DROP;
    if (r & REQ_BUILD) seq.section = SEC_BUILD;
    if (r & REQ_BREAK) seq.section = SEC_BREAK;
    if (r & REQ_CATCH) fx_catch_start();
}

/* live notes from the keys and MIDI */
static void seq_live(void)
{
    while (wb_live_r != wb_live_w) {
        wb_live_t *l = &wb_live[wb_live_r % WB_LIVE_N];
        uint32_t t = l->track, note = l->note, vel = l->vel, slice = l->slice;
        wb_live_r++;
        if (t == T_DRUMS) { if (vel) eng_hit(note & 7u, vel, 0xFF); }
        else if (t == T_WEATHER) { if (vel) weather_event(note & 3u); }
        else if (vel) { eng_note_on(t, note, vel, slice, 0xFF); seq.ph[t].last_note = (uint8_t)note; }
        else eng_note_off(t, note, slice);
    }
}

/* advance the transport by n frames (called once per sub-block) */
static void seq_advance(uint32_t n)
{
    seq_requests();
    seq_live();
    if (!seq.playing) return;
    seq.acc += n << 8;
    while (seq.acc >= wb_fpt_q8) {
        seq.acc -= wb_fpt_q8;
        seq_tick();
    }
    wb_pos.playing = seq.playing;
    wb_pos.tick = seq.tick >= SEQ_LOOK ? seq.tick - SEQ_LOOK : 0;
    wb_pos.bar = seq.bar;
    wb_pos.step16 = seq.bar_tick / SEQ_STEP;
    wb_pos.song_row = seq.song_row;
    wb_pos.section = seq.section;
}
static void seq_init(void)
{
    seq_reset_heads();
    seq.playing = 0;
    seq.section = SEC_INTRO;
}
