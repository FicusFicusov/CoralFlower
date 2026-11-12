/* SPDX-License-Identifier: GPL-3.0-only */
/* Command-level NOR model, NOT a target CPU/SFC timing measurement. Each test
 * selects either manufacturer's timing and both interpretations of SUS bits.
 * Model time and the independent USB/audio clocks can run while CPU IRQs cannot.
 */
#pragma once
#include <stdint.h>
#include <string.h>
#include <setjmp.h>
static struct {
    uint8_t *mem;
    uint64_t ticks, remain, resumed, pending_suspend;
    uint32_t pp_us, se_us, suspend_us, min_program, min_erase, byte_us;
    uint32_t addr, op_addr, op_n, min_run, commands, events, cut_at;
    uint8_t cmd, bytes[256], op_data[256], wel, busy, suspended, op, selected, swap_sus;
    uint32_t pos, programs, erases, suspends, resumes, bad_progress, bad_read, stop_count;
    int ignore_suspend, ignore_resume, reject_wren, stuck_busy, spi_fail;
} fnor;
static jmp_buf fnor_jump;
static void fnor_event(void);
#ifndef FNOR_ADVANCE
#define FNOR_ADVANCE(t) ((void)(t))
#endif

static void fnor_apply(uint32_t n)
{
    if (fnor.op == 0x20) memset(fnor.mem + fnor.op_addr, 0xFF, n);
    else for (uint32_t i = 0; i < n; i++) fnor.mem[fnor.op_addr + i] &= fnor.op_data[i];
}
static void fnor_advance(uint64_t dt)
{
    uint64_t end = fnor.ticks + dt, active = dt;
    if (fnor.pending_suspend && end >= fnor.pending_suspend)
        active = fnor.pending_suspend > fnor.ticks ? fnor.pending_suspend - fnor.ticks : 0;
    if (fnor.busy && !fnor.stuck_busy) {
        if (active >= fnor.remain) {
            fnor_apply(fnor.op_n);
            fnor.busy = fnor.wel = fnor.suspended = 0;
            fnor.pending_suspend = 0; fnor.remain = 0; fnor.op = 0;
        } else fnor.remain -= active;
    }
    if (fnor.pending_suspend && end >= fnor.pending_suspend) {
        fnor.pending_suspend = 0;
        if (fnor.busy) { fnor.busy = fnor.wel = 0; fnor.suspended = 1; }
    }
    fnor.ticks = end;
    FNOR_ADVANCE(end);
}
static void fnor_event(void)
{
    fnor.events++;
    if (fnor.cut_at && fnor.events == fnor.cut_at) {
        /* A partially programmed/erased destination, not an atomic fake write. */
        if (fnor.op) fnor_apply(fnor.op_n / 2u);
        longjmp(fnor_jump, 2);
    }
}
static void fnor_reset(uint8_t *mem, unsigned chip, unsigned swap)
{
    memset(&fnor, 0, sizeof fnor);
    fnor.mem = mem; fnor.pp_us = 3000; fnor.se_us = chip ? 10000 : 20000;
    fnor.suspend_us = chip ? 45 : 30;
    fnor.min_program = 100; fnor.min_erase = chip ? 200 : 100;
    fnor.byte_us = 2; fnor.swap_sus = (uint8_t)swap;
}
static void fls_cs(int high)
{
    fnor_advance(12); /* 0.5 us CS overhead in the model */
    if (!high) {
        CHECK(!fnor.selected, "NOR CS transaction cannot nest");
        fnor.selected = 1; fnor.pos = fnor.addr = 0; fnor.cmd = 0;
        return;
    }
    CHECK(fnor.selected, "NOR CS closes an open transaction");
    fnor.selected = 0; fnor.commands++;
    switch (fnor.cmd) {
    case 0x06:
        if (!fnor.busy && !fnor.suspended && !fnor.reject_wren) fnor.wel = 1;
        break;
    case 0x04: if (!fnor.busy) fnor.wel = 0; break;
    case 0x02: case 0x20:
        CHECK(fnor.wel && !fnor.busy && !fnor.suspended, "program/erase requires idle WEL, never nests");
        if (!fnor.wel || fnor.busy || fnor.suspended) break;
        fnor.op = fnor.cmd; fnor.op_addr = fnor.addr;
        fnor.op_n = fnor.op == 0x20 ? 4096u : fnor.pos - 4u;
        CHECK(fnor.op_addr >= 0x97000u && fnor.op_addr + fnor.op_n <= 0xFF000u, "operation stays outside application/loader");
        CHECK(fnor.op_n && (fnor.op != 0x02 || ((fnor.op_addr & 255u) + fnor.op_n <= 256u)), "program stays within page");
        CHECK(fnor.op != 0x20 || !(fnor.op_addr & 4095u), "erase is sector aligned");
        if (fnor.op == 0x02) { memcpy(fnor.op_data, fnor.bytes, fnor.op_n); fnor.programs++; }
        else fnor.erases++;
        fnor.min_run = fnor.op == 0x02 ? fnor.min_program : fnor.min_erase;
        fnor.remain = (fnor.op == 0x02 ? fnor.pp_us : fnor.se_us) * 24ull;
        fnor.busy = 1; fnor.resumed = fnor.ticks;
        break;
    case 0x75:
        if (fnor.busy && !fnor.ignore_suspend) {
            if (fnor.ticks - fnor.resumed < fnor.min_run * 24ull) fnor.bad_progress++;
            fnor.pending_suspend = fnor.ticks + fnor.suspend_us * 24ull;
            fnor.suspends++;
        }
        break;
    case 0x7A:
        if (fnor.suspended && !fnor.ignore_resume) {
            fnor.suspended = 0; fnor.busy = fnor.wel = 1;
            fnor.resumed = fnor.ticks; fnor.resumes++;
        }
        break;
    }
    fnor_event();
}
static void fls_stop(void) { fnor.stop_count++; longjmp(fnor_jump, 1); }
static void fls_tx(uint8_t b)
{
    if (fnor.spi_fail) fls_stop();
    CHECK(fnor.selected, "SPI byte requires CS");
    fnor_advance(fnor.byte_us * 24u);
    if (!fnor.pos) fnor.cmd = b;
    else if (fnor.pos < 4 && (fnor.cmd == 0x02 || fnor.cmd == 0x20 || fnor.cmd == 0x0B)) fnor.addr = fnor.addr << 8 | b;
    else if (fnor.cmd == 0x02) {
        CHECK(fnor.pos - 4u < 256u, "NOR page payload bound");
        if (fnor.pos - 4u < 256u) fnor.bytes[fnor.pos - 4u] = b;
    }
    fnor.pos++;
}
static uint8_t fls_rx(void)
{
    if (fnor.spi_fail) fls_stop();
    fnor_advance(fnor.byte_us * 24u);
    if (fnor.cmd == 0x05) return (uint8_t)(fnor.busy | fnor.wel << 1);
    if (fnor.cmd == 0x35) {
        if (!fnor.suspended) return 2; /* QE retained, never rewritten */
        return (uint8_t)(2u | (((fnor.op == 0x02) ^ fnor.swap_sus) ? 0x80u : 4u));
    }
    if (fnor.cmd == 0x0B) {
        if (fnor.busy || (fnor.suspended && fnor.addr >= fnor.op_addr && fnor.addr < fnor.op_addr+fnor.op_n)) fnor.bad_read++;
        CHECK(!fnor.bad_read,"NOR data read requires readable address");
        return fnor.mem[fnor.addr++];
    }
    CHECK(0, "unexpected command read"); return 0xFF;
}
static uint32_t fls_ticks(void) { fnor_advance(24); return (uint32_t)fnor.ticks; }
#include "../firmware/hal/fm1_flash_slice.h"

static int fl_slice_ram(fls_job_t *j)
{
    int rc;
    if (j->off < 0x97000u || j->off >= 0xFF000u ||
        (j->n ? (j->n > FLS_PROGRAM_BYTES || (j->off & 255u) + j->n > 256u) : (j->off & 4095u))) return -1;
    rc = fls_step(j);                  /* exact shared production transaction logic */
    CHECK(!fnor.busy && !fnor.wel, "XIP may resume only when WIP and WEL are clear");
    CHECK(rc == 1 ? fnor.suspended : !fnor.suspended, "complete and suspended cannot be confused");
    return rc;
}
