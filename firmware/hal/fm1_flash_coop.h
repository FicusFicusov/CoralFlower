/* SPDX-License-Identifier: GPL-3.0-only */
/* CoralFlower app only. Loader/OTA entry points in fm1_flash.h stay unchanged.
 * Storage calls are synchronous in the main loop; audio/input/USB IRQs and
 * the bounded musical-input pump run during suspensions. Code/tables are below the storage window,
 * their mutable data and DMA buffers are RAM. No nested storage operation. */
#pragma once

RAMINL uint32_t fls_ticks(void) { return T4_CNT; }
RAMINL void fls_stop(void)
{
    /* An SPI fault makes XIP unsafe. Stop stale analogue DMA; let the existing
     * watchdog/boot guard recover. No reset command is sent to the flash. */
    *(volatile uint16_t *)0x12E00u &= (uint16_t)~0x800u;
    for (;;) __asm__ volatile("csync" ::: "memory");
}
RAMINL void fls_cs(int high) { fl_cs(high); }
RAMINL void fls_wait(void)
{
    uint32_t t = T4_CNT, bound = 100000u;
    fl_csync();
    while (!(SPI0_CON & 0x8000u))
        if ((uint32_t)(T4_CNT - t) > 20u * 24u || !--bound) fls_stop();
    SPI0_CON |= 0x4000u;
}
RAMINL void fls_tx(uint8_t b)
{
    SPI0_CON |= 8u; SPI0_CON &= ~0x1000u;
    SPI0_BUF = b; fls_wait(); SPI0_CON &= ~8u;
}
RAMINL uint8_t fls_rx(void)
{
    uint8_t b;
    SPI0_CON |= 8u | 0x1000u;
    SPI0_BUF = 0xFFu; fls_wait(); b = (uint8_t)SPI0_BUF;
    SPI0_CON &= ~8u;
    return b;
}
#define FLS_INLINE RAMINL
#include "fm1_flash_slice.h"

static RAMFN int fl_slice_ram(fls_job_t *j)
{
    fl_saved_t s;
    int rc;
    if (!FL_STORE_OK(j->off, j->n ? j->n : 4096u) ||
        (j->n ? (j->n > FLS_PROGRAM_BYTES || (j->off & 255u) + j->n > 256u) : (j->off & 4095u)))
        return -1;
    if (fl_enter(&s)) return -2;
    rc = fls_step(j);
    if (rc <= 0) fl_inval(j->off, j->n ? j->n : 4096u);
    fl_exit(&s);                           /* only complete or confirmed suspended */
    return rc;
}

static RAMFN int fl_recover_ram(void)
{
    fl_saved_t s;
    if (fl_enter(&s)) return -2;
    fls_drain();                           /* MCU reset may have left the NOR suspended */
    fl_exit(&s);
    return 0;
}

static RAMFN int fl_read_slice_ram(uint32_t off, uint8_t *dst, uint32_t n)
{
    fl_saved_t s;
    int rc;
    if (!fls_read_range(off, n) || n > 32u) return -1;
    if (fl_enter(&s)) return -2;
    rc = fls_read_bytes(off, dst, n);
    fl_exit(&s);
    return rc;
}
