/* SPDX-License-Identifier: GPL-3.0-only */
/* WitchBox UI (main loop): pages on the section buttons, four knobs a page (rows on OCT-/OCT+), the keys
 * as steps or as an instrument, the macros on the black keys of HOME.
 *
 *   HOME   mixer, tempo, macros      EDIT  the track's sound        SEQ   steps (hold a step: its values)
 *   ENV    LOOSE timing              LFO   the track's generator     FX    the master chain
 *   GLO    COVEN grid                ARP   SUMMON                    SAVE  project and song
 *   PLAY   start / stop              REC   record live keys; held: shift
 *   ALGORITHM encoder: track         PRESETS encoder: pattern        SELECT encoder: page-specific */
#pragma once
#include "types.h"
#include "seq.h"
#include "gen.h"
#include "project.h"

enum { PG_HOME = 0, PG_SOUND, PG_SEQ, PG_LOOSE, PG_GEN, PG_FX, PG_COVEN, PG_SUMMON, PG_SONG, WB_NPAGE };
static const char *const PAGE_NAME[WB_NPAGE] = {"HOME", "SOUND", "STEPS", "LOOSE", "GEN", "FX", "COVEN", "SUMMON", "SONG"};

static const char *const PNAME_COMMON[8] = {"LEVEL", "PAN", "DUST", "ECHO", "SPACE", "DUCK", "FILT", "SWING"};
static const char *const PNAME[WB_NTRACK][8] = {
    {"MODE", "PITCH", "FORMNT", "GATE", "STRTCH", "DROP", "VOWEL", "BREATH"},
    {"SHAPE", "DECAY", "GLIDE", "REESE", "CUTOFF", "WOBBLE", "DEPTH", "DRIVE"},
    {"SPREAD", "VOICES", "CUTOFF", "RESO", "ATTACK", "RELEAS", "DISTNC", "SHAPE"},
    {"PATCH", "RATIO", "INDEX", "DECAY", "FEEDBK", "BRIGHT", "RELEAS", "DETUNE"},
    {"KIT", "K-TUNE", "K-DECY", "S-TONE", "S-SNAP", "H-DECY", "FOUND", "LATE"},
    {"RAIN", "CRACKL", "HISS", "DRONE", "TONE", "DENSTY", "WIDTH", "FIELD"}};
static const char *const FXNAME[WB_NFX] = {"BITS", "RATE", "HISS", "WOW", "SAT", "E-TIME", "E-FB", "E-TONE",
                                           "SIZE", "DAMP", "PREDLY", "D-REL", "D-DPTH", "GLUE", "LOPASS", "WIDTH"};
static const char *const FXROW[4] = {"DUST  TAPE", "ECHO", "SPACE  DUCK", "DUCK  GLUE  OUT"};
static const char *const CONDNAME[8] = {"ALWAYS", "FILL", "NOFILL", "1:2", "2:2", "1:4", "4:4", "FIRST"};
static const char *const MOODTXT[4] = {"2-step, rain on glass", "wet streets, far away", "a cathedral, a choir", "half-time, the reese"};

static struct {
    uint32_t page, track, row, lane, seqpage, octave, mood, slot, shift, rec;
    uint32_t held_step;              /* 0xFF none: a white key held in STEPS */
    uint32_t dirty, last_draw_ms, anim_ms, blink;
    uint32_t prev_keys, bpm_dirty, prefs_dirty, prefs_ms;
    uint32_t build_bar, build_on, break_on;
    uint32_t clock_tick, msg_ms;
    uint8_t live_note[27];
    char msg[24];
} ui;
#define DIRTY_HEAD 1u
#define DIRTY_BODY 2u
#define DIRTY_FOOT 4u
#define DIRTY_ALL 7u

/* ---- small drawing helpers */
#define C_BG C_BLACK
static void ui_msg(const char *s) { wb_strcpy(ui.msg, s, sizeof ui.msg); ui.msg_ms = fm1_ms; ui.dirty |= DIRTY_HEAD; }
static void txt(int32_t x, int32_t y, const char *s, uint16_t c) { cv_text(x, y, &FONT_S, s, c); }
static void txtn(int32_t x, int32_t y, int32_t v, uint16_t c) { char b[12]; wb_itoa(b, v); cv_text(x, y, &FONT_S, b, c); }
static void bar_h(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t v255, uint16_t c)
{
    cv_rect(x, y, w, h, C_LINE);
    cv_rect(x, y, (int32_t)((uint32_t)w * v255 / 255u), h, c);
}
/* a knob column: label above, value bar and number below; four across the 240 px footer strip */
static void knob_col(uint32_t i, const char *label, int32_t v, uint32_t v255, int on)
{
    int32_t x = (int32_t)i * 60 + 2;
    txt(x, 0, label, on ? C_AMB : C_GRAY);
    bar_h(x, 18, 54, 4, v255, on ? C_HI : C_AMB);
    txtn(x, 24, v, C_HI);
}

/* ---- what the four knobs mean on each page (name, value, 0..255 bar) */
typedef struct { const char *name; int32_t v; uint32_t bar; } knobinfo_t;
static void ui_knobinfo(uint32_t i, knobinfo_t *k)
{
    uint32_t t = ui.track;
    k->name = "-"; k->v = 0; k->bar = 0;
    switch (ui.page) {
    case PG_HOME:
        if (i == 0) { k->name = "BPM"; k->v = G.bpm; k->bar = (uint32_t)(G.bpm - 60) * 255u / 140u; }
        if (i == 1) { k->name = "SWING"; k->v = G.swing; k->bar = (uint32_t)G.swing * 255u / 100u; }
        if (i == 2) { k->name = "LOOSE"; k->v = G.loose; k->bar = (uint32_t)G.loose * 255u / 100u; }
        if (i == 3) { k->name = "LOPASS"; k->v = FXP[FX_MASTER_LP]; k->bar = FXP[FX_MASTER_LP]; }
        break;
    case PG_SOUND: {
        uint32_t pi = (ui.row & 3u) * 4u + i;
        k->name = pi < 8u ? PNAME[t][pi] : PNAME_COMMON[pi - 8u];
        k->v = P[t][pi]; k->bar = P[t][pi];
        break;
    }
    case PG_SEQ:
        if (ui.held_step != 0xFFu) {
            wb_step_t *s = t == T_DRUMS ? &gen_pat()->lane[ui.lane].s[ui.held_step] : &gen_pat()->t[t].s[ui.held_step];
            if (i == 0) { k->name = t == T_DRUMS ? "HIT" : t == T_GHOST ? "SLICE" : "NOTE"; k->v = t == T_GHOST ? s->slice : s->note; k->bar = t == T_GHOST ? (uint32_t)s->slice * 17u : (uint32_t)s->note * 2u; }
            if (i == 1) { k->name = "VEL"; k->v = s->vel; k->bar = (uint32_t)s->vel * 2u; }
            if (i == 2) { k->name = "MICRO"; k->v = s->micro; k->bar = (uint32_t)(s->micro + 24) * 5u; }
            if (i == 3) { k->name = "CHANCE"; k->v = s->chance; k->bar = (uint32_t)s->chance * 255u / 100u; }
        } else {
            if (i == 0) { k->name = "LENGTH"; k->v = t == T_DRUMS ? gen_pat()->lane[ui.lane].len : gen_pat()->t[t].len; k->bar = (uint32_t)k->v * 4u; }
            if (i == 1 && t != T_DRUMS) { k->name = "DIV"; k->v = gen_pat()->t[t].div; k->bar = (uint32_t)k->v * 100u; }
            if (i == 2) { k->name = "SHIFT"; k->v = 0; k->bar = 128; }
            if (i == 3) { k->name = "CLEAR"; k->v = 0; k->bar = 0; }
        }
        break;
    case PG_LOOSE:
        if (ui.row == 0) {
            if (i == 0) { k->name = "LOOSE"; k->v = G.loose; k->bar = (uint32_t)G.loose * 255u / 100u; }
            if (i == 1) { k->name = "SWING"; k->v = G.swing; k->bar = (uint32_t)G.swing * 255u / 100u; }
            if (i == 2) { k->name = "LATE"; k->v = P[T_DRUMS][PD_LATE_SNARE]; k->bar = P[T_DRUMS][PD_LATE_SNARE]; }
            if (i == 3) { k->name = "FEEL"; k->v = G.feel; k->bar = G.feel ? 255 : 0; }
        } else {
            uint32_t idx = (ui.row - 1u) * 4u + i;        /* rows 1..4: tracks 0..5 then lanes 0..7 */
            if (idx < WB_NTRACK + WB_NLANE) { k->name = idx < WB_NTRACK ? WB_TRACK_NAME[idx] : WB_LANE_NAME[idx - WB_NTRACK]; k->v = G.drift[idx]; k->bar = (uint32_t)G.drift[idx] * 255u / 100u; }
        }
        break;
    case PG_GEN: {
        const char *n[4] = {WB_GEN_KNOB[GEN[t].kind][0], WB_GEN_KNOB[GEN[t].kind][1], "DENSTY", "SEED"};
        k->name = n[i]; k->v = GEN[t].k[i]; k->bar = GEN[t].k[i];
        break;
    }
    case PG_FX: {
        uint32_t pi = (ui.row & 3u) * 4u + i;
        k->name = FXNAME[pi]; k->v = FXP[pi]; k->bar = FXP[pi];
        break;
    }
    case PG_COVEN: {
        uint32_t dst = ui.lane * 4u + i;              /* lane = column page 0 (targets 0..3) or 1 (4..5) */
        if (dst < WB_NTRACK) { k->name = WB_TRACK_NAME[dst]; k->v = G.coven[ui.row % WB_NTRACK][dst]; k->bar = (uint32_t)k->v * 36u; }
        break;
    }
    case PG_SUMMON:
        if (i == 0) { k->name = "MOOD"; k->v = ui.mood; k->bar = ui.mood * 85u; }
        if (i == 1) { k->name = "ROOT"; k->v = G.root; k->bar = (uint32_t)G.root * 23u; }
        if (i == 2) { k->name = "SCALE"; k->v = G.scale; k->bar = (uint32_t)G.scale * 255u / (WB_NSCALE - 1u); }
        if (i == 3) { k->name = "BPM"; k->v = G.bpm; k->bar = (uint32_t)(G.bpm - 60) * 255u / 140u; }
        break;
    case PG_SONG: {
        wb_songrow_t *r = &SONG[ui.row % WB_NSONG];
        if (i == 0) { k->name = "PATTRN"; k->v = r->pat < WB_NPAT ? (int32_t)r->pat + 1 : 0; k->bar = r->pat < WB_NPAT ? (uint32_t)r->pat * 36u : 0u; }
        if (i == 1) { k->name = "SECTN"; k->v = r->section % WB_NSEC; k->bar = (uint32_t)(r->section % WB_NSEC) * 63u; }
        if (i == 2) { k->name = "BARS"; k->v = r->bars; k->bar = (uint32_t)r->bars * 8u; }
        if (i == 3) { k->name = "SLOT"; k->v = (int32_t)ui.slot + 1; k->bar = ui.slot ? 255 : 0; }
        break;
    }
    default: break;
    }
}

/* ---- tempo to the ISR */
static void ui_tempo(void)
{
    uint32_t bpm = G.bpm < 60u ? 60u : G.bpm > 200u ? 200u : G.bpm;
    G.bpm = (uint16_t)bpm;
    wb_fpb = frames_per_beat(bpm);
    wb_fpt_q8 = (WB_FS * 60u * 256u / SEQ_PPQ) / bpm;    /* 7056000 / bpm */
    PREFS.bpm = (uint16_t)bpm; PREFS.swing = G.swing; PREFS.loose = G.loose; PREFS.feel = G.feel;
    ui.prefs_dirty = 1; ui.prefs_ms = fm1_ms;
}

/* ---- the knobs */
static void ui_knob(uint32_t i, int32_t d)
{
    uint32_t t = ui.track;
    int32_t d4 = d * 4;
    ui.dirty |= DIRTY_FOOT;
    switch (ui.page) {
    case PG_HOME:
        if (i == 0) { G.bpm = (uint16_t)CLAMP((int32_t)G.bpm + d, 60, 200); ui_tempo(); ui.dirty |= DIRTY_HEAD; }
        if (i == 1) { G.swing = (uint8_t)CLAMP((int32_t)G.swing + d, 0, 100); ui_tempo(); }
        if (i == 2) { G.loose = (uint8_t)CLAMP((int32_t)G.loose + d, 0, 100); ui_tempo(); }
        if (i == 3) FXP[FX_MASTER_LP] = (uint8_t)CLAMP((int32_t)FXP[FX_MASTER_LP] + d4, 0, 255);
        break;
    case PG_SOUND: {
        uint32_t pi = (ui.row & 3u) * 4u + i;
        P[t][pi] = (uint8_t)CLAMP((int32_t)P[t][pi] + d4, 0, 255);
        if (t == T_WEATHER && pi == PW_DENSITY) gen_weather_base = P[t][pi];
        ui.dirty |= DIRTY_BODY;
        break;
    }
    case PG_SEQ:
        if (ui.held_step != 0xFFu) {
            wb_step_t *s = t == T_DRUMS ? &gen_pat()->lane[ui.lane].s[ui.held_step] : &gen_pat()->t[t].s[ui.held_step];
            if (i == 0) { if (t == T_GHOST) s->slice = (uint8_t)CLAMP((int32_t)s->slice + d, 0, 15); else if (t != T_DRUMS) s->note = (uint8_t)CLAMP((int32_t)s->note + d, 1, 120); }
            if (i == 1) s->vel = (uint8_t)CLAMP((int32_t)s->vel + d * 4, 1, 127);
            if (i == 2) s->micro = (int8_t)CLAMP((int32_t)s->micro + d, -23, 23);
            if (i == 3) s->chance = (uint8_t)CLAMP((int32_t)s->chance + d * 5, 5, 100);
        } else {
            if (i == 0) {
                if (t == T_DRUMS) gen_pat()->lane[ui.lane].len = (uint8_t)CLAMP((int32_t)gen_pat()->lane[ui.lane].len + d, 1, 64);
                else gen_pat()->t[t].len = (uint8_t)CLAMP((int32_t)gen_pat()->t[t].len + d, 1, 64);
            }
            if (i == 1 && t != T_DRUMS) gen_pat()->t[t].div = (uint8_t)CLAMP((int32_t)gen_pat()->t[t].div + d, 0, 2);
            if (i == 2 && d) {                          /* rotate the pattern */
                wb_step_t *s = t == T_DRUMS ? gen_pat()->lane[ui.lane].s : gen_pat()->t[t].s;
                uint32_t len = t == T_DRUMS ? gen_pat()->lane[ui.lane].len : gen_pat()->t[t].len, k;
                wb_step_t tmp;
                if (!len) len = 16;
                if (d > 0) { tmp = s[len - 1u]; for (k = len - 1u; k > 0u; k--) s[k] = s[k - 1u]; s[0] = tmp; }
                else { tmp = s[0]; for (k = 0; k + 1u < len; k++) s[k] = s[k + 1u]; s[len - 1u] = tmp; }
            }
            if (i == 3 && d < 0) {                      /* turn left: clear */
                if (t == T_DRUMS) gen_clear_lane(&gen_pat()->lane[ui.lane], gen_pat()->lane[ui.lane].len);
                else gen_clear_track(&gen_pat()->t[t], gen_pat()->t[t].len);
                ui_msg("CLEARED");
            }
        }
        ui.dirty |= DIRTY_BODY;
        break;
    case PG_LOOSE:
        if (ui.row == 0) {
            if (i == 0) { G.loose = (uint8_t)CLAMP((int32_t)G.loose + d, 0, 100); ui_tempo(); }
            if (i == 1) { G.swing = (uint8_t)CLAMP((int32_t)G.swing + d, 0, 100); ui_tempo(); }
            if (i == 2) P[T_DRUMS][PD_LATE_SNARE] = (uint8_t)CLAMP((int32_t)P[T_DRUMS][PD_LATE_SNARE] + d4, 0, 255);
            if (i == 3) { G.feel = d > 0 ? 1 : 0; ui_tempo(); }
        } else {
            uint32_t idx = (ui.row - 1u) * 4u + i;
            if (idx < WB_NTRACK + WB_NLANE) G.drift[idx] = (uint8_t)CLAMP((int32_t)G.drift[idx] + d * 2, 0, 100);
        }
        ui.dirty |= DIRTY_BODY;
        break;
    case PG_GEN:
        GEN[t].k[i] = (uint8_t)CLAMP((int32_t)GEN[t].k[i] + d4, 0, 255);
        break;
    case PG_FX: {
        uint32_t pi = (ui.row & 3u) * 4u + i;
        FXP[pi] = (uint8_t)CLAMP((int32_t)FXP[pi] + (pi == FX_ECHO_TIME ? d : d4), 0, pi == FX_ECHO_TIME ? 7 : 255);
        ui.dirty |= DIRTY_BODY;
        break;
    }
    case PG_COVEN: {
        uint32_t dst = ui.lane * 4u + i, src = ui.row % WB_NTRACK;
        if (dst < WB_NTRACK && dst != src) G.coven[src][dst] = (uint8_t)CLAMP((int32_t)G.coven[src][dst] + d, 0, WB_NSPELL - 1);
        ui.dirty |= DIRTY_BODY;
        break;
    }
    case PG_SUMMON:
        if (i == 0) ui.mood = (uint32_t)CLAMP((int32_t)ui.mood + d, 0, 3);
        if (i == 1) G.root = (uint8_t)CLAMP((int32_t)G.root + d, 0, 11);
        if (i == 2) G.scale = (uint8_t)CLAMP((int32_t)G.scale + d, 0, (int32_t)WB_NSCALE - 1);
        if (i == 3) { G.bpm = (uint16_t)CLAMP((int32_t)G.bpm + d, 60, 200); ui_tempo(); }
        ui.dirty |= DIRTY_BODY | DIRTY_HEAD;
        break;
    case PG_SONG: {
        wb_songrow_t *r = &SONG[ui.row % WB_NSONG];
        if (i == 0) { int32_t v = (r->pat < WB_NPAT ? (int32_t)r->pat + 1 : 0) + d; v = CLAMP(v, 0, WB_NPAT); r->pat = (uint8_t)(v ? v - 1 : 0xFF); if (r->pat != 0xFFu && r->bars == 0xFFu) { r->bars = 4; r->section = SEC_DROP; r->mute = 0; } }
        if (i == 1 && r->pat < WB_NPAT) r->section = (uint8_t)CLAMP((int32_t)(r->section % WB_NSEC) + d, 0, WB_NSEC - 1);
        if (i == 2 && r->pat < WB_NPAT) r->bars = (uint8_t)CLAMP((int32_t)r->bars + d, 1, 32);
        if (i == 3) ui.slot = d > 0 ? 1u : 0u;
        ui.dirty |= DIRTY_BODY;
        break;
    }
    default: break;
    }
}

/* ---- the macros (HOME black keys) */
static void macro_drop(void)
{
    G.mute &= (uint8_t)~(1u << T_SUB);
    G.lane_mute = 0;
    FXP[FX_MASTER_LP] = 255;
    FXP[FX_DUCK_DEPTH] = 200;
    P[T_PADS][PP_CUTOFF] = (uint8_t)MAX(P[T_PADS][PP_CUTOFF], 150);
    if (G.feel) P[T_SUB][PS_REESE] = (uint8_t)MAX(P[T_SUB][PS_REESE], 150);
    ui.build_on = 0; ui.break_on = 0;
    wb_req |= REQ_DROP;
    ui_msg("DROP");
}
static void macro_build(void)
{
    ui.build_on = 1; ui.build_bar = wb_pos.bar;
    wb_req |= REQ_BUILD;
    ui_msg("BUILD");
}
static void macro_break(void)
{
    ui.break_on = 1;
    G.lane_mute = (uint8_t)((1u << L_KICK) | (1u << L_SNARE) | (1u << L_HATC));
    wb_req |= REQ_BREAK;
    ui_msg("BREAK");
}
static void macro_tick(void)                           /* the BUILD ramp, once a bar */
{
    static uint32_t last_bar = 0xFFFFFFFFu;
    uint32_t bar = wb_pos.bar;
    if (!ui.build_on || bar == last_bar) return;
    last_bar = bar;
    {
        uint32_t n = bar - ui.build_bar;                 /* 0..3 */
        if (n >= 4u) { macro_drop(); return; }
        FXP[FX_ECHO_FB] = (uint8_t)MIN(255u, 140u + n * 35u);
        FXP[FX_MASTER_LP] = (uint8_t)(255u - n * 45u);
        P[T_WEATHER][PW_DENSITY] = (uint8_t)MIN(255u, 128u + n * 30u);
        if (n == 3u) {                                   /* the snare roll: ratchets on every 16th of the last bar */
            wb_lanepat_t *lp = &gen_pat()->lane[L_SNARE];
            uint32_t i;
            for (i = 0; i < 16u; i++) { gen_set(&lp->s[i], 1, (uint8_t)(50u + i * 4u), 4, 0); lp->s[i].flags = (uint8_t)(i >= 12u ? 3u : i >= 8u ? 1u : 0u); lp->s[i].chance = 100; }
        }
    }
}

/* ---- keys */
static uint32_t white_index(uint32_t key)                 /* key 0..26 -> white 0..15 or 0xFF */
{
    static const uint8_t W[27] = {0, 0xFF, 1, 0xFF, 2, 3, 0xFF, 4, 0xFF, 5, 0xFF, 6, 7, 0xFF, 8, 0xFF, 9, 10, 0xFF, 11, 0xFF, 12, 0xFF, 13, 14, 0xFF, 15};
    return key < 27u ? W[key] : 0xFFu;
}
static uint32_t black_index(uint32_t key)
{
    static const uint8_t B[27] = {0xFF, 0, 0xFF, 1, 0xFF, 0xFF, 2, 0xFF, 3, 0xFF, 4, 0xFF, 0xFF, 5, 0xFF, 6, 0xFF, 0xFF, 7, 0xFF, 8, 0xFF, 9, 0xFF, 0xFF, 10, 0xFF};
    return key < 27u ? B[key] : 0xFFu;
}
static void ui_record_live(uint32_t t, uint32_t note, uint32_t vel, uint32_t slice)
{
    uint32_t tick = (wb_pos.tick + 12u), len, st;
    if (!wb_pos.playing || !ui.rec) return;
    if (t == T_DRUMS) {
        wb_lanepat_t *lp = &gen_pat()->lane[note & 7u];
        len = lp->len ? lp->len : 16u;
        st = (tick / SEQ_STEP) % len;
        gen_set(&lp->s[st], 1, vel, 4, 0);
    } else if (t != T_WEATHER) {
        wb_trackpat_t *tp = &gen_pat()->t[t];
        len = tp->len ? tp->len : 16u;
        st = (tick / step_ticks(tp->div)) % len;
        gen_set(&tp->s[st], note, vel, 4, slice);
    }
    ui.dirty |= DIRTY_BODY;
}
static void ui_key_down(uint32_t key)
{
    uint32_t t = ui.track, w = white_index(key), b = black_index(key), home_held = (fm1_in.buttons >> BTN_HOME) & 1u;
    if (ui.page == PG_HOME && home_held && w < WB_NTRACK) { G.mute ^= (uint8_t)(1u << w); ui.dirty |= DIRTY_BODY; return; }
    if (ui.page == PG_HOME && b != 0xFFu) {
        switch (b) {
        case 0: macro_drop(); break;
        case 1: macro_build(); break;
        case 2: macro_break(); break;
        case 3: wb_req |= REQ_FILL; ui_msg("FILL"); break;
        case 4: fx_throw_hold(1); ui_msg("THROW"); break;
        case 5: wb_req |= REQ_CATCH; ui_msg("CATCH 3S"); break;
        case 6: G.song_mode ^= 1u; ui_msg(G.song_mode ? "SONG MODE" : "PATTERN MODE"); break;
        default: break;
        }
        ui.dirty |= DIRTY_BODY;
        return;
    }
    if (ui.page == PG_SEQ) {
        if (w != 0xFFu) {
            uint32_t st = ui.seqpage * 16u + w;
            wb_step_t *s = t == T_DRUMS ? &gen_pat()->lane[ui.lane].s[st] : &gen_pat()->t[t].s[st];
            ui.held_step = st;
            if (s->note) { s->note = 0; }
            else {
                uint32_t note = t == T_DRUMS ? 1u : t == T_GHOST ? 60u : t == T_SUB ? (uint32_t)scale_note(G.root, G.scale, 2, 0) : (uint32_t)scale_note(G.root, G.scale, 4, 0);
                if (t != T_DRUMS && seq.ph[t].last_note) note = seq.ph[t].last_note;
                gen_set(s, note, 100, 4, t == T_GHOST ? (st & 15u) : 0u);
            }
            ui.dirty |= DIRTY_BODY | DIRTY_FOOT;
        } else if (b < WB_NPAT) { G.pattern = (uint8_t)b; ui.dirty |= DIRTY_ALL; }
        return;
    }
    /* an instrument */
    if (t == T_DRUMS) { if (w < WB_NLANE) { live_push(T_DRUMS, w, 110, 0); ui_record_live(T_DRUMS, w, 110, 0); } return; }
    if (t == T_WEATHER) { if (w < 4u) live_push(T_WEATHER, w, 100, 0); return; }
    if (t == T_GHOST) {
        uint32_t note = 60, slice;
        if (w != 0xFFu) slice = w; else { slice = 0; note = 60u + b + 1u; }
        ui.live_note[key] = (uint8_t)note;
        live_push(T_GHOST, note, 110, slice);
        ui_record_live(T_GHOST, note, 110, slice);
        return;
    }
    {
        uint32_t note = 41u + 12u * ui.octave + key;    /* F3 + octave + key */
        if (note > 120u) note = 120;
        ui.live_note[key] = (uint8_t)note;
        live_push(t, note, 110, 0);
        ui_record_live(t, note, 110, 0);
    }
}
static void ui_key_up(uint32_t key)
{
    uint32_t t = ui.track;
    if (ui.page == PG_SEQ) { if (ui.held_step != 0xFFu && white_index(key) != 0xFFu) { ui.held_step = 0xFF; ui.dirty |= DIRTY_FOOT; } return; }
    if (ui.page == PG_HOME && black_index(key) == 4u) fx_throw_hold(0);
    if (t == T_DRUMS || t == T_WEATHER) return;
    if (ui.live_note[key]) { live_push(t, ui.live_note[key], 0, t == T_GHOST ? white_index(key) : 0u); ui.live_note[key] = 0; }
}

/* ---- buttons */
static void ui_page(uint32_t pg) { ui.page = pg; ui.row = 0; ui.held_step = 0xFF; ui.dirty |= DIRTY_ALL; }
static void ui_button(uint32_t b)
{
    uint32_t t = ui.track;
    switch (b) {
    case BTN_HOME: ui_page(PG_HOME); break;
    case BTN_EDIT: ui_page(PG_SOUND); break;
    case BTN_SEQ: ui_page(PG_SEQ); break;
    case BTN_ENV: ui_page(PG_LOOSE); break;
    case BTN_LFO: ui_page(PG_GEN); break;
    case BTN_FX: ui_page(PG_FX); break;
    case BTN_GLO: ui_page(PG_COVEN); break;
    case BTN_ARP:
        if (ui.page == PG_GEN) { GEN[t].auto_run ^= 1u; ui_msg(GEN[t].auto_run ? "AUTO ON" : "AUTO OFF"); ui.dirty |= DIRTY_BODY; }
        else ui_page(PG_SUMMON);
        break;
    case BTN_SAVE: ui_page(PG_SONG); break;
    case BTN_PLAY:
        if (wb_pos.playing) { wb_req |= REQ_STOP; ui.build_on = 0; }
        else { wb_req |= REQ_PLAY; gen_last_bar = 0xFFFFFFFFu; }
        ui.dirty |= DIRTY_HEAD;
        break;
    case BTN_OCT_DOWN:
        if (ui.page == PG_HOME) { if (ui.octave) ui.octave--; }
        else if (ui.page == PG_GEN) { uint32_t k = GEN[t].kind, guard = WB_NGEN; do { k = (k + WB_NGEN - 1u) % WB_NGEN; } while (!gen_allowed(t, k) && guard--); GEN[t].kind = (uint8_t)k; }
        else if (ui.page == PG_SEQ) { if (ui.seqpage) ui.seqpage--; }
        else if (ui.row) ui.row--;
        ui.dirty |= DIRTY_ALL;
        break;
    case BTN_OCT_UP:
        if (ui.page == PG_HOME) { if (ui.octave < 3u) ui.octave++; }
        else if (ui.page == PG_GEN) { uint32_t k = GEN[t].kind, guard = WB_NGEN; do { k = (k + 1u) % WB_NGEN; } while (!gen_allowed(t, k) && guard--); GEN[t].kind = (uint8_t)k; }
        else if (ui.page == PG_SEQ) { if (ui.seqpage < 3u) ui.seqpage++; }
        else { uint32_t max = ui.page == PG_SOUND || ui.page == PG_FX ? 3u : ui.page == PG_LOOSE ? 4u : ui.page == PG_COVEN ? WB_NTRACK - 1u : ui.page == PG_SONG ? WB_NSONG - 1u : 0u; if (ui.row < max) ui.row++; }
        ui.dirty |= DIRTY_ALL;
        break;
    case BTN_SEL:
        if (ui.page == PG_GEN) {
            if (ui.shift) { gen_undo_cast(); ui_msg("UNDONE"); }
            else if (GEN[t].kind) { gen_cast(t); ui_msg("CAST"); }
        } else if (ui.page == PG_SUMMON) {
            gen_summon(ui.mood, ui.shift);
            ui_tempo();
            ui_msg(ui.shift ? "REROLLED" : "SUMMONED");
        } else if (ui.page == PG_SONG) {
            int rc;
            if (wb_pos.playing) { wb_req |= REQ_STOP; }
            if (ui.shift) { rc = prj_load(ui.slot); ui_msg(rc ? "LOAD FAILED" : "LOADED"); if (!rc) ui_tempo(); }
            else { rc = prj_save(ui.slot); ui_msg(rc ? "SAVE FAILED" : "SAVED"); }
            PREFS.slot = (uint8_t)ui.slot; ui.prefs_dirty = 1; ui.prefs_ms = fm1_ms;
        } else if (ui.page == PG_HOME) {
            wb_req |= REQ_RESTART;
        }
        ui.dirty |= DIRTY_ALL;
        break;
    default: break;
    }
}

/* ---- drawing: header 0..31, body 32..223 (three strips of 64), footer 224..239 (one strip of 16) + knob area */
static void draw_header(void)
{
    char b[16];
    cv_begin(240, 32, C_BG);
    txt(4, 0, PAGE_NAME[ui.page], C_WHITE);
    txt(80, 0, WB_TRACK_NAME[ui.track], C_HI);
    if (ui.track == T_DRUMS && (ui.page == PG_SEQ || ui.page == PG_SOUND)) { txt(150, 0, WB_LANE_NAME[ui.lane], C_AMB); }
    b[0] = (char)('A' + (G.pattern & 7u)); b[1] = 0;
    txt(212, 0, b, C_AMB);
    if (ui.msg[0] && fm1_ms - ui.msg_ms < 1500u) txt(4, 16, ui.msg, C_WHITE);
    else {
        txt(4, 16, wb_pos.playing ? ">" : "#", wb_pos.playing ? C_HI : C_DIM);
        txtn(20, 16, (int32_t)wb_pos.bar + 1, C_AMB);
        txt(52, 16, G.song_mode ? WB_SEC_NAME[wb_pos.section % WB_NSEC] : (G.feel ? "HALF" : "2-STEP"), C_GRAY);
    }
    txtn(150, 16, G.bpm, C_HI);
    txt(186, 16, "BPM", C_GRAY);
    if (ui.rec) txt(224, 16, "o", C_WHITE);
    cv_blit(0, 0);
}
static void strip_begin(uint32_t strip) { cv_begin(240, strip < 2u ? 64u : 40u, C_BG); }
static void draw_steps_strip(uint32_t strip)        /* STEPS: strip 0 = the 16 steps of this page, 1 = overview, 2 = info */
{
    uint32_t t = ui.track, i;
    wb_step_t *s = t == T_DRUMS ? gen_pat()->lane[ui.lane].s : gen_pat()->t[t].s;
    uint32_t len = t == T_DRUMS ? gen_pat()->lane[ui.lane].len : gen_pat()->t[t].len, play = 0xFFFFu;
    if (!len) len = 16;
    if (wb_pos.playing && seq.pat == G.pattern) {
        uint32_t phi = t == T_DRUMS ? WB_NTRACK + ui.lane : t;
        play = (seq.ph[phi].step + len - 1u) % len;    /* the step last evaluated */
    }
    strip_begin(strip);
    if (strip == 0) {
        for (i = 0; i < 16u; i++) {
            uint32_t st = ui.seqpage * 16u + i;
            int32_t x = 4 + (int32_t)i * 15;
            uint16_t c = st >= len ? C_LINE : s[st].note ? (ST_ACCENT(s[st].flags) ? C_WHITE : C_HI) : C_DIM;
            int32_t h = st >= len ? 4 : s[st].note ? 8 + (int32_t)s[st].vel / 4 : 4;
            cv_rect(x, 50 - h, 13, h, c);
            if (st == play) cv_rect(x, 56, 13, 3, C_WHITE);
            if (s[st].note && s[st].chance && s[st].chance < 100u) cv_rect(x + 5, 52 - h - 4, 3, 2, C_AMB);
            if (st == ui.held_step) cv_rect(x, 0, 13, 2, C_WHITE);
            if ((i & 3u) == 0u) cv_rect(x, 60, 1, 4, C_GRAY);
        }
    } else if (strip == 1) {
        txt(4, 0, "PAGE", C_GRAY); txtn(50, 0, (int32_t)ui.seqpage + 1, C_HI);
        txt(70, 0, "LEN", C_GRAY); txtn(104, 0, (int32_t)len, C_HI);
        if (t != T_DRUMS) { txt(130, 0, "DIV", C_GRAY); txt(164, 0, gen_pat()->t[t].div == 1 ? "1/8" : gen_pat()->t[t].div == 2 ? "1/32" : "1/16", C_HI); }
        for (i = 0; i < 64u; i++) {                  /* the whole track, 64 ticks */
            int32_t x = 4 + (int32_t)i * 58 / 16, h = i < len ? (s[i].note ? 10 : 3) : 1;
            cv_rect(x, 44 - h, 3, h, i == play ? C_WHITE : i < len ? (s[i].note ? C_AMB : C_DIM) : C_LINE);
        }
        cv_rect(4 + (int32_t)ui.seqpage * 58, 48, 58, 2, C_GRAY);
    } else {
        if (ui.held_step != 0xFFu) {
            wb_step_t *h = &s[ui.held_step];
            txt(4, 0, "STEP", C_GRAY); txtn(50, 0, (int32_t)ui.held_step + 1, C_HI);
            txt(90, 0, CONDNAME[ST_COND(h->flags)], C_AMB);
            txt(170, 0, "RATCH", C_GRAY); txtn(226, 0, (int32_t)ST_RATCHET(h->flags), C_HI);
            if (t == T_PADS || t == T_KEYS) { txt(4, 20, "CHORD", C_GRAY); txt(60, 20, WB_CHORD_NAME[h->slice & 15u], C_HI); }
            else txt(4, 20, "REC+KNOBS: COND RATCH THRW ACC", C_GRAY);
        } else {
            txt(4, 0, "WHITE: STEPS  BLACK: PATTERN", C_GRAY);
            txt(4, 20, "HOLD A STEP TO EDIT IT", C_GRAY);
            txt(4, 40, "OCT: PAGE   SELECT: LANE", C_GRAY);
        }
    }
    cv_blit(0, 32 + strip * 64);
}
static void draw_home_strip(uint32_t strip)          /* 6 tracks over strips 0..1 (32 px each), macros on 2 */
{
    uint32_t i;
    strip_begin(strip);
    if (strip < 2u) {
        for (i = 0; i < 3u; i++) {
            uint32_t t = strip * 3u + i, m = wb_pos.meter[t];
            int32_t y = (int32_t)i * 21;
            txt(4, y, WB_TRACK_NAME[t], t == ui.track ? C_WHITE : (G.mute >> t & 1u) ? C_DIM : C_AMB);
            bar_h(80, y + 4, 100, 8, (m > 32767u ? 32767u : m) * 255u / 32767u, (G.mute >> t & 1u) ? C_DIM : C_HI);
            txtn(190, y, P[t][PC_LEVEL], C_GRAY);
            if (GEN[t].auto_run && GEN[t].kind) txt(222, y, "*", C_AMB);
        }
    } else {
        txt(4, 0, "DROP BUILD BREAK FILL THROW", C_GRAY);
        txt(4, 20, "CATCH SONG", C_GRAY);
        txt(118, 20, "DUCK", C_GRAY);
        bar_h(164, 24, 72, 8, (uint32_t)(32767 - wb_pos.duck) * 255u / 32767u, C_AMB);
    }
    cv_blit(0, 32 + strip * 64);
}
static void draw_params_strip(uint32_t strip, uint32_t which)   /* SOUND / FX: 16 params as 4 rows; strip = row pair */
{
    uint32_t r, i;
    strip_begin(strip);
    for (r = 0; r < 2u; r++) {
        uint32_t row = strip * 2u + r;
        if (row > 3u) break;
        for (i = 0; i < 4u; i++) {
            uint32_t pi = row * 4u + i, v = which == PG_FX ? FXP[pi] : P[ui.track][pi];
            const char *n = which == PG_FX ? FXNAME[pi] : pi < 8u ? PNAME[ui.track][pi] : PNAME_COMMON[pi - 8u];
            int32_t x = (int32_t)i * 60 + 2, y = (int32_t)r * 32;
            txt(x, y, n, row == ui.row ? C_HI : C_GRAY);
            bar_h(x, y + 18, 54, 6, v, row == ui.row ? C_HI : C_DIM);
        }
    }
    cv_blit(0, 32 + strip * 64);
}
static void draw_loose_strip(uint32_t strip)
{
    uint32_t i;
    strip_begin(strip);
    if (strip == 0) {
        txt(4, 0, "LOOSE", C_GRAY); txtn(60, 0, G.loose, C_HI);
        txt(100, 0, "SWING", C_GRAY); txtn(160, 0, G.swing, C_HI);
        txt(4, 20, "LATE SNARE", C_GRAY); txtn(110, 20, (int32_t)P[T_DRUMS][PD_LATE_SNARE] * 24 / 255, C_HI); txt(130, 20, "TICKS", C_DIM);
        txt(4, 40, "FEEL", C_GRAY); txt(60, 40, G.feel ? "HALF-TIME" : "2-STEP", C_HI);
        txt(160, 40, ui.row == 0 ? "<" : "", C_WHITE);
    } else if (strip == 1) {
        static const char *const TR2[WB_NTRACK] = {"GH", "SU", "PA", "KE", "DR", "WE"};
        for (i = 0; i < WB_NTRACK; i++) {
            int32_t x = 4 + (int32_t)i * 40;
            txt(x, 0, TR2[i], (ui.row == 1u && i < 4u) || (ui.row == 2u && i >= 4u) ? C_HI : C_GRAY);
            bar_h(x, 18, 34, 6, (uint32_t)G.drift[i] * 255u / 100u, C_AMB);
            txtn(x, 26, G.drift[i], C_HI);
        }
    } else {
        static const char *const LN2[WB_NLANE] = {"KK", "SN", "RM", "HC", "HO", "CL", "SH", "CR"};
        for (i = 0; i < WB_NLANE; i++) {
            int32_t x = 4 + (int32_t)i * 30;
            txt(x, 0, LN2[i], (ui.row == 2u && i < 2u) || (ui.row == 3u && i >= 2u && i < 6u) || (ui.row == 4u && i >= 6u) ? C_HI : C_GRAY);
            bar_h(x, 18, 26, 6, (uint32_t)G.drift[WB_NTRACK + i] * 255u / 100u, C_AMB);
            txtn(x, 26, G.drift[WB_NTRACK + i], C_HI);
        }
    }
    cv_blit(0, 32 + strip * 64);
}
static void draw_gen_strip(uint32_t strip)
{
    uint32_t t = ui.track;
    strip_begin(strip);
    if (strip == 0) {
        txt(4, 0, "GENERATOR", C_GRAY); txt(100, 0, WB_GEN_NAME[GEN[t].kind], C_WHITE);
        txt(4, 20, "AUTO", C_GRAY); txt(60, 20, GEN[t].auto_run ? "EVERY BAR" : "OFF", GEN[t].auto_run ? C_HI : C_DIM);
        if (t == T_DRUMS) { txt(130, 20, "LANE", C_GRAY); txt(180, 20, GEN[t].lane == 0xFFu ? "ALL" : WB_LANE_NAME[GEN[t].lane], C_HI); }
        txt(4, 40, "OCT:PICK SEL:CAST ARP:AUTO", C_DIM);
    } else if (strip == 1) {
        txt(4, 0, "REC+SEL: UNDO LAST CAST", C_DIM);
        txt(4, 20, "KEY", C_GRAY); txt(40, 20, WB_SCALES[G.scale % WB_NSCALE].name, C_HI);
        txtn(120, 20, G.root, C_AMB);
        txt(4, 40, gen_undo_valid ? "UNDO READY" : "", C_GRAY);
    } else {
        static const char *const HELP[WB_NGEN] = {"PICK ONE WITH OCT-/OCT+", "CHORD WALK; GRAVITY PULLS HOME",
            "MARKOV PHRASES, WITH GAPS", "TURING MACHINE: LOCK, LENGTH",
            "EUCLID HITS, FEEL TEMPLATE", "A FEW STEPS CHANGE; UNDO",
            "SHAPED MELODY; SUB FOLLOWS PADS", "GHOST ANSWERS KEYS NEXT BAR"};
        txt(4, 0, HELP[GEN[t].kind], C_GRAY);
    }
    cv_blit(0, 32 + strip * 64);
}
static void draw_coven_strip(uint32_t strip)
{
    uint32_t src, dst;
    strip_begin(strip);
    if (strip == 0) {
        txt(50, 0, "GH SU PA KE DR WE", C_GRAY);
        for (src = 0; src < 2u; src++) {
            txt(4, 20 + (int32_t)src * 20, WB_TRACK_NAME[src], src == ui.row ? C_WHITE : C_GRAY);
            for (dst = 0; dst < WB_NTRACK; dst++) txt(50 + (int32_t)dst * 30, 20 + (int32_t)src * 20, WB_SPELL_NAME[G.coven[src][dst]], src == ui.row ? C_HI : C_AMB);
        }
    } else if (strip == 1) {
        for (src = 2; src < 5u; src++) {
            txt(4, (int32_t)(src - 2u) * 20, WB_TRACK_NAME[src], src == ui.row ? C_WHITE : C_GRAY);
            for (dst = 0; dst < WB_NTRACK; dst++) txt(50 + (int32_t)dst * 30, (int32_t)(src - 2u) * 20, WB_SPELL_NAME[G.coven[src][dst]], src == ui.row ? C_HI : C_AMB);
        }
    } else {
        txt(4, 0, WB_TRACK_NAME[5], ui.row == 5u ? C_WHITE : C_GRAY);
        for (dst = 0; dst < WB_NTRACK; dst++) txt(50 + (int32_t)dst * 30, 0, WB_SPELL_NAME[G.coven[5][dst]], ui.row == 5u ? C_HI : C_AMB);
        txt(4, 20, "ROW:OCT COLS:SELECT KNOB:SPELL", C_DIM);
    }
    cv_blit(0, 32 + strip * 64);
}
static void draw_summon_strip(uint32_t strip)
{
    strip_begin(strip);
    if (strip == 0) {
        txt(4, 0, "MOOD", C_GRAY); txt(60, 0, WB_MOOD_NAME[ui.mood & 3u], C_WHITE);
        txt(4, 20, MOODTXT[ui.mood & 3u], C_AMB);
        txt(4, 40, "SEL:SUMMON  REC+SEL:KEEP DRUMS", C_DIM);
    } else if (strip == 1) {
        txt(4, 0, "KEY", C_GRAY); txtn(40, 0, G.root, C_HI); txt(70, 0, WB_SCALES[G.scale % WB_NSCALE].name, C_HI);
        txt(4, 20, "BPM", C_GRAY); txtn(40, 20, G.bpm, C_HI); txt(100, 20, G.feel ? "HALF" : "2-STEP", C_AMB);
        txt(160, 20, "SEED", C_GRAY); txtn(206, 20, (int32_t)(G.seed[0] | (uint32_t)G.seed[1] << 8) % 1000, C_HI);
        txt(4, 40, "SONG: INTRO BUILD DROP BREAK", C_DIM);
    } else {
        txt(4, 0, "PATTERNS A-D ARE WRITTEN,", C_GRAY);
        txt(4, 20, "THE SONG TOO. EDIT AFTER.", C_GRAY);
    }
    cv_blit(0, 32 + strip * 64);
}
static void draw_song_strip(uint32_t strip)
{
    uint32_t i;
    strip_begin(strip);
    if (strip == 0) {
        txt(4, 0, "SLOT", C_GRAY); txtn(50, 0, (int32_t)ui.slot + 1, C_HI); txt(70, 0, prj_exists(ui.slot) ? "USED" : "EMPTY", C_AMB);
        txt(4, 20, "SEL: SAVE   REC+SEL: LOAD", C_DIM);
        txt(4, 40, G.song_mode ? "SONG MODE  (HOME: BLACK 7)" : "PATTERN MODE (HOME: BLACK 7)", C_GRAY);
    } else {
        for (i = 0; i < (strip == 1u ? 3u : 2u); i++) {
            uint32_t r = (ui.row / 5u) * 5u + i + (strip - 1u) * 3u;
            wb_songrow_t *sr;
            if (r >= WB_NSONG) break;
            sr = &SONG[r];
            txtn(4, (int32_t)i * 20, (int32_t)r + 1, r == ui.row ? C_WHITE : C_GRAY);
            if (sr->pat < WB_NPAT) {
                char b[2] = {(char)('A' + sr->pat), 0};
                txt(40, (int32_t)i * 20, b, C_HI);
                txt(70, (int32_t)i * 20, WB_SEC_NAME[sr->section % WB_NSEC], C_AMB);
                txtn(150, (int32_t)i * 20, sr->bars, C_HI); txt(180, (int32_t)i * 20, "BARS", C_GRAY);
            } else txt(40, (int32_t)i * 20, "- END -", C_DIM);
            if (wb_pos.playing && G.song_mode && wb_pos.song_row == r) txt(226, (int32_t)i * 20, ">", C_WHITE);
        }
    }
    cv_blit(0, 32 + strip * 64);
}
static void draw_body(void)
{
    uint32_t s;
    for (s = 0; s < 3u; s++) {
        switch (ui.page) {
        case PG_HOME: draw_home_strip(s); break;
        case PG_SOUND: if (s < 2u) draw_params_strip(s, PG_SOUND); else { strip_begin(s); txt(4, 0, "ROWS: OCT-/OCT+   KEYS PLAY", C_DIM); if (ui.track == T_KEYS) { txt(4, 20, "PATCH", C_GRAY); txt(60, 20, KEYS_PATCH_NAME[P[T_KEYS][PK_PATCH] >> 5], C_HI); } if (ui.track == T_GHOST) { txt(4, 20, P[T_GHOST][PG_MODE] < 128u ? "CHOP: WHITE KEYS = SLICES" : "SING: KEYS = NOTES", C_GRAY); } cv_blit(0, 32 + s * 64); } break;
        case PG_SEQ: draw_steps_strip(s); break;
        case PG_LOOSE: draw_loose_strip(s); break;
        case PG_GEN: draw_gen_strip(s); break;
        case PG_FX: if (s < 2u) draw_params_strip(s, PG_FX); else { strip_begin(s); txt(4, 0, FXROW[ui.row & 3u], C_GRAY); txt(4, 20, "DUST>TAPE>ECHO>SPACE>DUCK>GLUE", C_DIM); cv_blit(0, 32 + s * 64); } break;
        case PG_COVEN: draw_coven_strip(s); break;
        case PG_SUMMON: draw_summon_strip(s); break;
        case PG_SONG: draw_song_strip(s); break;
        default: break;
        }
    }
}
static void draw_footer(void)
{
    uint32_t i;
    cv_begin(240, 40, C_BG);
    for (i = 0; i < 4u; i++) {
        knobinfo_t k;
        ui_knobinfo(i, &k);
        knob_col(i, k.name, k.v, k.bar > 255u ? 255u : k.bar, 1);
    }
    cv_blit(0, 200);
}

/* ---- LEDs: steps on the keys in STEPS, mutes on HOME, the page button */
static void ui_leds(void)
{
    uint32_t k, t = ui.track;
    static const uint8_t PAGE_BTN[WB_NPAGE] = {BTN_HOME, BTN_EDIT, BTN_SEQ, BTN_ENV, BTN_LFO, BTN_FX, BTN_GLO, BTN_ARP, BTN_SAVE};
    for (k = 0; k < 14u; k++) fm1_led_key(k, k == PAGE_BTN[ui.page] || (k == BTN_PLAY && wb_pos.playing) || (k == BTN_REC && ui.rec) || (k == BTN_ARP && ui.page == PG_GEN && GEN[t].auto_run));
    for (k = 0; k < 27u; k++) {
        uint32_t w = white_index(k), on = 0;
        if (ui.page == PG_SEQ && w != 0xFFu) {
            uint32_t st = ui.seqpage * 16u + w;
            wb_step_t *s = t == T_DRUMS ? gen_pat()->lane[ui.lane].s : gen_pat()->t[t].s;
            on = s[st].note != 0u;
            if (wb_pos.playing && st == (uint32_t)((wb_pos.tick / SEQ_STEP) % (t == T_DRUMS ? (gen_pat()->lane[ui.lane].len ? gen_pat()->lane[ui.lane].len : 16u) : (gen_pat()->t[t].len ? gen_pat()->t[t].len : 16u)))) on ^= 1u;
        } else if (ui.page == PG_HOME && w < WB_NTRACK) on = !(G.mute >> w & 1u);
        else if (ui.live_note[k]) on = 1;
        fm1_led_key(KEY_LED(k), on);
    }
}

/* ---- MIDI in / out (main loop) */
static void ui_midi(void)
{
    uint8_t st, d1, d2;
    static const uint8_t GM[16] = {L_KICK, L_RIM, L_SNARE, L_FOUND1, L_SNARE, L_FOUND2, L_HATC, L_FOUND1, L_HATC, L_FOUND2, L_HATO, L_FOUND2, L_FOUND1, L_CRASH, L_FOUND1, L_CRASH};   /* 36..51 */
    while (midi_get(&st, &d1, &d2)) {
        uint32_t ch = st & 15u, kind = st & 0xF0u;
        if (kind == 0x90u || kind == 0x80u) {
            uint32_t vel = kind == 0x90u ? d2 : 0u, t;
            if (ch == 9u || ch == T_DRUMS) { if (vel && d1 >= 36u && d1 < 52u) live_push(T_DRUMS, GM[d1 - 36u], vel, 0); continue; }
            t = ch < WB_NTRACK ? ch : ui.track;
            if (t == T_WEATHER) continue;
            live_push(t, d1, vel, t == T_GHOST ? (d1 & 15u) : 0u);
            if (vel) ui_record_live(t, d1, vel, t == T_GHOST ? (d1 & 15u) : 0u);
        } else if (kind == 0xB0u && ch < WB_NTRACK && d1 >= 16u && d1 < 32u) {
            P[ch][d1 - 16u] = (uint8_t)(d2 * 2u);
            if (ui.page == PG_SOUND && ui.track == ch) ui.dirty |= DIRTY_BODY | DIRTY_FOOT;
        } else if (st == 0xFAu) wb_req |= REQ_PLAY;
        else if (st == 0xFCu) wb_req |= REQ_STOP;
    }
    /* clock out: 24 per quarter = every 4 ticks */
    if (wb_pos.playing) {
        uint32_t t4 = wb_pos.tick / 4u;
        if (t4 != ui.clock_tick) { ui.clock_tick = t4; midi_send(0xF8, 0, 0); }
    }
}

/* ---- the main loop body */
static void ui_init(void)
{
    memset(&ui, 0, sizeof ui);
    ui.held_step = 0xFF;
    ui.octave = PREFS.octave < 4u ? PREFS.octave : 1u;
    ui.slot = PREFS.slot & 1u;
    ui.dirty = DIRTY_ALL;
    ui.track = T_DRUMS;
    palette_set(PREFS.palette);
    ui_tempo();
    lcd_fill(0, 0, 240, 240, C_BG);
}
static void ui_poll(void)
{
    uint32_t keys, down, up, k, b, rel, pressed_now = fm1_in.buttons;
    int32_t d;
    /* shift = REC held; REC alone toggles record on release */
    b = fm1_input_edges(&rel);
    if (b & (1u << BTN_REC)) { ui.shift = 1; ui.rec ^= 0u; }
    if (rel & (1u << BTN_REC)) { if (ui.shift == 1u) { ui.rec ^= 1u; ui.dirty |= DIRTY_HEAD; } ui.shift = 0; }
    for (k = 0; k < 14u; k++)
        if ((b >> k) & 1u && k != BTN_REC) { if (ui.shift) ui.shift = 2; ui_button(k); }
    (void)pressed_now;
    keys = fm1_in.notes;
    down = keys & ~ui.prev_keys;
    up = ui.prev_keys & ~keys;
    ui.prev_keys = keys;
    for (k = 0; k < 27u; k++) {
        if ((down >> k) & 1u) { if (ui.shift) ui.shift = 2; ui_key_down(k); }
        if ((up >> k) & 1u) ui_key_up(k);
    }
    /* shift + knobs on a held step: condition / ratchet / throw / accent */
    for (k = 0; k < 4u; k++) {
        d = fm1_enc_take(ENC_KNOB1 + k);
        if (!d) continue;
        if (ui.page == PG_SEQ && ui.held_step != 0xFFu && ui.shift) {
            wb_step_t *s = ui.track == T_DRUMS ? &gen_pat()->lane[ui.lane].s[ui.held_step] : &gen_pat()->t[ui.track].s[ui.held_step];
            uint32_t f = s->flags;
            if (k == 0) f = (f & ~(7u << 2)) | ((uint32_t)CLAMP((int32_t)ST_COND(f) + d, 0, 7) << 2);
            if (k == 1) f = (f & ~3u) | (uint32_t)CLAMP((int32_t)ST_RATCHET(f) + d, 0, 3);
            if (k == 2) f = d > 0 ? f | (1u << 7) : f & ~(1u << 7);
            if (k == 3) f = d > 0 ? f | (1u << 6) : f & ~(1u << 6);
            s->flags = (uint8_t)f;
            ui.shift = 2;
            ui.dirty |= DIRTY_BODY;
        } else ui_knob(k, d);
    }
    d = fm1_enc_take(ENC_ALGORITHM);
    if (d) { ui.track = (uint32_t)CLAMP((int32_t)ui.track + d, 0, WB_NTRACK - 1); ui.held_step = 0xFF; ui.dirty |= DIRTY_ALL; }
    d = fm1_enc_take(ENC_PRESETS);
    if (d) { G.pattern = (uint8_t)CLAMP((int32_t)G.pattern + d, 0, WB_NPAT - 1); ui.dirty |= DIRTY_ALL; }
    d = fm1_enc_take(ENC_SELECT);
    if (d) {
        if (ui.page == PG_SEQ || ui.page == PG_SOUND || ui.page == PG_GEN) {
            if (ui.track == T_DRUMS) {
                if (ui.page == PG_GEN) { int32_t l = GEN[T_DRUMS].lane == 0xFFu ? -1 : (int32_t)GEN[T_DRUMS].lane; l = CLAMP(l + d, -1, (int32_t)WB_NLANE - 1); GEN[T_DRUMS].lane = (uint8_t)(l < 0 ? 0xFF : l); }
                else ui.lane = (uint32_t)CLAMP((int32_t)ui.lane + d, 0, WB_NLANE - 1);
            } else if (ui.page == PG_SEQ) ui.seqpage = (uint32_t)CLAMP((int32_t)ui.seqpage + d, 0, 3);
        } else if (ui.page == PG_COVEN) ui.lane = d > 0 ? 1u : 0u;
        else if (ui.page == PG_SONG) ui.row = (uint32_t)CLAMP((int32_t)ui.row + d, 0, WB_NSONG - 1);
        else if (ui.page == PG_HOME) { G.bpm = (uint16_t)CLAMP((int32_t)G.bpm + d, 60, 200); ui_tempo(); }
        ui.dirty |= DIRTY_ALL;
    }
    ui_midi();
    gen_auto_tick();
    macro_tick();
    if (fm1_ms - ui.anim_ms >= 100u) { ui.anim_ms = fm1_ms; gen_weather_drift(); if (ui.page == PG_HOME || ui.page == PG_SEQ || ui.page == PG_SONG) ui.dirty |= DIRTY_BODY; if (ui.msg[0] && fm1_ms - ui.msg_ms < 1700u) ui.dirty |= DIRTY_HEAD; }
    if (ui.dirty && fm1_ms - ui.last_draw_ms >= 33u) {
        ui.last_draw_ms = fm1_ms;
        if (ui.dirty & DIRTY_HEAD) draw_header();
        if (ui.dirty & DIRTY_BODY) draw_body();
        if (ui.dirty & DIRTY_FOOT) draw_footer();
        ui.dirty = 0;
    }
    ui_leds();
    if (ui.prefs_dirty && fm1_ms - ui.prefs_ms > 3000u && !wb_pos.playing) { PREFS.octave = (uint8_t)ui.octave; prefs_save(); ui.prefs_dirty = 0; }
}
