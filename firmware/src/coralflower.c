/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 CoralFlower contributors; ChoralRoot FM-1 contributors; Felucca by Leo Kuroshita, Hügelton
 * Instruments */
/* CORALFLOWER: one compilation unit (the HAL is header-only), after ChoralRoot's choralroot.c: Felucca's platform and
 * sound engines, ChoralRoot's FX, sound pools and MIDI-in logic, and CoralFlower's own instrument: the chord model
 * (cf_theory.c, cf_chord.c), the player (cf_play.c, cf_out.c), the plant (flora/), the screens (cf_fb.c, cf_draw.c,
 * cf_screens.c) and the UI (cf_ui.c), with the names main.c calls (cf_shim.c).
 *
 * Host check (no device toolchain): cc -fsyntax-only -w -Ibuild/gen -Ifirmware/src -Ifirmware/hal
 * firmware/src/coralflower.c. The device build is tools/build.py with this file as the unit. */

/* ---------------------------------------------------- build options --- */
#ifndef FELUCCA_FLASH
#define FELUCCA_FLASH 1
#endif
#ifndef FELUCCA_OTA
#define FELUCCA_OTA 1
#endif
#ifndef FELUCCA_OTA_DRYRUN
#define FELUCCA_OTA_DRYRUN 0
#endif
#ifndef FELUCCA_CDC
#define FELUCCA_CDC 1
#endif
#ifndef FELUCCA_UAC
#define FELUCCA_UAC 0            /* USB audio recording: later (the settings that switch it are not here yet) */
#endif
#ifndef FELUCCA_UART
#define FELUCCA_UART 1
#endif
#define FELUCCA_SLICE 0
#define FELUCCA_SLICER 0
#define FELUCCA_FM4 0
#define FELUCCA_SAMPLE 0
#define FELUCCA_GRAIN 0
#define FELUCCA_DRUM 1             /* the sequencer's DRUMS (part 3: eng_drum.c keeps its own state) */
#define DRUM_PART0 3u              /* eng_drum.c: the kit's state for part 3 alone */
#define DRUM_NPART 1u
#define DRUM_LANE_KNOBS 1         /* eng_drum.c: each drum its own kit (the sequencer mixes kits) */
#define ENG_STATE_NPART 3u         /* engines.c: the chord, the bass and the sequencer's LAYER (part 2, a sound of its
                                    * own) have an engine's state; the drums need none */
#define VA_NPART 3u                /* eng_va.c: .. and a VA patch */
#define MIX_NPART 4u               /* fx.c: all four parts sound */
#define FELUCCA_ANALOG 0
#define FELUCCA_PHASE 0
#define FELUCCA_LOFI 0
#define FELUCCA_VOICE 0
#define FELUCCA_TRIO 0
#define FELUCCA_NOISE 0
#define FELUCCA_PHYS 0
#define FELUCCA_SEQ 0
#ifndef FELUCCA_VA
#define FELUCCA_VA 1
#endif
#define FELUCCA_CZ 0             /* no CZ-1: its factory tones carry no licence (docs/PLAN.md) */
#ifndef FELUCCA_QUAD
#define FELUCCA_QUAD 0
#endif
#ifndef FM6_POLY
#define FM6_POLY 8
#endif
#define FM6_PLAIN_NAMES 1        /* eng_fm6.c: "PAD", not "FM PAD" (the screens show the engine before the name) */
#ifndef CR_FX
#define CR_FX 1
#endif
#define FELUCCA_PERFORM 0
#ifndef CR_CPU1
#define CR_CPU1 1
#endif
#ifndef CV_ROWS
#define CV_ROWS 120u             /* gfx.c's canvas: CoralFlower's framebuffer lives in it (cf_fb.c) */
#endif
#define FELUCCA_ICONS 0
#define FELUCCA_KEYCAPS 0
#ifndef FELUCCA_ID
#define FELUCCA_ID "FM-1_930"    /* package identity (build.py: the .fwsc marker string) */
#endif
#ifndef FELUCCA_VERSION
#define FELUCCA_VERSION "CoralFlower 0.1"
#endif
#if FELUCCA_OTA && !FELUCCA_FLASH
#error "FELUCCA_OTA needs FELUCCA_FLASH"
#endif

#define CF_UNIT 1                /* (main.c: CoralFlower's calibration and update hooks) */

#if defined(__APPLE__)            /* the host syntax check only: Mach-O has no ELF sections (.noinit, .pool) */
#define __attribute__(x)
#endif

/* ------------------------------------------------------------- HAL --- */
#include <stdint.h>
#include "fm1_time.h"
#include "fm1_sys.h"
#include "fm1_irq.h"
#include "fm1_guard.h"
#include "fm1_input.h"
#include "fm1_timer.h"
#include "fm1_audio.h"
#include "fm1_adc.h"
#include "fm1_lcd_hw.h"
#if CR_CPU1
#include "fm1_cpu1.h"
#endif
#include "felucca_tables.h"

/* ----------------------------------------------------- base, display --- */
#include "libc.c"
#include "lcd.c"
#include "gfx.c"

/* ----------------------------------------------------------- sound --- */
#include "core.h"
#include "engines.c"
#include "params.c"
#include "mod.c"
#include "voice.c"
#include "slicer.c"
#define mix_block fx_mix_block
#define CF_FX_OWN 1                       /* fx.c: CoralFlower's chain (cf_fx.c) on the mix, not ChoralRoot's sends */
#define CF_LOOP_STORE 1                   /* storage.c: the looper's object (OBJ_CFLOOP, 0xB6000; cf_loop.c) */
#define ST_LOG_OBJ (OBJ_PROJECT0 + 3u)    /* storage.c: the settings' object keeps a log (cf_settings.c CF_SET_OBJ) */
#define CLN_TRIM 4096                     /* fx.c's Clean output: the mix in at 0 dB (ChoralRoot's -2.5 dB left it quieter
                                           * than the Classic chain it replaces here) */
static void cf_fx_chain(int32_t *l, int32_t *r, uint32_t n);
#define FX_MIX_HOOK cf_fx_chain
#include "fx.c"
#undef mix_block
#include "cf_gran.c"                      /* the granular layer on the master bus (run first by cf_fx_chain) */
#include "cf_fx.c"                        /* the effects chain (cr_fx.c's delay, reverb and chorus in it) */

/* ------------------------------------------------------------ MIDI --- */
#include "usb.c"
#if FELUCCA_UART
#include "midi_uart.c"
#endif

/* ------------------------------------------------- the instrument --- */
#include "cf_theory.c"
#include "cf_chord.c"
#include "cf_play.c"
#include "cf_out.c"
static void mix_block(int32_t *out, uint32_t n)
{
    CR_PROF_START();
    cf_audio_block(n);
    fx_mix_block(out, n);
    cf_click_mix(out, n);
    CR_PROF(CRP_MASTER);
}
#include "audio.c"

/* -------------------------------------------------------------- UI --- */
#include "panel.c"
#if FELUCCA_FLASH
#include "fm1_flash.h"
#include "storage_hw.c"
#include "storage.c"
#endif
#include "cr_bank.c"             /* upreset.c (32 user sounds, fm6_bank.c), the pools per engine */
#include "flora/flora_canvas.c"
#include "flora/flora.c"
#define CF_PUMP() cf_ui_pump()
static void cf_ui_pump(void);
#include "cf_fb.c"
#include "cf_sound.c"
#include "cf_state.h"
#include "cf_draw.c"
#include "cf_settings.c"           /* the settings in flash */
#include "cf_ui.c"
#include "cf_screens.c"
#include "cf_splash.c"
#include "fm6_store.c"
#include "cf_backup.c"                     /* backup / restore over SysEx (after usb.c, the stores, cf_ui.c, cf_settings.c) */
#include "cf_shim.c"

/* ----------------------------------------------------------- update --- */
#if FELUCCA_OTA
#include "ota.c"
#include "ota_hw.c"
#endif
#if FELUCCA_CDC
#include "console.c"
#endif
#include "main.c"
