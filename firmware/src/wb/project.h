/* SPDX-License-Identifier: GPL-3.0-only */
/* WitchBox projects: everything that makes a piece (patterns, song, params, generators, globals) in one of
 * two 64 KiB flash slots (PROJECT_AT in platform.c), and the small preferences in the settings sector.
 * Saving erases sectors with the interrupts off: the sound stops. The UI stops the transport first.
 * On the host the slots are files under build/host/. */
#pragma once
#include "types.h"
#include "gen.h"

#define PRJ_MAGIC 0x31584257u          /* "WBX1" */
#define PRJ_VERSION 1u
typedef struct { uint32_t magic, version, size, crc; } prj_head_t;

typedef struct { const void *p; uint32_t n; } prj_part_t;
static const prj_part_t *prj_parts(uint32_t *count)
{
    static prj_part_t parts[6];
    parts[0].p = &G; parts[0].n = sizeof G;
    parts[1].p = SONG; parts[1].n = sizeof SONG;
    parts[2].p = (const void *)P; parts[2].n = sizeof P;
    parts[3].p = (const void *)FXP; parts[3].n = sizeof FXP;
    parts[4].p = GEN; parts[4].n = sizeof GEN;
    parts[5].p = PAT; parts[5].n = sizeof PAT;
    *count = 6;
    return parts;
}
static uint32_t prj_crc(uint32_t c, const uint8_t *p, uint32_t n)
{
    uint32_t k;
    while (n--) {
        c ^= *p++;
        for (k = 0; k < 8u; k++) c = c & 1u ? (c >> 1) ^ 0xEDB88320u : c >> 1;
    }
    return c;
}
static uint32_t prj_size(void)
{
    uint32_t n, i, s = sizeof(prj_head_t);
    const prj_part_t *pp = prj_parts(&n);
    for (i = 0; i < n; i++) s += pp[i].n;
    return s;
}

#ifdef DINGHY_HOST
#include <stdio.h>
static int prj_slot_name(char *b, uint32_t slot) { snprintf(b, 64, "build/host/project-%u.bin", slot); return 0; }
static int prj_save(uint32_t slot)
{
    char name[64];
    uint32_t n, i, crc = 0xFFFFFFFFu;
    const prj_part_t *pp = prj_parts(&n);
    prj_head_t h = {PRJ_MAGIC, PRJ_VERSION, prj_size(), 0};
    FILE *f;
    prj_slot_name(name, slot);
    for (i = 0; i < n; i++) crc = prj_crc(crc, pp[i].p, pp[i].n);
    h.crc = ~crc;
    if (!(f = fopen(name, "wb"))) return -1;
    fwrite(&h, sizeof h, 1, f);
    for (i = 0; i < n; i++) fwrite(pp[i].p, 1, pp[i].n, f);
    fclose(f);
    return 0;
}
static int prj_load(uint32_t slot)
{
    char name[64];
    uint32_t n, i, crc = 0xFFFFFFFFu;
    const prj_part_t *pp = prj_parts(&n);
    prj_head_t h;
    FILE *f;
    prj_slot_name(name, slot);
    if (!(f = fopen(name, "rb"))) return -1;
    if (fread(&h, sizeof h, 1, f) != 1 || h.magic != PRJ_MAGIC || h.version != PRJ_VERSION || h.size != prj_size()) { fclose(f); return -2; }
    for (i = 0; i < n; i++) if (fread((void *)pp[i].p, 1, pp[i].n, f) != pp[i].n) { fclose(f); return -3; }
    fclose(f);
    for (i = 0; i < n; i++) crc = prj_crc(crc, pp[i].p, pp[i].n);
    return ~crc == h.crc ? 0 : -4;
}
static int prj_exists(uint32_t slot)
{
    char name[64];
    FILE *f;
    prj_slot_name(name, slot);
    if (!(f = fopen(name, "rb"))) return 0;
    fclose(f);
    return 1;
}
#else
/* a linear writer over the slot: erases each 4 KiB sector when it is first touched */
static struct { uint32_t off, erased_to; } prj_w;
static int prj_w_bytes(const uint8_t *src, uint32_t n)
{
    while (n) {
        uint32_t sec = prj_w.off & ~0xFFFu, chunk, took;
        int rc;
        if (prj_w.erased_to <= sec) {
            if ((rc = fl_erase4k(sec, &took)) != 0) return rc;
            prj_w.erased_to = sec + 0x1000u;
        }
        chunk = (sec + 0x1000u) - prj_w.off;
        if (chunk > n) chunk = n;
        if ((rc = fl_write(prj_w.off, src, chunk)) != 0) return rc;
        prj_w.off += chunk; src += chunk; n -= chunk;
    }
    return 0;
}
static int prj_save(uint32_t slot)
{
    uint32_t n, i, crc = 0xFFFFFFFFu, base = PROJECT_AT + slot * PROJECT_SLOT;
    const prj_part_t *pp = prj_parts(&n);
    prj_head_t h = {PRJ_MAGIC, PRJ_VERSION, prj_size(), 0};
    int rc;
    if (!flash_ok || slot >= PROJECT_SLOTS || prj_size() > PROJECT_SLOT) return -1;
    for (i = 0; i < n; i++) crc = prj_crc(crc, pp[i].p, pp[i].n);
    h.crc = ~crc;
    prj_w.off = base + sizeof h;              /* the parts first, the header last: a cut save stays invalid */
    prj_w.erased_to = 0;
    {
        uint32_t took;
        if ((rc = fl_erase4k(base, &took)) != 0) return rc;
        prj_w.erased_to = base + 0x1000u;
    }
    for (i = 0; i < n; i++) {
        static uint8_t tmp[256];
        const uint8_t *src = pp[i].p;
        uint32_t left = pp[i].n;
        while (left) {                        /* through a RAM bounce (P and FXP are volatile; the pool is RAM too, but keep one path) */
            uint32_t k = left > sizeof tmp ? sizeof tmp : left;
            memcpy(tmp, src, k);
            if ((rc = prj_w_bytes(tmp, k)) != 0) return rc;
            src += k; left -= k;
        }
    }
    return fl_write(base, (const uint8_t *)&h, sizeof h);
}
static int prj_load(uint32_t slot)
{
    uint32_t n, i, crc = 0xFFFFFFFFu, base = PROJECT_AT + slot * PROJECT_SLOT, off;
    const prj_part_t *pp = prj_parts(&n);
    prj_head_t h;
    if (!flash_ok || slot >= PROJECT_SLOTS) return -1;
    if (st_read(base, &h, sizeof h) || h.magic != PRJ_MAGIC || h.version != PRJ_VERSION || h.size != prj_size()) return -2;
    off = base + sizeof h;
    for (i = 0; i < n; i++) {
        if (st_read(off, (void *)pp[i].p, pp[i].n)) return -3;
        off += pp[i].n;
    }
    for (i = 0; i < n; i++) crc = prj_crc(crc, pp[i].p, pp[i].n);
    return ~crc == h.crc ? 0 : -4;
}
static int prj_exists(uint32_t slot)
{
    prj_head_t h;
    uint32_t base = PROJECT_AT + slot * PROJECT_SLOT;
    if (!flash_ok || slot >= PROJECT_SLOTS) return 0;
    return !st_read(base, &h, sizeof h) && h.magic == PRJ_MAGIC && h.version == PRJ_VERSION;
}
#endif

/* ---- preferences in the settings sector (256 bytes max) */
typedef struct {
    uint32_t magic;
    uint8_t palette, slot, octave, pad;
    uint16_t bpm;
    uint8_t feel, swing, loose;
    uint8_t rsv[5];
} wb_prefs_t;
#define PREFS_MAGIC 0x50584257u
static wb_prefs_t PREFS;
static void prefs_defaults(void) { memset(&PREFS, 0, sizeof PREFS); PREFS.magic = PREFS_MAGIC; PREFS.palette = 1; PREFS.bpm = 136; PREFS.swing = 56; PREFS.loose = 50; PREFS.octave = 1; }
static void prefs_load(void) { prefs_defaults(); if (settings_load(&PREFS, sizeof PREFS) || PREFS.magic != PREFS_MAGIC) prefs_defaults(); }
static void prefs_save(void) { settings_save(&PREFS, sizeof PREFS); }

/* ---- a fresh project: sensible sounds, empty patterns */
static void prj_defaults(void)
{
    uint32_t t, p;
    memset((void *)P, 0, sizeof P);
    memset((void *)FXP, 0, sizeof FXP);
    memset(&G, 0, sizeof G);
    memset(SONG, 0xFF, sizeof SONG);
    for (p = 0; p < WB_NPAT; p++) {
        for (t = 0; t < WB_NTRACK - 2u; t++) gen_clear_track(&PAT[p].t[t], 16);
        for (t = 0; t < WB_NLANE; t++) gen_clear_lane(&PAT[p].lane[t], 16);
    }
    G.bpm = PREFS.bpm ? PREFS.bpm : 136; G.feel = PREFS.feel; G.swing = PREFS.swing; G.loose = PREFS.loose;
    G.root = 9; G.scale = 0; G.pattern = 0;
    for (t = 0; t < WB_NTRACK + WB_NLANE; t++) G.drift[t] = 50;
    G.drift[WB_NTRACK + L_KICK] = 15; G.drift[WB_NTRACK + L_SNARE] = 60; G.drift[WB_NTRACK + L_HATC] = 45;
    G.coven[T_DRUMS][T_PADS] = SPELL_DUCK;
    G.seed[0] = 0x57; G.seed[1] = 0x42; G.seed[2] = 0x01; G.seed[3] = 0x00;
    for (t = 0; t < WB_NTRACK; t++) { P[t][PC_LEVEL] = 200; P[t][PC_FILT] = 128; P[t][PC_PAN] = 128; }
    P[T_GHOST][PG_MODE] = 40; P[T_GHOST][PG_PITCH] = 128; P[T_GHOST][PG_FORMANT] = 128; P[T_GHOST][PG_GATE] = 0; P[T_GHOST][PG_VOWEL] = 100; P[T_GHOST][PG_BREATH] = 60;
    P[T_GHOST][PC_SPACE] = 180; P[T_GHOST][PC_ECHO] = 100;
    P[T_SUB][PS_SHAPE] = 30; P[T_SUB][PS_DECAY] = 120; P[T_SUB][PS_GLIDE] = 40; P[T_SUB][PS_CUTOFF] = 100; P[T_SUB][PS_WOBBLE_DEPTH] = 128; P[T_SUB][PC_LEVEL] = 230; P[T_SUB][PC_DUCK] = 120;
    P[T_PADS][PP_SPREAD] = 110; P[T_PADS][PP_VOICES] = 150; P[T_PADS][PP_CUTOFF] = 100; P[T_PADS][PP_RESO] = 40; P[T_PADS][PP_ATTACK] = 110; P[T_PADS][PP_RELEASE] = 160; P[T_PADS][PP_DISTANCE] = 100;
    P[T_PADS][PC_SPACE] = 150; P[T_PADS][PC_DUST] = 90; P[T_PADS][PC_DUCK] = 220; P[T_PADS][PC_ECHO] = 50;
    P[T_KEYS][PK_PATCH] = 10; P[T_KEYS][PK_RATIO] = 128; P[T_KEYS][PK_INDEX] = 128; P[T_KEYS][PK_DECAY] = 128; P[T_KEYS][PK_BRIGHT] = 160; P[T_KEYS][PK_RELEASE] = 100; P[T_KEYS][PK_DETUNE] = 128;
    P[T_KEYS][PC_SPACE] = 160; P[T_KEYS][PC_ECHO] = 110; P[T_KEYS][PC_DUCK] = 120;
    P[T_DRUMS][PD_KIT] = 30; P[T_DRUMS][PD_KICK_TUNE] = 110; P[T_DRUMS][PD_KICK_DECAY] = 120; P[T_DRUMS][PD_SNARE_TONE] = 120; P[T_DRUMS][PD_SNARE_SNAP] = 140;
    P[T_DRUMS][PD_HAT_DECAY] = 80; P[T_DRUMS][PD_FOUND_PITCH] = 128; P[T_DRUMS][PD_LATE_SNARE] = 70; P[T_DRUMS][PC_DUST] = 110; P[T_DRUMS][PC_SPACE] = 40;
    P[T_WEATHER][PW_RAIN] = 90; P[T_WEATHER][PW_CRACKLE] = 140; P[T_WEATHER][PW_HISS] = 50; P[T_WEATHER][PW_DRONE] = 30; P[T_WEATHER][PW_TONE] = 110;
    P[T_WEATHER][PW_DENSITY] = 128; P[T_WEATHER][PW_WIDTH] = 160; P[T_WEATHER][PW_FIELD] = 30; P[T_WEATHER][PC_LEVEL] = 150; P[T_WEATHER][PC_DUCK] = 80;
    FXP[FX_DUST_BITS] = 70; FXP[FX_DUST_RATE] = 50; FXP[FX_DUST_HISS] = 50; FXP[FX_TAPE_WOW] = 80; FXP[FX_TAPE_SAT] = 80;
    FXP[FX_ECHO_TIME] = 2; FXP[FX_ECHO_FB] = 140; FXP[FX_ECHO_TONE] = 110; FXP[FX_SPACE_SIZE] = 160; FXP[FX_SPACE_DAMP] = 120; FXP[FX_SPACE_PRE] = 60;
    FXP[FX_DUCK_REL] = 110; FXP[FX_DUCK_DEPTH] = 180; FXP[FX_GLUE] = 110; FXP[FX_MASTER_LP] = 255; FXP[FX_WIDTH] = 150;
    gen_init();
}
