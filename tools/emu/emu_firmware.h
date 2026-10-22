/* SPDX-License-Identifier: GPL-3.0-only */
/* The firmware the emulator runs: the ONE include list (included once, by emu_fw.c).
 * The sound side through tests/hostsim.c (engines, voices, FX, usb.c MIDI queues; FELUCCA_SEQ 0: without
 * Felucca's sequencer and hostsim's own renders), then the emulator's HAL (emu_hal_fw.h), the audio ISR (audio.c),
 * then the instrument's UI.
 * The instrument is ChoralRoot (docs/INTEGRATION.md): its engine (cr_engine.c) ticks in the audio ISR through
 * the mix_block shim below (cr_out.c cr_audio_block, then fx.c's mix), its UI (cr_ui.c) draws cr_draw.c's
 * screens. As choralroot.c: no seq.c (cr_out.c's events_block and midi_event), no SAMPLE / GRAIN / DRUM engines, no
 * Felucca icons or keycaps. The hook bodies are in emu_fw.c. */

/* ------------------------------------------------ sound, MIDI, sequencer --- */
#include <stddef.h>
#include <stdint.h>
#define CF_EMU 1                                      /* the device-only parts left out (cf_shim.c, cf_backup.c) and
                                                       * the palette as designed (cf_state.h: no panel correction);
                                                       * first, before cf_ui.c brings cf_state.h in */
#define main hostsim_main
#ifndef FELUCCA_SEQ
#define FELUCCA_SEQ 0                                 /* as choralroot.c: no Felucca sequencer */
#endif
#ifndef FELUCCA_SLICE
#define FELUCCA_SLICE 0                               /* as choralroot.c: no SLICE engine, so engine numbers match the device */
#endif
#ifndef FELUCCA_VA
#define FELUCCA_VA 1                                  /* as choralroot.c: the VA engine, its patch store */
#endif
#ifndef FELUCCA_CZ
#define FELUCCA_CZ 0                                  /* as coralflower.c: no CZ-1 */
#endif
#ifndef FELUCCA_QUAD
#define FELUCCA_QUAD 0                                /* as coralflower.c: no QUAD (yet) */
#endif
#ifndef FELUCCA_SAMPLE
#define FELUCCA_SAMPLE 0                              /* as choralroot.c: SAMPLE, GRAIN, DRUM retired */
#endif
#ifndef FELUCCA_GRAIN
#define FELUCCA_GRAIN 0
#endif
#ifndef FELUCCA_DRUM
#define FELUCCA_DRUM 1                                /* as coralflower.c: the sequencer's DRUMS on part 2 */
#endif
#define DRUM_PART0 3u              /* as coralflower.c: the drums on part 3, the LAYER on part 2 */
#define DRUM_NPART 1u
#define DRUM_LANE_KNOBS 1          /* as coralflower.c: each drum its own kit */
#define ENG_STATE_NPART 3u
#define VA_NPART 3u
#ifndef MIX_NPART
#define MIX_NPART 4u
#endif
#ifndef FELUCCA_ANALOG
#define FELUCCA_ANALOG 0                              /* as choralroot.c: ANALOG retired (engine 0) */
#endif
#ifndef FELUCCA_PHASE
#define FELUCCA_PHASE 0                               /* as choralroot.c: PHASE, LOFI, VOICE, TRIO, NOISE retired (0.18) */
#endif
#ifndef FELUCCA_LOFI
#define FELUCCA_LOFI 0
#endif
#ifndef FELUCCA_VOICE
#define FELUCCA_VOICE 0
#endif
#ifndef FELUCCA_TRIO
#define FELUCCA_TRIO 0
#endif
#ifndef FELUCCA_NOISE
#define FELUCCA_NOISE 0
#endif
#ifndef FELUCCA_PHYS
#define FELUCCA_PHYS 0                                /* as choralroot.c: PHYS retired (0.18) */
#endif
#ifndef FELUCCA_SLICER
#define FELUCCA_SLICER 0                              /* as coralflower.c */
#endif
#ifndef FELUCCA_PERFORM
#define FELUCCA_PERFORM 0                             /* as choralroot.c: no FX hold layer (perform.c; fx.c) */
#endif
#ifndef CR_FX
#define CR_FX 1                                       /* as choralroot.c: ChoralRoot's delay and reverb (cr_fx.c) */
#endif
#ifndef CR_CHORUS_MONO
#define CR_CHORUS_MONO 2                              /* fx.c: the unit's stereo chorus, and 0.18's mono return when
                                                         * asked (EMU_CHORUS=mono: emu_fw.c, tools/emu/classic_exact.sh) */
#endif
#ifndef CR_CPU1
#define CR_CPU1 1                                     /* as choralroot.c: the bass part as the second core's job; here the
                                                         * stub core (firmware/src/cr_cpu1.h) runs it when handed */
#endif
#define mix_block fx_mix_block                        /* fx.c's mix; audio.c gets ChoralRoot's below */
#ifndef FM6_POLY
#define FM6_POLY 8                                    /* as choralroot.c: FM6 capped at 8 voices (docs/FM6.md) */
#endif
#define CF_FX_OWN 1                                   /* as coralflower.c: CoralFlower's effects chain (cf_fx.c) */
#define FM6_PLAIN_NAMES 1                             /* as coralflower.c: "PAD", not "FM PAD" (eng_fm6.c) */
#define CF_LOOP_STORE 1                               /* as coralflower.c: the looper's flash object */
#define ST_LOG_OBJ (OBJ_PROJECT0 + 3u)                /* as coralflower.c: the settings' object keeps a log */
#define CLN_TRIM 4096                                 /* as coralflower.c */
static void cf_fx_chain(int32_t *l, int32_t *r, uint32_t n);
#define FX_MIX_HOOK cf_fx_chain
#include "../../tests/hostsim.c"
#undef mix_block
#undef main

/* ------------------------------------------------------- the emulator HAL --- */
#include "emu_hal_fw.h"
#include "../../firmware/src/cf_gran.c"
#include "../../firmware/src/cf_fx.c"
/* ------------------------------------------------- the instrument --- */
#define CF_UNIT 1
#include "../../firmware/src/cf_theory.c"
#include "../../firmware/src/cf_chord.c"
#include "../../firmware/src/cf_play.c"
#include "../../firmware/src/cf_out.c"
static void mix_block(int32_t *out, uint32_t n)        /* the audio ISR's block: the player first, then the mix */
{
    CR_PROF_START();
    cf_audio_block(n);
    fx_mix_block(out, n);
    cf_click_mix(out, n);
    CR_PROF(CRP_MASTER);
}
#include "../../firmware/src/audio.c"                 /* fm1_alnk0_irq: the audio ISR, called per block */

/* --------------------------------------------------------------------- UI --- */
#define FELUCCA_FLASH 1                               /* the file-backed NOR (emu_hal_fw.h): user sounds persist */
#ifndef FELUCCA_KEYCAPS
#define FELUCCA_KEYCAPS 0
#endif
#ifndef CV_ROWS
#define CV_ROWS 120u                                  /* gfx.c's canvas: CoralFlower's framebuffer (cf_fb.c) */
#endif
#ifndef FELUCCA_VERSION
#define FELUCCA_VERSION "CoralFlower EMU"
#endif
#include "../../firmware/src/gfx.c"
#include "../../firmware/src/panel.c"
#include "../../firmware/src/storage.c"
#include "../../firmware/src/cr_bank.c"               /* upreset.c (the flash file), the pools per engine */
#include "../../firmware/src/flora/flora_canvas.c"
#include "../../firmware/src/flora/flora.c"
#define CF_PUMP() cf_ui_pump()
static void cf_ui_pump(void);
#include "../../firmware/src/cf_fb.c"
#include "../../firmware/src/cf_sound.c"
#include "../../firmware/src/cf_state.h"
#include "../../firmware/src/cf_draw.c"
#include "../../firmware/src/cf_settings.c"
#include "../../firmware/src/cf_ui.c"
#include "../../firmware/src/cf_screens.c"
#include "../../firmware/src/cf_splash.c"
#include "../../firmware/src/fm6_store.c"
#include "../../firmware/src/cf_backup.c"             /* (the backup protocol, its handler; no SysEx transport here) */
/* the update gesture (cf_shim.c's cr_update_poll on the unit): the emulator says what the unit would do and ends */
#define CRB_STUB_VER() ((unsigned)(getenv("EMU_STUB") ? atoi(getenv("EMU_STUB")) : 0))
#define CRB_STUB_GO(w) do { printf("restart: stub request %08X\n", (unsigned)(w)); fflush(stdout); exit(0); } while (0)
#define CRB_UPDATE_SCREEN(t) printf("update: screen '%s'\n", (t))
#define CRB_UBOOT_GO() do { printf("restart: ROM USB boot\n"); fflush(stdout); exit(0); } while (0)
#include "../../firmware/src/cf_shim.c"
