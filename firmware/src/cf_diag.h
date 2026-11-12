/* SPDX-License-Identifier: GPL-3.0-only */
/* Temporary on-device ARP/flash/audio diagnosis. No persistent state or sound marker.
 * Each lane has one serialized producer: main, ALNK0, and USB's SIE owner.
 * In particular, USB hooks must not use the audio lane when TIMER5 nests. */
#ifndef CF_DIAG_H
#define CF_DIAG_H
#include <stdint.h>
#ifndef CF_EVENT_DIAG
#define CF_EVENT_DIAG 0
#endif
enum { CFD_MAIN, CFD_AUDIO, CFD_USB, CFD_LANES };
enum {
    CFD_MODE = 1, CFD_CONFIG, CFD_SAVE_BEGIN, CFD_SAVE_END,
    CFD_FLASH_BEGIN, CFD_FLASH_END, CFD_FLASH_SLOW,
    CFD_SHED, CFD_SHED_REQUEST, CFD_AUDIO_LATE, CFD_BLOCK_LATE,
    CFD_AUDIO_STATE, CFD_VOICES, CFD_ARP_NOTE, CFD_PCM,
    CFD_USB_ARM_GAP, CFD_USB_SERVICE_GAP, CFD_USB_RING, CFD_USB_STREAM, CFD_CONTEXT, CFD_FLASH_WINDOW,
    CFD_PART,
    /* Schema 2: virtual records, assembled by the reader from per-second totals. */
    CFD_COUNTS, CFD_SHED_COUNTS, CFD_VOICE_TOTALS, CFD_PEAK, CFD_PART_PEAK, CFD_PEAK_IRQ
};
#if CF_EVENT_DIAG
#define CFD_SECONDS 10u
#define CFD_RECORDS 239u
#define CFD_COUNT_SLOTS 24u        /* kinds 1..22; audio RING and USB FLASH_WINDOW have separate writers */
#define CFD_SUMMARY_LANE 3u
typedef struct { uint32_t ticks, tag, a, b; } cf_diag_event_t;
typedef struct {
    uint32_t peak_at, wall, lag, nested;
    uint16_t part_us[4];
    uint32_t part_voices[4];
    uint16_t count[CFD_COUNT_SLOTS], shed[4][4], kills, steals;
} cf_diag_second_t;
static struct {
    volatile uint32_t active, generation, t0, ms;
    volatile uint32_t n[CFD_LANES], dropped[CFD_LANES], max_emit[CFD_LANES];
    uint32_t pcm_at, health_at;
    volatile uint32_t flash_at, flash_ticks, flash_seq;
    uint32_t flash_seen[CFD_LANES];
    volatile uint32_t saturated[CFD_LANES];
    uint32_t parts_generation, max_audio_hook;
    volatile uint8_t prefix[CFD_LANES], used[CFD_LANES][CFD_SECONDS];
    volatile cf_diag_second_t second[CFD_SECONDS];
    volatile cf_diag_event_t events[CFD_RECORDS];
} cf_diag;
static uint32_t cf_diag_capacity(uint32_t lane) { return lane == CFD_MAIN ? 44u : lane == CFD_AUDIO ? 115u : 80u; }
static uint32_t cf_diag_base(uint32_t lane) { return lane == CFD_MAIN ? 0u : lane == CFD_AUDIO ? 44u : 159u; }
static uint32_t cf_diag_prefix(uint32_t lane) { return lane == CFD_MAIN ? 4u : lane == CFD_AUDIO ? 5u : 40u; }
static uint32_t cf_diag_per_second(uint32_t lane) { return lane == CFD_AUDIO ? 11u : 4u; }
static uint32_t cf_diag_summary_count(void);
static int cf_diag_read_event(uint32_t lane, uint32_t index, cf_diag_event_t *out);
static void cf_diag_event(uint32_t lane, uint32_t tag, uint32_t a, uint32_t b)
    __attribute__((section(".ram_text"), noinline));
static void cf_diag_start(uint32_t old_mode, uint32_t new_mode, uint32_t ms);
static void cf_diag_poll(void);
static void cf_diag_pcm(const uint8_t *p, uint32_t bytes, uint32_t frame);
static void cf_diag_flash_note(uint32_t lane);
#define CFD_FLASH_CONTEXT(lane) cf_diag_flash_note(lane)
#ifdef FM1_IRQ_TARGET
/* XIP -> .ram_text exceeds the target's direct call range, as in FL_FAR.
 * The volatile pointer prevents clang folding this back to a near call. */
typedef void (*cf_diag_writer_t)(uint32_t, uint32_t, uint32_t, uint32_t);
static inline cf_diag_writer_t cf_diag_writer(void)
{
    cf_diag_writer_t volatile writer = cf_diag_event;
    return writer;
}
#define CFD_EVENT(lane, kind, detail, a, b) cf_diag_writer()((lane), (kind) | (uint32_t)(detail) << 16, (a), (b))
#else
#define CFD_EVENT(lane, kind, detail, a, b) cf_diag_event((lane), (kind) | (uint32_t)(detail) << 16, (a), (b))
#endif
#define CFD_ACTIVE (cf_diag.active)
#else
#define CFD_EVENT(lane, kind, detail, a, b) ((void)0)
#define CFD_ACTIVE 0
#define CFD_FLASH_CONTEXT(lane) ((void)0)
#endif
#endif
