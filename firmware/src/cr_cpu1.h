/* SPDX-License-Identifier: GPL-3.0-only */
/* ChoralRoot on the FM-1's second core (docs/DUALCORE.md), and the audio ISR's stage profile (cr_dbg).
 * The core's bring-up is hal/fm1_cpu1.{h,S} (after fm1-x0x (GPL-3.0) and Melodee (GPL-3.0), following JieLi's AC79
 * SDK EnableOtherCpu (Apache-2.0)); what runs there, the budgets, the meter and the fail-safes are ChoralRoot's:
 *
 *   power-on (main.c cr_c1_boot, before audio_init; not in SAFE MODE): core 1 started, 20 ms to answer, else held
 *     for the session: cr_dbg.c1 = C1_FAILED, single core, the shared voice budget;
 *   each audio block (fx.c mix_block, cr_c1_split): core 0 does everything that touches shared state first (the
 *     events, part 1's voice allocation and stealing, both parts' matrix and LFO), hands part 1's render to core 1
 *     (one job: track_render of trk[1] into its own buffer), renders part 0, waits, then mixes every part in the
 *     order of one core, so the samples are one core's bit for bit;
 *   a job not done in 8 ms (fm1_cpu1_wait): core 1 held for the session, core 0 renders part 1 itself,
 *     cr_dbg.c1 = C1_GAVE_UP, the shared budget again.
 * Reliability (docs/DUALCORE.md, after Melodee PR 21 "fix(dual-core): raise the core supply and survive CPU1
 * misreads, faults and stalls", Kerem Kilic / Melodee, GPL-3.0): with Dual Core On the core rails go up at power-on
 * (hal/fm1_power.h: SYSVDD 14 = 1.35 V, VDC14 4 = 1.45 V) before core 1 starts; core 1 runs only a request it read
 * right (hal/fm1_cpu1.h fm1_c1_poll; a misread is counted, never run); a fault about core 1 holds it and core 0 goes
 * on (fm1_c1_fault_c). The escalation (cr_c1_assess): a first misread is counted and core 1 keeps working; a second
 * misread (a later block that sees new ones), or any fault or stall: C1_GAVE_UP, cr_dbg.c1_fallback = 1. The rails
 * are never raised past 14 / 4 (the user's decision after Charles Vestal's review of the PR: device safety).
 * While core 1 is alive (C1_OK) each of parts 0 and 1 has its own budget of VBUDGET units (voice.c voices_busy).
 * CR_CPU1 0 (felucca.c, tests/hostsim.c, tests/regress.c): none of this, the code as before. The emulator builds it
 * with 1 and a stub core that runs the job at once, when handed (before part 0: the split must not depend on the
 * order of the two renders). */
#pragma once
#include <stdint.h>
#ifndef CR_CPU1
#define CR_CPU1 0
#endif
#ifndef CR_CORE1_TEST
#define CR_CORE1_TEST 0                  /* 1: SysEx 75 CORE1_TEST injects core 1 failures (tools/core1_fault_test.py,
                                          * cr_backup.c); a test build only, never a release */
#endif

enum { C1_OFF, C1_OK, C1_FAILED, C1_GAVE_UP };
/* the audio ISR's stages: the engine tick (cr_out.c: the engine, the looper, the events, the voices' allocation),
 * part 0's render, part 1's (core 1's job time when it renders it), the rest of the mix (the parts' dist, level,
 * sends, parts 2..3, the FX buses), the master (limiter, the click, the USB tap), core 0's wait for core 1 */
enum { CRP_TICK, CRP_PART0, CRP_PART1, CRP_FX, CRP_MASTER, CRP_WAIT, CRP_N };
/* inside the fx stage (cr_fx.c xf_block): the sends' down-sampler, the delay's loop, the reverb's, the wet's
 * up-sampler */
enum { CRX_DOWN, CRX_DELAY, CRX_REVERB, CRX_UP, CRX_N };
typedef struct {
    uint8_t c1;                      /* C1_*: off (CR_CPU1 0, SAFE MODE, Options > Dual Core Off), ok, failed at start, gave up on a job */
    uint8_t win_reset;               /* audio.c cpu_window took the window: the ISR starts a new one */
    uint8_t prof_on;                 /* the stage counters run (device: always; emulator: EMU_STAGES=1) */
    uint32_t c1_jobs;                /* jobs core 1 rendered */
    uint32_t c1_stack;               /* bytes of core 1's stack used at most (its "C1MK" fill, cr_c1_stack_used) */
    uint32_t c1_start_us;            /* how long core 1 took to answer at power-on */
    uint8_t c1_fallback;             /* 1: core 1 was given up this session (a second misread, a fault, a stall); RAM only */
    uint8_t c1_misreads;             /* audio blocks that found new misreads (cr_c1_assess): the first is forgiven */
    uint8_t rail_boot[2], rail_now[2];   /* SYSVDD, VDC14 levels: the boot loader's, now (hal/fm1_power.h; host 11, 3) */
    uint32_t mark;                   /* the last stage mark (prof units) */
    uint32_t cur[CRP_N];             /* this half buffer's stages (prof units, its blocks summed) */
    uint32_t win_sum[CRP_N], win_max[CRP_N], win_n;   /* the window being filled */
    uint32_t avg_us[CRP_N], max_us[CRP_N];             /* the last full second (console `cpu`, GEEK OUT) */
    uint64_t tot_sum[CRP_N];         /* since power-on (the emulator's summary, tools/emu/perf.sh) */
    uint32_t tot_max[CRP_N], tot_n;
    /* THE WORST CASES (0.18, docs/FX.md "What the unit says"): since the last 73 DEBUG read (cr_backup.c crb_debug
     * clears them: play, read, and the numbers are that stretch's) and, for the emulator, since power-on (x_tot_*) */
    uint32_t pk[CRP_N];              /* each stage's largest half (prof units) */
    uint32_t pk_all, pk_n;           /* the largest whole half (us: audio.c's measure, TIMER5 nested in it); halves */
    uint32_t last[CRP_N];            /* the half just rendered */
    uint32_t late_n, late_stage, late_us;   /* late halves; the latest one's largest stage (CRP_* + 1) and its us */
    uint32_t xmark, xcur[CRX_N];     /* the fx stage's parts: this half's */
    uint32_t x_pk[CRX_N], x_busy;    /* .. their largest half, and the halves in which any of them ran */
    uint64_t x_sum[CRX_N];           /* .. summed over those halves */
    uint64_t x_tot_sum[CRX_N];
    uint32_t x_tot_max[CRX_N], x_tot_busy;
    /* WHEN THE ISR GOT TO START (0.18's third build, docs/FX.md "Late is not slow"): a half is late when it ends
     * after its boundary, and that is its render plus what kept the ISR from starting. All in TIMER4 ticks */
    uint32_t lat_on, lat_tb, lat_min, lat_n;   /* the boundary estimate (cr_prof_entry) */
    uint32_t lat;                    /* this half's lag behind its boundary */
    uint32_t pk_lat, pk_wall, pk_t5; /* the largest lag, whole half (TIMER5 nested in it) and TIMER5 nested in one half */
    uint32_t stale;                  /* halves in which the DAC played something old: a boundary nobody served (the
                                      * ISR more than a half behind) or a half finished after the DAC had reached
                                      * its last block. THE AUDIBLE ONES; a half that is merely late is not */
    uint32_t t5_last, pk_t5gap, pk_t5pc;   /* TIMER5's largest gap between two entries (100 us when nothing masks
                                      * the interrupts) and where it came back to after it (main.c) */
} cr_dbg_t;
static cr_dbg_t cr_dbg;
/* Options > Dual Core Off (settings v12, the default; read by cr_settings_boot before cr_c1_boot): core 1 is never
 * started this power-on, cr_dbg.c1 stays C1_OFF ("c1 off"), the shared budget. A change in Options waits for the next
 * power-on. 1 until the record says On: a build without the settings (or a boot that never reads them) is one core */
static uint8_t cr_c1_off = 1;
static const char *const CRP_NAME[CRP_N] = {"tick", "part0", "part1", "fx", "master", "wait"};
static const char *const CRX_NAME[CRX_N] = {"down", "delay", "reverb", "up"};

#if CR_CPU1
/* ------------------------------------------------ the platform: the core and the clock --- */
#ifdef FM1_HAVE_CPU1                 /* the device (hal/fm1_cpu1.h, included before core.h by choralroot.c) */
#define CR_PROF_PER_US FM1_TICKS_PER_US   /* TIMER4 ticks (24 MHz) */
static inline uint32_t cr_prof_now(void) { return fm1_ticks(); }
static int cr_c1_run(void (*fn)(uint32_t), uint32_t arg) { return fm1_cpu1_run(fn, arg); }
static int cr_c1_wait_core(void) { return fm1_cpu1_wait(); }
static void cr_c1_hold(void) { fm1_cpu1_hold(); }
static inline uint32_t cr_c1_rejected(void) { return fm1_c1_rejected; }
static inline uint32_t cr_c1_faults(void) { return fm1_c1_fault.count; }
static void cr_rails_record(void)                   /* the boot loader's levels (P33 reads only) */
{
    fm1_power_record();
    cr_dbg.rail_boot[0] = fm1_power.boot[FM1_RAIL_SYSVDD];
    cr_dbg.rail_boot[1] = fm1_power.boot[FM1_RAIL_VDC14];
}
static void cr_rails_now(void)
{
    cr_dbg.rail_now[0] = fm1_power.now[FM1_RAIL_SYSVDD];
    cr_dbg.rail_now[1] = fm1_power.now[FM1_RAIL_VDC14];
}
static void cr_rails_init(void) { fm1_power_init(); cr_rails_now(); }       /* IRQs off: power-on */
static uint32_t cr_c1_stack_used(void)              /* the C1MK fill: the deepest word core 1's usp reached */
{
    uint32_t i = 0;
    while (i < FM1_C1_STACK_WORDS && _c1_ustack[i] == FM1_C1_MARK)
        i++;
    return (FM1_C1_STACK_WORDS - i) * 4u;
}
#else                                /* the host (the emulator, the tests): the job runs when handed */
#ifdef __APPLE__
#include <libproc.h>
#include <unistd.h>
#endif
#define CR_PROF_PER_US 259u          /* host instructions per device us (tools/emu/emu.c DEV_INSTR_PER_US) */
static uint8_t cr_c1_test_hang;      /* tests: the next jobs never answer (cr_c1_wait times out) */
static uint8_t cr_c1_test_nostart;   /* tests: core 1 does not answer at start */
static uint8_t cr_c1_test_late;      /* tests: the job runs at the wait (after part 0), not when handed (before it) */
static uint32_t cr_c1_test_rejected; /* tests: core 1's misread count (the device's fm1_c1_rejected) */
static uint32_t cr_c1_test_faults;   /* tests: faults about core 1 (the device's fm1_c1_fault.count) */
static void (*cr_c1_fn)(uint32_t);
static uint32_t cr_c1_arg;
static inline uint32_t cr_prof_now(void)
{
#ifdef __APPLE__
    struct rusage_info_v4 ri;
    if (!proc_pid_rusage(getpid(), RUSAGE_INFO_V4, (rusage_info_t *)&ri))
        return (uint32_t)ri.ri_instructions;
#endif
    return 0;
}
static int cr_c1_run(void (*fn)(uint32_t), uint32_t arg)
{
    cr_c1_fn = fn;
    cr_c1_arg = arg;
    if (!cr_c1_test_hang && !cr_c1_test_late)
        fn(arg);
    return 1;
}
static int cr_c1_wait_core(void)
{
    if (cr_c1_test_faults)
        return -2;
    if (cr_c1_test_hang)
        return -1;
    if (cr_c1_test_late)
        cr_c1_fn(cr_c1_arg);
    return 0;
}
static void cr_c1_hold(void) {}
static inline uint32_t cr_c1_rejected(void) { return cr_c1_test_rejected; }
static inline uint32_t cr_c1_faults(void) { return cr_c1_test_faults; }
static uint32_t cr_c1_stack_used(void) { return 0; }
/* the rails on the host: P33 reads the boot loader's defaults (SYSVDD 11, VDC14 3) */
static void cr_rails_record(void) { cr_dbg.rail_boot[0] = cr_dbg.rail_now[0] = 11; cr_dbg.rail_boot[1] = cr_dbg.rail_now[1] = 3; }
static void cr_rails_init(void)
{
    if (cr_dbg.rail_now[0] < 14u) cr_dbg.rail_now[0] = 14;
    if (cr_dbg.rail_now[1] < 4u) cr_dbg.rail_now[1] = 4;
}
#endif

/* a stage's time without the reading of the clock (the host's counter costs ~6.6 k instructions a reading: measured
 * once, cr_prof_calibrate; the device's timer read costs nothing worth it) */
static uint32_t cr_prof_cost;
static inline uint32_t cr_prof_less(uint32_t d) { return d > cr_prof_cost ? d - cr_prof_cost : 0u; }
static void cr_prof_calibrate(void)
{
    uint32_t i, a, b, best = 0xFFFFFFFFu;
    for (i = 0; i < 16u; i++) {
        a = cr_prof_now();
        b = cr_prof_now();
        if (b - a < best)
            best = b - a;
    }
    cr_prof_cost = best == 0xFFFFFFFFu ? 0u : best;
}
static inline int cr_dualcore_active(void) { return cr_dbg.c1 == C1_OK; }

/* ------------------------------------------------ the escalation (after Melodee PR 21, no boost) --- */
static uint32_t cr_c1_seen;          /* core 1's misread count at the last look */
/* core 0 (the audio ISR): what core 1's counters say. 0 = keep core 1, -1 = give it up. New misreads in a first block:
 * counted, core 1 kept (one block's burst is one event); new misreads in a later block, or any fault or stall: give up */
static int cr_c1_assess(uint32_t rejected, uint32_t faults, int stalled)
{
    if (faults || stalled)
        return -1;
    if (rejected == cr_c1_seen)
        return 0;
    cr_c1_seen = rejected;
    return ++cr_dbg.c1_misreads > 1u ? -1 : 0;
}
static void cr_c1_give_up(void)
{
    cr_c1_hold();
    cr_dbg.c1 = C1_GAVE_UP;
    cr_dbg.c1_fallback = 1;
}
/* each block before the hand-over (fx.c): 1 = core 1 may take it */
static int cr_c1_check(void)
{
    if (cr_dbg.c1 != C1_OK)
        return 0;
    if (cr_c1_assess(cr_c1_rejected(), cr_c1_faults(), 0) < 0) {
        cr_c1_give_up();
        return 0;
    }
    return 1;
}
/* the wait (fx.c): 0 = done; else core 1 is held for good (a stall or a fault), C1_GAVE_UP */
static int cr_c1_wait(void)
{
    int rc = cr_c1_wait_core();
    if (rc) {
        (void)cr_c1_assess(cr_c1_rejected(), cr_c1_faults(), rc == -1);
        cr_c1_give_up();
    }
    return rc;
}
/* this block: part 1 renders as core 1's job (its own scratch where an engine has shared scratch: fm6_core.c) */
static volatile uint8_t cr_c1_split;
#define CR_C1_IDX(t) (cr_c1_split && (t) == &trk[1])
#define CR_PROF_START() do { if (cr_dbg.prof_on) cr_dbg.mark = cr_prof_now(); } while (0)
#define CR_PROF_SKIP() do { if (cr_dbg.prof_on) cr_dbg.mark = cr_prof_now(); } while (0)   /* (not counted) */
#define CR_PROF(k) do { if (cr_dbg.prof_on) { uint32_t t_ = cr_prof_now(); cr_dbg.cur[k] += cr_prof_less(t_ - cr_dbg.mark); \
                                              cr_dbg.mark = t_; } } while (0)
/* .. and the fx stage's parts, on their own mark (inside CRP_FX's time: the stage's figure includes them and their
 * five timer reads a block, on the device ~0.1 us each) */
#define CR_PROFX_START() do { if (cr_dbg.prof_on) { cr_dbg.xmark = cr_prof_now(); cr_dbg.mark += cr_prof_cost; } } while (0)
#define CR_PROFX(k) do { if (cr_dbg.prof_on) { uint32_t t_ = cr_prof_now(); cr_dbg.xcur[k] += cr_prof_less(t_ - cr_dbg.xmark); \
                                               cr_dbg.xmark = t_; cr_dbg.mark += cr_prof_cost; } } while (0)   /* (the
                                                * host's mark costs 6.6 k instructions: not the outer stage's) */

/* power-on (main.c fm1_main, before audio_init and the timers; not in SAFE MODE): start core 1. It boots through the
 * chip's ROM, which the PC limits refuse, and its entry word is in the top of RAM: both open while it starts.
 * Not answering in 20 ms: held for the session (C1_FAILED), single core, the shared budget */
static void cr_c1_boot(void)
{
    int rc;
    cr_rails_record();                                  /* (the boot loader's levels, reported either way) */
    if (cr_c1_off) {                                    /* Options > Dual Core Off: one core this session, the boot
                                                         * loader's rails (the stock behaviour) */
        cr_dbg.c1 = C1_OFF;
        return;
    }
    cr_rails_init();                                    /* the core rails up before core 1 starts (IRQs off) */
#ifdef FM1_HAVE_CPU1
    {
    uint32_t t0 = fm1_ticks();
    fm1_guard_unlock_top();                             /* (fm1_main locks it after the IRQs are attached) */
    fm1_guard_pc_open();
    rc = fm1_cpu1_start();
    fm1_guard_enable(FM1_GUARD_PC);
    cr_dbg.c1_start_us = (fm1_ticks() - t0) / CR_PROF_PER_US;
    }
#else
    rc = cr_c1_test_nostart ? -1 : 0;
#endif
    cr_dbg.c1 = rc ? C1_FAILED : C1_OK;
}

/* end of a half buffer (audio.c): the stages into the window and the totals */
static void cr_prof_half(void)
{
    uint32_t k;
    if (!cr_dbg.prof_on)
        return;
    if (cr_dbg.win_reset) {
        for (k = 0; k < CRP_N; k++)
            cr_dbg.win_sum[k] = cr_dbg.win_max[k] = 0;
        cr_dbg.win_n = 0;
        cr_dbg.win_reset = 0;
    }
    for (k = 0; k < CRP_N; k++) {
        uint32_t c = cr_dbg.cur[k];
        cr_dbg.win_sum[k] += c;
        if (c > cr_dbg.win_max[k])
            cr_dbg.win_max[k] = c;
        cr_dbg.tot_sum[k] += c;
        if (c > cr_dbg.tot_max[k])
            cr_dbg.tot_max[k] = c;
        if (c > cr_dbg.pk[k])
            cr_dbg.pk[k] = c;
        cr_dbg.last[k] = c;
        cr_dbg.cur[k] = 0;
    }
    if (cr_dbg.xcur[CRX_DOWN] | cr_dbg.xcur[CRX_DELAY] | cr_dbg.xcur[CRX_REVERB] | cr_dbg.xcur[CRX_UP]) {
        for (k = 0; k < CRX_N; k++) {
            uint32_t c = cr_dbg.xcur[k];
            cr_dbg.x_sum[k] += c;
            cr_dbg.x_tot_sum[k] += c;
            if (c > cr_dbg.x_pk[k])
                cr_dbg.x_pk[k] = c;
            if (c > cr_dbg.x_tot_max[k])
                cr_dbg.x_tot_max[k] = c;
            cr_dbg.xcur[k] = 0;
        }
        cr_dbg.x_busy++;
        cr_dbg.x_tot_busy++;
    }
    cr_dbg.win_n++;
    cr_dbg.tot_n++;
}
/* the main loop, once a second (audio.c cpu_window): the window closed into avg_us / max_us */
/* audio.c, after a half: its whole time (us, as felucca_dbg.max_us measures it) and whether the DMA had moved on */
static void cr_prof_whole(uint32_t us, int late, uint32_t all, uint32_t t5, uint32_t per)
{
    uint32_t k, m = 0;
    if (us > cr_dbg.pk_all)
        cr_dbg.pk_all = us;
    if (all > cr_dbg.pk_wall)
        cr_dbg.pk_wall = all;
    if (t5 > cr_dbg.pk_t5)
        cr_dbg.pk_t5 = t5;
    /* the DAC needs block k of the four at the next boundary + k quarters of a half, and block k is written by
     * lat + (k + 1) quarters of `all`: both straight lines in k, so the first and the last block decide */
    if (cr_dbg.lat + all / 4u > per || cr_dbg.lat + all > per + per / 4u * 3u)
        cr_dbg.stale++;
    cr_dbg.pk_n++;
    if (late) {
        for (k = 1; k < CRP_N; k++)
            if (cr_dbg.last[k] > cr_dbg.last[m])
                m = k;
        cr_dbg.late_n++;
        cr_dbg.late_stage = m + 1u;
        cr_dbg.late_us = us;
    }
}
/* audio.c, first thing in a half: t0 its entry, per a half in ticks -> its lag behind its boundary. Nobody tells
 * the ISR when the boundary was, so it is estimated: one every `per` from the earliest entry seen (an entry is never
 * early), moved up by the smallest lag of every 64 halves, 43 us at most (the two clocks drift, `per` is rounded). A lag of a
 * whole half or more: that many boundaries went unserved (one pending bit), and the DAC replays what it played two
 * halves before: stale */
static uint32_t cr_prof_entry(uint32_t t0, uint32_t per)
{
    uint32_t lag;
    if (!cr_dbg.lat_on) {
        cr_dbg.lat_on = 1;
        cr_dbg.lat_tb = t0;
        cr_dbg.lat_min = 0xFFFFFFFFu;
        cr_dbg.lat_n = 0;
    } else {
        cr_dbg.lat_tb += per;
    }
    lag = t0 - cr_dbg.lat_tb;
    if ((int32_t)lag < 0) {                              /* (before the estimate: the estimate was late) */
        cr_dbg.lat_tb = t0;
        lag = 0;
    }
    if (lag >= per) {
        uint32_t k = lag / per;
        cr_dbg.stale += k;
        cr_dbg.lat_tb += k * per;
        lag -= k * per;
    }
    if (lag < cr_dbg.lat_min)
        cr_dbg.lat_min = lag;
    if (++cr_dbg.lat_n >= 64u) {
        uint32_t d = cr_dbg.lat_min > 1024u ? 1024u : cr_dbg.lat_min;   /* (at most 43 us a window: the drift is a
                                                          * few; 64 halves that were ALL behind are an overload, and
                                                          * its lag is not taken for drift) */
        cr_dbg.lat_tb += d;
        lag -= d;
        cr_dbg.lat_min = 0xFFFFFFFFu;
        cr_dbg.lat_n = 0;
    }
    if (lag > cr_dbg.pk_lat)
        cr_dbg.pk_lat = lag;
    cr_dbg.lat = lag;
#ifdef FM1_IRQ_TARGET
    return lag;
#else
    return 0u;                                           /* (the emulator's halves come when the host's sound card asks
                                                          * for them, in bursts: no boundary to be behind, and
                                                          * audio.c's shed must not take the bursts for an overload) */
#endif
}
/* main.c, TIMER5's entry (10 kHz, above everything but a fault): the gap since the last one. 100 us, unless the
 * interrupts were masked: then the gap is the masked stretch (to the tick: +-100 us) and pc where it ended (the
 * instruction after the sti: build/choralroot.dis names the section) */
static inline void cr_prof_t5(uint32_t t0, uint32_t pc)
{
    uint32_t gap = t0 - cr_dbg.t5_last;
    if (cr_dbg.t5_last && gap > cr_dbg.pk_t5gap) {
        cr_dbg.pk_t5gap = gap;
        cr_dbg.pk_t5pc = pc;
    }
    cr_dbg.t5_last = t0 | 1u;                            /* (never 0: "no entry yet") */
}
/* 73 DEBUG has read the worst cases: the next read's are the stretch from here (the main loop; the ISR may be half
 * way through a half: that half then counts partly, once) */
static void cr_prof_clear(void)
{
    uint32_t k;
    for (k = 0; k < CRP_N; k++)
        cr_dbg.pk[k] = 0;
    for (k = 0; k < CRX_N; k++) {
        cr_dbg.x_pk[k] = 0;
        cr_dbg.x_sum[k] = 0;
    }
    cr_dbg.pk_all = cr_dbg.pk_n = cr_dbg.late_n = cr_dbg.late_stage = cr_dbg.late_us = cr_dbg.x_busy = 0;
    cr_dbg.pk_lat = cr_dbg.pk_wall = cr_dbg.pk_t5 = cr_dbg.stale = cr_dbg.pk_t5gap = cr_dbg.pk_t5pc = 0;
}
static void cr_prof_window(void)
{
    uint32_t k;
    if (cr_dbg.win_reset || !cr_dbg.win_n)
        return;
    for (k = 0; k < CRP_N; k++) {
        cr_dbg.avg_us[k] = cr_dbg.win_sum[k] / cr_dbg.win_n / CR_PROF_PER_US;
        cr_dbg.max_us[k] = cr_dbg.win_max[k] / CR_PROF_PER_US;
    }
    cr_dbg.win_reset = 1;
}
#else
#define cr_dualcore_active() 0
#define CR_C1_IDX(t) 0
#define CR_PROF_START() ((void)0)
#define CR_PROF(k) ((void)0)
#define CR_PROF_SKIP() ((void)0)
#define CR_PROFX_START() ((void)0)
#define CR_PROFX(k) ((void)0)
#define cr_prof_half() ((void)0)
#define cr_prof_whole(us, late, all, t5, per) ((void)0)
#define cr_prof_entry(t0, per) 0u
#define cr_prof_t5(t0, pc) ((void)0)
#define cr_prof_window() ((void)0)
#define cr_c1_boot() ((void)0)
#define cr_c1_stack_used() 0u
#define cr_c1_rejected() 0u
#define cr_c1_faults() 0u
#endif

/* GEEK OUT's / the console's word for core 1 */
static const char *cr_c1_name(void)
{
    static const char *const N[4] = {"off", "ok", "failed", "gave up"};
    return N[cr_dbg.c1 & 3u];
}
