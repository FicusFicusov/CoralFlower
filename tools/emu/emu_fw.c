/* SPDX-License-Identifier: GPL-3.0-only */
/* The firmware side of the emulator: the firmware sources (emu_firmware.h) and the hooks of emu_hooks.h.
 * Everything Felucca-specific the emulator relies on is in this file's hook bodies. */
#include <os/lock.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "emu_hooks.h"

emu_hal_t emu_hal;
/* hal/fm1_input.h FM1_KEYMAP: key id at (physical column, packed row bit), -1 = none */
const int8_t emu_keymap[6][EMU_NCOL] = {
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},          /* PA0: encoders */
    { 5, 11,  4, 10,  3,  9,  2,  8, -1, -1, -1},          /* PA5 */
    {34, 35, 36, 37, 38, 40, 39, 13,  7,  6, 12},          /* PA6 */
    {23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33},          /* PA7 */
    { 0,  1, 15, 14, 17, 16, 19, 18, 20, 21, 22},          /* PA8 */
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},          /* PB7: encoder 6 */
};

#include "emu_firmware.h"

/* ------------------------------------------------------------- flash --- */
/* emu_firmware.h builds storage.c (FELUCCA_FLASH 1) on emu_hal_fw.h's file-backed NOR; emu.c
 * parses --flash / --no-flash / --save-on-exit and hands them over here before the power-on (emu_fw_options). */
static uint8_t emu_save_on_exit;
void emu_fw_options(const char *flash_path, int no_flash, int save_on_exit, int headless)
{
    emu_save_on_exit = (uint8_t)(save_on_exit != 0);
    if (no_flash || (headless && !flash_path))
        emu_flash_path[0] = 0;                    /* RAM only: a fresh flash each run (deterministic scripts) */
    else
        snprintf(emu_flash_path, sizeof emu_flash_path, "%s", flash_path ? flash_path : "build/emu/flash.bin");
}
/* the boot guard (cr_bootguard.h, main.c fm1_cstart): what a crashed run left in .noinit, and this reset's reason */
static uint32_t emu_reset_reason = BG_P3_POWERON;
int emu_fw_boot_options(int fail, const char *reason, int stage)
{
    if (reason) {
        char *e;
        if (!strcmp(reason, "poweron") || !strcmp(reason, "power-on")) emu_reset_reason = BG_P3_POWERON;
        else if (!strcmp(reason, "wdt")) emu_reset_reason = BG_P3_WDT;
        else if (!strcmp(reason, "soft")) emu_reset_reason = BG_P3_SOFT;
        else if (!strcmp(reason, "other") || !strcmp(reason, "pin")) emu_reset_reason = 0x10u;   /* long press */
        else {
            emu_reset_reason = (uint32_t)strtoul(reason, &e, 0);
            if (*e)
                return 0;
        }
    }
    if (fail >= 0) {
        bootguard_clear(&bootguard);
        bootguard.failed = (uint32_t)fail;
        bootguard.pending = 1;                    /* the last run died within 30 s */
        felucca_dbg.magic = DBG_MAGIC;
        felucca_dbg.stage = (uint32_t)(stage >= 0 ? stage : BS_USER_SOUNDS);
    } else if (stage >= 0) {
        felucca_dbg.magic = DBG_MAGIC;
        felucca_dbg.stage = (uint32_t)stage;
    }
    return 1;
}
static void emu_boot_guard(void)                  /* main.c fm1_cstart + dbg_boot */
{
    uint32_t mode = bootguard_step(&bootguard, emu_reset_reason);
    if (mode == BOOT_UBOOT) {
        printf("boot: UBOOT (ROM boot): reset %s, the guard cleared\n", bootguard_class_name(bootguard.cls));
        fflush(stdout);
        exit(3);                                  /* (the device: fm1_enter_uboot, the PC tool's mode) */
    }
    cr_safe = mode == BOOT_SAFE;
    if (felucca_dbg.magic != DBG_MAGIC) {
        memset(&felucca_dbg, 0, sizeof felucca_dbg);
        felucca_dbg.magic = DBG_MAGIC;
    }
    felucca_dbg.boots++;
    felucca_dbg.prev_stage = felucca_dbg.stage;
    felucca_dbg.prev_rst = emu_reset_reason;
    CR_STAGE(BS_GUARD);
    printf("boot: %s reset %s failed %u pending %u counted %u prev_stage %u (%s)\n", bootguard_mode_name(mode),
           bootguard_class_name(bootguard.cls), (unsigned)bootguard.failed, (unsigned)bootguard.pending,
           (unsigned)bootguard.counted, (unsigned)felucca_dbg.prev_stage, bootguard_stage_name(felucca_dbg.prev_stage));
}

static void emu_fw_exit(void) {}

#define EMU_RELEASE_MS 9u            /* hal/fm1_input.h: a release needs ~9 ms open (FM1_DEB_RELEASE) */
static uint32_t emu_pa2_ms;          /* ms PA2 was high since power-on (emu_hooks.h pa2: the matrix dead; the dump) */
static uint32_t in_btn, in_key;      /* the debounced state the tick delivers */
static uint32_t btn_ms[32], key_ms[32];/* ms each went down */
/* the MASTER pot as the unit reads it (hal/fm1_adc.h: 0..1023), and main.c's own two functions on it
 * (firmware/src/master.c): the pot's level before the first block (master_boot), then smoothed a frame (master_poll) */
#define FM1_ADC_MASTER 0u
static int32_t fm1_adc_read(uint32_t ch)
{
    int32_t a = emu_hal.master;
    (void)ch;
    return a < 0 ? 0 : a > 1023 ? 1023 : a;
}
#include "../../firmware/src/master.c"

void emu_fw_init(int demo)
{
    struct timespec ts;
    uint32_t i;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    emu_t0_ns = (uint64_t)ts.tv_sec * 1000000000u + (uint64_t)ts.tv_nsec;
    if (emu_hal.ready == 2u)                      /* --bench: simulated time (set before init) */
        emu_sim = 1;
    /* power-on (coralflower.c's main.c path): the flash (persist_boot), the panel, the splash, the sounds */
    emu_boot_guard();
    panel_init();                                 /* main.c: the printed labels -> matrix ids (panel.c) */
    emu_flash_open();
    CR_STAGE(BS_FLASH);
    cr_bank_boot();                               /* persist_boot: the user sounds (upreset.c) from the flash */
    {   /* the MASTER pot at power-on: EMU_MASTER=ADC (0..1023), else 724 */
        const char *m = getenv("EMU_MASTER");
        if (m)
            emu_hal.master = atoi(m);
        else if (!emu_hal.master)
            emu_hal.master = 724;
        master_boot();
    }
    fm6_init();
    for (i = 0; i < NTRK; i++) {                  /* main.c felucca_init: the parts' defaults */
        track_defaults(&trk[i]);
        set_engine_of(&trk[i], ENGI_FM6);
        trk[i].engine = trk[i].eng_req;
    }
    song.master_q12 = 2048;
    cf_splash();                                  /* main.c's: the first picture (the UI's first frames grow it) */
    cf_ui_init();
    atexit(emu_fw_exit);
    usb.up = 1;                                   /* a host is there: MIDI out flows (usb.c midi_out_event) */
    usb.config = 1;
    (void)demo;
    for (i = 0; i < EMU_NB; i++)
        emu_hal.btn_id[i] = panel.btn[i];
    for (i = 0; i < EMU_NE - 1u; i++) {
        emu_hal.enc_id[i] = panel.enc[i];
        emu_hal.enc_dir[i] = panel.dir[i];
    }
    emu_hal.led_play_green = (8u << 3) | 1u;
    emu_hal.ready = 1;
}

/* the 1 ms timer: what fm1_timer5_irq / fm1_input_tick give the main loop */
void emu_fw_tick(uint32_t ms)
{
    uint32_t b, k, i, down, dead;
    fm1_irq_off();
    dead = emu_hal.pa2;
    if (emu_sim)
        emu_sim_us = ms * 1000u;
    if ((int32_t)(ms - fm1_ms) > 0)
        fm1_ms = ms;
    b = emu_hal.buttons | __atomic_exchange_n(&emu_hal.buttons_tap, 0u, __ATOMIC_SEQ_CST);
    k = emu_hal.keys | __atomic_exchange_n(&emu_hal.keys_tap, 0u, __ATOMIC_SEQ_CST);
    if (dead) {                                   /* PA2 high (emu_hooks.h): the 595 chain is dead, every row reads */
        b = k = 0;                                /* open: no key, no button (a held one is let go), no knob step */
        emu_pa2_ms++;
    }
    down = b & ~in_btn;
    for (i = 0; i < EMU_NBTN; i++) {
        uint32_t m = 1u << i;
        if (down & m)
            btn_ms[i] = ms;
        else if ((in_btn & m) && !(b & m) && ms - btn_ms[i] < EMU_RELEASE_MS)
            b |= m;                               /* (held at least the debounce time) */
    }
    in_btn = b;
    if (down)
        __atomic_fetch_or(&host_pressed, down, __ATOMIC_SEQ_CST);
    down = k & ~in_key;
    for (i = 0; i < EMU_NKEY; i++) {
        uint32_t m = 1u << i;
        if (down & m) {
            key_ms[i] = ms;
            if (i < 27u)
                emu_key_t0[i] = fm1_ticks();      /* (hal/fm1_input.h note_t0: the scan that saw it closed) */
        } else if ((in_key & m) && !(k & m) && ms - key_ms[i] < EMU_RELEASE_MS)
            k |= m;
    }
    in_key = k;
    if (down)
        __atomic_fetch_or(&host_notes, down, __ATOMIC_SEQ_CST);
    fm1_in.buttons = in_btn;
    fm1_in.notes = in_key;
    for (i = 0; i < EMU_NENC; i++) {
        int32_t s = __atomic_exchange_n(&emu_hal.enc[i], 0, __ATOMIC_SEQ_CST);
        if (s && !dead)
            __atomic_fetch_add(&host_enc[i], s, __ATOMIC_SEQ_CST);
    }
    fm1_irq_on();
}

static uint32_t real_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(((uint64_t)ts.tv_sec * 1000000000u + (uint64_t)ts.tv_nsec) / 1000u);
}

void emu_fw_frame(void)                           /* main.c's loop body (coralflower.c: the same calls) */
{
    uint32_t t0, d;
    fm1_irq_off();
    t0 = real_us();
    master_poll();
    cf_ui_input();
    cf_ui_frame();
    d = real_us() - t0;
    fm1_irq_on();
    if (d > emu_hal.ui_lock_max_us)
        emu_hal.ui_lock_max_us = d;
    cf_ui_draw();
    cr_update_poll();                             /* main.c: OCT- + OCT+ + HOME held 5 s */
    settings_poll();                              /* main.c: the settings record, once quiet (cf_settings.c) */
    if (fm1_ms > BG_SETTLE_MS && bootguard.pending)
        bootguard_settled(&bootguard);
    felucca_dbg.stage = BS_LOOP_IDLE;
}

void emu_fw_idle(void)                            /* main.c: the input scan while it waits for the next frame */
{
    fm1_irq_off();
    cf_ui_pump();
    fm1_irq_on();
}

static uint32_t emu_model_over85, emu_model_over100, emu_model_max;   /* CF_LOADMODEL, since the last dump */
void emu_fw_audio(int16_t *out, uint32_t frames)  /* the ALNK0 ISR, one half buffer per EMU_BLOCK */
{
    uint32_t f, i;
    for (f = 0; f + HALF_FRAMES <= frames; f += HALF_FRAMES) {
        const int32_t *h;
        if (!emu_cpu_try()) {   /* the UI (or the timer) is inside a critical section */
            uint32_t t0 = real_us(), d;
            fm1_irq_off();
            d = real_us() - t0;
            if (d > emu_hal.audio_wait_max_us)
                emu_hal.audio_wait_max_us = d;
        }
        if (emu_stall_blocks) {                   /* a flash erase on the device: IRQs off, the buffer zeroed */
            emu_stall_blocks--;
            emu_stall_blocks_all++;
            fm1_irq_on();
            for (i = 0; i < HALF_WORDS; i++)
                out[2u * f + i] = 0;
            continue;
        }
        emu_half ^= 1u;
        if (emu_model_instr) {                    /* (CF_LOADMODEL: the half's clock, emu_hal_fw.h) */
            emu_model_t0 += HALF_FRAMES * 1000000u / 44100u;
            emu_model_i0 = emu_model_instr();
            emu_model_n = 0;
            emu_model_calls++;
        }
        emu_in_audio = 1;
        fm1_alnk0_irq();
        emu_in_audio = 0;
        if (emu_model_instr) {                    /* (the halves the unit would have rendered too slowly: dump) */
            uint32_t us = felucca_dbg.last_us;
            emu_model_over85 += us * 100u > (HALF_FRAMES * 1000000u / 44100u) * 85u;
            emu_model_over100 += us > HALF_FRAMES * 1000000u / 44100u;
            emu_model_max = us > emu_model_max ? us : emu_model_max;
        }
        h = &abuf[emu_half * HALF_WORDS];
        fm1_irq_on();
        for (i = 0; i < HALF_WORDS; i++) {        /* 24-bit (Q15 << OUT_SHIFT) -> Q15 = 0 dBFS */
            int32_t v = h[i] >> OUT_SHIFT;
            out[2u * f + i] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
        }
    }
}

int emu_fw_midi_in(uint32_t pkt)
{
    int ok = 0;
    fm1_irq_off();
    if (MQ - (mi_w - mi_r) >= 8u) {               /* (usb.c ep1_take: room kept for TRS MIDI) */
        midi_in_event(pkt);
        ok = 1;
    }
    fm1_irq_on();
    return ok;
}

int emu_fw_midi_out_take(uint32_t *pkt)
{
    int ok = 0;
    fm1_irq_off();
    if (mo_r != mo_w) {                           /* usb.c ep1_tx */
        *pkt = midi_out_q[mo_r % MQ];
        mo_r++;
        ok = 1;
    }
    fm1_irq_on();
    return ok;
}

void emu_fw_dump(void)
{
    uint32_t c, i;
    char nm[16];
    fm1_irq_off();
    printf("fm1_in: notes %07X buttons %04X  fm1_ms %u\n", (unsigned)fm1_in.notes, (unsigned)fm1_in.buttons,
           (unsigned)fm1_ms);
    printf("  leds:");
    for (c = 0; c < FM1_NCOL; c++)
        printf(" %02X/%02X/%02X/%02X", fm1_led[c], fm1_led_dim2[c], fm1_led_dim[c], fm1_led_faint[c]);
    printf("  (lit/mid/dim/faint)\n");
    printf("  key lights:");                     /* (each key's LED, keys 0..26: L lit, M mid, D dim, F faint, . off) */
    for (i = 0; i < 27u; i++) {
        uint8_t q = cu_led_pos[14u + i], col = q >> 3, bit = (uint8_t)(1u << (q & 7u));   /* (ids 0..13 the buttons) */
        printf("%c", q == 0xFF ? '?' : fm1_led[col] & bit ? 'L' : fm1_led_dim2[col] & bit ? 'M' :
               fm1_led_dim[col] & bit ? 'D' : fm1_led_faint[col] & bit ? 'F' : '.');
    }
    printf("\n");
    printf("  screen %s  algo %u quantize %u key %u scale %u octave %d glide %u\n", SCR_NAME[cu.scr],
           (unsigned)cs.chord.algo, (unsigned)cs.chord.quantize, (unsigned)cs.chord.tonic, (unsigned)cs.chord.scale,
           (int)cs.chord.octave,
           (unsigned)(trk[0].p[P_GLIDE] > 0 ? cfe_glide_ms(trk[0].p[P_GLIDE]) : 0u));   /* (GLIDE: the chord sound's, 0.5) */
    cf_chord_name(cf_sn.out.root_pc, cf_sn.out.mask, cf_sn.out.bass_pc, &cu_spell, nm);
    printf("  chord: %s sounding %u single %u buttons %03X notes", nm, (unsigned)cf_sn.out.sounding,
           (unsigned)cf_sn.out.single, (unsigned)cf_sn.out.buttons);
    for (i = 0; i < cf_sn.n; i++)
        printf(" %u", (unsigned)cf_sn.note[i]);
    printf("  bass");
    for (i = 0; i < cf_sn.nb; i++)
        printf(" %u", (unsigned)cf_sn.bass[i]);
    printf("\n  sounds: chord %s (%s) bass %s (%s)\n", cf_snd[0].name, cf_engine_name(cf_snd[0].engine), cf_snd[1].name,
           cf_engine_name(cf_snd[1].engine));
    for (i = 0; i < 2u; i++) {                   /* the two lists of sounds (cf_sound.c), the part's place in it */
        uint32_t n = cf_snd_count(i), k, e, pos;
        printf("  %s list (%u, at %u):", i ? "bass" : "chord", (unsigned)n, (unsigned)cf_snd_pos(i));
        for (k = 0; k < n; k++) {
            cf_snd_at(i, k, &e, &pos);
            pool_name(e, pos, nm);
            printf("%s %s %s", k ? "," : "", cf_engine_name(e), nm);
        }
        printf("\n");
    }
    for (i = 0; i < 3u; i++) {                   /* (the chords, the bass, CoralFlower's LAYER) */
        uint32_t n = 0, k;
        for (k = 0; k < NVOICE; k++)
            n += trk[i].v[k].active;
        printf("  part %u: engine %u voices %u level %d\n", (unsigned)i, (unsigned)trk[i].engine, (unsigned)n,
               (int)trk[i].p[P_LEVEL]);
        printf("  part %u gated:", (unsigned)i);       /* (the held voices' notes: a stuck one shows here) */
        for (k = 0; k < NVOICE; k++)
            if (trk[i].v[k].active && trk[i].v[k].gate)
                printf(" %u", (unsigned)trk[i].v[k].note);
        printf("\n");
        printf("  part %u voice %d glide %d pitch:", (unsigned)i, trk[i].p[P_VOICE], trk[i].p[P_GLIDE]);
        /* (VOICE, GLIDE; the sounding voices' pitch now, 1/16 semitone: a glide shows) */
        for (k = 0; k < NVOICE; k++)
            if (trk[i].v[k].active)
                printf(" %d", (int)trk[i].v[k].pitch_cur);
        printf("\n");
    }
#if CF_NTAIL
    {                                            /* the tail (cf_sound.c): whose sound rings out there, its voices */
        uint32_t n = 0, k, e;
        int32_t pk = trk[NTRK].peak;
        for (k = 0; k < NVOICE; k++)
            n += trk[NTRK].v[k].active;
        e = trk[NTRK].engine;
        printf("  tail: of part %u, engine %u voices %u peak %d notes:", (unsigned)cf_tail_part, (unsigned)e, (unsigned)n,
               (int)pk);
        for (k = 0; k < NVOICE; k++)
            if (trk[NTRK].v[k].active)
                printf(" %u(st%u env%d)", (unsigned)trk[NTRK].v[k].note, (unsigned)trk[NTRK].v[k].stage,
                       (int)trk[NTRK].v[k].env_out);
        printf("\n");
    }
#endif
    if (emu_model_instr) {                       /* (CF_LOADMODEL: the unit's load as the firmware measured it) */
        printf("  load: max %u us of %u, halves over 85 %% %u, over 100 %% %u, guard sheds %u, kills %u, steals %u;"
               " voice cost us", (unsigned)emu_model_max, (unsigned)(HALF_FRAMES * 1000000u / 44100u),
               (unsigned)emu_model_over85, (unsigned)emu_model_over100, (unsigned)shed_count, (unsigned)voice_kills,
               (unsigned)voice_steals);
#if CF_VLOAD
        for (i = 0; i < NSND; i++)
            printf(" %u", (unsigned)vl_cost_of(&trk[i]));
        printf("; caps");
        for (i = 0; i < NPART; i++)
            printf(" %u", (unsigned)trk_nvoice(&trk[i]));
#endif
        printf("\n");
        emu_model_over85 = emu_model_over100 = emu_model_max = 0;
    }
    printf("  plant: %s, segments %u, tiles sent %u\n", flo.species ? "coral" : "plant", (unsigned)flo.nseg,
           (unsigned)fb_tiles_sent);
    printf("  chord: root %u, notes", (unsigned)cf_sn.out.root_note);
    for (i = 0; i < cf_sn.n; i++)
        printf(" %u", (unsigned)cf_sn.note[i]);
    printf("; bass");
    for (i = 0; i < cf_sn.nb; i++)
        printf(" %u", (unsigned)cf_sn.bass[i]);
    printf("\n");
    printf("  settings: %s, key %u, colour %u, saved %08X now %08X\n", cfs_loaded ? "loaded" : "defaults",
           (unsigned)cs.chord.tonic, (unsigned)cs.accent, (unsigned)cfs_saved, (unsigned)cfs_fnv(&cs, sizeof cs));
    {                                             /* what a power-on brings back (cf_settings.c, cf_sequi.c sq_resume) */
        uint32_t k;
        printf("  kept: resume %02X (play: %s %u), edits", (unsigned)cs.resume, sqk.song ? "song from block" : "pattern",
               (unsigned)(sqk.song ? squ.slot : sqk.last));
        for (k = 0; k < 3u; k++) {
            cfk_build(k, &cfk_rec);
            printf(" %s %s %c %08X", k == 0 ? "chord" : k == 1 ? "bass" : "layer", cfk_rec.edited ? "edited" : "-",
                   cfk_own[k] ? (char)('A' + cfk_own[k] - 1) : 'H', (unsigned)cfk_rec.check);
        }
        printf(" (owner H: HOME), chord attack %d", cf_env_get(0, 0));
        printf("\n  pattern sounds (current %c):", sq_cur_pat() < SQ_NPAT ? (char)('A' + sq_cur_pat()) : '-');
        for (k = 0; k < SQ_NPAT; k++) {
            uint32_t t;
            for (t = 0; t < 3u; t++)
                if (cfps.r[k][t].own)
                    printf(" %c.%s=%s", (char)('A' + k), t == 0 ? "chords" : t == 1 ? "bass" : "layer", sq_snd_name(k, t));
        }
        {                                         /* the open pattern's frame and the framed track's chords */
            const sq_pat_t *p = sq_cur();
            uint32_t e;
            printf("\n  sq: pat %u trk %u bar %u step %u nofr %u mv %u edit %u playing %u, track:", (unsigned)squ.pat,
                   (unsigned)squ.trk, (unsigned)squ.bar, (unsigned)squ.step, (unsigned)squ.nofr, (unsigned)squ.mv,
                   squ.edit_down ? 1u : 0u, (unsigned)sqp.on);
            for (e = 0; squ.pat < SQ_NPAT && e < p->nev; e++)
                if (p->ev[e].trk == squ.trk)
                    printf(" %u.%u(%u)", (unsigned)p->ev[e].bar, (unsigned)p->ev[e].pos, (unsigned)p->ev[e].root);
        }
        {                                         /* (rc2 cost study: part 0's VA patch, what a voice renders) */
            const int8_t *vp = va_patch[0];
            uint32_t k;
            printf("\n  va0: lvl");
            for (k = 0; k < 4u; k++)
                printf(" %d", vp[VA_OSC(k, VO_LEVEL)]);
            printf(" wave");
            for (k = 0; k < 4u; k++)
                printf(" %d", vp[VA_OSC(k, VO_WAVE)]);
            printf(" mode");
            for (k = 0; k < 4u; k++)
                printf(" %d", vp[VA_OMODE0 + k]);
            printf(" sync %d ring %d ftype %d spread %d uspread %d", vp[VA_SYNC2], vp[VA_RING4], vp[VA_FTYPE],
                   vp[VA_FSPREAD], vp[VA_USPREAD]);
        }
        printf("%s\n  tweak stores:", cfps_check() == cfps_saved ? "" : " (not written)");
        for (k = 0; k < CFK_NST; k++)
            if (cfk_st[k].edited)
                printf(" %u:%s%c.%u=%s", (unsigned)k, cfk_st[k].pat1 ? "" : "HOME", cfk_st[k].pat1 ? (char)('A' + cfk_st[k].pat1 - 1) : ' ',
                       (unsigned)cfk_st[k].part, cfk_st[k].name);
        printf("\n");
    }
    {
        cf_keycap_t kc;
        printf("  fb probe (5,190) %u (5,175) %u  lcd %04X\n", (unsigned)fb[190 * 240 + 5], (unsigned)fb[175 * 240 + 5],
               (unsigned)emu_hal.lcd[190 * 240 + 5]);
        printf("  ui keys %07X caps:", (unsigned)cu.keys);
        for (i = 0; i < CF_NKEYS; i++) {
            kc.top[0] = kc.bot[0] = 0;
            kc.style = 0;
            ui_keycap((int)i, &kc);
            printf(" %s/%u", kc.top, (unsigned)kc.style);
        }
        printf("\n");
    }
    fm1_irq_on();
    fflush(stdout);
}

void emu_fw_ui_info(char *buf, uint32_t n)
{
    snprintf(buf, n, "%s pop %u", SCR_NAME[cu.scr], (unsigned)cu.pop);
}
uint32_t emu_fw_keys(uint32_t v[5])
{
    v[0] = v[1] = v[2] = v[3] = v[4] = 0;
    return 0;
}
void emu_fw_stats(uint32_t *shed, uint32_t *cpu_pct)
{
    *shed = shed_count;
    *cpu_pct = song.cpu_q8 * 100u / 256u;
}
void emu_fw_stages(char *buf, uint32_t n)
{
    if (n)
        buf[0] = 0;
}
