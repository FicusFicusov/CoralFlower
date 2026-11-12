/* SPDX-License-Identifier: GPL-3.0-only */
/* Passive main-loop timing for diagnostic test builds. No ISR hook, trace
 * arming, allocation, reset or storage. Millisecond resolution is deliberate:
 * this locates a visibly stalled UI, not the audio deadline profiler's work.
 * A completed slow phase remains visible after the UI recovers. */
#ifndef CF_UI_DIAG_H
#define CF_UI_DIAG_H
#include <stdint.h>
enum {
    CFU_NONE, CFU_HOUSEKEEPING, CFU_SOUNDS, CFU_NOTES, CFU_SEQUENCE,
    CFU_CONTROLS, CFU_SETTINGS, CFU_FRAME, CFU_DRAW, CFU_IDLE, CFU_PHASES
};
#if CF_EVENT_DIAG
static struct {
    uint32_t phase, phase_ms, max_ms[CFU_PHASES];
    uint32_t input_ms, input_calls, input_gap_max_ms;
    uint32_t buttons, encoders, last_control, control_ms;
    int32_t control_delta;
} cf_ui_diag;

static void cf_ui_mark(uint32_t phase, uint32_t now)
{
    uint32_t old = cf_ui_diag.phase, elapsed = now - cf_ui_diag.phase_ms;
    if (old && old < CFU_PHASES && elapsed > cf_ui_diag.max_ms[old])
        cf_ui_diag.max_ms[old] = elapsed;
    cf_ui_diag.phase = phase;
    cf_ui_diag.phase_ms = now;
}
static void cf_ui_input_mark(uint32_t now)
{
    uint32_t gap = now - cf_ui_diag.input_ms;
    if (cf_ui_diag.input_calls && gap > cf_ui_diag.input_gap_max_ms)
        cf_ui_diag.input_gap_max_ms = gap;
    cf_ui_diag.input_calls++;
    cf_ui_diag.input_ms = now;
}
static void cf_ui_control_mark(uint32_t control, int32_t delta, uint32_t now)
{
    if (control & 256u) cf_ui_diag.encoders++;
    else cf_ui_diag.buttons++;
    cf_ui_diag.last_control = control;
    cf_ui_diag.control_delta = delta;
    cf_ui_diag.control_ms = now;
}
#define CFU_MARK(phase) cf_ui_mark((phase), fm1_ms)
#define CFU_INPUT() cf_ui_input_mark(fm1_ms)
#define CFU_BUTTON(b) cf_ui_control_mark((b), 1, fm1_ms)
#define CFU_ENCODER(k, d) cf_ui_control_mark(256u + (k), (d), fm1_ms)
#else
#define CFU_MARK(phase) ((void)0)
#define CFU_INPUT() ((void)0)
#define CFU_BUTTON(b) ((void)0)
#define CFU_ENCODER(k, d) ((void)0)
#endif
#endif
