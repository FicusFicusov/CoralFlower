/* SPDX-License-Identifier: GPL-3.0-only */
/* CoralFlower: both cores render independent FM/VA voices. The chord job also
 * renders the drum part, balancing its cost against the voices kept on core0.
 * Parts are mixed in their original order after the join; FX stay sequential.
 * No job survives into a main-loop NOR slice. All admission remains shared. */
#pragma once
#define CF_UI_RESERVE_PERCENT 18u
/* Early tentative declaration; audio.c also supports standalone timing tests. */
static volatile uint32_t t5_nested_ticks;

typedef struct { voice_t *v; vmod_t m; } cf_c1_voice_t;
static struct {
    track_t *t;
    cf_c1_voice_t item[NVOICE];
    int32_t mid[CTL], side[CTL];
    int32_t drum_mid[CTL];
    uint32_t drum_nr, drum_ticks, drum_total, bundle_total;
    uint32_t units0, units1, unit_ticks, drum_estimate, estimate_mark;
    uint32_t count, first, frames, side_on, busy_ticks, progress;
    uint32_t saved_ticks, irq_us, last_wall_us, last_serial_us, wall_total, serial_total;
    uint32_t jobs, voices0, voices1, faults, busy_total, wait_total, wait_max, main_total;
    uint8_t inflight;
    uint8_t drum_armed, drum_job, drum_ready;
} cf_c1 __attribute__((section(".pool")));

/* Defined after track_render_x. The drum part has private LFO/noise state and
 * cannot enter the FM/VA dispatcher. Core0 prepares its modulation first. */
static void cf_c1_drum_render(void);

/* Shared by dispatch and admission for the next block. A zero last drum price
 * can mean that the previous block was serial, not that the next job is. */
static int cf_c1_drum_allowed(uint32_t frames)
{
    return DRUM_PART0 >= LFO_SHARED_RNG && frames <= CTL &&
        ENGINES[trk[DRUM_PART0].engine] == &ENG_DRUM &&
        !(trk[DRUM_PART0].xf_on && trk[DRUM_PART0].xf);
}

/* Consistent core0 CPU time: TIMER5 work is charged separately to the budget. */
static uint32_t cf_c1_cpu_ticks(void)
{
    uint32_t nested, now;
    do { nested = t5_nested_ticks; now = fm1_ticks(); } while (nested != t5_nested_ticks);
    return now - nested;
}
static uint32_t cf_c1_worker_ticks(void)
{
#ifdef FM1_HAVE_CPU1
    return fm1_ticks();
#else
    /* The ordinary emulator runs jobs inline. It must not invent time saved. */
    return 0;
#endif
}

static uint32_t cf_c1_voice(uint32_t i, uint32_t core, int32_t *mid, int32_t *side)
{
    cf_c1_voice_t *a = &cf_c1.item[i];
    if (ENGINES[cf_c1.t->engine] == &ENG_FM6) {
        fm6_render_prepared(cf_c1.t, a->v, mid, cf_c1.frames, &a->m, core);
        return 0;
    }
    return (uint32_t)va_render(cf_c1.t, a->v, mid, side, cf_c1.frames, &a->m);
}

static void cf_c1_voice_job(uint32_t unused)
{
    uint32_t i, start = cf_c1_worker_ticks();
    (void)unused;
    cf_c1.side_on = cf_c1.progress = 0;
    for (i = 0; i < cf_c1.frames; i++) cf_c1.mid[i] = cf_c1.side[i] = 0;
    for (i = 0; i < cf_c1.first; i++) {
        cf_c1.side_on |= cf_c1_voice(i, 1, cf_c1.mid, cf_c1.side);
        cf_c1.progress = i + 1u;
    }
    cf_c1.busy_ticks = cf_c1_worker_ticks() - start;
    start = cf_c1_worker_ticks();
    if (cf_c1.drum_job) cf_c1_drum_render();
    cf_c1.drum_ticks = cf_c1_worker_ticks() - start;
}

/* A contiguous split preserves voice order within each integer accumulator.
 * FM estimates use the already prepared operator plan; VA voices share a patch. */
static uint32_t cf_c1_partition(void)
{
    uint32_t i, k, total = 0, left = 0, best = 0, best_left = 0, imbalance = ~0u, cost[NVOICE];
    int fm = ENGINES[cf_c1.t->engine] == &ENG_FM6;
    for (i = 0; i < cf_c1.count; i++) {
        cost[i] = 1;
        if (fm) {
            const fm6_voice_t *s = fm6_state(cf_c1.t, cf_c1.item[i].v);
            for (k = 0; k < 6u; k++) cost[i] += (s->plan[k] & FM6_P_RUN) != 0;
        }
        total += cost[i];
    }
    /* Convert the last measured drum work into voice-work units. Until there
     * is a target measurement, reserve two average voices for it. Zero voices
     * on core1 is valid when drums alone balance the chord on core0. */
    uint32_t extra = !cf_c1.drum_job ? 0u : cf_c1.unit_ticks ?
        (cf_c1.drum_estimate + cf_c1.unit_ticks - 1u) / cf_c1.unit_ticks :
        2u * total / cf_c1.count;
    for (i = cf_c1.drum_job ? 0u : 1u; i < cf_c1.count; i++) {
        uint32_t d;
        if (i) left += cost[i - 1u];
        d = left + extra > total - left ? 2u * left + extra - total : total - 2u * left - extra;
        /* Equal loads favour core0: it does not pay the worker's start delay. */
        if (d < imbalance) { imbalance = d; best = i; best_left = left; }
    }
    cf_c1.units0 = total - best_left;
    cf_c1.units1 = best_left;
    return best;
}

static uint32_t cf_c1_cost_update(uint32_t old, uint32_t measured)
{
    return measured > old ? measured : old - (old - measured) / 8u;
}

static void cf_c1_render(int32_t *mid, int32_t *side, uint8_t *side_on)
{
    uint32_t i, main_start, main_ticks, wait_start, waited;
    int posted = 0;
    cf_c1.drum_job = cf_c1.drum_armed && cf_c1.t == &trk[0] && cf_c1.count >= 2u;
    if (cf_c1.t == &trk[0]) {
        uint32_t mark = cf_c1.t->engine;
        if (ENGINES[cf_c1.t->engine] == &ENG_VA) mark |= va_cost(cf_c1.t) << 8;
        if (mark != cf_c1.estimate_mark) {
            cf_c1.unit_ticks = cf_c1.drum_estimate = 0;
            cf_c1.estimate_mark = mark;
        }
    }
    cf_c1.first = cf_c1_partition();
    if ((cf_c1.first || cf_c1.drum_job) && cr_c1_check()) {
        if (cf_c1.drum_job) {
            mod_begin(&trk[DRUM_PART0]);
            cf_c1.drum_nr = 0;
        }
        cf_c1.inflight = 1;
        posted = cr_c1_run(cf_c1_voice_job, 0);
        if (!posted) {
            cf_c1.inflight = 0;
            if (cf_c1.drum_job && MODP(&trk[DRUM_PART0]).on) mod_end(&trk[DRUM_PART0]);
        } else if (cf_c1.drum_job) {
            /* Even a failed job may have advanced drums. Never render again
             * in this block; the fault path discards its untrusted output. */
            cf_c1.drum_ready = 1;
            cf_c1.drum_armed = 0;
        }
    }
    if (!posted) cf_c1.first = cf_c1.drum_job = 0;
    main_start = cf_c1_cpu_ticks();
    for (i = cf_c1.first; i < cf_c1.count; i++)
        *side_on |= (uint8_t)cf_c1_voice(i, 0, mid, side);
    main_ticks = cf_c1_cpu_ticks() - main_start;
    cf_c1.main_total += main_ticks;
    cf_c1.voices0 += cf_c1.count - cf_c1.first;
    if (!posted) return;
    wait_start = cf_c1_cpu_ticks();
    if (cr_c1_wait()) {
        /* The worker is held before returning. Its partly advanced states must
         * never be rendered twice. Omit its untrusted output for this block;
         * those same voices continue serially next block, with a gain ramp. */
        cf_c1.inflight = 0;
        cf_c1.faults++;
        if (cf_c1.drum_job) {
            for (i = 0; i < cf_c1.frames; i++) cf_c1.drum_mid[i] = 0;
            cf_c1.drum_nr = 0;
            for (i = 0; i < NVOICE; i++) trk[DRUM_PART0].v[i].env_out = 0;
        }
        for (i = 0; i < cf_c1.first; i++) {
            voice_t *v = cf_c1.item[i].v;
            v->env_out = 0;
            /* VA ramps its own final envelope/velocity gain, not m.amp0. */
            if (ENGINES[cf_c1.t->engine] == &ENG_VA) v->s[7] = 0;
        }
        return;
    }
    waited = cf_c1_cpu_ticks() - wait_start;
    cf_c1.inflight = 0;
    cf_c1.jobs++;
    cr_dbg.c1_jobs++;
    cf_c1.voices1 += cf_c1.first;
    cf_c1.busy_total += cf_c1.busy_ticks;
    if (cf_c1.drum_job) {
        uint32_t main = cf_c1.units0 ? (main_ticks + cf_c1.units0 - 1u) / cf_c1.units0 : 0;
        uint32_t worker = cf_c1.units1 ? (cf_c1.busy_ticks + cf_c1.units1 - 1u) / cf_c1.units1 : 0;
        cf_c1.unit_ticks = cf_c1_cost_update(cf_c1.unit_ticks, main > worker ? main : worker);
        cf_c1.drum_estimate = cf_c1_cost_update(cf_c1.drum_estimate, cf_c1.drum_ticks);
        cf_c1.busy_total += cf_c1.drum_ticks;
        cf_c1.drum_total += cf_c1.drum_ticks;
        cf_c1.bundle_total++;
    }
    cf_c1.wait_total += waited;
    if (waited > cf_c1.wait_max) cf_c1.wait_max = waited;
#ifdef FM1_HAVE_CPU1
    /* audio CPU time includes join polling. Add only worker work not already
     * counted there, to retain the serial-equivalent diagnostic alongside actual elapsed time. */
    uint32_t work = cf_c1.busy_ticks + (cf_c1.drum_job ? cf_c1.drum_ticks : 0u);
    if (work > waited) cf_c1.saved_ticks += work - waited;
#endif
    *side_on |= (uint8_t)cf_c1.side_on;
    for (i = 0; i < cf_c1.frames; i++) {
        mid[i] += cf_c1.mid[i];
        if (cf_c1.side_on) side[i] += cf_c1.side[i];
    }
}
