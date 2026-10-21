/* SPDX-License-Identifier: GPL-3.0-only */
/* WitchBox: a six-track groovebox for the M-VAVE FM-1, for Burial-style 2-step and melodic dubstep.
 *
 *   GHOST    a chop sampler on a 3 s tape, and a formant singer
 *   SUB      a sliding sub with a reese layer and a tempo-synced wobble
 *   PADS     a super-saw pad with a swell and a 'distance' macro
 *   KEYS     4-op FM keys: e-piano, bells, plucks
 *   DRUMS    an 8-lane synthesized 2-step kit
 *   WEATHER  rain, crackle, hiss, a drone, the tape scrubbed as a field recording
 *
 * The sequencer's LOOSE system (drift, swing, late snares) keeps nothing on the grid (wb/seq.h).
 * Generators write ordinary steps you can edit (wb/gen.h). Everything goes through DUST, TAPE, ECHO,
 * SPACE, DUCK and GLUE (wb/fx.h). The UI is in wb/ui.h. Projects live in flash (wb/project.h).
 *
 * This file is one compilation unit: platform.c (Dinghy's wiring of the Felucca / SLOOP platform)
 * first, then the WitchBox headers, then the two functions the platform asks for.
 * Integers only: the chip has no FPU and no libgcc (docs/PLATFORM-RULES.md). */
#define APP_NAME "WITCHBOX"
#include "platform.c"

#include "wb/dsp.h"
#include "wb/types.h"
#include "wb/eng_ghost.h"
#include "wb/eng_sub.h"
#include "wb/eng_pads.h"
#include "wb/eng_keys.h"
#include "wb/eng_drums.h"
#include "wb/eng_weather.h"
#include "wb/fx.h"
#include "wb/seq.h"
#include "wb/gen.h"
#include "wb/project.h"
#include "wb/ui.h"

/* ---- the audio interrupt: sub-blocks of WB_SUB frames; the sequencer, then every track into its own
 * block, then the master chain. Nothing here touches the flash or the screen. */
static int32_t wb_tb[WB_NTRACK][2 * WB_SUB];
static volatile uint32_t wb_audio_ready;

static void app_audio(int32_t *out, uint32_t n)
{
    uint32_t done = 0;
#ifndef DINGHY_HOST
    uint32_t t0 = fm1_ticks();
#endif
    if (!wb_audio_ready) return;
    while (done < n) {
        uint32_t k = n - done > WB_SUB ? WB_SUB : n - done, t;
        seq_advance(k);
        for (t = 0; t < WB_NTRACK; t++) memset(wb_tb[t], 0, k * 2u * sizeof(int32_t));
        ghost_render(wb_tb[T_GHOST], k);
        sub_render(wb_tb[T_SUB], k);
        pads_render(wb_tb[T_PADS], k);
        keys_render(wb_tb[T_KEYS], k);
        drums_render(wb_tb[T_DRUMS], k);
        weather_render(wb_tb[T_WEATHER], k);
        fx_process(wb_tb, out + 2u * done, k);
        done += k;
    }
#ifndef DINGHY_HOST
    wb_pos.isr_us = (fm1_ticks() - t0) / FM1_TICKS_PER_US;
#endif
}

/* ---- boot and the main loop */
static void app_main(void)
{
    prefs_load();
    prj_defaults();
    ghost_init(); sub_init(); pads_init(); keys_init(); drums_init(); weather_init();
    fx_init();
    seq_init();
    ui_init();
    draw_text_box(0, 90, 240, &FONT_L, "WITCHBOX", C_HI, 1);
    draw_text_box(0, 130, 240, &FONT_S, "v" APP_VER "  summoning the choir", C_GRAY, 1);
    ghost_boot_choir();                       /* the tape sings out of the box */
    ghost_reslice();
#ifndef DINGHY_HOST
    if (prj_exists(PREFS.slot & 1u) && !prj_load(PREFS.slot & 1u)) { ui.slot = PREFS.slot & 1u; }
#endif
    ui_tempo();
    wb_audio_ready = 1;
    lcd_fill(0, 0, 240, 240, C_BLACK);
    ui.dirty = DIRTY_ALL;
    for (;;) {
        platform_poll();
        ui_poll();
    }
}
