/* SPDX-License-Identifier: GPL-3.0-only */
/* MIDI 1.0 transport: FA/FB arm, the next F8 plays; FC preserves position.
 * F8 anchors every fourth 96-PPQN tick. The intervening three ticks are spread
 * over the measured period, never extrapolated past the next missing pulse.
 * All players and the metronome consume those same ticks. A late source can
 * delay the next boundary, but cannot cause accumulated phase/tempo drift. */
#define CF_CLOCK_LOST_MS 300u

static void cf_clock_steps(uint32_t ticks)
{
    uint32_t gen = cf_grid.gen;
    sqp_tick(0, ticks);
    dmp_tick(0, ticks);
    /* A count-in ending in sqp_tick has already begun a new downbeat. */
    cfm_tick(0, cf_grid.gen == gen ? ticks : 0u);
    cfa_tick(0);
    sqp_arp_tick(0);
}

static void cf_clock_play(void)
{
    uint32_t target = cft_target, a = target >> 8 & 255u;
    if ((target & 3u) == 1u)
        sqp_start(a, 0);                         /* MIDI Start is the beginning of the song, not the UI cursor */
    else if ((target & 3u) == 2u) {
        cfm.beats = sq_beats = DM_RHY[a % DM_N].beats;
        dmp_start(a, target >> 16 & 255u, target >> 24);
    } else {
        cfm.running = 0;
        cfm_restart();
        cfm.running = 1;
    }
}

static void cf_clock_quiet(void)
{
    sqp_chord_off(); sqp_bass_off(); sqp_layer_off();
    sqp_arp_off(SQ_CHORDS); sqp_arp_off(SQ_LAYER);
    sqp_drum_offs();
    cfa_note_off(); cfa_strum_off();
}

static void cf_clock_stop(void)
{
    if (!cfm.running)
        return;
    cfx_in.resume = sqp.on ? 1u : dmp.on ? 2u : 3u;
    sqp_stop(); dmp_stop();
    cfa_note_off(); cfa_strum_off();
    cfm.running = 0;
    cfx_in.wait = 0;
    cfm.click = 0;
}

static void cf_clock_continue(void)
{
    if (cfm.running)
        return;                                 /* redundant Start/Continue are ignored (MIDI 1.0, p32) */
    if (!cfx_in.resume) {
        cf_clock_play();
        return;
    }
    sqp.on = cfx_in.resume == 1u;
    dmp.on = cfx_in.resume == 2u;
    cfm.running = 1;
    cfx_in.wait = cfx_in.located ? 3u : 2u;
}

/* Locate without sounding intervening notes or writing a take. SPP counts
 * six MIDI clocks (=24 internal ticks). Songs loop over their nonempty slots,
 * just as sqp_next_bar does; each track keeps its own bar length. */
static void cf_clock_locate(uint32_t spp)
{
    uint32_t ticks = spp * 24u, bars, t;
    cf_clock_play();
    bars = ticks / sqp_bar_ticks();
    t = ticks % sqp_bar_ticks();
    cf_clock_quiet();
    if (sqp.on) {
        if (sqp.song && sq_st.song.n) {
            uint32_t total = 0, i;
            for (i = 0; i < sq_st.song.n; i++) {
                const sq_pat_t *p = &sq_st.pat[sq_st.song.pat[i] % SQ_NPAT];
                if (!sq_empty(p)) total += sq_len(p);
            }
            if (total) {
                bars %= total;
                for (i = 0; i < sq_st.song.n; i++) {
                    const sq_pat_t *p = &sq_st.pat[sq_st.song.pat[i] % SQ_NPAT];
                    if (sq_empty(p)) continue;
                    if (bars < sq_len(p)) {
                        sqp.slot = (uint8_t)i;
                        sqp.pat = sq_st.song.pat[i] % SQ_NPAT;
                        break;
                    }
                    bars -= sq_len(p);
                }
            }
        }
        sqp.nbar = bars;
        sqp.bar = (uint8_t)(bars % sq_len(&sq_st.pat[sqp.pat]));
        sqp_bars(); cf_stage_in(sqp.pat);
        sqp.t = t;
        sqp.nu = (uint8_t)((t + 3u) / 4u);
        sqp.nh = 0;
        while (sqp.nh < sq_dsteps() && sqp_drum_tick(&sq_st.pat[sqp.pat], sqp.nh) < t) sqp.nh++;
        sqp.on = 0;
        cfx_in.resume = 1;
    } else if (dmp.on) {
        uint32_t len = DM_RHY[dmp.r].bars;
        dmp.nbar = bars;
        dmp.bar = (uint8_t)(bars % (len ? len : 1u));
        dmp.t = t;
        dmp.nh = 0;
        while (dmp.nh < sq_dsteps() && dmp_tick_of(dmp.nh) < t) dmp.nh++;
        dmp.on = 0;
        cfx_in.resume = 2;
    } else cfx_in.resume = 3;
    cfm.running = 0;
    cfx_in.wait = 0;
    cfx_in.located = 1;
    cfx_in.sub = 0;
    cfx_in.metro_sub = (uint8_t)(ticks % 96u);
    cfm.acc = cfx_in.metro_sub * (FS * 60u) / 96u;
    cfm.beats_done = ticks / 96u + 1u;
    cfm.beat = (uint8_t)(cfm.beats_done % cfm.beats);
    cf_grid.g24 = cfm.beats_done * 24u + cfx_in.metro_sub / 4u;
}

static void cf_clock_event(uint32_t st, uint32_t value, uint32_t ms, uint32_t source)
{
    uint32_t gap = ms - cfx_in.last_ms;
    if (!cfp_cfg.clock_in || (st != 0xF8u && st != 0xFAu && st != 0xFBu && st != 0xFCu && st != 0xF2u))
        return;
    /* One clock source at a time; two connected masters must not double tempo.
     * Channel notes still pass from both. A silent source releases the lease. */
    if (cfx_in.source && source != cfx_in.source && gap < CF_CLOCK_LOST_MS)
        return;
    if (source != cfx_in.source) {
        crm_clock_reset(&cfx_in.f);
        cfx_in.source = (uint8_t)source;
        cfx_in.seen = 0;
        cfx_in.last_ms = ms;
    }
    if (st == 0xFAu) {
        if (!cfm.running || cfx_in.lost) cf_clock_play();
    } else if (st == 0xFBu) cf_clock_continue();
    else if (st == 0xFCu) cf_clock_stop();
    else if (st == 0xF2u) {
        if (!cfm.running) cf_clock_locate(value);
    } else {
        int b = crm_clock_pulse(&cfx_in.f, ms);
        if (b) cfm.bpm = cfx_in.bpm = (uint16_t)b;
        if (cfx_in.f.bpm10)
            cfx_in.period = (FS * 60u * 10u / 24u) / cfx_in.f.bpm10;
        else if (cfx_in.seen && gap >= 6u && gap <= 150u)
            cfx_in.period = gap * FS / 1000u;
        cfx_in.last_ms = ms;
        cfx_in.seen = cfx_in.active = cfx_in.fresh = 1;
        cfx_in.lost = 0;
        cfx_in.elapsed = 0;
        if (cfm.running) {
            uint32_t wait = cfx_in.wait;
            cfx_in.wait = 0;
            if (wait == 1u || wait == 3u) {
                cfm_tick(0, 0);                  /* the arp's first note sees the new downbeat, not the old grid */
                if (sqp.on && !sqp.cin) sqp_now();
                if (dmp.on) dmp_now();
                if (wait == 3u && !cfx_in.metro_sub) {
                    cfm.click = (cfm.beats_done - 1u) % cfm.beats ? 1u : 2u;
                    cfm.beat_ms = cf_now_ms;
                }
                cf_clock_steps(0);
            } else cf_clock_steps(4u - cfx_in.sub);
            cfx_in.sub = 0;
            cfx_in.located = 0;
        }
    }
}

static void cf_clock_mode(void)
{
    if (cfx_in.enabled == cfp_cfg.clock_in)
        return;
    cfx_in.enabled = cfp_cfg.clock_in;
    cfx_in.active = cfx_in.source = cfx_in.seen = cfx_in.lost = 0;
    cfx_in.last_ms = fm1_ms;                     /* timeout also covers In selected before any source arrives */
    cfx_in.bpm = 0;
    crm_clock_reset(&cfx_in.f);
    cfx_in.period = FS * 60u / (24u * cf_tempo());
    if (cfp_cfg.clock_in) {
        if (cfm.running && !cfx_in.wait) {
            cfx_in.sub = (uint8_t)((cfm.acc * 96u / (FS * 60u)) % 4u);
            cfx_in.metro_sub = (uint8_t)(cfm.acc * 96u / (FS * 60u));
            cfx_in.wait = 2;
        }
        cf_grid.on = 1;
    } else {
        if (cfx_in.wait == 1u) {
            if (sqp.on && !sqp.cin) sqp_now();
            if (dmp.on) dmp_now();
        }
        cfx_in.wait = cfx_in.resume = cfx_in.located = 0;
        cfm.bpm = (uint16_t)cf_tempo();
        sqp.acc = dmp.acc = cfm.acc * 96u % (FS * 60u);
    }
}

static void cf_clock_tick(uint32_t n)
{
    cfx_in.active = cfx_in.seen && (uint32_t)(fm1_ms - cfx_in.last_ms) < CF_CLOCK_LOST_MS;
    if (!cfx_in.active && !cfx_in.lost &&
        (cfx_in.seen || (cfm.running && cfx_in.wait)) &&
        (uint32_t)(fm1_ms - cfx_in.last_ms) >= CF_CLOCK_LOST_MS) {
        if (cfm.running)
            cf_clock_quiet();                    /* a lost running clock releases notes; stopped DAW may have live ARP */
        cfx_in.lost = 1;
    }
    if (cfx_in.fresh) cfx_in.fresh = 0;
    else if (cfx_in.active && cfm.running && !cfx_in.wait) {
        uint32_t next;
        cfx_in.elapsed += n;
        next = cfx_in.elapsed * 4u / (cfx_in.period ? cfx_in.period : 1u);
        if (next > 3u) next = 3u;
        if (next > cfx_in.sub) {
            cf_clock_steps(next - cfx_in.sub);
            cfx_in.sub = (uint8_t)next;
        }
    }
    cfm_tick(0, 0);
}
