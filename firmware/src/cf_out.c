/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca); CoralFlower contributors */
/* CoralFlower: the audio side's glue (after ChoralRoot's cr_out.c).
 *
 *   part 0 CHORD  <- the chord player (cf_play.c): the chord sound (PRESETS)
 *   part 1 BASS   <- the bass of the chord: the bass sound (SELECT)
 *
 * Everything here runs in the audio ISR: cf_audio_block() is called at the top of every CTL-sample block, before
 * fx.c renders it (the mix_block shim in coralflower.c / emu_firmware.h). It drains the events the UI posted
 * (cf_post: a lock-free single-producer ring), runs the chord player and the metronome, and reads MIDI in.
 * Felucca's sequencer (seq.c and its satellites) is not in the unit: what the kept files call of it is below, as
 * ChoralRoot's cr_out.c provided it (events_block, midi_event, panic_req, motion_guard ...). */

#if !FELUCCA_SEQ
/* ---------------------------------------------- what seq.c provided --- */
static volatile uint8_t panic_req;               /* bit per part: its notes released (a sound load, M-UPGRADE) */
static const uint8_t transport_req = 0;          /* (upreset.c, cr_bank.c: Felucca's transport never runs) */
#define chain_defaults(c) ((void)0)              /* (main.c felucca_init: no song chain) */
#ifdef FM1_INPUT_LAT
static uint32_t kb_out_tick;                     /* (audio.c: seq.c's key latency stamp; nothing reads it here) */
#endif
static uint32_t trk_index(const track_t *t) { return (uint32_t)(t - trk); }
static int drum_track(const track_t *t) { return ENGINES[eng_idx(t->eng_req)] == &ENG_DRUM; }
/* motion.c's: no motion lanes, so a part's sounding value is its saved one; the guard keeps the audio IRQ out while
 * a part's parameters are copied (cr_bank.c fm4_apply), as motion.c's did */
static int16_t motion_base_value(const track_t *t, uint32_t id) { return t->p[id]; }
static uint32_t motion_guard(void)
{
#if defined(FM1_IRQ_TARGET)
    uint32_t f = fm1_icfg();
    fm1_irq_off();
    return f;
#else
    return 0;
#endif
}
static void motion_unguard(uint32_t f)
{
#if defined(FM1_IRQ_TARGET)
    fm1_icfg_set(f);
#else
    (void)f;
#endif
}

/* ------------------------------------------------- the parts' MIDI in --- */
/* cr_midi_in (below) forwards a CHORD / BASS channel's notes, pedal, bend, pressure and the other CCs here as
 * midi_event(status, part, d1, d2), as it handed them to midi_control.c before; what of that ChoralRoot uses:
 *   note on / off       trk_note_on / trk_note_off on the part (a repeated note-on restarts it; velocity 0: off)
 *   CC 64 sustain       a released note is held while the pedal is down, released with it
 *   pitch bend          voice.c's midi_bend_target, +-2 semitones (RPN 0 sets 0..24 semitones, 0..99 cents); FM6 bends by its
 *                       own BEND range (track_t.bend_raw, docs/FM6.md)
 *   CC 1 / 11, pressure mod.c's MODW / EXPR / AT sources (mod_midi)
 *   CC 120              all sound off: the part's voices cut (a short fade), the pedal ignored
 *   CC 121              reset controllers: bend, wheel, pressure, expression, the pedal up (the bend range kept)
 *   CC 123              all notes off: the part's MIDI notes released (the pedal honoured), the part's voices too
 *                       when no MIDI note is held then
 * Felucca's chords (CHRD), its arpeggiator and live recording were the sequencer's: gone. A part's MIDI notes and the
 * engine's stream share its voices exactly as before (a MIDI note-off ends that note, whoever started it). */
#define CR_MPED 0x80u                            /* cr_mnote: the key is up, the pedal holds the note */
typedef struct {
    int16_t bend;                                /* signed 14-bit, 0 = centre */
    uint8_t pedal, semis, cents, rpn_msb, rpn_lsb, ready;
} cr_mch_t;
static cr_mch_t cr_mch[2];                       /* parts 0 / 1 (CHORD / BASS) */
static uint8_t cr_mnote[2][128];                 /* 1: a MIDI note-on holds it; | CR_MPED: the pedal does */
static uint8_t cr_mheld[2];                      /* notes cr_mnote holds per part (CC 123's check), at most 128 */

static cr_mch_t *cr_mchan(uint32_t p)
{
    cr_mch_t *c = &cr_mch[p];
    if (!c->ready) {
        c->semis = 2;
        c->rpn_msb = c->rpn_lsb = 127;
        c->ready = 1;
    }
    return c;
}
static void cr_mbend(uint32_t p)                 /* the channel's bend -> the part's (voice.c glides to it) */
{
    const cr_mch_t *c = cr_mchan(p);
    int32_t range = ((int32_t)c->semis * 100 + c->cents) * 256 / 100;
    midi_bend_target[p] = (int32_t)c->bend * range / (c->bend < 0 ? 8192 : 8191);
    trk[p].bend_raw = c->bend;                   /* (FM6 bends by its own BEND range: its patch's, docs/FM6.md) */
}
static void cr_mrelease(uint32_t p, uint32_t note)
{
    if (cr_mnote[p][note]) {
        cr_mnote[p][note] = 0;
        if (cr_mheld[p])
            cr_mheld[p]--;
        trk_note_off(&trk[p], note);
    }
}
static void cr_mpedal_up(uint32_t p)
{
    uint32_t n;
    cr_mchan(p)->pedal = 0;
    for (n = 0; n < 128u; n++)
        if (cr_mnote[p][n] & CR_MPED)
            cr_mrelease(p, n);
}
static void cr_mforget(uint32_t p)               /* a panic: the part's MIDI notes forgotten (their offs end nothing) */
{
    if (p < 2u) {
        memset(cr_mnote[p], 0, sizeof cr_mnote[p]);
        cr_mheld[p] = 0;
    }
    midi_bend_q8[p] = midi_bend_target[p] = 0;
    trk[p].bend_raw = 0;
}
static void cr_mcontrol(uint32_t p, uint32_t cc, uint32_t v)
{
    cr_mch_t *c = cr_mchan(p);
    track_t *t = &trk[p];
    uint32_t n;
    switch (cc) {
    case 64:
        if (v >= 64u)
            c->pedal = 1;
        else
            cr_mpedal_up(p);
        break;
    case 120:                                    /* all sound off: ignores the pedal */
        trk_all_off(t);
        for (n = 0; n < NVOICE; n++)
            if (t->v[n].active)
                voice_kill(&t->v[n]);
        cr_mforget(p);
        break;
    case 121:                                    /* reset all controllers (mod_midi: wheel, pressure, expression) */
        c->bend = 0;
        c->rpn_msb = c->rpn_lsb = 127;
        cr_mbend(p);
        cr_mpedal_up(p);
        break;
    case 123:                                    /* all notes off: normal releases, the pedal honoured */
        for (n = 0; n < 128u; n++)
            if (cr_mnote[p][n] && !(cr_mnote[p][n] & CR_MPED)) {
                if (c->pedal)
                    cr_mnote[p][n] |= CR_MPED;
                else
                    cr_mrelease(p, n);
            }
        if (!cr_mheld[p])
            trk_all_off(t);
        break;
    case 101: c->rpn_msb = (uint8_t)v; break;
    case 100: c->rpn_lsb = (uint8_t)v; break;
    case 99: case 98: c->rpn_msb = c->rpn_lsb = 127; break;   /* NRPN selected: no RPN data entry */
    case 6: case 38:
        if (!c->rpn_msb && !c->rpn_lsb) {        /* RPN 0, pitch bend sensitivity */
            if (cc == 6u)
                c->semis = (uint8_t)(v > 24u ? 24u : v);
            else
                c->cents = (uint8_t)(v > 99u ? 99u : v);
            cr_mbend(p);
        }
        break;
    default: break;
    }
}
/* st: the status' high nibble (0x80..0xE0); p: the part 0 / 1 (cr_midi.c crm_map) */
static void __attribute__((noinline)) midi_event(uint32_t st, uint32_t p, uint32_t d1, uint32_t d2)
{
    if (p >= 2u)
        return;
    if (st == 0x90u && d2) {
        if (cr_mnote[p][d1])                     /* a repeated note replaces the previous press (a pedal-held one too) */
            cr_mrelease(p, d1);
        cr_mbend(p);
        trk_note_on(&trk[p], d1, d2);
        cr_mnote[p][d1] = 1;
        if (cr_mheld[p] < 128u)
            cr_mheld[p]++;
    } else if (st == 0x80u || st == 0x90u) {
        if (cr_mnote[p][d1] && cr_mchan(p)->pedal)
            cr_mnote[p][d1] |= CR_MPED;
        else
            cr_mrelease(p, d1);
    } else if (st == 0xE0u) {
        cr_mchan(p)->bend = (int16_t)((int32_t)(d1 | d2 << 7) - 8192);
        cr_mbend(p);
    } else if (st == 0xB0u) {
        mod_midi(&trk[p], st, d1, d2);
        cr_mcontrol(p, d1, d2);
    } else if (st == 0xD0u) {
        mod_midi(&trk[p], st, d1, d2);
    }
}

/* the keys' chord and bass still held (HOLD, or fingers) after their part's notes were released for another sound:
 * struck again on it (the owner, 2026-10-21: "Играет дальше новым пэдом"). At the bar line a pattern's sound comes in
 * (cf_sound.c cf_stage_in); stopped, a load is now (PRESETS, another pattern opened: panic_req, below). An arpeggio
 * strikes its next note itself */
static void cf_restrike(uint32_t part)
{
    track_t *t = &trk[part];
    uint32_t i;
    if (cfa.on)
        return;
    if (part == cfp_cp)
        for (i = 0; i < cfp.n; i++)
            trk_note_on(t, cfp.note[i], cfp_cfg.vel);
    if (part == 1u)
        for (i = 0; i < cfp.nb; i++)
            trk_note_on(t, cfp.bass[i], cfp_cfg.vel);
}

/* fx.c's, between two rendered blocks (after cr_audio_block): the releases asked for, the engine switches. A MIDI in
 * queue that overflowed (a lost note-off) is dropped and every part released: no stuck note */
static void events_block(uint32_t n)
{
    uint32_t i, pr = panic_req;
    (void)n;
    panic_req = 0;
    if (midi_in_overflow) {
        mi_r = mi_w;
        pr |= (1u << NTRK) - 1u;
        RING_PUBLISH();
        midi_in_overflow = 0;
    }
    for (i = 0; i < NTRK; i++) {
        if ((pr >> i) & 1u) {
            if (i < 2u)
                cr_mchan(i)->pedal = 0;
            cr_mforget(i);
            trk_all_off(&trk[i]);
            cf_restrike(i);                      /* (the keys' held notes on the new sound) */
        }
        engine_block(&trk[i]);                   /* engine switch: fade, then switch (voice.c) */
    }
}
#endif /* !FELUCCA_SEQ */

/* ------------------------------------------------------------ the queue --- */
/* UI -> ISR. One producer (the main loop), one consumer (the audio ISR): the index stores are the publication */
enum {
    CFE_KEY,          /* a: key 0..26, v: down */
    CFE_CONFIG,       /* the chord settings: cfc_shadow (cf_play.c) */
    CFE_PANIC,
    CFE_METRO,        /* a: on, b: level 0..100 */
    CFE_TEMPO,        /* v: BPM 30..300 */
    CFE_SIG,          /* v: beats a bar 1..12 */
    CFE_TRANSPORT,    /* v: 1 start (from beat 1), 0 stop */
    CFE_SEQ,          /* the sequencer: a 0 stop, 1 play pattern b (SQ_NPAT: the song from place v), 2 drum b heard (kit v),
                       * 3 pattern b next (the song from place v), 4 a take on pattern b, track v, 5 the take ends,
                       * 6 a take from HOME on pattern b, track v (8 bars at most) */
    CFE_ROOT,         /* a root from outside (NOTES IN Roots): a its MIDI note, b its age in ms, v down */
};
typedef struct { uint8_t op, a, b, rsv; int16_t v; } cf_ev_in_t;   /* (CFE_KEY: a key, b its age in ms, v down) */
#define CF_EVQ 128u                              /* a power of two */
static cf_ev_in_t cf_evq[CF_EVQ];
static volatile uint32_t cf_evq_w, cf_evq_r;
static volatile uint32_t cf_evq_lost;

static int cf_post(uint32_t op, uint32_t a, uint32_t b, int32_t v)
{
    cf_ev_in_t *e;
    if (cf_evq_w - cf_evq_r >= CF_EVQ) {
        cf_evq_lost++;
        return 0;
    }
    e = &cf_evq[cf_evq_w % CF_EVQ];
    e->op = (uint8_t)op;
    e->a = (uint8_t)a;
    e->b = (uint8_t)b;
    e->v = (int16_t)v;
    RING_PUBLISH();
    cf_evq_w++;
    return 1;
}

/* -------------------------------------------------------- the metronome --- */
/* the beat clock runs while the transport plays (PLAY: from beat 1) or the metronome screen is open; the click is
 * a decaying sine (ChoralRoot's cr_click_mix): 1.5 kHz on beat 1, 1 kHz on the others */
static struct {
    uint8_t on;                  /* the click sounds (SEQ open, recording, or the transport running) */
    uint8_t running;             /* the transport */
    uint8_t level;               /* 0..100 */
    uint8_t beats;               /* a bar */
    uint16_t bpm;
    uint32_t acc;                /* samples x bpm since the last beat, against FS x 60 */
    uint8_t beat;                /* the beat about to come (0 = the downbeat) */
    uint8_t click;               /* 1 a beat, 2 the downbeat: set for the mix */
    volatile uint32_t beats_done;
    uint32_t beat_ms;            /* the engine clock at the last beat */
} cfm = {0, 0, 0, 4, 120, 0, 0, 0, 0, 0};

#include "cr_midi.c"                             /* (MIDI in's pure part: the clock's follower, the channels' map) */
/* MIDI clock in (GLO > MIDI > CLOCK IN, cf_midi_in): while its pulses come, they run the beat (a pulse is 1/24 of
 * one) instead of the tempo, so the metronome, the arpeggiator and the sequencer follow the other instrument */
static struct {
    crm_clock_t f;               /* (cr_midi.c's tempo follower) */
    uint32_t pulses;             /* come since the last block */
    uint32_t last_ms;            /* the last one (cf_now_ms) */
    uint8_t active;              /* pulses within the last 300 ms */
    uint16_t bpm;                /* the tempo they make (0: none yet) */
} cfx_in;

static void cfm_tick(uint32_t n, uint32_t pulses)
{
    uint32_t k;
    if (!cfm.on && !cfm.running) {
        cf_grid.on = 0;
        return;
    }
    if (!cfm.acc && !cfm.beat && !cfm.beats_done) {
        cfm.click = 2;                           /* the first beat right away */
        cfm.beat = 1 % cfm.beats;
        cfm.beats_done++;
        cfm.beat_ms = cf_now_ms;
    }
    if (cfx_in.active) {                         /* the clock's pulses: FS x 60 / 24 each */
        cfm.acc += pulses * (FS * 60u / 24u);
    } else {
        cfm.acc += n * cfm.bpm;
    }
    for (k = 0; k < 4u && cfm.acc >= FS * 60u; k++) {
        cfm.acc -= FS * 60u;
        cfm.click = cfm.beat == 0 ? 2u : 1u;
        cfm.beat = (uint8_t)((cfm.beat + 1u) % cfm.beats);
        cfm.beats_done++;
        cfm.beat_ms = cf_now_ms;
#ifdef CF_TRACE
        fprintf(stderr, "%6u beat %u\n", (unsigned)cf_now_ms, (unsigned)cfm.beats_done);
#endif
    }
    cf_grid.on = 1;                              /* (the arpeggiator's lock: where the beat is, in 1/24 beats) */
    cf_grid.g24 = cfm.beats_done * 24u + cfm.acc / (FS * 60u / 24u);   /* (FS x 60 / 24 is whole: acc x 24 / (FS x 60)
                                                                          * without a 64-bit division, which the
                                                                          * device has none of) */
}
/* MIDI clock out (CLOCK Out): while the beat runs (PLAY, the sequencer), 24 pulses a beat on the metronome's
 * own grid (cf_grid.g24: a beat's first pulse is its downbeat), a start when it begins, a stop when it ends. USB MIDI
 * CIN 0x0F: a single-byte system real-time message (as ChoralRoot's cr_out.c sent them) */
static struct { uint8_t was; uint32_t g24, gen; } cfo;
static void cf_clock_out(void)
{
    int run = cfp_cfg.clock_out && cfp_cfg.midi_out && cfm.running && !cfx_in.active;
    if (run != cfo.was) {
        cfo.was = (uint8_t)run;
        midi_out_event(0x0Fu | (run ? 0xFAu : 0xFCu) << 8);
        cfo.g24 = cf_grid.g24;
        cfo.gen = cf_grid.gen;
        if (run)
            midi_out_event(0x0Fu | 0xF8u << 8);  /* (the downbeat's pulse with the start) */
        return;
    }
    if (!run)
        return;
    if (cfo.gen != cf_grid.gen) {                /* the beat started again from 1 (a new take, the sequencer's PLAY):
                                                  * start again with it */
        cfo.gen = cf_grid.gen;
        cfo.g24 = cf_grid.g24;
        midi_out_event(0x0Fu | 0xFAu << 8);
        midi_out_event(0x0Fu | 0xF8u << 8);
        return;
    }
    while (cfo.g24 < cf_grid.g24) {
        cfo.g24++;
        midi_out_event(0x0Fu | 0xF8u << 8);
    }
}

static void cfm_restart(void)
{
    cfm.acc = 0;
    cfm.beat = 0;
    cfm.beats_done = 0;
    cf_grid.gen++;
}

#include "cf_loop.c"                             /* (the old looper's saved format: read once, cf_sequi.c) */
#include "cf_seqd.c"                             /* (the sequencer's patterns and song) */
#include "cf_seqp.c"                             /* (.. playing them) */

/* the metronome's sounds: a decaying resonator (the accent on the downbeat a different pitch), some with noise.
 * a1 = 2 r cos w and a2 = r^2 (Q14), s = sin w (x 10000) sets the start; len: samples to -60 dB; noise: % */
typedef struct { const char *name; int16_t a1_acc, s_acc, a1, s, a2; uint16_t len; uint8_t noise; } cf_click_t;
static const cf_click_t CF_CLICKS[] = {
    {"Beep", 30673, 3480, 32008, 2074, 16335, 4601, 0},   /* (ChoralRoot's) */
    {"Wood", 32105, 1772, 32364, 1251, 16237, 1531, 0},
    {"Click", 28266, 4907, 29710, 4015, 16058, 687, 35},
    {"Hat", 17511, 8400, 20482, 7729, 15896, 457, 100},
    {"Rim", 31369, 2674, 31605, 2398, 16172, 1059, 30},
};
#define CF_NCLICKS ((uint32_t)NELEM(CF_CLICKS))
static volatile uint8_t cf_click_sound;          /* CF_CLICKS index (the UI's) */
static int32_t cf_ck_y1, cf_ck_y2, cf_ck_a1, cf_ck_a2, cf_ck_amp, cf_ck_ne, cf_ck_nd;
static uint32_t cf_ck_n, cf_ck_rng = 0x1234567u;
static uint8_t cf_ck_noise;
static void cf_click_mix(int32_t *out, uint32_t n)
{
    uint32_t i;
    if (cfm.click) {
        const cf_click_t *k = &CF_CLICKS[cf_click_sound < CF_NCLICKS ? cf_click_sound : 0];
        int32_t lv = sqp.click && !cfm.level ? 100 : (int32_t)cfm.level;   /* (a count-in and a take are
                                                  * always heard: METRONOME's LEVEL never set (0), 100, its beat 1 a
                                                  * little over a usual chord, the median of the 51 sounds' peaks
                                                  * +1 dB: "я бы наоборот чуть громче сделал", the owner, 2026-10-20;
                                                  * a LEVEL the player set is used as it is) */
        int32_t amp = (cfm.on || cfm.running) ? 100 * lv : 0;
        int acc = cfm.click == 2u;
        if (!fx_usb_fixed)                       /* MASTER on it, as on the music: it was mixed in after MASTER, and the
                                                  * volume knob at its bottom left the click as loud as ever (the
                                                  * owner, 2026-10-20: "орёт при записи. Даже когда громкость синта на
                                                  * минимум"). With USB Level Fixed MASTER comes after it (audio.c) */
            amp = amp * (int32_t)song.master_q12 >> 12;
        cf_ck_a1 = acc ? k->a1_acc : k->a1;
        cf_ck_a2 = k->a2;
        cf_ck_y1 = amp * (acc ? k->s_acc : k->s) / 10000;
        cf_ck_y2 = 0;
        cf_ck_n = amp ? k->len : 0u;
        cf_ck_amp = acc ? amp : amp * 3 / 4;
        cf_ck_noise = k->noise;
        cf_ck_ne = cf_ck_amp * 2;                /* the noise's envelope, Q12 steps down over len */
        cf_ck_nd = k->len ? cf_ck_ne / (int32_t)k->len + 1 : cf_ck_ne;
        cfm.click = 0;
    }
    for (i = 0; i < n && cf_ck_n; i++, cf_ck_n--) {
        int32_t y = ((cf_ck_a1 * cf_ck_y1) >> 14) - ((cf_ck_a2 * cf_ck_y2) >> 14), o;
        cf_ck_y2 = cf_ck_y1;
        cf_ck_y1 = y;
        o = y * (100 - cf_ck_noise) / 100;
        if (cf_ck_noise) {
            int32_t w;
            cf_ck_rng ^= cf_ck_rng << 13;
            cf_ck_rng ^= cf_ck_rng >> 17;
            cf_ck_rng ^= cf_ck_rng << 5;
            w = (int32_t)(cf_ck_rng & 0xFFFFu) - 32768;
            o += (int32_t)(((int64_t)w * cf_ck_ne) >> 15) * cf_ck_noise / 100;
            cf_ck_ne = cf_ck_ne > cf_ck_nd ? cf_ck_ne - cf_ck_nd : 0;
        }
        out[2u * i] += o;
        out[2u * i + 1u] += o;
    }
}

/* ------------------------------------------------------------- MIDI in --- */
/* ISR -> UI: the notes an outside keyboard plays when NOTES IN is not Direct (the UI makes them the FM-1's keys, or
 * roots: cf_ui.c cf_ui_pump). One producer (the audio ISR), one consumer (the main loop); a full queue drops a note-on
 * (its note-off then finds nothing down) */
#define CF_XQ 64u                                /* a power of two */
static uint16_t cf_xq[CF_XQ];                    /* note | down << 7 */
static uint32_t cf_xq_ms[CF_XQ];                 /* when it came (fm1_ms) */
static volatile uint32_t cf_xq_w, cf_xq_r;
static void cf_xq_put(uint32_t note, uint32_t down, uint32_t ms)
{
    uint32_t w = cf_xq_w;
    if (w - cf_xq_r >= CF_XQ && down)
        return;
    if (w - cf_xq_r >= CF_XQ)
        cf_xq_r++;                               /* (a note-off never lost: the oldest goes) */
    cf_xq[w % CF_XQ] = (uint16_t)(note | (down ? 0x80u : 0u));
    cf_xq_ms[w % CF_XQ] = ms;
    RING_PUBLISH();
    cf_xq_w = w + 1u;
}

/* usb.c's midi_in_q (USB and TRS) is drained here: notes on the chord / bass channels play the parts directly
 * (cr_midi.c crm_map, ChoralRoot's), with the pedal, bend and controllers (midi_event above) */
static void cf_midi_in(void)
{
    uint32_t w = mi_w;
    uint8_t en[2] = {1, 1}, ch[2];
    ch[0] = cfp_cfg.ch_chord;
    ch[1] = cfp_cfg.ch_bass;
    if (midi_in_overflow)
        return;
    while (mi_r != w) {
        uint32_t at = mi_r % MQ, pkt = midi_in_q[at], status = (pkt >> 8) & 0xFFu;
        uint32_t d1 = (pkt >> 16) & 0x7Fu, d2 = (pkt >> 24) & 0x7Fu;
        mi_r++;
        if (status >= 0xF8u) {                   /* MIDI clock in (CLOCK IN On): the pulses, start, continue, stop */
            if (!cfp_cfg.clock_in)
                continue;
            if (status == 0xF8u) {
                int b = crm_clock_pulse(&cfx_in.f, midi_in_ms[at]);
                if (b) {
                    cfx_in.bpm = (uint16_t)b;
                    cfm.bpm = (uint16_t)b;
                }
                cfx_in.pulses++;
                cfx_in.last_ms = cf_now_ms;
            } else if (status == 0xFAu || status == 0xFBu) {   /* start / continue: from beat 1 */
                cfm_restart();
                cfm.running = 1;
            } else if (status == 0xFCu) {        /* stop */
                cfm.running = 0;
            }
            continue;
        }
        if (cfp_cfg.notes_in != CF_NI_DIRECT && (status & 0xE0u) == 0x80u) {   /* a note on or off, any channel */
            cf_xq_put(d1, (status & 0xF0u) == 0x90u && d2, midi_in_ms[at]);
            continue;
        }
        if (status >= 0x80u && status < 0xF0u) {
            crm_act_t a[2];
            int i, n = crm_map(status, d1, d2, en, ch, a);
            for (i = 0; i < n; i++)
                if (a[i].kind == CRM_FORWARD)
                    midi_event(status & 0xF0u, a[i].part, d1, d2);
        }
    }
}

/* ------------------------------------------------------- the audio side --- */
static volatile uint8_t cf_ready;

static void cf_apply(const cf_ev_in_t *e)
{
    switch (e->op) {
    case CFE_KEY:
        if (e->v)
            sqp.rage = e->b;                     /* (its age, ms: a take puts it back where it was played) */
        cfp_key(e->a, e->v);
        break;
    case CFE_ROOT:
        if (e->v)
            sqp.rage = e->b;
        cfp_key(CF_KEY_EXT + e->a, e->v);
        break;
    case CFE_CONFIG: cfp_config(); break;
    case CFE_PANIC:
        sqp_stop();
        cfp_panic();
        break;
    case CFE_SEQ:
        if (e->a == 1)
            sqp_start(e->b, (uint32_t)e->v);
        else if (e->a == 2)
            sqp_audition(e->b, (uint32_t)e->v);
        else if (e->a == 3)
            sqp_queue(e->b, (uint32_t)e->v);
        else if (e->a == 4)
            sqp_rec_start(e->b, (uint32_t)e->v);
        else if (e->a == 5)
            sqp_rec_stop();
        else if (e->a == 6) {                    /* (a take from HOME: into a new pattern, 8 bars at most) */
            sqp_rec_start(e->b, (uint32_t)e->v);
            sqp.rlim = SQ_BARS;
        }
        else
            sqp_stop();
        break;
    case CFE_METRO:
        if (e->a && !cfm.on && !cfm.running)
            cfm_restart();
        cfm.on = e->a;
        cfm.level = e->b;
        break;
    case CFE_TEMPO: cfm.bpm = (uint16_t)(e->v < 30 ? 30 : e->v > 300 ? 300 : e->v); break;
    case CFE_SIG: cfm.beats = (uint8_t)(e->v < 1 ? 1 : e->v > 12 ? 12 : e->v); break;
    case CFE_TRANSPORT:
        if (e->v)
            cfm_restart();
        cfm.running = (uint8_t)(e->v != 0);
        break;
    default: break;
    }
}

static void cf_out_init(void)                    /* power-on (the audio ISR may already run: IRQ off) */
{
    fm1_irq_off();
    cf_ready = 0;
    if (master_pot_q12)                          /* MASTER: the pot's level, before the first block */
        fx_master_now(master_pot_q12 - 1u);
    voice_fade_steal = 1;                        /* voice.c: a stolen voice fades out, its new note waits a block */
    fx_smooth = 1;                               /* fx.c: the limiter's gain eased, no audio-rate crackle */
    fx_output_set(1u, 1);                        /* fx.c's Clean output (ChoralRoot 0.19): the limiter a safety at
                                                  * -2 dBFS that holds its gain, a knee above, MASTER last; the
                                                  * Classic one (-5 dB, 0.15 s release) pumped under a big reverb */
#if CR_FX
    xf_defaults();                               /* cr_fx.c: the delay's and the reverb's parameters */
#endif
    cfp_init();
    cf_now_ms = cf_ms_rem = 0;
    cf_evq_r = cf_evq_w = 0;
    cf_ready = 1;
    fm1_irq_on();
}

/* the audio ISR, once per CTL-sample block, before the block renders: input, the player, the metronome */
static void cf_audio_block(uint32_t n)
{
    uint32_t w = cf_evq_w;
    if (!cf_ready)
        return;
    while (cf_evq_r != w) {
        cf_apply(&cf_evq[cf_evq_r % CF_EVQ]);
        cf_evq_r++;
    }
    cf_midi_in();
    cf_ms_rem += n * 1000u;
    while (cf_ms_rem >= FS) {
        cf_ms_rem -= FS;
        cf_now_ms++;
    }
    {
        uint32_t pulses = cfx_in.pulses;         /* (MIDI clock in: the pulses come since the last block) */
        cfx_in.pulses = 0;
        cfx_in.active = cfp_cfg.clock_in && cfx_in.bpm && (uint32_t)(cf_now_ms - cfx_in.last_ms) < 300u;
        cfa_tick(n);                             /* (the arpeggiator: its steps, its strum) */
        sqp_arp_tick(n);                         /* (the sequencer's tracks' with ARP on) */
        sqp_tick(n, pulses);                     /* (the sequencer) */
        cfm_tick(n, pulses);
        cf_clock_out();
    }
}

/* ---------------------------------------------------- what the UI reads --- */
typedef struct {
    cf_chord_out_t out;          /* the chord model's answer */
    uint8_t n, note[CF_MAX_NOTES], nb, bass[2];
    uint8_t onset;
    uint8_t beat, metro_on, running;
    uint32_t beats_done, beat_ms, now_ms;
    uint32_t keys_held;          /* bit k: key k is down in the chord model's view */
    uint8_t arp_on, arp_latched, arp_steps, arp_note, arp_n, arp_note_list[CF_MAX_NOTES];
    uint32_t arp_idx;            /* the arpeggiator: steps played, the last note, the chord it plays */
    uint8_t ext_clock;           /* MIDI clock in runs the beat */
    uint16_t ext_bpm;            /* .. at this tempo */
    uint8_t sq_on, sq_song, sq_pat, sq_slot, sq_bar;   /* the sequencer: playing, the song, where (bar: in the
                                                        * pattern, 0 .. its length - 1) */
    uint32_t sq_t, sq_bt;        /* .. ticks into the bar, of the bar */
    uint32_t sq_n;               /* .. bars since the pattern began (each track's bar: cf_seqd.c sq_trk_bar) */
    uint8_t sq_lit[SQ_NTRK];     /* .. each track's step that sounded last */
    uint32_t sq_lit_t[SQ_NTRK];  /* .. when */
    uint32_t sq_lane_t[SQ_NLANE];   /* .. each drum's last hit */
    uint8_t sq_rec, sq_next;     /* .. a take's track (0xFF none); the pattern coming in next (0xFF none) */
    uint32_t sq_cin;             /* .. count-in ticks left */
    uint8_t sq_ckey, sq_lkey;    /* .. the chord, the layer sounding: its root key (0xFF), its buttons */
    uint16_t sq_cbtns, sq_lbtns;
    uint8_t sq_dn, sq_dnote[6], sq_droot, sq_dsingle, sq_dser, sq_bn, sq_bass[2];   /* .. the CHORDS track's chord
                                  * for HOME (cf_seqp.c sqp.dn ..), the bass sounding */
    uint16_t sq_dmask;
} cf_sn_t;
static cf_sn_t cf_sn;

static void cf_snapshot(void)
{
    uint32_t i;
    fm1_irq_off();
    cf_sn.out = cfc.out;
    cf_sn.n = cfp.n;
    for (i = 0; i < cfp.n; i++)
        cf_sn.note[i] = cfp.note[i];
    cf_sn.nb = cfp.nb;
    cf_sn.bass[0] = cfp.bass[0];
    cf_sn.bass[1] = cfp.bass[1];
    cf_sn.onset = cfp.onset;
    cf_sn.arp_on = cfa.on;
    cf_sn.arp_latched = cfa.latched;
    cf_sn.arp_steps = cfa.steps;
    cf_sn.arp_note = cfa.last_note;
    cf_sn.arp_idx = cfa.idx;
    cf_sn.arp_n = cfa.n;
    cf_sn.ext_clock = cfx_in.active;
    cf_sn.ext_bpm = cfx_in.bpm;
    for (i = 0; i < cfa.n; i++)
        cf_sn.arp_note_list[i] = cfa.note[i];
    cf_sn.beat = cfm.beat;
    cf_sn.metro_on = cfm.on;
    cf_sn.running = cfm.running;
    cf_sn.beats_done = cfm.beats_done;
    cf_sn.beat_ms = cfm.beat_ms;
    cf_sn.now_ms = cf_now_ms;
    cf_sn.sq_on = sqp.on;
    cf_sn.sq_rec = sqp.rec;
    cf_sn.sq_next = sqp.next;
    cf_sn.sq_cin = sqp.cin;
    cf_sn.sq_song = sqp.song;
    cf_sn.sq_pat = sqp.pat;
    cf_sn.sq_slot = sqp.slot;
    cf_sn.sq_bar = sqp.bar;
    cf_sn.sq_n = sqp.nbar;
    cf_sn.sq_t = sqp.t;
    cf_sn.sq_bt = sqp_bar_ticks();
    for (i = 0; i < SQ_NTRK; i++) {
        cf_sn.sq_lit[i] = sqp.lit[i];
        cf_sn.sq_lit_t[i] = sqp.lit_t[i];
    }
    for (i = 0; i < SQ_NLANE; i++)
        cf_sn.sq_lane_t[i] = sqp.lane_t[i];
    cf_sn.sq_ckey = sqp.on && sqp.n ? sqp.ckey : 0xFFu;
    cf_sn.sq_cbtns = sqp.on && sqp.n ? sqp.cbtns : 0u;
    cf_sn.sq_lkey = sqp.on && sqp.ln ? sqp.lkey : 0xFFu;
    cf_sn.sq_lbtns = sqp.on && sqp.ln ? sqp.lbtns : 0u;
    cf_sn.sq_dn = sqp.on ? sqp.dn : 0u;
    for (i = 0; i < 6u; i++)
        cf_sn.sq_dnote[i] = sqp.dnote[i];
    cf_sn.sq_droot = sqp.droot;
    cf_sn.sq_dmask = sqp.dmask;
    cf_sn.sq_dsingle = sqp.dsingle;
    cf_sn.sq_dser = sqp.dser;
    cf_sn.sq_bn = sqp.on ? sqp.nb : 0u;
    cf_sn.sq_bass[0] = sqp.bass[0];
    cf_sn.sq_bass[1] = sqp.bass[1];
    fm1_irq_on();
}
