/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* The firmware side of storage.c (FELUCCA_FLASH). CoralFlower suspends writes
 * between bounded IRQ-off windows; legacy users retain their original path. */
#include "cf_diag.h"
static uint32_t st_erases;
static uint8_t flash_ok;                 /* JEDEC id matched at boot (persist_boot) */
#ifdef CF_FLASH_COOP
#ifndef ST_FLASH_PUMP
#define ST_FLASH_PUMP() ((void)0)
#endif
static uint8_t st_runtime;               /* audio/TIMER5 started, set by main.c */
static struct {
    uint32_t programs, slices, suspends, max_off_us, over_budget, errors, recoveries, jedec;
    int fault;
} st_flash;
static void st_flash_snapshot(uint32_t *v)
{
    v[0]=st_flash.jedec; v[1]=st_runtime; v[2]=st_flash.programs;
    v[3]=st_flash.slices; v[4]=st_flash.suspends; v[5]=st_flash.max_off_us;
    v[6]=st_flash.over_budget; v[7]=st_flash.errors; v[8]=st_flash.recoveries;
    v[9]=(uint32_t)st_flash.fault;
}

/* One slice per rendered half bounds average IRQ-off load, not just a single
 * stall. Only note input is serviced here, never the main UI/save/backup polls.
 * Its call graph must not read/write storage. XIP is restored and NOR readable. */
static void st_flash_yield(void)
{
    uint32_t t = fm1_ticks(), h = audio_halves;
    if (!st_runtime) return;
    while (audio_halves == h && (uint32_t)(fm1_ticks() - t) < 5000u * FM1_TICKS_PER_US) ;
    ST_FLASH_PUMP();
    fm1_wdt_feed();
}
static int st_flash_step(fls_job_t *j)
{
    uint32_t f, t, us;
    int rc;
    f = irq_save(); t = fm1_ticks();
    rc = FL_FAR(fl_slice_ram)(j);
    us = (uint32_t)(fm1_ticks() - t) / FM1_TICKS_PER_US;
#if CF_EVENT_DIAG
    if (CFD_ACTIVE) {
        cf_diag.flash_at = t - cf_diag.t0;
        cf_diag.flash_ticks = fm1_ticks() - t;
        cf_diag.flash_seq++;
    }
#endif
    irq_restore(f);
    if (us > 450u) CFD_EVENT(CFD_MAIN, CFD_FLASH_SLOW, j->n, us, j->off);
    st_flash.slices++;
    if (us > st_flash.max_off_us) st_flash.max_off_us = us;
    if (us > 450u) st_flash.over_budget++;  /* hardware acceptance, not a substituted success metric */
    if (rc == 1) st_flash.suspends++;
    if (rc < 0) { st_flash.errors++; st_flash.fault = rc; }
    if (rc != 1) CFD_EVENT(CFD_MAIN, CFD_FLASH_END, (uint32_t)rc & 65535u, j->off, st_flash.slices);
    return rc;
}
static int st_flash_run(uint32_t off, const uint8_t *src, uint32_t n)
{
    fls_job_t j = {0};
    int rc;
    if (st_flash.fault) return st_flash.fault;
    j.off = off; j.src = src; j.n = n;
    CFD_EVENT(CFD_MAIN, CFD_FLASH_BEGIN, n, off, st_flash.slices);
    do { st_flash_yield(); rc = st_flash_step(&j); } while (rc == 1);
    return rc;
}
#ifdef ST_ASYNC
#define ST_ASYNC_IO 1
static fls_job_t st_io_job;
static uint32_t st_io_half;
static int st_io_active, st_io_result;
static int st_io_begin(uint32_t off, const uint8_t *src, uint32_t n)
{
    if (st_io_active || !flash_ok || !FL_STORE_OK(off, n ? n : 4096u)) return -8;
    if (st_flash.fault) return st_flash.fault;
    memset(&st_io_job, 0, sizeof st_io_job);
    st_io_job.off = off; st_io_job.src = src; st_io_job.n = n;
    st_io_active = 1; st_io_result = 1; st_io_half = audio_halves;
    if (n) st_flash.programs++; else st_erases++;
    CFD_EVENT(CFD_MAIN, CFD_FLASH_BEGIN, n, off, st_flash.slices);
    return 0;
}
static int st_io_poll(void)
{
    if (st_io_active && (!st_runtime || audio_halves != st_io_half)) {
        ST_FLASH_PUMP();
        st_io_result = st_flash_step(&st_io_job);
        st_io_half = audio_halves;
        st_io_active = st_io_result == 1;
    }
    return st_io_result;
}
/* A normal read needs a ready chip. Finish just this command, not the whole
 * autosave; its immutable source remains owned by storage_async.c. Never feed
 * a suspended operation to fls_read_bytes (which treats it as a fault). */
static int st_io_finish(void)
{
    while (st_io_active) {
        st_flash_yield();
        st_io_result = st_flash_step(&st_io_job);
        st_io_active = st_io_result == 1;
    }
    return st_io_result;
}
#define ST_ASYNC_WAIT() st_io_finish()
#endif
#endif
/* All app object I/O must respect the probe, including callers with no local check. */
static int st_read(uint32_t off, void *dst, uint32_t n)
{
    uint8_t *d = dst;
    if (!flash_ok)
        return -8;
#ifdef CF_FLASH_COOP
    if (!fls_read_range(off, n)) return -1;
#ifdef ST_ASYNC
    if (st_io_active && st_io_finish() < 0) return st_io_result;
#endif
#endif
    while (n) {
#ifdef CF_FLASH_COOP
        if (st_runtime) ST_FLASH_PUMP();
        uint32_t k = n > 32u ? 32u : n, f = irq_save(), t = fm1_ticks();
        int rc = FL_FAR(fl_read_slice_ram)(off, d, k);
        uint32_t us = (uint32_t)(fm1_ticks() - t) / FM1_TICKS_PER_US;
#if CF_EVENT_DIAG
        if (CFD_ACTIVE) {
            cf_diag.flash_at = t - cf_diag.t0;
            cf_diag.flash_ticks = fm1_ticks() - t;
            cf_diag.flash_seq++;
        }
#endif
        if (us > st_flash.max_off_us) st_flash.max_off_us = us;
        if (us > 450u) st_flash.over_budget++;
#else
        uint32_t k = n > 256u ? 256u : n, f = irq_save();
        int rc = FL_FAR(fl_read_ram)(off, d, k);
#endif
        irq_restore(f);
#ifdef CF_FLASH_COOP
        if (us > 450u) CFD_EVENT(CFD_MAIN, CFD_FLASH_SLOW, 0xFFFFu, us, off);
#endif
        if (rc)
            return rc;
        off += k;
        d += k;
        n -= k;
    }
    return 0;
}
static void audio_silence(void)                 /* IRQs off: the DMA would loop stale audio (buzz) */
{
    uint32_t i;
    for (i = 0; i < sizeof abuf / sizeof abuf[0]; i++)
        abuf[i] = 0;
}

static int st_erase(uint32_t off)
{
    uint32_t took, f;
    int rc;
    if (!flash_ok || !FL_STORE_OK(off, 0x1000u))
        return -8;
    st_erases++;
#ifdef CF_FLASH_COOP
    uint8_t b[32];
    rc = st_flash_run(off, 0, 0);
    if (rc) return rc;
    /* Ready alone does not prove an erase was accepted (e.g. write protection). */
    for (uint32_t i = 0; i < 4096u; i += sizeof b) {
        if (st_read(off + i, b, sizeof b)) rc = -7;
        else for (uint32_t k = 0; k < sizeof b; k++) if (b[k] != 255u) rc = -7;
        if (rc) { st_flash.errors++; st_flash.fault = rc; return rc; }
    }
    return 0;
#else
    st_holes += (uint32_t)audio_loud();
    f = irq_save();
    audio_silence();
    rc = FL_FAR(fl_erase4k_ram)(off, &took);
    irq_restore(f);
    return rc;
#endif
}
#if defined(ST_LOG_OBJ) && !defined(CF_FLASH_COOP)
/* a page program while the audio plays (storage.c ST_NOERASE: CoralFlower's settings) starts just after the audio ISR
 * has rendered a half: its IRQs-off time (about 1 ms) then falls in the rest of that half, and the next half starts on
 * time or a little late, well inside the 2.1 ms that audio.c's DMA forgives */
static void st_after_render(void)
{
    uint32_t h = audio_halves, t = fm1_ms, i;
    for (i = 0; audio_halves == h && fm1_ms - t < 5u && i < 4000000u; i++)
        ;                                       /* (bounded twice: IRQs off would stop both counters) */
}
#define ST_PROG_SYNC() st_after_render()
#endif
static int st_prog(uint32_t off, const void *src, uint32_t n)
{
    if (!flash_ok || !FL_STORE_OK(off, n))      /* only inside the storage areas */
        return -8;
#ifdef CF_FLASH_COOP
    const uint8_t *p = src;
    while (n) {
        uint32_t k = 256u - (off & 255u);
        int rc;
        if (k > FLS_PROGRAM_BYTES) k = FLS_PROGRAM_BYTES;
        if (k > n) k = n;
        st_flash.programs++;
        if ((rc = st_flash_run(off, p, k)) != 0) return rc;
        off += k; p += k; n -= k;
    }
    return 0;
#else
    return fl_write(off, src, n);
#endif
}
