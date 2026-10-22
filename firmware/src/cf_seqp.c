/* SPDX-License-Identifier: GPL-3.0-only
 * CoralFlower: the sequencer playing (cf_seqd.c's patterns; the audio side). Included by cf_out.c after cf_seqd.c.
 *
 * PLAY on the SEQUENCER screens plays the pattern shown, round, or the song from the place picked, round. Its clock is
 * 96 ticks a beat from the tempo (MIDI clock in: 4 ticks a pulse); the metronome's beat 1 at its start.
 * A step sounds at its place in the bar (1/24 of a beat: 4 ticks; the drums at their steps, every second one late by
 * the pattern's SWING). CHORDS on part 0, BASS on part 1 (a pattern
 * with no bass steps: the bass each chord had when it was written, as under the hands), LAYER on part 2 (its own
 * sound), DRUMS on part 3 (the DRUM engine, a General MIDI note a lane). A muted track (sq_pat_t.mute, the pattern
 * playing's) is skipped.
 * A note the keys also hold is theirs to let go (cfp_holds), and theirs do not cut the
 * sequencer's (cf_play.c cfp_off asks sqp_holds).
 * Another pattern picked while one plays comes in at the next bar line (sqp.next; it waited for the pattern's end, and
 * did not seem to switch at all: the owner, 2026-10-19).
 * REC: a take into one track. From a stop, a bar of count-in (the click loud), then the pattern
 * from its first bar; while it plays, at once. What the keys play then (the chord model's changes; on DRUMS the drum
 * keys) goes into a queue with where it fell in the bar (sqp_rq); the UI takes it from there and writes the steps
 * (cf_sequi.c sq_rec_poll: one writer of the patterns, the UI). */

#define SQ_PPQN 96u
#define CF_PART_DRUM 3u
/* each lane's General MIDI note (the DRUM engine's map, eng_drum.c DRUM_GM; MIDI out on the drums' channel): kick,
 * snare, clap, closed hat, open hat, low / mid / high tom (43, 47, 50: the tom 4 below, 3 and 8 above its design),
 * rim, bell, crash. The toms share the engine's tom voice, the crash and the bell its bell voice (a hit cuts the
 * other's ring) */
static const uint8_t SQ_LANE_NOTE[SQ_NLANE] = {36, 38, 39, 42, 46, 43, 47, 50, 37, 56, 49};

static struct {
    uint8_t on, song;            /* playing; the song (else the one pattern, round) */
    uint8_t pat, slot, bar;      /* where (bar: in the pattern, 0 .. its length - 1: nbar round it) */
    uint32_t nbar;               /* bars since the pattern began (on and on while it plays round; 0 at a new one) */
    uint8_t sb[SQ_NTRK];         /* .. the bar each track plays in this one (sq_trk_bar, taken as the bar begins:
                                  * BARS turned while it plays counts from the next bar line) */
    uint32_t t, acc;             /* ticks into the bar; the clock's samples x bpm */
    uint8_t nu;                  /* the next place in the bar (1/24 of a beat) whose steps have not sounded yet */
    uint8_t nh;                  /* .. the drums' next step */
    uint8_t n, note[6], nb, bass[2], ln, lnote[6];   /* what sounds: the chord, the bass, the layer */
    uint32_t left[3];            /* .. the ticks it still lasts (it may run on over a bar line) */
    uint8_t lit[SQ_NTRK];        /* where in the bar (1/24 of a beat) each track sounded last (the screens' pollen),
                                  * 0xFF none */
    uint32_t lit_t[SQ_NTRK];     /* .. when (cf_now_ms) */
    uint32_t lane_t[SQ_NLANE];   /* each drum's last hit (cf_now_ms): its key's flash */
    uint16_t doff;               /* the drums whose MIDI note goes off at the next tick */
    cf_chord_t cm;               /* a pattern that follows the key: its steps' keys pressed again here (sq_resolve) */
    uint8_t ckey, lkey;          /* the chord, the layer sounding: its root key, 0xFF (the keys' lights) */
    uint16_t cbtns, lbtns;       /* .. its chord buttons */
    uint8_t dn, dnote[6], droot, dsingle, dser;   /* the CHORDS track's chord sounding, its arpeggio's too, for HOME's
                                  * flower and name (0.5, the owner: "то, что играется в секвенсоре я бы хотел, чтобы
                                  * на главной отображалось на цветочке"): notes, root, single notes, + 1 a chord */
    uint16_t dmask;              /* .. its pitch classes over the root */
    uint8_t gn, gnote[6], gbn, gbass[2];   /* the last chord and bass struck (GLO > Play's Always glides from them) */
    uint32_t gms, gbms;          /* .. when they were let go (cf_now_ms) */
    uint8_t next;                /* the pattern that comes in when this one ends (SQ_NPAT + k: the song from place k),
                                  * 0xFF none */
    uint8_t rec;                 /* a take: its track, 0xFF none */
    uint8_t rlim;                /* .. from HOME: it ends by itself after this many bars, the pattern plays on (0: no
                                  * limit; the owner, 2026-10-19: "до второго REC, максимум 8 тактов") */
    uint8_t click;               /* the click loud (count-in, a take) */
    uint32_t cin;                /* count-in ticks left */
    uint32_t rserial;            /* the chord model's serial last seen in a take */
    uint8_t ropen;               /* a chord of the take still sounds (its off not sent yet) */
    uint32_t rheld;              /* .. the ticks it has sounded (its length: kind 2's dur) */
    uint8_t rage;                /* the last key down's age when it got here (ms: cf_ui.c, the main loop's delay) */
    uint32_t rback;              /* .. the open chord's start, put back by it (ticks): its length gets them too */
    uint8_t aon;                 /* bit t: track t's arpeggiator plays a chord of it (ARP on the track: sqp_arp_on) */
    uint8_t dsk[SQ_NLANE], dskb[SQ_NLANE], dskp[SQ_NLANE];   /* a drum struck in a take just before the step it is
                                  * written on: that step (+ 1), its bar and pattern, not struck again there */
} sqp = {.next = 0xFF, .rec = 0xFF};

/* the take's queue, ISR -> UI: a chord began (kind 1, its notes, rkey the root key that made it: 0xFF several roots
 * at once), it ended (2, dur: the ticks it sounded), a drum struck (3, lane in n, h the step it falls on: the nearest
 * as SWING plays them, the bar's step count: the next bar's first) */
#define SQR_N 32u
typedef struct {
    uint8_t kind, pat, bar, n, note[6], root, nb, bass[2], single, rkey, h;
    uint16_t mask, btns;
    uint32_t t, dur;
} sqr_ev_t;
static sqr_ev_t sqp_rq[SQR_N];
static volatile uint32_t sqp_rw;                 /* (the UI's read index: cf_sequi.c squ.rr) */
/* a pattern comes in: the sounds made ready for it go on its parts now, before its first notes (cf_sound.c) */
static void cf_stage_in(uint32_t pat);

static void sqp_rpush(const sqr_ev_t *e)
{
    sqp_rq[sqp_rw % SQR_N] = *e;
    sqp_rw++;
}

static int sqp_holds(uint32_t part, uint32_t note)
{
    uint32_t i;
    if (!sqp.on)
        return 0;
    if (part == 0u || part == 2u) {              /* (a track's arpeggiator: the notes it sounds) */
        const cfa_t *a = &cfa_sq[part == 2u];
        if (a->on && a->n) {
            if (a->cur == note)
                return 1;
            for (i = 0; i < a->sn; i++)
                if (a->son[i] && a->snote[i] == note)
                    return 1;
        }
    }
    if (part == 1u) {
        for (i = 0; i < sqp.nb; i++)
            if (sqp.bass[i] == note)
                return 1;
        return 0;
    }
    if (part == 2u) {
        for (i = 0; i < sqp.ln; i++)
            if (sqp.lnote[i] == note)
                return 1;
        return 0;
    }
    if (part)
        return 0;
    for (i = 0; i < sqp.n; i++)
        if (sqp.note[i] == note)
            return 1;
    return 0;
}
static void sqp_off(uint32_t part, uint32_t note)
{
    /* (also the tracks' arpeggiators' notes: cf_arp.c) */
    if (cfp_holds(part, note))                   /* (the keys sound it too: theirs to let go) */
        return;
    trk_note_off(&trk[part], note);
    cfp_midi(0x80u, cfp_ch(part), note, 0);
}
static void sqp_chord_off(void)
{
    uint32_t i, n = sqp.n;
    if (n)
        sqp.gms = cf_now_ms;                     /* (Always glides from it a few seconds after) */
    sqp.n = 0;                                   /* (first: sqp_holds no longer counts them) */
    sqp.ckey = 0xFF;
    sqp.cbtns = 0;
    for (i = 0; i < n; i++)
        sqp_off(0, sqp.note[i]);
}
static void sqp_layer_off(void)
{
    uint32_t i, n = sqp.ln;
    sqp.ln = 0;
    sqp.lkey = 0xFF;
    sqp.lbtns = 0;
    for (i = 0; i < n; i++)
        sqp_off(2, sqp.lnote[i]);
}
static void sqp_bass_off(void)
{
    uint32_t i, n = sqp.nb;
    if (n)
        sqp.gbms = cf_now_ms;
    sqp.nb = 0;
    for (i = 0; i < n; i++)
        sqp_off(1, sqp.bass[i]);
}

static uint32_t sqp_bar_ticks(void) { return (uint32_t)cfm.beats * SQ_PPQN; }

/* the chords' Glide on the sequencer (0.5, the owner: "glide работает как обычно, но влияет на аккорды из секвенсора
 * ещё"): the chord sound's own (cf_play.c cfp_glide_g), in control blocks; 0: none (no Glide, or the keys sound on the
 * chords' part: theirs glide) */
static uint32_t sqp_glide_ticks(void)
{
    int32_t g = trk[0].p[P_GLIDE];
    if (g <= 0 || (cfp.n && cfp_cp == 0u))
        return 0;
    g = g > 127 ? 127 : g;
    return 1u + (uint32_t)(g * g) / 8u;
}

/* the bass: ticks, the chords' Glide when it is the chords' own bass ("Басс по-умолчанию подвязывается"); a BASS
 * track's own notes come with 0 ("Если бас прописан отдельно от аккордов, то не подвязывается") */
static void sqp_bass_on(const uint8_t *b, uint32_t n, uint32_t left, uint32_t ticks)
{
    uint32_t i, on = sqp.nb, m = n < 2u ? n : 2u;
    uint8_t from[2];
    int legato = ticks && on && m, always = ticks && !on && m && cfp_cfg.glide_always && sqp.gbn &&
                 cf_now_ms - sqp.gbms < 4000u;
    for (i = 0; i < 2u; i++)
        from[i] = legato ? sqp.bass[i < on ? i : 0u] : sqp.gbass[i < sqp.gbn ? i : 0u];
    if (legato) {                                /* the bass voices move to the new notes, at the Glide time */
        sqp.nb = 0;
        for (i = m; i < on; i++)
            sqp_off(1, sqp.bass[i]);
        for (i = 0; i < m; i++) {
            if (i < on)
                cfp_move(1, from[i], b[i], ticks);
            else
                cfp_on_from(1, b[i], cfp_cfg.vel, from[0], ticks);
            sqp.bass[i] = b[i];
        }
    } else {
        sqp_bass_off();
        cfp_bass_mono();                         /* (one voice: a new note cuts the old one's tail) */
        for (i = 0; i < m; i++) {
            sqp.bass[i] = b[i];
            if (cfp_holds(1, b[i]))
                continue;
            if (always)                          /* (Always: out of the last bass, a few seconds after it) */
                cfp_on_from(1, b[i], cfp_cfg.vel, from[i], ticks);
            else
                cfp_on(1, b[i], cfp_cfg.vel);
        }
    }
    sqp.nb = (uint8_t)m;
    sqp.gbn = (uint8_t)m;
    for (i = 0; i < m; i++)
        sqp.gbass[i] = b[i];
    sqp.left[1] = left;
}

/* a CHORDS step's chord with the Glide: legato (the last still sounds): its voices move to the new notes; Always
 * (GLO > Play, the last let go a few seconds before): the new notes start out of the last chord's. Paired as the keys'
 * (cf_play.c cf_match) */
static void sqp_chord_glide(const sq_ev_t *e, uint32_t ticks, int legato)
{
    uint8_t old[6], pi[24], qj[24], up[6] = {0}, uq[6] = {0};
    uint32_t n = legato ? sqp.n : sqp.gn, m = e->n < 6u ? e->n : 6u, i;
    int k, x;
    for (i = 0; i < n; i++)
        old[i] = legato ? sqp.note[i] : sqp.gnote[i];
    k = cf_match(old, (int)n, e->note, (int)m, pi, qj);
    if (legato)
        sqp.n = 0;                               /* (first: sqp_holds no longer counts the old ones) */
    for (x = 0; x < k; x++) {
        int a = pi[x], b = qj[x];
        if (!up[a] && !uq[b]) {                  /* the first pairing: legato, the old voice moves; Always, the new
                                                  * note starts out of the old one */
            up[a] = uq[b] = 1;
            if (legato)
                cfp_move(0, old[a], e->note[b], ticks);
            else if (old[a] != e->note[b])
                cfp_on_from(0, e->note[b], cfp_cfg.vel, old[a], ticks);
            else
                cfp_on(0, e->note[b], cfp_cfg.vel);
        } else if (!uq[b]) {                     /* a split: a new voice out of this old note */
            uq[b] = 1;
            cfp_on_from(0, e->note[b], cfp_cfg.vel, old[a], ticks);
        } else if (legato && !up[a]) {           /* a merge: the old voice slides into the note another reaches and
                                                  * lets go on the way (its tail not at the old pitch: the keys' too) */
            voice_t *v = trk_voice_of(&trk[0], old[a]);
            up[a] = 1;
            if (v) {
                if (old[a] != e->note[b])
                    trk_retune(&trk[0], v, e->note[b], ticks);
                v->gate = 0;
                v->stage = 3;
                cfp_midi(0x80u, cfp_ch(0), old[a], 0);
            } else {
                sqp_off(0, old[a]);
            }
        }
    }
    if (legato)
        for (i = 0; i < n; i++)
            if (!up[i])
                sqp_off(0, old[i]);              /* (one no new note took: it goes) */
    for (i = 0; i < m; i++) {
        if (!uq[i])
            cfp_on(0, e->note[i], cfp_cfg.vel);
        sqp.note[i] = e->note[i];
    }
    sqp.n = (uint8_t)m;
}

/* drum l in kit k: the DRUM engine's eight knobs (SQ_KITS) on the engine's lane that plays it (eng_drum.c
 * DRUM_LANE_KNOBS), before its hit */
static void sqp_lane_kit(uint32_t l, uint32_t k, uint32_t lvl)   /* lvl: its level, 0..100 % */
{
    uint32_t i, el = drum_lane(SQ_LANE_NOTE[l % SQ_NLANE]);
    for (i = 0; i < 8u; i++)
        drum_lane_knobs[0][el][i] = (int16_t)SQ_KITS[k % SQ_NKIT].e[i];
    drum_lane_att[0][el] = (uint8_t)(100u - (lvl < 100u ? lvl : 100u));
}

/* ARP on a track (CHORDS, LAYER; the ARP button in its editor: the owner, 2026-10-19): its steps are played by an
 * arpeggiator of its own, cf_arp.c's, with the ARP page's settings (the strum too), on the beat; the chord stays the
 * step's, so ARP off plays it whole again. While the keys sound on its part they take over (two arpeggios of one chord
 * ran over each other in a take) */
static int sqp_arp_trk(const sq_pat_t *p, uint32_t tk) { return (tk == SQ_CHORDS || tk == SQ_LAYER) && (p->arp >> tk) & 1u; }
static void sqp_arp_on(uint32_t tk, const sq_ev_t *e)
{
    cf_chord_out_t o = {0};
    uint32_t j, part = tk == SQ_LAYER ? 2u : 0u;
    if (cfp_cp == part && (cfp.n || cfa_k.n))
        return;
    o.sounding = 1;
    o.n = (uint8_t)(e->n < CF_MAX_NOTES ? e->n : CF_MAX_NOTES);
    for (j = 0; j < o.n; j++)
        o.note[j] = e->note[j];
    o.root_pc = e->root;
    cfa_at = &cfa_sq[tk == SQ_LAYER];
    cfa.on = 1;
    cfa_chord(&o, o.n);
    cfa_at = &cfa_k;
    sqp.aon |= (uint8_t)(1u << tk);
}
static void sqp_arp_off(uint32_t tk)
{
    if (!((sqp.aon >> tk) & 1u))
        return;
    cfa_at = &cfa_sq[tk == SQ_LAYER];
    cfa_stop();
    cfa_at = &cfa_k;
    sqp.aon &= (uint8_t)~(1u << tk);
}
static void sqp_arp_tick(uint32_t n)             /* every audio block (cf_out.c): their steps, their strums */
{
    uint32_t i;
    for (i = 0; i < 2u; i++) {
        cfa_at = &cfa_sq[i];
        cfa_tick(n);
    }
    cfa_at = &cfa_k;
}

/* the tick in the bar where the drums' step i sounds: every second one late by the pattern's SWING (up to half a
 * step) */
static uint32_t sqp_drum_tick(const sq_pat_t *p, uint32_t i)
{
    uint32_t bt = sqp_bar_ticks(), sd = sq_dsteps(), t = i * bt / sd;
    if (i & 1u)
        t += p->swing * (bt / sd) / 200u;
    return t;
}

/* track tk at place u of the bar now (1/24 of a beat; the drums: their step h): what begins there sounds. A track
 * shorter than the pattern plays its own bars round (sq_trk_bar) */
static void sqp_fire(const sq_pat_t *p, uint32_t tk, uint32_t u)
{
    uint32_t i, b = sqp.sb[tk], gt = 0;
    if (tk == SQ_DRUMS) {
        uint32_t l, any = 0, h = u;
        for (l = 0; l < SQ_NLANE; l++)
            if ((p->hit[l][b] >> h) & 1u) {
                if (sqp.dsk[l] == h + 1u && sqp.dskb[l] == (uint8_t)sqp.nbar && sqp.dskp[l] == sqp.pat) {
                    sqp.dsk[l] = 0;              /* (struck by hand a moment ago, in the take: once is enough) */
                    continue;
                }
                sqp_lane_kit(l, p->dkit[l], p->dlev[l]);   /* (its kit, its level) */
                trk_note_on(&trk[CF_PART_DRUM], SQ_LANE_NOTE[l], 100);
                cfp_midi(0x90u, cfp_ch(CF_PART_DRUM), SQ_LANE_NOTE[l], 100);
                sqp.doff |= (uint16_t)(1u << l);
                sqp.lane_t[l] = cf_now_ms;
                any = 1;
            }
#ifdef CF_TRACE
        if (any)
            fprintf(stderr, "%6u seq %c bar %u (its %u) drums step %u:%s%s%s\n", (unsigned)cf_now_ms, 'A' + sqp.pat,
                    sqp.bar + 1u, (unsigned)b + 1u, (unsigned)h, (p->hit[0][b] >> h) & 1u ? " kick" : "",
                    (p->hit[1][b] >> h) & 1u ? " snare" : "", (p->hit[3][b] >> h) & 1u ? " hat" : "");
#endif
        if (any) {
            sqp.lit[tk] = (uint8_t)(h * (SQ_U / sq_dsteps()));
            sqp.lit_t[tk] = cf_now_ms;
        }
        return;
    }
    for (i = 0; i < p->nev; i++) {
        const sq_ev_t *e = &p->ev[i];
        sq_ev_t now;
        uint32_t off, j;
        if (e->trk != tk || e->bar != b || e->pos != u)
            continue;
        if (p->follow && sq_resolve(&sqp.cm, e, &cfc.cfg, &now))   /* (the key HOME has now) */
            e = &now;
        off = e->len * sqp_bar_ticks() / SQ_U;   /* (how long: it may run on into the bars after) */
#ifdef CF_TRACE
        fprintf(stderr, "%6u seq %c bar %u (its %u) %s at %u/24 len %u: %u notes, first %u\n", (unsigned)cf_now_ms,
                'A' + sqp.pat, sqp.bar + 1u, (unsigned)b + 1u, tk == SQ_LAYER ? "layer" : tk ? "bass" : "chord",
                (unsigned)u, e->len, e->n, e->note[0]);
#endif
        sqp.lit[tk] = (uint8_t)u;
        sqp.lit_t[tk] = cf_now_ms;
        if (tk == SQ_BASS) {
            sqp_bass_on(e->note, 1, off, 0);   /* (its own notes: no Glide of the chords') */
            return;
        }
        if (tk == SQ_LAYER && sqp_arp_trk(p, SQ_LAYER)) {   /* (ARP on the track: its arpeggiator plays it) */
            sqp_layer_off();
            sqp_arp_on(SQ_LAYER, e);
            sqp.left[2] = off;
            sqp.lkey = e->rootkey;
            sqp.lbtns = e->btns;
            return;
        }
        if (tk == SQ_LAYER) {                    /* (its own part, no bass) */
            if (trk[2].p[P_VOICE] == V_LEGATO) {
                /* a legato sound: the new note first, then the old one let go, so its own glide slides between
                 * them (cf_edit.c's Glide page: a melody that slides in the sequencer too). MONO strikes every note
                 * and slides anyway (voice.c); first it held a repeated note as one */
                uint8_t old[6], no = sqp.ln, k;
                for (j = 0; j < no; j++)
                    old[j] = sqp.lnote[j];
                sqp.ln = 0;
                for (j = 0; j < e->n; j++) {
                    sqp.lnote[j] = e->note[j];
                    if (!cfp_holds(2, e->note[j]))
                        cfp_on(2, e->note[j], cfp_cfg.vel);
                }
                sqp.ln = e->n;
                for (j = 0; j < no; j++) {
                    for (k = 0; k < e->n && e->note[k] != old[j]; k++)
                        ;
                    if (k == e->n)
                        sqp_off(2, old[j]);
                }
            } else {
                sqp_layer_off();
                for (j = 0; j < e->n; j++) {
                    sqp.lnote[j] = e->note[j];
                    if (!cfp_holds(2, e->note[j]))
                        cfp_on(2, e->note[j], cfp_cfg.vel);
                }
            }
            sqp.ln = e->n;
            sqp.left[2] = off;
            sqp.lkey = e->rootkey;
            sqp.lbtns = e->btns;
            return;
        }
        gt = sqp_arp_trk(p, SQ_CHORDS) ? 0u : sqp_glide_ticks();
        if (gt && (sqp.n || (cfp_cfg.glide_always && sqp.gn && cf_now_ms - sqp.gms < 4000u))) {
            sqp_chord_glide(e, gt, sqp.n != 0);  /* (the Glide: out of the last chord, as the keys' do) */
        } else {
            sqp_chord_off();                     /* (a chord strikes again whole: a sequencer's stab) */
            if (sqp_arp_trk(p, SQ_CHORDS)) {     /* (ARP on the track: its arpeggiator plays the chord) */
                sqp_arp_on(SQ_CHORDS, e);
            } else {
                for (j = 0; j < e->n; j++) {
                    sqp.note[j] = e->note[j];
                    if (!cfp_holds(0, e->note[j]))
                        cfp_on(0, e->note[j], cfp_cfg.vel);
                }
                sqp.n = e->n;
            }
        }
        sqp.gn = e->n < 6u ? e->n : 6u;
        for (j = 0; j < sqp.gn; j++)
            sqp.gnote[j] = e->note[j];
        sqp.left[0] = off;
        sqp.ckey = e->rootkey;
        sqp.cbtns = e->btns;
        sqp.dn = e->n < 6u ? e->n : 6u;          /* (HOME shows it: cf_ui.c cf_feed_plant) */
        for (j = 0; j < sqp.dn; j++)
            sqp.dnote[j] = e->note[j];
        sqp.droot = e->root;
        sqp.dmask = e->mask;
        sqp.dsingle = !e->btns;
        sqp.dser++;
        if (!sq_own(p, SQ_BASS) && e->nb && !((p->mute >> SQ_BASS) & 1u))   /* no bass steps: the chord's
                                                  * own bass (BASS muted: none; it sounded for a tick) */
            sqp_bass_on(e->bass, e->nb, off, gt);   /* (the chords' own bass: with their Glide) */
        return;
    }
}

/* the tick now: what ends goes, then what begins sounds */
static void sqp_now(void)
{
    const sq_pat_t *p = &sq_st.pat[sqp.pat % SQ_NPAT];
    uint32_t tk, gl = sqp_glide_ticks();
    /* with the Glide a chord (and the bass made of it) that ends in this tick goes at its end, after what begins in
     * it, so a chord that meets the next slides into it, as the keys' held chords do (the LAYER's legato, below) */
    if (sqp.n && !sqp.left[0] && !gl)
        sqp_chord_off();
    if (!sqp.left[0])
        sqp.dn = 0;                              /* (its chord over, an arpeggio's too: HOME's flower lets go) */
    if (sqp.nb && !sqp.left[1] && !(gl && !sq_own(p, SQ_BASS)))
        sqp_bass_off();
    if (!sqp.left[0])
        sqp_arp_off(SQ_CHORDS);                  /* (its chord over: the arpeggio ends with it) */
    if (!sqp.left[2])
        sqp_arp_off(SQ_LAYER);
    if (sqp.ln && !sqp.left[2] && trk[2].p[P_VOICE] != V_LEGATO)
        sqp_layer_off();                         /* (a legato LAYER: at the end of this tick, after what begins in
                                                  * it, so a note that meets the next slides into it) */
    {   /* a track muted while it sounds: it goes now */
        uint32_t mu = p->mute;                   /* (the pattern playing's: another pattern's come in with it) */
        if ((mu >> SQ_CHORDS) & 1u && sqp.n)
            sqp_chord_off();
        if ((mu >> SQ_CHORDS) & 1u)
            sqp_arp_off(SQ_CHORDS);
        if ((mu >> SQ_LAYER) & 1u)
            sqp_arp_off(SQ_LAYER);
        if (((mu >> SQ_BASS) & 1u || ((mu >> SQ_CHORDS) & 1u && !sq_own(p, SQ_BASS))) && sqp.nb)
            sqp_bass_off();
        if ((mu >> SQ_LAYER) & 1u && sqp.ln)
            sqp_layer_off();
    }
    if (sq_empty(p)) {
        if (sqp.ln && !sqp.left[2])
            sqp_layer_off();
        if (sqp.n && !sqp.left[0])
            sqp_chord_off();
        if (sqp.nb && !sqp.left[1])
            sqp_bass_off();
        return;
    }
    while (sqp.nu < SQ_U && sqp.t >= sqp.nu * sqp_bar_ticks() / SQ_U) {
        for (tk = 0; tk < SQ_DRUMS; tk++)
            if (!((p->mute >> tk) & 1u))         /* (muted: its steps pass by) */
                sqp_fire(p, tk, sqp.nu);
        sqp.nu++;
    }
    while (sqp.nh < sq_dsteps() && sqp.t >= sqp_drum_tick(p, sqp.nh)) {   /* (the drums, swung) */
        if (!((p->mute >> SQ_DRUMS) & 1u))
            sqp_fire(p, SQ_DRUMS, sqp.nh);
        sqp.nh++;
    }
    if (sqp.ln && !sqp.left[2])                  /* (a mono or legato LAYER whose note ended and none began) */
        sqp_layer_off();
    if (sqp.n && !sqp.left[0])                   /* (the Glide: a chord that ended and none began) */
        sqp_chord_off();
    if (sqp.nb && !sqp.left[1])
        sqp_bass_off();
}

/* the next bar: the pattern's next, or the song's next pattern (an empty one passed over) */
static void sqp_bars(void)                       /* a bar begins: the bar each track plays in it */
{
    const sq_pat_t *p = &sq_st.pat[sqp.pat % SQ_NPAT];
    uint32_t k;
    for (k = 0; k < SQ_NTRK; k++)
        sqp.sb[k] = (uint8_t)sq_trk_bar(p, k, sqp.nbar);
}
static void sqp_rec_stop(void);
static void sqp_next_bar(void)
{
    const sq_pat_t *p = &sq_st.pat[sqp.pat % SQ_NPAT];
    uint32_t i, len;
    if (sqp.rec != 0xFFu && sqp.rlim && sqp.nbar + 1u >= sqp.rlim)   /* (a take from HOME: its bars are up; its last
                                                  * chord ends at this bar's end) */
        sqp_rec_stop();
    len = sq_len(p);
    sqp.t = 0;                                   /* (what sounds lasts on over the bar line: its own count) */
    sqp.nu = sqp.nh = 0;
    if (sqp.next == 0xFFu && (!sqp.song || !sq_st.song.n || sqp.nbar + 1u < len)) {   /* it goes on: alone, round and
                                                  * round (each track on its own length); in the song, to its end */
        sqp.nbar++;
        sqp.bar = (uint8_t)(sqp.nbar % len);
        sqp_bars();
        return;
    }
    sqp.nbar = 0;
    sqp.bar = 0;
    if (sqp.next != 0xFFu) {                     /* another pattern picked (the song: PLAY on it): it comes in now */
        if (sqp.next >= SQ_NPAT && sq_st.song.n) {
            sqp.song = 1;
            sqp.slot = (uint8_t)((sqp.next - SQ_NPAT) % sq_st.song.n);
            sqp.pat = sq_st.song.pat[sqp.slot] % SQ_NPAT;
        } else if (sqp.next < SQ_NPAT) {
            sqp.song = 0;
            sqp.pat = sqp.next;
        }
        sqp.next = 0xFF;
#ifdef CF_TRACE
        fprintf(stderr, "%6u seq comes in: %s%c\n", (unsigned)cf_now_ms, sqp.song ? "the song, " : "", 'A' + sqp.pat);
#endif
        cf_stage_in(sqp.pat);
        sqp_bars();
        return;
    }
    for (i = 0; i < sq_st.song.n; i++) {            /* (at most once round: a song of empty patterns stays put) */
        sqp.slot = (uint8_t)((sqp.slot + 1u) % sq_st.song.n);
        sqp.pat = sq_st.song.pat[sqp.slot] % SQ_NPAT;
        if (!sq_empty(&sq_st.pat[sqp.pat]))
            break;
    }
    cf_stage_in(sqp.pat);
    sqp_bars();
}

static void sqp_drum_offs(void)                  /* the drums' MIDI notes of the last tick go off */
{
    uint32_t l;
    for (l = 0; l < SQ_NLANE && sqp.doff; l++)
        if ((sqp.doff >> l) & 1u) {
            cfp_midi(0x80u, cfp_ch(CF_PART_DRUM), SQ_LANE_NOTE[l], 0);
            sqp.doff &= (uint16_t)~(1u << l);
        }
}
/* a take on a synth track: the chord model changed (the serial): what sounded ends here, what sounds now begins */
static void sqp_rec_watch(void)
{
    const cf_chord_out_t *o = &cfc.out;
    sqr_ev_t e;
    uint32_t i, back;
    if (sqp.rec == 0xFFu || sqp.rec == SQ_DRUMS || o->serial == sqp.rserial)
        return;
    sqp.rserial = o->serial;
    if (sqp.cin >= SQ_PPQN / 4u)                 /* (the count-in: only its last sixteenth counts, as the first step) */
        return;
    back = sqp.rage * SQ_PPQN * cfm.bpm / 60000u;   /* (the key's age: where in the bar it was played) */
    sqp.rage = 0;
    e.pat = sqp.pat;
    e.bar = sqp.sb[sqp.rec % SQ_NTRK];           /* (the take's track's own bar) */
    e.t = sqp.cin ? 0u : sqp.t > back ? sqp.t - back : 0u;
    if (sqp.ropen) {
        e.kind = 2;
        e.dur = sqp.rheld + sqp.rback > back ? sqp.rheld + sqp.rback - back : 1u;
        sqp_rpush(&e);
        sqp.ropen = 0;
    }
    if (!o->sounding || !o->n)
        return;
    sqp.rback = sqp.cin ? 0u : back;
    e.kind = 1;
    e.rkey = (uint8_t)(o->single && o->n > 1u ? 0xFFu : cfc.nroots ? cfc.root_key[cfc.nroots - 1u] : 0xFFu);
    sqp.rheld = 0;                               /* (the root key the chord model built it on: the last one down; a
                                                  * take read the lowest held when the UI got there, and a legato
                                                  * change wrote the chord before) */
    e.n = (uint8_t)(o->n > 6u ? 6u : o->n);
    for (i = 0; i < e.n; i++)
        e.note[i] = o->note[o->n - e.n + i];
    e.root = o->root_pc;
    e.mask = (uint16_t)(o->mask & 0xFFFu);
    e.single = o->single;
    e.btns = o->buttons;
    e.nb = (uint8_t)(cfp.nb < 2u ? cfp.nb : 2u);
    e.bass[0] = cfp.bass[0];
    e.bass[1] = cfp.bass[1];
    sqp_rpush(&e);
    sqp.ropen = 1;
}
static void sqp_tick1(void)
{
    uint32_t i;
    if (sqp.ropen)
        sqp.rheld++;
    sqp_drum_offs();
    for (i = 0; i < 3u; i++)                     /* (what sounds: a tick less to last) */
        if (sqp.left[i])
            sqp.left[i]--;
    if (sqp.cin) {                               /* the count-in: the click alone, then the pattern from its top */
        if (--sqp.cin == 0u) {
            cfm_restart();
            sqp.t = 0;
            sqp_now();
        }
        return;
    }
    if (++sqp.t >= sqp_bar_ticks())
        sqp_next_bar();
    sqp_now();
}

static void sqp_stop(void)
{
    if (!sqp.on)
        return;
    sqp_chord_off();
    sqp_bass_off();
    sqp_layer_off();
    sqp_arp_off(SQ_CHORDS);
    sqp_arp_off(SQ_LAYER);
    sqp_drum_offs();
    sqp.on = 0;
    sqp.click = 0;
    sqp.cin = 0;
    sqp.next = 0xFF;
    if (sqp.rec != 0xFFu && sqp.ropen) {         /* (the take's last chord ends with the stop) */
        sqr_ev_t e;
        e.kind = 2;
        e.pat = sqp.pat;
        e.bar = sqp.sb[sqp.rec % SQ_NTRK];
        e.t = sqp.t;
        e.dur = sqp.rheld + sqp.rback;
        sqp_rpush(&e);
    }
    sqp.ropen = 0;
    sqp.rec = 0xFF;
    sqp.rlim = 0;
    cfm.running = 0;
}
/* PLAY: pattern pat round, or (pat SQ_NPAT) the song from place slot; cin: after a bar of count-in */
static void sqp_begin(uint32_t pat, uint32_t slot, int cin)
{
    uint32_t i;
    sqp_stop();
    sqp.song = pat >= SQ_NPAT;
    sqp.slot = (uint8_t)(sqp.song && sq_st.song.n ? slot % sq_st.song.n : 0u);
    sqp.pat = (uint8_t)(sqp.song ? sq_st.song.pat[sqp.slot] % SQ_NPAT : pat);
    for (i = 0; sqp.song && i < sq_st.song.n && sq_empty(&sq_st.pat[sqp.pat]); i++) {   /* (an empty one passed over,
                                                  * as the song goes on: it played a silent bar first) */
        sqp.slot = (uint8_t)((sqp.slot + 1u) % sq_st.song.n);
        sqp.pat = sq_st.song.pat[sqp.slot] % SQ_NPAT;
    }
    for (i = 0; i < SQ_NLANE; i++)
        sqp.dsk[i] = 0;
    sqp.bar = 0;
    sqp.nbar = 0;
    cf_stage_in(sqp.pat);
    sqp_bars();
    sqp.t = 0;
    sqp.acc = 0;
    sqp.nu = sqp.nh = 0;
    for (i = 0; i < SQ_NTRK; i++)
        sqp.lit[i] = 0xFFu;
    sqp.ckey = sqp.lkey = 0xFF;
    sqp.cbtns = sqp.lbtns = 0;
    sqp.dn = 0;
    sqp.on = 1;
    cfm_restart();
    cfm.running = 1;
    if (cin)
        sqp.cin = sqp_bar_ticks();
    else
        sqp_now();
}
static void sqp_start(uint32_t pat, uint32_t slot) { sqp_begin(pat, slot, 0); }

/* every audio block */
static void sqp_tick(uint32_t n, uint32_t pulses)
{
    uint32_t k;
    if (!sqp.on)
        return;
    sqp_rec_watch();
    if (cfx_in.active) {
        for (k = 0; k < pulses * (SQ_PPQN / 24u); k++)
            sqp_tick1();
        return;
    }
    sqp.acc += n * cfm.bpm * SQ_PPQN;
    if (sqp.acc < FS * 60u)
        return;
    sqp.acc -= FS * 60u;
    sqp_tick1();
}

/* PLAY on another pattern than the one playing (or the song): it comes in when this one ends */
static void sqp_queue(uint32_t pat, uint32_t slot)
{
    if (!sqp.on) {
        sqp_start(pat, slot);
        return;
    }
    sqp.next = (uint8_t)(pat >= SQ_NPAT ? SQ_NPAT + slot % SQ_SONG : pat);
    if ((pat < SQ_NPAT && !sqp.song && pat == sqp.pat) || (pat >= SQ_NPAT && sqp.song))
        sqp.next = 0xFF;                         /* (back to the one playing, or to the song playing: it plays on) */
}
/* REC: a take into track trk of pattern pat. Playing that pattern: at once; else from its top after a bar of
 * count-in */
static void sqp_rec_start(uint32_t pat, uint32_t trk)
{
#ifdef CF_TRACE
    fprintf(stderr, "%6u take start: pattern %c, its count-in now\n", (unsigned)cf_now_ms, (char)('A' + pat % SQ_NPAT));
#endif
    if (!sqp.on || sqp.song || sqp.pat != pat)
        sqp_begin(pat, 0, 1);
    sqp.rec = (uint8_t)trk;
    sqp.click = 1;
    sqp.rserial = cfc.out.serial;                /* (what the keys hold already is not the take's) */
    sqp.ropen = 0;
}
static void sqp_rec_stop(void)                   /* REC again: the take ends, the pattern plays on */
{
    if (sqp.ropen) {
        sqr_ev_t e;
        e.kind = 2;
        e.pat = sqp.pat;
        e.bar = sqp.sb[sqp.rec % SQ_NTRK];
        e.t = sqp.t;
        e.dur = sqp.rheld + sqp.rback;
        sqp_rpush(&e);
        sqp.ropen = 0;
    }
    sqp.rec = 0xFF;
    sqp.rlim = 0;
    sqp.click = 0;
}

/* a drum heard at once (the DRUMS screen: a drum picked, a step set); in a take on DRUMS, a hit there too */
static void sqp_audition(uint32_t lane, uint32_t kit)   /* kit: the drum's, its level 0..100 % above it (bits 8-15) */
{
    sqp_lane_kit(lane, kit & 0xFFu, (kit >> 8) & 0xFFu);
    sqp.lane_t[lane % SQ_NLANE] = cf_now_ms;
    if (sqp.rec == SQ_DRUMS && sqp.on && sqp.cin < SQ_PPQN / 4u) {   /* (the take: where it fell; in the count-in's last
                                                                       * sixteenth, the first step) */
        const sq_pat_t *p = &sq_st.pat[sqp.pat % SQ_NPAT];
        sqr_ev_t e;
        uint32_t sd = sq_dsteps(), h, bd = 0xFFFFFFFFu, l = lane % SQ_NLANE;
        e.kind = 3;
        e.n = (uint8_t)l;
        e.pat = sqp.pat;
        e.bar = sqp.sb[SQ_DRUMS];
        e.t = sqp.cin ? 0u : sqp.t;
        e.h = 0;
        for (h = 0; h <= sd; h++) {              /* (the nearest step as it plays: SWING's late ones late) */
            uint32_t th = h < sd ? sqp_drum_tick(p, h) : sqp_bar_ticks(), d = th > e.t ? th - e.t : e.t - th;
            if (d < bd) {
                bd = d;
                e.h = (uint8_t)h;
            }
        }
        if (e.h >= sqp.nh) {                     /* (a step still to come: it is not struck again there) */
            sqp.dsk[l] = (uint8_t)(e.h < sd ? e.h + 1u : 1u);
            sqp.dskb[l] = (uint8_t)(sqp.nbar + (e.h < sd ? 0u : 1u));   /* (the bar: nbar's low byte) */
            sqp.dskp[l] = sqp.pat;
        }
        sqp_rpush(&e);
        cfp_midi(0x90u, cfp_ch(CF_PART_DRUM), SQ_LANE_NOTE[lane % SQ_NLANE], 100);
        sqp.doff |= (uint16_t)(1u << (lane % SQ_NLANE));
    }
    trk_note_on(&trk[CF_PART_DRUM], SQ_LANE_NOTE[lane % SQ_NLANE], 100);
}
