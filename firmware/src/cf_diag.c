/* SPDX-License-Identifier: GPL-3.0-only */
#include "cf_diag.h"
#if CF_EVENT_DIAG
/* No cli/sti and no calls/XIP data in this emitter. Separate append-only lanes
 * avoid a shared writer lock and do not extend an existing IRQ-off window.
 * Count is published last; main-loop reads never see a partly written record.
 * A full lane retains its earliest evidence and explicitly counts omissions. */
#ifndef CFD_TICKS
#define CFD_TICKS() fm1_ticks()
#endif
static void cf_diag_event(uint32_t lane, uint32_t tag, uint32_t a, uint32_t b)
{
    uint32_t now, n, base, cap, dt;
    if (!cf_diag.active) return;
    now = CFD_TICKS();
    if ((uint32_t)(now - cf_diag.t0) >= CFD_SECONDS * 1000000u * FM1_TICKS_PER_US) return;
    n = cf_diag.n[lane];
    base = lane == CFD_MAIN ? 0u : lane == CFD_AUDIO ? 64u : 192u;
    cap = lane == CFD_AUDIO ? 128u : 64u;
    if (n == cap) { cf_diag.dropped[lane]++; return; }
    cf_diag.events[base + n].ticks = now - cf_diag.t0;
    cf_diag.events[base + n].tag = tag;
    cf_diag.events[base + n].a = a;
    cf_diag.events[base + n].b = b;
    __asm__ volatile("" ::: "memory");
    cf_diag.n[lane] = n + 1u;
    dt = CFD_TICKS() - now;          /* wall time, including an interrupt nested in the main/audio lane */
    if (dt > cf_diag.max_emit[lane]) cf_diag.max_emit[lane] = dt;
}
/* Main-loop MODE gesture only, never from the flash's note-input pump. A later
 * gesture starts a new generation, rather than mixing two trigger timelines. */
static void cf_diag_start(uint32_t old_mode, uint32_t new_mode, uint32_t ms)
{
    fm1_irq_off();
    cf_diag.active = 0;
    cf_diag.generation++;
    if (!cf_diag.generation) cf_diag.generation = 1;
    cf_diag.t0 = CFD_TICKS();
    cf_diag.ms = ms;
    for (uint32_t i = 0; i < CFD_LANES; i++)
        cf_diag.n[i] = cf_diag.dropped[i] = cf_diag.max_emit[i] = cf_diag.flash_seen[i] = 0;
    cf_diag.pcm_at = cf_diag.health_at = 0;
    cf_diag.flash_at = cf_diag.flash_ticks = cf_diag.flash_seq = 0;
    cf_diag.active = 1;
    fm1_irq_on();
    CFD_EVENT(CFD_MAIN, CFD_MODE, old_mode << 8 | new_mode, 0, 0);
}
/* The most recent measured IRQ-off flash window, only alongside an anomaly.
 * Published by main with IRQs off AFTER SFC/XIP is restored, before sti. */
static void cf_diag_flash_note(uint32_t lane)
{
    if (cf_diag.active && cf_diag.flash_seq != cf_diag.flash_seen[lane]) {
        cf_diag.flash_seen[lane] = cf_diag.flash_seq;
        CFD_EVENT(lane, CFD_FLASH_WINDOW, cf_diag.flash_seq & 65535u, cf_diag.flash_at, cf_diag.flash_ticks);
    }
}
static void cf_diag_poll(void)
{
    if (cf_diag.active && (uint32_t)(CFD_TICKS() - cf_diag.t0) >= CFD_SECONDS * 1000000u * FM1_TICKS_PER_US)
        cf_diag.active = 0;
}
/* Called AFTER arming the packet, with the SIE owner held. A 16-frame stereo
 * PCM16 prefix, FNV-1a, four times/second: locate it in a raw host recording.
 * An armed packet is not proof of delivery; the offline matcher says whether
 * and where it arrived. No samples, sizes, scheduling or rate are changed. */
static void cf_diag_pcm(const uint8_t *p, uint32_t bytes, uint32_t frame)
{
    uint32_t t, hash = 2166136261u;
    if (!cf_diag.active || bytes < 64u) return;
    t = CFD_TICKS() - cf_diag.t0;
    if (t < cf_diag.pcm_at) return;
    cf_diag.pcm_at = t + 250000u * FM1_TICKS_PER_US;
    for (uint32_t i = 0; i < 64u; i++) hash = (hash ^ p[i]) * 16777619u;
    CFD_EVENT(CFD_USB, CFD_PCM, bytes / 4u, frame, hash);
}
#endif
