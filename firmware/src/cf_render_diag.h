/* SPDX-License-Identifier: GPL-3.0-only */
/* Passive HOME profile. All totals wrap modulo 2^32; read pairs <=10 s apart.
 * The update clock is the first changed central-flora tile submitted to LCD,
 * not main-loop iterations or an assumed panel refresh rate. No extra wait,
 * pixel hash, allocation, reset command or persistent write. */
#ifndef CF_RENDER_DIAG_H
#define CF_RENDER_DIAG_H
#include <stdint.h>
#if CF_EVENT_DIAG
#ifndef CFR_CLOCK
#define CFR_CLOCK() fm1_ticks()
#endif
typedef struct { uint32_t t, audio, timer; } cfr_stamp_t;
enum {
    CFR_FRAMES, CFR_UPDATES, CFR_GAPS, CFR_GAP_TICKS, CFR_GAP_MAX,
    CFR_GAP_20, CFR_GAP_33, CFR_GAP_50, CFR_GAP_100, CFR_GAP_LONG,
    CFR_WALL, CFR_CPU, CFR_COMPOSE_WALL, CFR_COMPOSE_CPU,
    CFR_WAIT_WALL, CFR_WAIT_CPU, CFR_WAIT_CALLS, CFR_FRAME_MAX,
    CFR_AUDIO, CFR_TIMER, CFR_STAMPS, CFR_STAMP_MIN, CFR_SCENE_EPOCH, CFR_VALUES
};
static struct {
    volatile uint32_t audio, timer;                /* one writer per IRQ priority */
    uint32_t v[CFR_VALUES], last_update, active, submitted, scene, have_update;
    cfr_stamp_t begin, compose;
} cf_render;
_Static_assert(sizeof cf_render <= 160u, "HOME profile RAM budget");

/* Retry if an IRQ interrupted the tuple. No masking and no shared IRQ RMW. */
static cfr_stamp_t cfr_stamp(void)
{
    cfr_stamp_t s;
    do {
        s.audio = cf_render.audio; s.timer = cf_render.timer; s.t = CFR_CLOCK();
    } while (s.audio != cf_render.audio || s.timer != cf_render.timer);
    cf_render.v[CFR_STAMPS]++;
    return s;
}
static uint32_t cfr_cpu(cfr_stamp_t a, cfr_stamp_t b)
{
    return (b.t - a.t) - (b.audio - a.audio) - (b.timer - a.timer);
}
static void cfr_frame_begin(uint32_t scene)
{
    cf_render.active = scene != 0; cf_render.submitted = 0;
    if (scene != cf_render.scene) {
        cf_render.v[CFR_SCENE_EPOCH]++;
        cf_render.scene = scene; cf_render.have_update = 0;
    }
    if (!scene) return;
    if (!cf_render.v[CFR_STAMP_MIN]) {
        uint32_t best = ~0u;
        for (uint32_t i = 0; i < 16u; i++) {
            cfr_stamp_t a = cfr_stamp(), b = cfr_stamp();
            uint32_t d = cfr_cpu(a, b);
            if (d < best) best = d;
        }
        cf_render.v[CFR_STAMP_MIN] = best ? best : 1u;
    }
    cf_render.begin = cfr_stamp();
}
static void cfr_compose_end(void)
{
    if (cf_render.active) cf_render.compose = cfr_stamp();
}
static void cfr_submitted(void)
{
    if (!cf_render.active || cf_render.submitted) return;
    uint32_t now = CFR_CLOCK();
    cf_render.submitted = 1; cf_render.v[CFR_UPDATES]++;
    if (cf_render.have_update) {
        uint32_t gap = now - cf_render.last_update;
        cf_render.v[CFR_GAPS]++; cf_render.v[CFR_GAP_TICKS] += gap;
        if (gap > cf_render.v[CFR_GAP_MAX]) cf_render.v[CFR_GAP_MAX] = gap;
        uint32_t bin = gap <= 20000u * FM1_TICKS_PER_US ? CFR_GAP_20 :
                       gap <= 33000u * FM1_TICKS_PER_US ? CFR_GAP_33 :
                       gap <= 50000u * FM1_TICKS_PER_US ? CFR_GAP_50 :
                       gap <= 100000u * FM1_TICKS_PER_US ? CFR_GAP_100 : CFR_GAP_LONG;
        cf_render.v[bin]++;
    }
    cf_render.last_update = now;
    cf_render.have_update = 1;
}
static void cfr_frame_end(void)
{
    if (!cf_render.active) return;
    cfr_stamp_t end = cfr_stamp();
    uint32_t wall = end.t - cf_render.begin.t;
    cf_render.v[CFR_FRAMES]++; cf_render.v[CFR_WALL] += wall;
    cf_render.v[CFR_CPU] += cfr_cpu(cf_render.begin, end);
    cf_render.v[CFR_COMPOSE_WALL] += cf_render.compose.t - cf_render.begin.t;
    cf_render.v[CFR_COMPOSE_CPU] += cfr_cpu(cf_render.begin, cf_render.compose);
    cf_render.v[CFR_AUDIO] += end.audio - cf_render.begin.audio;
    cf_render.v[CFR_TIMER] += end.timer - cf_render.begin.timer;
    if (wall > cf_render.v[CFR_FRAME_MAX]) cf_render.v[CFR_FRAME_MAX] = wall;
    cf_render.active = 0;
}
static void cfr_wait_end(cfr_stamp_t start)
{
    if (!cf_render.active) return;
    cfr_stamp_t end = cfr_stamp();
    cf_render.v[CFR_WAIT_CALLS]++;
    cf_render.v[CFR_WAIT_WALL] += end.t - start.t;
    cf_render.v[CFR_WAIT_CPU] += cfr_cpu(start, end);
}
/* Audio can be preempted by TIMER5. Its contribution must exclude that IRQ. */
static void cfr_audio_end(uint32_t start, uint32_t timer_start)
{
    uint32_t timer, end;
    do { timer = cf_render.timer; end = CFR_CLOCK(); } while (timer != cf_render.timer);
    cf_render.audio += (end - start) - (timer - timer_start);
}
#define CFR_FRAME_BEGIN(home) cfr_frame_begin(home)
#define CFR_COMPOSE_END() cfr_compose_end()
#define CFR_SUBMITTED() cfr_submitted()
#define CFR_FRAME_END() cfr_frame_end()
#define CFR_WAIT_BEGIN() cfr_stamp_t cfr_wait_ = {0}; if (cf_render.active) cfr_wait_ = cfr_stamp()
#define CFR_WAIT_END() cfr_wait_end(cfr_wait_)
#define CFR_AUDIO_BEGIN() uint32_t cfr_a_timer_, cfr_a_start_; \
    do { cfr_a_timer_ = cf_render.timer; cfr_a_start_ = CFR_CLOCK(); } while (cfr_a_timer_ != cf_render.timer)
#define CFR_AUDIO_END() cfr_audio_end(cfr_a_start_, cfr_a_timer_)
#define CFR_TIMER_END(start) (cf_render.timer += CFR_CLOCK() - (start))
#else
#define CFR_FRAME_BEGIN(home) ((void)0)
#define CFR_COMPOSE_END() ((void)0)
#define CFR_SUBMITTED() ((void)0)
#define CFR_FRAME_END() ((void)0)
#define CFR_WAIT_BEGIN() ((void)0)
#define CFR_WAIT_END() ((void)0)
#define CFR_AUDIO_BEGIN() ((void)0)
#define CFR_AUDIO_END() ((void)0)
#define CFR_TIMER_END(start) ((void)0)
#endif
#endif
