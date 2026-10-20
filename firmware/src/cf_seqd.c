/* SPDX-License-Identifier: GPL-3.0-only
 * CoralFlower: the sequencer's patterns and song (the data; the SEQUENCER screens edit it).
 *
 * A pattern is 1..8 bars of four tracks, as FL Studio's: CHORDS (chords on the chord sound), BASS (single notes on
 * the bass sound; empty, the bass follows the chords as it does under the hands), LAYER (chords or notes, whatever
 * the keys play, on a sound of its own: part 2) and DRUMS (eight lanes of hits). A bar is a row of steps, each
 * track's own count of them in each bar (the owner's mock-up: "one bar is one row", its STEPS its divisions; a bar of
 * 16ths can follow a bar of 8ths, the hats can run in 16ths over chords in 8ths): the grid the frame walks and new
 * steps go on, so many steps a beat (a bar of any metre is whole beats: the grid stays on them). What is written keeps
 * its time when the grid changes (the owner, 2026-10-19: STEPS turned after a take moved every note, and turned to 6
 * and back lost some): CHORDS / BASS / LAYER keep events (where in its bar a step starts and how long it lasts, in
 * 1/24 of a beat, its notes). DRUMS keep a bit a step for each lane: their grid is fixed (the metronome's beats x 4,
 * the 16 lower keys; x 2 in a bar of more than 4 beats). The song is a row of patterns played in turn. Included by
 * cf_out.c (the audio side plays it; the UI edits it between blocks). */

#define SQ_NPAT 8u                               /* patterns A..H */
#define SQ_BARS 8u                               /* bars a pattern can have */
#define SQ_MAXST 32u                             /* steps a bar can have */
#define SQ_NLANE 11u                             /* drum lanes, one an upper key: kick, snare, clap, hat, open hat,
                                                  * three toms, rim, bell, crash (the owner, 2026-10-19: one tom, no
                                                  * rim shot, no crash was too few) */
#define SQ_NEV 96u                               /* chord, bass and layer steps in a pattern */
#define SQ_SONG 32u                              /* patterns in the song */
enum { SQ_CHORDS, SQ_BASS, SQ_LAYER, SQ_DRUMS, SQ_NTRK };

/* time: 24 parts a beat (every count of steps a beat takes, 1 2 3 4 6 8, divides it), a bar the metronome's beats of
 * them (sq_beats: 2 .. 7; a bar's 96 parts were no grid for 5/4's and 7/4's 10 and 14 steps: CoralFlower, 2026-10-19).
 * The UI keeps sq_beats as cs.beats (cf_ui.c cf_send_metro); both sides read it */
static uint8_t sq_beats = 4;
#define SQ_UB 24u
#define SQ_U ((uint32_t)sq_beats * SQ_UB)
typedef struct {
    uint8_t trk, bar, pos, n;                    /* SQ_CHORDS / BASS / LAYER; the bar; where in it it starts (1/24 of a
                                                  * beat); notes */
    uint16_t len;                                /* how long it lasts (1/24 of a beat; on over the bar lines after) */
    uint16_t mask;                               /* the chord's pitch classes over the root (bit 0: the root) */
    uint8_t note[6];
    uint8_t nb, bass[2];                         /* CHORDS: the bass that sounded with it (played when BASS is empty) */
    uint8_t root;                                /* the root's pitch class (its colour, its name) */
    uint8_t rootkey;                             /* the keys that wrote it (their lights show it; a pattern that follows
                                                  * the key plays what they play now): the root key, 0xFF */
    uint8_t algo_oct;                            /* .. the algorithm (bits 0-1) and OCT + 3 (bits 2-4) they were under */
    uint16_t btns;                               /* .. the chord buttons (cf_chord_out_t.buttons) */
} sq_ev_t;

typedef struct {
    uint8_t bars;                                /* 0: empty (never written); else its first count of bars (how long
                                                  * it plays: sq_len, its longest track's) */
    uint8_t ts[SQ_NTRK];                         /* each track's first bar: its bars ts .. ts + tb - 1 play, the others
                                                  * keep their steps (the screen's green side bars; K4 POSITION moves them
                                                  * up and down, the owner's mock-up). A track's own (the owner,
                                                  * 2026-10-19: PLAYS, now POSITION, turned on the drums moved the chords too) */
    uint8_t tb[SQ_NTRK];                         /* each track's own bars from its ts (BARS in its editor): a shorter
                                                  * track plays round under the longer ones, from start each time,
                                                  * and an empty bar of it is silence (the owner, 2026-10-19: a bar of
                                                  * chords goes on under two of drums; nothing is copied for it) */
    uint8_t steps[SQ_NTRK][SQ_BARS];             /* each track's steps a beat in each bar (its grid there: sq_steps) */
    uint8_t nev;
    uint8_t kit;                                 /* the drum kit (SQ_KITS) */
    uint8_t follow;                              /* its chords and bass follow HOME's KEY and SCALE (the keys a step was
                                                  * written with play what they play now); 0: the notes as written */
    uint8_t tonic, scale;                        /* the key it was written in (fixed: the screen names it) */
    uint8_t dkit[SQ_NLANE];                      /* each drum's kit (SQ_KITS): the kit's own, or another (kits mixed:
                                                  * the owner, 2026-10-19, "а миксовать наборы можно?") */
    uint8_t swing;                               /* DRUMS: 0..100 %, how late every second step of a bar comes (100: half
                                                  * a step, an MPC's 75 %). The drums' K2: their steps a bar stay the
                                                  * metronome's beats x 4, the 16 lower keys (STEPS there was "unusable,
                                                  * no way to tell how to use it": the owner, 2026-10-19) */
    uint8_t arp;                                 /* bit t: track t (CHORDS, LAYER) is played by an arpeggiator (ARP in its
                                                  * editor; in what was a padding byte after SWING: CFQ5 kept) */
    uint8_t mute;                                /* bit t: track t is silent in this pattern (K4 MUTE on the SEQUENCER;
                                                  * the owner, 2026-10-20: "мьютил дорожки на отдельных паттернах, не на
                                                  * всех сразу"). In the padding byte before hit: CFQ6 kept, a backup
                                                  * goes back to 0.4 (without the mutes) */
    uint32_t hit[SQ_NLANE][SQ_BARS];             /* lane l, bar b: bit i, a hit on the drums' step i (sq_dsteps) */
    sq_ev_t ev[SQ_NEV];
    uint8_t dlev[SQ_NLANE];                      /* each drum's level, 0..100 % (K2 in the DRUMS editor: the owner,
                                                  * 2026-10-19). Last: a CFQ5 pattern is this one without it */
} sq_pat_t;

typedef struct {
    uint8_t n;                                   /* patterns in the song */
    uint8_t pat[SQ_SONG];                        /* their indices */
    uint8_t mute;                                /* SQ_MUTE_OWN: the mutes are the patterns' own (sq_pat_t.mute); without
                                                  * it, bit t: track t silent in every pattern (0.4.1-rc4 and before),
                                                  * given to every pattern at power-on (sq_mute_own) */
} sq_song_t;
#define SQ_MUTE_OWN 0x80u

/* all of it in one block, as flash keeps it (cf_sequi.c: six objects of it) */
#define SQ_MAGIC 0x36514643u                     /* "CFQ6" (CFQ5: no drum levels, read in: sq_load; CFQ4: one first
                                                  * bar for every track; CFQ3: one count of bars; CFQ2: 1/96 of a bar,
                                                  * the drums at 1/48; CFQ1: positions in steps, a grid a bar) */
#define SQ_MAGIC5 0x35514643u                    /* "CFQ5" */
#define SQ_PAT5 2524u                            /* .. its pattern's size: this one's up to dlev */
_Static_assert(sizeof(sq_pat_t) == 2536u && __builtin_offsetof(sq_pat_t, dlev) == SQ_PAT5,
               "CFQ6's pattern layout, CFQ5's up to dlev (a change: a new magic)");
_Static_assert(__builtin_offsetof(sq_pat_t, mute) + 1u == __builtin_offsetof(sq_pat_t, hit), "mute: the padding byte");
typedef struct {
    uint32_t magic, size, check;                 /* check: FNV-1a over what follows */
    sq_song_t song;
    sq_pat_t pat[SQ_NPAT];
} sq_store_t;
static sq_store_t sq_st;                         /* (RAM, not the pool: the pool is the engines', 2026-10-18) */

/* the drum kits (KIT, PRESETS on DRUMS): the DRUM engine's eight knobs set together (eng_drum.c: KIT, TUNE, TONE,
 * DECY, SNAP, ACC, KICK, DRV), so a kit changes every drum (its KIT alone swapped three of the eight, and the kit
 * seemed to do nothing: the owner, 2026-10-19). KIT 1: congas and a clave for the toms and the rim (the crash has a
 * lane of its own, so KIT's cymbal for the bell is not used); KICK 1: the round kick */
typedef struct {
    const char *name;
    uint8_t e[8];
} sq_kit_t;
static const sq_kit_t SQ_KITS[] = {
    {"Std", {0, 64, 64, 64, 64, 100, 0, 0}},      /* (the engine's design) */
    {"808", {0, 52, 44, 98, 48, 100, 1, 0}},      /* low, long and dark; the round kick */
    {"909", {0, 66, 84, 58, 88, 110, 0, 24}},     /* bright and snappy, a little drive */
    {"Tight", {0, 74, 78, 30, 92, 100, 0, 10}},   /* short and dry, higher */
    {"Boom", {0, 46, 50, 112, 56, 100, 1, 44}},   /* deep and long, driven */
    {"Lo-fi", {0, 56, 26, 50, 60, 90, 0, 96}},    /* dark, short, dirty */
    {"Hand", {1, 64, 70, 64, 64, 100, 0, 0}},     /* hand drums: congas, claves */
    {"Crush", {0, 70, 100, 70, 104, 120, 0, 127}},   /* bright and fully driven */
};
#define SQ_NKIT ((uint32_t)(sizeof SQ_KITS / sizeof SQ_KITS[0]))

/* the steps a beat takes (K2: quarters, 8ths, 8th triplets, 16ths, 16th triplets, 32nds), a bar's at most SQ_MAXST */
static const uint8_t SQ_KS[] = {1, 2, 3, 4, 6, 8};
#define SQ_KDEF 2u                                /* a synth track's steps a beat at first: 8 a bar in 4/4 */
#define SQ_NKS ((int)(sizeof SQ_KS))
/* the drums' steps a bar: 16ths (the 16 lower keys hold 4 beats of them), 8ths in a longer bar */
static uint32_t sq_dsteps(void) { return sq_beats * 4u <= 16u ? sq_beats * 4u : sq_beats * 2u; }
/* track trk's steps a beat in bar b as it plays: its own, or the most a bar can hold in this metre (32nds written in
 * 4/4 are 40 steps in 5/4: 16th triplets then; the count is kept for a metre that holds it again) */
static uint32_t sq_kbeat(const sq_pat_t *p, uint32_t trk, uint32_t b)
{
    uint32_t k = p->steps[trk % SQ_NTRK][b % SQ_BARS];
    int i;
    for (i = SQ_NKS - 1; k * sq_beats > SQ_MAXST && i >= 0; i--)
        if (SQ_KS[i] < k)
            k = SQ_KS[i];
    return k ? k : 1u;
}
/* track trk's steps in bar b */
static uint32_t sq_steps(const sq_pat_t *p, uint32_t trk, uint32_t b)
{
    return trk == SQ_DRUMS ? sq_dsteps() : sq_kbeat(p, trk, b) * sq_beats;
}

static int sq_empty(const sq_pat_t *p) { return !p->bars; }

/* a pattern written for the first time: 4 bars; chords, bass and layer 2 steps a beat (the owner's default: the
 * metronome's beats x 2); the drums' grid is fixed (sq_dsteps); it follows the key */
static void sq_fresh(sq_pat_t *p, uint32_t beats)
{
    uint32_t b, l;
    (void)beats;
    p->follow = 1;
    p->bars = 4;
    for (l = 0; l < SQ_NTRK; l++) {
        p->tb[l] = 4;
        p->ts[l] = 0;
    }
    p->tb[SQ_DRUMS] = 2;                         /* (the drums 2 bars, round under longer tracks: "сложно прописывать
                                                  * сразу 4, долго ждать приходится или постоянно переключать кнопку
                                                  * PLAY", the owner, 2026-10-20) */
    p->nev = 0;
    p->kit = 0;
    p->swing = 0;
    p->arp = 0;
    p->mute = 0;
    for (l = 0; l < SQ_NLANE; l++)
        p->dlev[l] = 100;
    for (l = 0; l < SQ_NLANE; l++)
        p->dkit[l] = 0;
    for (b = 0; b < SQ_BARS; b++) {
        p->steps[SQ_CHORDS][b] = p->steps[SQ_BASS][b] = p->steps[SQ_LAYER][b] = SQ_KDEF;
        p->steps[SQ_DRUMS][b] = 4;                   /* (not read: sq_dsteps) */
        for (l = 0; l < SQ_NLANE; l++)
            p->hit[l][b] = 0;
    }
}

/* what step e plays under the chord settings g (HOME's KEY, SCALE, QUANTIZE, the buttons' forms): its keys pressed
 * again in the chord model c (its own algorithm and octave), the notes, root and bass they give now into *o. 0 when
 * it has no keys (then the notes as written stand) */
static int sq_resolve(cf_chord_t *c, const sq_ev_t *e, const cf_chord_cfg_t *g, sq_ev_t *o)
{
    cf_chord_cfg_t cfg = *g;
    const cf_chord_out_t *r = &c->out;
    uint8_t nb[2];
    int k, idx, m;
    uint32_t i;
    if (e->rootkey >= CF_NKEYS)
        return 0;
    cfg.algo = (uint8_t)(e->algo_oct & 3u);
    cfg.octave = (int8_t)(((e->algo_oct >> 2) & 7u) - 3);
    cf_chord_init(c, &cfg);
    for (k = 0; k < CF_NKEYS; k++)               /* the buttons, then the root: the chord, not a bare note */
        if (cf_key_role(cfg.algo, k, &idx) == CF_ROLE_BUTTON && ((e->btns >> idx) & 1u))
            cf_chord_key(c, k, 1);
    cf_chord_key(c, e->rootkey, 1);
    if (!r->sounding || !r->n)
        return 0;
    *o = *e;
    m = cfp_bass_notes_of(r, nb);
    if (e->trk == SQ_BASS) {
        o->n = 1;
        o->note[0] = m ? nb[0] : r->note[0];
        o->root = (uint8_t)(o->note[0] % 12u);
        o->mask = 1;
        o->nb = 0;
        return 1;
    }
    k = r->n > 6u ? r->n - 6 : 0;
    o->n = (uint8_t)(r->n - k);
    for (i = 0; i < o->n; i++)
        o->note[i] = r->note[k + (int)i];
    o->root = r->root_pc;
    o->mask = (uint16_t)(r->single ? 1u : r->mask & 0xFFFu);
    o->nb = (uint8_t)m;
    o->bass[0] = nb[0];
    o->bass[1] = nb[1];
    return 1;
}

static uint32_t sq_ev_start(const sq_pat_t *p, const sq_ev_t *e);
static uint32_t sq_ev_end(const sq_pat_t *p, const sq_ev_t *e);
/* an event that begins inside its bar in this metre (one written further on in a longer bar sleeps, silent and
 * unseen, kept for that metre again: a TIME SIG of fewer beats showed it in the bar below and never played it) */
static int sq_ev_on(const sq_ev_t *e) { return e->pos < SQ_U; }
/* the event of track `trk` on step `s` of bar `b` of its grid: the first that begins in it, else the one that lasts
 * over it (from a step before, a bar above too), -1 none */
static int sq_at(const sq_pat_t *p, uint32_t trk, uint32_t b, uint32_t s)
{
    uint32_t i, u = SQ_U / sq_steps(p, trk, b), s0 = b * SQ_U + s * u, s1 = s0 + u, first = 0xFFFFu;
    int in = -1, over = -1;
    for (i = 0; i < p->nev; i++) {
        const sq_ev_t *e = &p->ev[i];
        uint32_t es;
        if (e->trk != trk || !sq_ev_on(e))
            continue;
        es = sq_ev_start(p, e);
        if (es >= s0 && es < s1 && es < first) {
            first = es;
            in = (int)i;
        } else if (es < s0 && sq_ev_end(p, e) > s0) {
            over = (int)i;
        }
    }
    return in >= 0 ? in : over;
}
static int sq_count(const sq_pat_t *p, uint32_t trk)
{
    uint32_t i;
    int n = 0;
    for (i = 0; i < p->nev; i++)
        n += p->ev[i].trk == trk;
    return n;
}
static int sq_drums(const sq_pat_t *p)
{
    uint32_t l, b, n = 0;
    for (l = 0; l < SQ_NLANE; l++)
        for (b = 0; b < SQ_BARS; b++)
            n |= p->hit[l][b];
    return n != 0;
}

static void sq_del(sq_pat_t *p, uint32_t i)
{
    if (i >= p->nev)
        return;
    p->ev[i] = p->ev[p->nev - 1u];
    p->nev--;
}

/* time in the pattern, in 1/24 of a beat: where an event starts and ends. It may run on into the bars after (the
 * owner, 2026-10-19: a chord at a bar's end could not be made longer, it stopped at the bar line) */
static uint32_t sq_ev_start(const sq_pat_t *p, const sq_ev_t *e)
{
    (void)p;
    return e->bar * SQ_U + e->pos;
}
static uint32_t sq_ev_end(const sq_pat_t *p, const sq_ev_t *e) { return sq_ev_start(p, e) + e->len; }

/* a step written: what of the track starts inside its time goes, what began before and lasts into it ends where it
 * begins. Returns its index, -1 when the pattern is full */
static int sq_put(sq_pat_t *p, const sq_ev_t *e)
{
    uint32_t i, s = sq_ev_start(p, e), t = sq_ev_end(p, e);
    for (i = 0; i < p->nev;) {
        sq_ev_t *o = &p->ev[i];
        uint32_t os, oe;
        if (o->trk != e->trk || !sq_ev_on(o)) {
            i++;
            continue;
        }
        os = sq_ev_start(p, o);
        oe = sq_ev_end(p, o);
        if (os >= s && os < t) {
            sq_del(p, i);
            continue;
        }
        if (os < s && oe > s)                    /* (cut back to where the new one begins) */
            o->len = (uint16_t)(s - os);
        i++;
    }
    if (p->nev >= SQ_NEV)
        return -1;
    p->ev[p->nev] = *e;
    return (int)p->nev++;
}

/* how long an event can last (1/24 of a beat): to the next event of its track, in this bar or a later one, or the
 * grid's end */
static uint32_t sq_room(const sq_pat_t *p, const sq_ev_t *e)
{
    uint32_t i, s = sq_ev_start(p, e), end = SQ_BARS * SQ_U;
    for (i = 0; i < p->nev; i++) {
        const sq_ev_t *o = &p->ev[i];
        uint32_t os;
        if (o == e || o->trk != e->trk || !sq_ev_on(o))
            continue;
        os = sq_ev_start(p, o);
        if (os > s && os < end)
            end = os;
    }
    return end > s ? end - s : 1u;
}
/* how long an event lasts in the steps of its bar (LENGTH): the nearest whole count, 1 at least */
static uint32_t sq_len_steps(const sq_pat_t *p, const sq_ev_t *e)
{
    uint32_t su = SQ_U / sq_steps(p, e->trk, e->bar), n = (e->len + su / 2u) / su;
    return n ? n : 1u;
}

/* a track's steps a beat in a bar (K2): its grid there. What is written stays where it is in time (the frame and the
 * steps written next go by the new grid; a step off it stands between its marks) */
static void sq_set_steps(sq_pat_t *p, uint32_t trk, uint32_t b, uint32_t k) { p->steps[trk][b] = (uint8_t)k; }

/* track trk's first bar (K4 POSITION) */
static uint32_t sq_ts(const sq_pat_t *p, uint32_t trk)
{
    uint32_t s = p->ts[trk % SQ_NTRK];
    return s < SQ_BARS ? s : SQ_BARS - 1u;
}
/* track trk's own bars (1 .. the grid from its first) */
static uint32_t sq_tbars(const sq_pat_t *p, uint32_t trk)
{
    uint32_t n = p->tb[trk % SQ_NTRK], lim = SQ_BARS - sq_ts(p, trk);
    return n < 1u ? 1u : n > lim ? lim : n;
}
/* bar b is one of track trk's own (it plays) */
static int sq_trk_plays(const sq_pat_t *p, uint32_t trk, uint32_t b)
{
    return b >= sq_ts(p, trk) && b < sq_ts(p, trk) + sq_tbars(p, trk);
}
/* the bar track trk plays n bars after the pattern began: its own bars round from start, on and on (a track of 3
 * under one of 4 goes 1 2 3 1 2 3, they meet again every 12; the owner, 2026-10-19: its first bar came twice in a row
 * when everything began again with the longest). Every track counts from the same n, so tracks of one length stay
 * together, also when BARS is turned while it plays (from the next bar line) */
static uint32_t sq_trk_bar(const sq_pat_t *p, uint32_t trk, uint32_t n)
{
    return sq_ts(p, trk) + n % sq_tbars(p, trk);
}

/* track trk has something in the bars it plays (what is outside them, kept, does not count) */
static int sq_own(const sq_pat_t *p, uint32_t trk)
{
    uint32_t i, b, b0 = sq_ts(p, trk), b1 = b0 + sq_tbars(p, trk);
    if (trk == SQ_DRUMS) {
        for (i = 0; i < SQ_NLANE; i++)
            for (b = b0; b < b1; b++)
                if (p->hit[i][b])
                    return 1;
        return 0;
    }
    for (i = 0; i < p->nev; i++)
        if (p->ev[i].trk == trk && sq_ev_on(&p->ev[i]) && p->ev[i].bar >= b0 && p->ev[i].bar < b1)
            return 1;
    return 0;
}
/* the bars the pattern plays before it begins again: its longest track's with something in it (an empty track does
 * not make it longer); nothing in it: its longest track's */
static uint32_t sq_len(const sq_pat_t *p)
{
    uint32_t t, n = 0, all = 1;
    for (t = 0; t < SQ_NTRK; t++) {
        uint32_t k = sq_tbars(p, t);
        if (k > all)
            all = k;
        if (k > n && sq_own(p, t))
            n = k;
    }
    return n ? n : all;
}
/* track trk's bars (K3 in its editor): it grows at its foot, up to the grid's end; a bar that comes in has what it
 * had (nothing: silence), the bars let go keep theirs. Nothing is copied or repeated into a bar by itself (the owner,
 * 2026-10-19: "Повторы там вообще не нужны. Если там пусто, то играет пустота"; a bar written once had come out in
 * the others): a copy is OCT+ held, SELECT turned */
static void sq_set_bars(sq_pat_t *p, uint32_t trk, uint32_t n)
{
    uint32_t lim = SQ_BARS - sq_ts(p, trk);
    p->tb[trk % SQ_NTRK] = (uint8_t)(n < 1u ? 1u : n > lim ? lim : n);
}
/* track trk's window moved (K4 in its editor): up or down a bar, its length kept, the other tracks where they are */
static void sq_set_start(sq_pat_t *p, uint32_t trk, int s)
{
    int hi = (int)SQ_BARS - (int)sq_tbars(p, trk);
    p->ts[trk % SQ_NTRK] = (uint8_t)(s < 0 ? 0 : s > hi ? hi : s);
}
