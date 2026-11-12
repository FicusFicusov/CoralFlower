/* SPDX-License-Identifier: GPL-3.0-only */
#include "cf_diag.h"
#if CF_EVENT_DIAG
/* The detailed records have a fixed quota in EACH second. MODE/context and
 * PCM anchors have their own prefix slots. All occurrences are counted even
 * when only representative details fit. Each counter has one producer; USB
 * RING and FLASH_WINDOW use separate slots when nested in the audio lane.
 * No cli/sti, calls or XIP data in the RAM emitter. A bounded scan (<=11) keeps
 * distinct event kinds before repeats; severe anomalies win remaining ties. */
#ifndef CFD_TICKS
#define CFD_TICKS() fm1_ticks()
#endif
static inline __attribute__((always_inline)) uint32_t cf_diag_priority(uint32_t kind)
{
    /* A conditional chain becomes an XIP lookup table with the JieLi compiler.
     * Immediate bit masks keep this RAM path independent of flash, including
     * the full-bucket scan while a NOR operation has suspended XIP. */
    const uint32_t severe = (1u << CFD_BLOCK_LATE) | (1u << CFD_USB_RING) | (1u << CFD_FLASH_SLOW);
    const uint32_t notable = severe | (1u << CFD_SHED) | (1u << CFD_AUDIO_LATE) |
        (1u << CFD_USB_ARM_GAP) | (1u << CFD_USB_SERVICE_GAP) | (1u << CFD_USB_STREAM);
    return 1u + ((notable >> (kind & 31u)) & 1u) + ((severe >> (kind & 31u)) & 1u);
}
static void cf_diag_event(uint32_t lane, uint32_t tag, uint32_t a, uint32_t b)
{
    uint32_t now, age, second, kind = tag & 65535u, ci, base, cap, n, at, prefix, dt;
    if (!cf_diag.active) return;
    now = CFD_TICKS();
    age = now - cf_diag.t0;
    if (age >= CFD_SECONDS * 1000000u * FM1_TICKS_PER_US) return;
    second = age / (1000000u * FM1_TICKS_PER_US);
    ci = kind == CFD_USB_RING && lane == CFD_AUDIO ? 22u :
         kind == CFD_FLASH_WINDOW && lane == CFD_USB ? 23u : kind - 1u;
    if (cf_diag.second[second].count[ci] != 65535u) cf_diag.second[second].count[ci]++;
    else cf_diag.saturated[lane] = 1;
    if (kind == CFD_SHED && (tag >> 16 & 15u)) {
        uint32_t detail = tag >> 16, p = detail >> 4 & 7u;
        uint32_t cat = detail & (1u << 11) ? 2u : !(a & 0xff00u) ? 0u : (detail & 15u) == 4u ? 3u : 1u;
        if (p < 4u) {
            if (cf_diag.second[second].shed[p][cat] != 65535u) cf_diag.second[second].shed[p][cat]++;
            else cf_diag.saturated[lane] = 1;
        }
    }
    if (kind == CFD_VOICES) {
        uint32_t k = cf_diag.second[second].kills + a, s = cf_diag.second[second].steals + b;
        if (k > 65535u || s > 65535u) cf_diag.saturated[lane] = 1;
        cf_diag.second[second].kills = (uint16_t)(k > 65535u ? 65535u : k);
        cf_diag.second[second].steals = (uint16_t)(s > 65535u ? 65535u : s);
    }
    base = lane == CFD_MAIN ? 0u : lane == CFD_AUDIO ? 44u : 159u;
    cap = lane == CFD_AUDIO ? 11u : 4u;
    prefix = lane == CFD_MAIN ? 4u : lane == CFD_AUDIO ? 5u : 40u;
    if (((lane == CFD_MAIN && (kind == CFD_MODE || kind == CFD_CONTEXT)) ||
         (lane == CFD_AUDIO && (kind == CFD_CONFIG || kind == CFD_PART)) ||
         (lane == CFD_USB && kind == CFD_PCM)) && cf_diag.prefix[lane] < prefix) {
        at = base + cf_diag.prefix[lane]++;
        cf_diag.n[lane]++;
    } else {
        base += prefix + second * cap;
        n = cf_diag.used[lane][second];
        if (n < cap) {
            at = base + n;
            cf_diag.used[lane][second] = (uint8_t)(n + 1u);
            cf_diag.n[lane]++;
        } else {
            uint32_t seen = 0, score = 0xffffffffu, victim = 0;
            cf_diag.dropped[lane]++;
            for (uint32_t i = 0; i < cap; i++) {
                uint32_t k = cf_diag.events[base + i].tag & 65535u;
                uint32_t rank = cf_diag_priority(k) + ((seen & (1u << k)) ? 0u : 16u);
                seen |= 1u << k;
                if (rank < score) { score = rank; victim = i; }
            }
            if (cf_diag_priority(kind) + ((seen & (1u << kind)) ? 0u : 16u) < score) goto measured;
            at = base + victim;
        }
    }
    cf_diag.events[at].ticks = age;
    cf_diag.events[at].tag = tag;
    cf_diag.events[at].a = a;
    cf_diag.events[at].b = b;
measured:
    dt = CFD_TICKS() - now;
    if (dt > cf_diag.max_emit[lane]) cf_diag.max_emit[lane] = dt;
}
/* Main-loop MODE gesture only. Stop writers first, then clear the summaries
 * with IRQs ON. The larger reset must not become a new audio/USB pause. */
static void cf_diag_start(uint32_t old_mode, uint32_t new_mode, uint32_t ms)
{
    fm1_irq_off(); cf_diag.active = 0; fm1_irq_on();
    cf_diag.generation++;
    if (!cf_diag.generation) cf_diag.generation = 1;
    for (uint32_t i = 0; i < CFD_LANES; i++) {
        cf_diag.n[i] = cf_diag.dropped[i] = cf_diag.max_emit[i] = cf_diag.flash_seen[i] = 0;
        cf_diag.saturated[i] = cf_diag.prefix[i] = 0;
        for (uint32_t s = 0; s < CFD_SECONDS; s++) cf_diag.used[i][s] = 0;
    }
    for (uint32_t i = 0; i < CFD_SECONDS; i++) cf_diag.second[i] = (cf_diag_second_t){0};
    cf_diag.pcm_at = cf_diag.health_at = cf_diag.parts_generation = cf_diag.max_audio_hook = 0;
    cf_diag.flash_at = cf_diag.flash_ticks = cf_diag.flash_seq = 0;
    fm1_irq_off();
    cf_diag.t0 = CFD_TICKS(); cf_diag.ms = ms; cf_diag.active = 1;
    fm1_irq_on();
    CFD_EVENT(CFD_MAIN, CFD_MODE, old_mode << 8 | new_mode, 0, 0);
}
/* Frozen captures only: the SysEx path returns no records while active. The
 * sparse physical layout is compacted on read; it is never copied in an ISR. */
static int cf_diag_raw_event(uint32_t lane, uint32_t index, cf_diag_event_t *out)
{
    uint32_t base = cf_diag_base(lane), n = cf_diag.prefix[lane];
    if (index < n) { *out = cf_diag.events[base + index]; return 1; }
    index -= n; base += cf_diag_prefix(lane);
    for (uint32_t s = 0; s < CFD_SECONDS; s++) {
        n = cf_diag.used[lane][s];
        if (index < n) { *out = cf_diag.events[base + s * cf_diag_per_second(lane) + index]; return 1; }
        index -= n;
    }
    return 0;
}
static uint32_t cf_diag_count_kind(uint32_t ci) { return ci == 22u ? CFD_USB_RING : ci == 23u ? CFD_FLASH_WINDOW : ci + 1u; }
static uint32_t cf_diag_count_lane(uint32_t ci)
{
    const uint8_t lanes[CFD_COUNT_SLOTS] = {0,1,0,0,0,0,0,1,1,1,1,1,1,1,2,2,2,2,2,0,1,1,1,2};
    return lanes[ci];
}
static uint32_t cf_diag_summary_count(void)
{
    uint32_t n = 0;
    for (uint32_t s = 0; s < CFD_SECONDS; s++) {
        n += 3u + (cf_diag.second[s].wall ? 4u : 0u);
        for (uint32_t k = 0; k < CFD_COUNT_SLOTS; k++) n += cf_diag.second[s].count[k] != 0u;
        for (uint32_t p = 0; p < 4u; p++) {
            uint32_t any = 0;
            for (uint32_t k = 0; k < 4u; k++) any |= cf_diag.second[s].shed[p][k];
            n += any != 0u;
        }
    }
    return n;
}
static int cf_diag_read_event(uint32_t lane, uint32_t index, cf_diag_event_t *out)
{
    if (lane < CFD_LANES) return cf_diag_raw_event(lane, index, out);
    for (uint32_t s = 0; s < CFD_SECONDS; s++) {
        const volatile cf_diag_second_t *v = &cf_diag.second[s];
        uint32_t t = s * 1000000u * FM1_TICKS_PER_US;
        for (uint32_t ci = 0; ci < CFD_COUNT_SLOTS; ci++) if (v->count[ci]) {
            if (!index--) {
                uint32_t kind = cf_diag_count_kind(ci), producer = cf_diag_count_lane(ci), kept = 0;
                cf_diag_event_t e;
                for (uint32_t i = 0; cf_diag_raw_event(producer, i, &e); i++)
                    kept += (e.tag & 65535u) == kind && e.ticks / (1000000u * FM1_TICKS_PER_US) == s;
                *out = (cf_diag_event_t){t, CFD_COUNTS | (kind | producer << 8) << 16, v->count[ci], v->count[ci] - kept};
                return 1;
            }
        }
        for (uint32_t p = 0; p < 4u; p++) {
            uint32_t a = v->shed[p][0] | (uint32_t)v->shed[p][1] << 16;
            uint32_t b = v->shed[p][2] | (uint32_t)v->shed[p][3] << 16;
            if ((a || b) && !index--) { *out = (cf_diag_event_t){t, CFD_SHED_COUNTS | p << 16, a, b}; return 1; }
        }
        if (!index--) { *out = (cf_diag_event_t){t, CFD_VOICE_TOTALS, v->kills, v->steals}; return 1; }
        if (!index--) { *out = (cf_diag_event_t){v->wall ? v->peak_at : t, CFD_PEAK | s << 16, v->wall, v->lag}; return 1; }
        if (!index--) { *out = (cf_diag_event_t){v->wall ? v->peak_at : t, CFD_PEAK_IRQ | s << 16, v->nested, v->wall - v->nested}; return 1; }
        if (v->wall) for (uint32_t p = 0; p < 4u; p++)
            if (!index--) { *out = (cf_diag_event_t){v->peak_at, CFD_PART_PEAK | (p | s << 8) << 16, v->part_us[p], v->part_voices[p]}; return 1; }
    }
    return 0;
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
