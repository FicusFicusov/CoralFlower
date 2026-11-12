/* SPDX-License-Identifier: GPL-3.0-only */
/* NOR transaction logic, shared by the RAM driver and the command-level host
 * model. The backend supplies fls_cs, fls_tx, fls_rx, fls_ticks and fls_stop.
 * All helpers are inlined into a .ram_text entry: no flash fetch while busy.
 *
 * P25Q80H v1.7 table 5-4 / 10.36-38; UC25HQ80 v1.2 table 18 / 9.33-35:
 * allow >=100 us program and >=200 us erase progress before each suspend;
 * suspend latency <=45 us. Read only outside the suspended page/sector.
 * UCUN's table 4 and 9.33 disagree on which SUS bit means program vs erase.
 * We never nest operations, and test BOTH bits, without assigning their names.
 */
#pragma once
#include <stdint.h>
#ifndef FLS_INLINE
#define FLS_INLINE static inline __attribute__((always_inline))
#endif
#define FLS_TICKS_PER_US 24u
#define FLS_RUN_US 220u
#define FLS_PROGRAM_US 120u
#define FLS_SUSPEND_US 50u
#define FLS_SUS_MASK 0x8400u
#define FLS_PROGRAM_BYTES 32u

FLS_INLINE int fls_read_range(uint32_t off, uint32_t n)
{
    /* The updater reads the boot header and staging slot too. Read permission
     * covers the whole 1 MiB chip; store-only write bounds remain separate. */
    return off <= 0x100000u && n <= 0x100000u - off;
}

typedef struct {
    uint32_t off, n, active_ticks, slices;
    const uint8_t *src;                 /* RAM; n == 0 means sector erase */
    uint8_t started;
    int error;
} fls_job_t;

FLS_INLINE void fls_delay_us(uint32_t us)
{
    uint32_t t = fls_ticks();
    while ((uint32_t)(fls_ticks() - t) < us * FLS_TICKS_PER_US) ;
}
FLS_INLINE void fls_command(uint8_t cmd)
{
    fls_cs(0); fls_tx(cmd); fls_cs(1);
}
FLS_INLINE uint16_t fls_status(void)
{
    uint16_t a, b;
    fls_cs(0); fls_tx(0x05); a = fls_rx(); fls_cs(1);
    fls_cs(0); fls_tx(0x35); b = fls_rx(); fls_cs(1);
    /* Disconnected/stuck-high SPI is not a readable NOR. Never return to XIP. */
    if (a == 0xFFu || b == 0xFFu) fls_stop();
    return a | b << 8;
}

/* Error recovery and warm-reset recovery: finish the one existing operation
 * before returning, even if that sacrifices audio on the faulty path. Never
 * reset the NOR (that would clear volatile QE/read configuration), and never
 * return with BUSY or SUS set. A nonresponding part stays in RAM until WDT. */
FLS_INLINE void fls_drain(void)
{
    uint32_t t = fls_ticks();
    for (;;) {
        if ((uint32_t)(fls_ticks() - t) > 500000u * FLS_TICKS_PER_US)
            fls_stop();
        uint16_t s = fls_status();
        if (!(s & (FLS_SUS_MASK | 1u))) {
            if (s & 2u) { fls_command(0x04); continue; }
            return;
        }
        if (s & FLS_SUS_MASK) {
            fls_command(0x7A);
            fls_delay_us(FLS_RUN_US);
        }
    }
}

/* Called with IRQs off and SFC disconnected. 1 = suspended, 0 = complete,
 * negative = failed but fully quiescent. The caller restores SFC before IRQs.
 * Completion is distinguished from suspension even though both clear WIP. */
FLS_INLINE int fls_step(fls_job_t *j)
{
    uint32_t t, i;
    uint16_t s = fls_status();
    if (!j->started) {
        if (s & (FLS_SUS_MASK | 3u)) { fls_drain(); return -9; }
        fls_command(0x06);
        if ((fls_status() & (FLS_SUS_MASK | 3u)) != 2u) {
            fls_drain(); return -3;
        }
        fls_cs(0); fls_tx(j->n ? 0x02 : 0x20);
        fls_tx((uint8_t)(j->off >> 16)); fls_tx((uint8_t)(j->off >> 8)); fls_tx((uint8_t)j->off);
        for (i = 0; i < j->n; i++) fls_tx(j->src[i]);
        fls_cs(1);
        j->started = 1;
    } else {
        if (!(s & FLS_SUS_MASK) || (s & 3u)) { fls_drain(); return -9; }
        fls_command(0x7A);
    }
    t = fls_ticks();
    fls_delay_us(j->n ? FLS_PROGRAM_US : FLS_RUN_US); /* >=100/200 us actual progress */
    j->active_ticks += fls_ticks() - t;
    j->slices++;
    s = fls_status();
    if (!(s & (FLS_SUS_MASK | 3u))) return 0;
    if ((s & FLS_SUS_MASK) || !(s & 1u) ||
        j->active_ticks > (j->n ? 10000u : 500000u) * FLS_TICKS_PER_US) {
        fls_drain(); return -10;           /* ignored resume, rejected command, or no progress */
    }
    fls_command(0x75);
    fls_delay_us(FLS_SUSPEND_US);          /* don't trust an early SUS indication */
    s = fls_status();
    if (!(s & 3u)) {
        if (s & FLS_SUS_MASK) return 1;
        return 0;                         /* completed while the suspend command was being sent */
    }
    fls_drain();                          /* failed suspend: no unsafe XIP return */
    return -11;
}

FLS_INLINE int fls_read_bytes(uint32_t off, uint8_t *dst, uint32_t n)
{
    if (!fls_read_range(off, n) || n > 32u) return -1;
    if (fls_status() & (FLS_SUS_MASK | 3u)) { fls_drain(); return -9; }
    fls_cs(0); fls_tx(0x0B);
    fls_tx((uint8_t)(off >> 16)); fls_tx((uint8_t)(off >> 8)); fls_tx((uint8_t)off); fls_tx(0xFF);
    for (uint32_t i = 0; i < n; i++) dst[i] = fls_rx();
    fls_cs(1);
    return 0;
}
