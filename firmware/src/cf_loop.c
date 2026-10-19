/* SPDX-License-Identifier: GPL-3.0-only
 * CoralFlower: the looper, in the audio ISR (ChoralRoot's, made over for CoralFlower's chord model). Included by
 * cf_out.c after the metronome, before the event queue's dispatch.
 *
 * It records what the keys do, not audio: each key that goes down or up (roots and chord buttons), on a clock of
 * CFL_PPQN ticks a beat at the tempo, in layers. It plays them back through a chord model of its own (cf_chord.c, as
 * the keys' is, in the algorithm it was played in), so the loop follows the key, the scale, the buttons' forms and
 * the sounds of now, and the tempo changes its speed, never its pitch. Its notes go to the same two parts as the
 * keys' (a note both sound goes when the last of them lets it go: cfp_off, cfl_off); with the arpeggiator on, the
 * loop's chords are arpeggiated while no key is held (cf_play.c cfp_sync).
 *
 * REC (cfl_rec), as ChoralRoot's: with no take, it arms one (Free: the first chord starts it; bars: a bar is counted
 * in, then that many bars are taken and the loop plays by itself); over a loop in Overwrite the same (the old loop
 * goes when the new take starts), in Overdub it arms a layer (the next chord opens it). While armed: cancels; while
 * taking: ends the take, the loop plays at once (a Free take rounded to the nearest beat). PLAY (cfl_play): plays and
 * stops the loop (the metronome's clock with it, from beat 1); with no loop the metronome alone; while taking: ends
 * the take (an overdub: and stops). REC held (cfl_undo): the last layer goes; the last one going clears the loop.
 * Quantize moves a chord's start to the grid as it is recorded (its end keeps its length). */

#define CFL_PPQN 96u                                 /* ticks a beat */
#define CFL_MAX_EV 500u                              /* events in a loop (all its layers) */
#define CFL_MAX_LAYERS 32u
#define CFL_MAGIC 0x314C4643u                        /* "CFL1" */
#define CFL_DOWN 0x80u                               /* cfl_ev_t.f: the key went down (else up); bits 0-4: its layer */
static const uint8_t CF_LOOP_BARS[] = {0, 1, 2, 4, 8};
#define CF_LOOP_NBARS ((uint32_t)sizeof CF_LOOP_BARS)

typedef struct { uint16_t t; uint8_t key, f; } cfl_ev_t;
typedef struct {                                     /* a loop: what flash keeps (OBJ_CFLOOP) */
    uint32_t magic;
    uint16_t nev, nlayers;
    uint32_t len;                                    /* ticks; 0: empty */
    uint8_t algo, beats, rsv[2];                     /* the algorithm and the bar it was played in */
    cfl_ev_t ev[CFL_MAX_EV];                         /* in time order */
} cfl_data_t;

enum { CFL_EMPTY, CFL_STOPPED, CFL_PLAYING };
enum { CFL_CAP_NONE, CFL_CAP_ARMED, CFL_CAP_COUNTIN, CFL_CAP_REC, CFL_CAP_OD_ARMED, CFL_CAP_OD };

static struct {
    cfl_data_t d;
    uint8_t state, cap, layer;                       /* layer: the one being taken */
    uint32_t pos;                                    /* ticks into the loop (playing) */
    uint32_t acc;                                    /* samples x BPM x PPQN, against FS x 60 */
    uint16_t scan;                                   /* the next event to play */
    uint32_t rec_t, rec_len;                         /* a take: ticks so far; its length (bars; 0: Free) */
    uint32_t cin_t, cin_len;                         /* the count-in */
    int16_t qd[CF_NKEYS];                            /* Quantize: what each key's down was moved by (its up follows) */
    uint32_t kdown;                                  /* the keys down now (the player's view) */
    uint8_t took;                                    /* the layer being taken got an event */
    cf_chord_t c;                                    /* the loop's chord model */
    uint8_t n, note[CF_MAX_NOTES], nb, bass[2];      /* what the loop sounds */
    uint32_t root_ms;                                /* (cfp_key's gather window, for the loop's keys) */
    uint8_t gen;                                     /* bumped whenever the content changes (the screen) */
    uint8_t click;                                   /* the count-in sounds the click (cf_click_mix) */
} cfl __attribute__((section(".pool")));

static int cfl_holds(uint32_t part, uint32_t note)
{
    uint32_t i;
    if (part > 1u)                                   /* (the loop plays the chords and the bass, never the LAYER: it
                                                      * read the LAYER's notes as its bass, and kept one held) */
        return 0;
    if (part) {
        for (i = 0; i < cfl.nb; i++)
            if (cfl.bass[i] == note)
                return 1;
        return 0;
    }
    for (i = 0; i < cfl.n; i++)
        if (cfl.note[i] == note)
            return 1;
    return 0;
}
static const cf_chord_out_t *cfl_chord(int *n)
{
    *n = cfl.state == CFL_PLAYING && cfl.c.out.sounding ? cfl.c.out.n : 0;
    return &cfl.c.out;
}

/* ------------------------------------------------------------- sound --- */
static void cfl_off(uint32_t part, uint32_t note)
{
    uint32_t ch = part ? cfp_cfg.ch_bass : cfp_cfg.ch_chord;
    if (cfp_holds(part, note) || sqp_holds(part, note))   /* (the keys or the sequencer sound it too: they let go) */
        return;
    trk_note_off(&trk[part], note);
    cfp_midi(0x80u, ch, note, 0);
}
/* the loop's chord model answered: its notes go to the parts as a fresh strike would (the tones two chords share
 * hold); with ARP on and no key held, to the arpeggiator */
static void cfl_sound(void)
{
    const cf_chord_out_t *o = &cfl.c.out;
    uint8_t nb[2];
    int m = o->sounding ? o->n : 0, mb = cfp_bass_notes_of(o, nb), i, j;
#ifdef CF_TRACE
    fprintf(stderr, "%6u loop %u:", (unsigned)cf_now_ms, (unsigned)cfl.pos);
    for (i = 0; i < m; i++)
        fprintf(stderr, " %u", o->note[i]);
    fprintf(stderr, mb ? " / %u\n" : "\n", nb[0]);
#endif
    if (cfa.on) {
        for (i = 0; i < cfl.n; i++)
            cfl_off(0, cfl.note[i]);
        cfl.n = 0;
        if (!cfc.out.sounding && !(cfp_cfg.arp.latch && cfa.latched))
            cfa_chord(o, m);
    } else {
        for (i = 0; i < cfl.n; i++) {
            for (j = 0; j < m && o->note[j] != cfl.note[i]; j++)
                ;
            if (j == m)
                cfl_off(0, cfl.note[i]);
        }
        for (j = 0; j < m; j++) {
            for (i = 0; i < cfl.n && cfl.note[i] != o->note[j]; i++)
                ;
            if (i == cfl.n && !cfp_holds(0, o->note[j]))
                cfp_on(0, o->note[j], cfp_cfg.vel);
        }
        cfl.n = (uint8_t)m;
        for (i = 0; i < m; i++)
            cfl.note[i] = o->note[i];
    }
    for (i = 0; i < cfl.nb; i++) {                   /* the bass (one voice: a new note cuts the old one's tail) */
        for (j = 0; j < mb && nb[j] != cfl.bass[i]; j++)
            ;
        if (j == mb)
            cfl_off(1, cfl.bass[i]);
    }
    if (mb)
        cfp_bass_mono();
    for (j = 0; j < mb; j++) {
        for (i = 0; i < cfl.nb && cfl.bass[i] != nb[j]; i++)
            ;
        if (i == cfl.nb && !cfp_holds(1, nb[j]))
            cfp_on(1, nb[j], cfp_cfg.vel);
    }
    cfl.nb = (uint8_t)mb;
    for (i = 0; i < mb; i++)
        cfl.bass[i] = nb[i];
}

/* a recorded key into the loop's chord model (the keys' rules: cfp_key's Wait and gather window) */
static void cfl_key_play(uint32_t key, int down)
{
    int idx, role;
    cf_chord_cfg_t g = cfc.cfg;
    g.algo = cfl.d.algo < CF_NALGS ? cfl.d.algo : cfc.cfg.algo;
    if (cfl.c.cfg.algo != g.algo)                    /* (a change of algorithm drops the model's keys) */
        cf_chord_config(&cfl.c, &g);
    else
        cfl.c.cfg = g;
    role = cf_key_role(g.algo, (int)key, &idx);
    cfl.c.keep = cfp_cfg.retrig == CF_RT_WAIT &&
                 (role != CF_ROLE_BUTTON || (uint32_t)(cf_now_ms - cfl.root_ms) >= CFP_GATHER_MS);
    if (role == CF_ROLE_ROOT && down)
        cfl.root_ms = cf_now_ms;
    if (cf_chord_key(&cfl.c, (int)key, down))
        cfl_sound();
}
static void cfl_all_up(void)                         /* every loop key up, every loop note let go */
{
    cf_chord_reset(&cfl.c);
    cf_chord_config(&cfl.c, &cfl.c.cfg);
    cfl_sound();
}

/* -------------------------------------------------------------- content --- */
static void cfl_insert(uint32_t t, uint32_t key, uint32_t f)   /* in time order, after the events of that time */
{
    uint32_t i, k;
    if (cfl.d.nev >= CFL_MAX_EV)
        return;
#ifdef CF_TRACE
    fprintf(stderr, "%6u loop rec t %u key %u %s layer %u\n", (unsigned)cf_now_ms, (unsigned)t, (unsigned)key,
            (f & CFL_DOWN) ? "down" : "up", (unsigned)(f & 0x1Fu));
#endif
    for (i = cfl.d.nev; i > 0u && cfl.d.ev[i - 1u].t > t; i--)
        ;
    for (k = cfl.d.nev; k > i; k--)
        cfl.d.ev[k] = cfl.d.ev[k - 1u];
    cfl.d.ev[i].t = (uint16_t)t;
    cfl.d.ev[i].key = (uint8_t)key;
    cfl.d.ev[i].f = (uint8_t)f;
    cfl.d.nev++;
    /* before the play position, or at it while overdubbing (the keys play it now): not played again this pass */
    if (cfl.state == CFL_PLAYING && (i < cfl.scan || (cfl.cap == CFL_CAP_OD && t <= cfl.pos)))
        cfl.scan++;
    cfl.took = 1;
    cfl.gen++;
}
static uint32_t cfl_grid(void) { return cfp_cfg.loop.quant == 1u ? CFL_PPQN / 2u : cfp_cfg.loop.quant == 2u ? CFL_PPQN / 4u : 0u; }
/* a key of a take or an overdub at time t (ticks; an overdub: t in the loop, wrapping at its length) */
static void cfl_record(uint32_t key, int down, uint32_t t)
{
    uint32_t g = cfl_grid(), len = cfl.cap == CFL_CAP_OD ? cfl.d.len : 0u;
    int32_t tt = (int32_t)t;
    if (key >= CF_NKEYS || cfl.layer >= CFL_MAX_LAYERS)
        return;
    if (down) {
        int32_t q = g ? (int32_t)(((t + g / 2u) / g) * g) : tt;
        cfl.qd[key] = (int16_t)(q - tt);
        tt = q;
    } else {
        tt += cfl.qd[key];
        if (tt < 0)
            tt = 0;
    }
    if (len)
        tt %= (int32_t)len;
    if (tt > 65535)
        tt = 65535;
    cfl_insert((uint32_t)tt, key, (down ? CFL_DOWN : 0u) | cfl.layer);
}

/* ---------------------------------------------------------- transport --- */
static void cfl_scan_to(uint32_t pos)                /* the next event to play: the first after pos */
{
    cfl.scan = 0;
    while (cfl.scan < cfl.d.nev && cfl.d.ev[cfl.scan].t <= pos)
        cfl.scan++;
}
static void cfl_play_at(uint32_t pos)                /* the events of time pos */
{
    while (cfl.scan < cfl.d.nev && cfl.d.ev[cfl.scan].t <= pos) {
        const cfl_ev_t *e = &cfl.d.ev[cfl.scan++];
        cfl_key_play(e->key, (e->f & CFL_DOWN) != 0u);
    }
}
static void cfl_start(void)                          /* from the top, the metronome's clock from beat 1 with it */
{
    cfl_all_up();
    cfl.state = CFL_PLAYING;
    cfl.pos = 0;
    cfl.acc = 0;
    cfl.scan = 0;
    cfm_restart();
    cfm.running = 1;
    cfl_play_at(0);
}
static void cfl_stop(void)
{
    cfl_all_up();
    if (cfl.d.len)
        cfl.state = CFL_STOPPED;
    cfm.running = 0;
}
static void cfl_clear(void)
{
    cfl_stop();
    cfl.d.nev = 0;
    cfl.d.nlayers = 0;
    cfl.d.len = 0;
    cfl.state = CFL_EMPTY;
    cfl.gen++;
}

/* a take starts (the first chord, or the count-in's end): the keys down now are its first events */
static void cfl_take_start(void)
{
    uint32_t k;
    cfl_clear();                                     /* (Overwrite: the old loop goes now) */
    cfl.d.magic = CFL_MAGIC;
    cfl.d.algo = cfc.cfg.algo;
    cfl.d.beats = cfm.beats;
    cfl.layer = 0;
    cfl.cap = CFL_CAP_REC;
    cfl.rec_t = 0;
    cfl.rec_len = CF_LOOP_BARS[cfp_cfg.loop.bars % CF_LOOP_NBARS] * cfm.beats * CFL_PPQN;
    for (k = 0; k < CF_NKEYS; k++)
        if ((cfl.kdown >> k) & 1u)
            cfl_record(k, 1, 0);
    cfm_restart();                                   /* (beat 1 with the take: after a count-in, the next bar's) */
    cfm.running = 1;
    cfl.click = 0;
    cfl.acc = 0;
}
static void cfl_take_end(void)                       /* the take becomes the loop, which plays at once */
{
    uint32_t len = cfl.rec_t, bar = (uint32_t)cfm.beats * CFL_PPQN, i;
    if (cfl.rec_len)                                 /* bars: whole bars (an early end rounds up) */
        len = len >= cfl.rec_len ? cfl.rec_len : ((len + bar - 1u) / bar) * bar;
    else                                             /* Free: the nearest beat */
        len = ((len + CFL_PPQN / 2u) / CFL_PPQN) * CFL_PPQN;
    if (len < CFL_PPQN)
        len = CFL_PPQN;
    if (len > 65535u - 65535u % CFL_PPQN)
        len = 65535u - 65535u % CFL_PPQN;
    for (i = 0; i < cfl.d.nev; i++)                  /* (a start moved past the end by Quantize: its last tick) */
        if (cfl.d.ev[i].t >= len)
            cfl.d.ev[i].t = (uint16_t)(len - 1u);
    cfl.d.len = len;
    cfl.d.nlayers = 1;
    cfl.cap = CFL_CAP_NONE;
    cfl.gen++;
    cfl_start();
}
static void cfl_od_end(void)
{
    if (cfl.took && cfl.d.nlayers < CFL_MAX_LAYERS)
        cfl.d.nlayers++;
    cfl.cap = CFL_CAP_NONE;
}

/* REC tapped */
static void cfl_rec(void)
{
    switch (cfl.cap) {
    case CFL_CAP_ARMED:
    case CFL_CAP_COUNTIN:
        cfl.cap = CFL_CAP_NONE;                      /* cancelled: the loop as it was */
        cfl.click = 0;
        if (!cfl.d.len)
            cfm.running = 0;
        return;
    case CFL_CAP_REC: cfl_take_end(); return;
    case CFL_CAP_OD_ARMED: cfl.cap = CFL_CAP_NONE; return;
    case CFL_CAP_OD: cfl_od_end(); return;
    default: break;
    }
    if (cfp_cfg.loop.mode == 1u && cfl.d.len) {      /* Overdub: a layer on top, the loop playing */
        if (cfl.state != CFL_PLAYING)
            cfl_start();
        cfl.cap = CFL_CAP_OD_ARMED;
        return;
    }
    if (cfl.state == CFL_PLAYING)
        cfl_stop();
    if (cfp_cfg.loop.bars % CF_LOOP_NBARS) {        /* bars: a bar counted in (unless Count-in Off: at once) */
        cfl.cin_t = 0;
        cfl.cin_len = cfp_cfg.loop.count_in ? (uint32_t)cfm.beats * CFL_PPQN : 0u;
        cfl.click = cfl.cin_len != 0u;
        cfm_restart();
        cfm.running = 1;
        cfl.acc = 0;
        if (cfl.cin_len)
            cfl.cap = CFL_CAP_COUNTIN;
        else
            cfl_take_start();
        return;
    }
    cfl.cap = CFL_CAP_ARMED;                         /* Free: the first chord starts it */
}

/* PLAY tapped */
static void cfl_play(void)
{
    if (cfl.cap == CFL_CAP_REC) {
        cfl_take_end();
        return;
    }
    if (cfl.cap == CFL_CAP_OD) {
        cfl_od_end();
        cfl_stop();
        return;
    }
    if (cfl.cap != CFL_CAP_NONE) {                   /* armed or counting in: cancelled, then as a tap */
        cfl.cap = CFL_CAP_NONE;
        cfl.click = 0;
    }
    if (cfl.state == CFL_PLAYING) {
        cfl_stop();
    } else if (cfl.state == CFL_STOPPED) {
        cfl_start();
    } else {                                         /* no loop: the metronome's clock alone, from beat 1 */
        if (!cfm.running)
            cfm_restart();
        cfm.running = (uint8_t)!cfm.running;
    }
}

/* REC held: the last layer goes (the last one: the loop is cleared). 1: done, 0: nothing to undo */
static int cfl_undo(void)
{
    uint32_t i, k = 0, top;
    if (cfl.cap == CFL_CAP_REC || cfl.cap == CFL_CAP_COUNTIN || cfl.cap == CFL_CAP_ARMED) {
        cfl.cap = CFL_CAP_NONE;                      /* (a take under way: dropped) */
        cfl.click = 0;
        if (!cfl.d.len) {
            cfl_clear();
            return 1;
        }
    }
    if (cfl.cap == CFL_CAP_OD || cfl.cap == CFL_CAP_OD_ARMED)
        cfl_od_end();
    if (!cfl.d.nlayers)
        return 0;
    top = cfl.d.nlayers - 1u;
    if (!top) {
        cfl_clear();
        return 1;
    }
    for (i = 0; i < cfl.d.nev; i++)
        if ((cfl.d.ev[i].f & 0x1Fu) != top)
            cfl.d.ev[k++] = cfl.d.ev[i];
    cfl.d.nev = (uint16_t)k;
    cfl.d.nlayers = (uint16_t)top;
    cfl_all_up();                                    /* (a chord of that layer may be sounding) */
    cfl_scan_to(cfl.pos);
    cfl.gen++;
    return 1;
}

/* every key the player gets (cf_apply, before cfp_key): taken into a take or an overdub */
static void cfl_key(uint32_t key, int down)
{
    int idx, role = cf_key_role(cfc.cfg.algo, (int)key, &idx);
    if (key >= CF_NKEYS)
        return;
    cfl.kdown = down ? cfl.kdown | (1u << key) : cfl.kdown & ~(1u << key);
    if (cfl.cap == CFL_CAP_ARMED && down && role == CF_ROLE_ROOT) {
        cfl_take_start();                            /* (the keys down now, this one too) */
        return;
    }
    if (cfl.cap == CFL_CAP_OD_ARMED && down && role == CF_ROLE_ROOT) {
        uint32_t k;
        cfl.cap = CFL_CAP_OD;
        cfl.layer = (uint8_t)cfl.d.nlayers;
        cfl.took = 0;
        for (k = 0; k < CF_NKEYS; k++)               /* (the buttons held: this layer's from here) */
            if (((cfl.kdown >> k) & 1u) && k != key)
                cfl_record(k, 1, cfl.pos);
    }
    if (cfl.cap == CFL_CAP_REC)
        cfl_record(key, down, cfl.rec_t);
    else if (cfl.cap == CFL_CAP_OD)
        cfl_record(key, down, cfl.pos);
}

/* one tick of the loop's clock */
static void cfl_tick1(void)
{
    if (cfl.cap == CFL_CAP_COUNTIN) {
        if (++cfl.cin_t >= cfl.cin_len)
            cfl_take_start();
        return;
    }
    if (cfl.cap == CFL_CAP_REC) {
        cfl.rec_t++;
        if ((cfl.rec_len && cfl.rec_t >= cfl.rec_len) || cfl.rec_t >= 65535u - CFL_PPQN)
            cfl_take_end();
        return;
    }
    if (cfl.state == CFL_PLAYING && cfl.d.len) {
        if (++cfl.pos >= cfl.d.len) {                /* round again: every loop key up, then its start */
            cfl.pos = 0;
            cfl.scan = 0;
            cfl_all_up();                            /* (overdubbing: the keys being taken are the keys', not these) */
            cfm_restart();                           /* (the beat stays on the loop's: beat 1 at its start) */
            cfm.running = 1;
        }
        cfl_play_at(cfl.pos);
    }
}

/* every audio block: the clock, from the tempo (one tick at most a block: 300 BPM x 96 is far below a tick a block)
 * or, while MIDI clock comes in, from its pulses (4 ticks a pulse: 96 a beat against 24) */
static void cfl_tick(uint32_t n, uint32_t pulses)
{
    uint32_t k;
    if (cfl.cap == CFL_CAP_NONE && cfl.state != CFL_PLAYING)
        return;
    if (cfx_in.active) {
        for (k = 0; k < pulses * (CFL_PPQN / 24u); k++)
            cfl_tick1();
        return;
    }
    cfl.acc += n * cfm.bpm * CFL_PPQN;
    if (cfl.acc < FS * 60u)
        return;
    cfl.acc -= FS * 60u;
    cfl_tick1();
}

static void cfl_panic(void)
{
    if (cfl.cap != CFL_CAP_NONE && cfl.cap != CFL_CAP_OD && cfl.cap != CFL_CAP_OD_ARMED)
        cfl.cap = CFL_CAP_NONE;
    else if (cfl.cap)
        cfl_od_end();
    cfl.click = 0;
    cfl_stop();
    cfl.kdown = 0;
}

/* the UI loaded a loop from flash into cfl.d (power-on, before the audio runs): ready, stopped */
static void cfl_loaded(void)
{
    if (cfl.d.magic != CFL_MAGIC || !cfl.d.len || cfl.d.nev > CFL_MAX_EV || cfl.d.len > 65535u) {
        cfl.d.nev = 0;
        cfl.d.len = 0;
        cfl.d.nlayers = 0;
        cfl.state = CFL_EMPTY;
        return;
    }
    cfl.state = CFL_STOPPED;
    cf_chord_init(&cfl.c, &cfc.cfg);
    cfl.gen++;
}
