/* SPDX-License-Identifier: GPL-3.0-only
 * CoralFlower: the sequencer's patterns and song (the data; the SEQUENCER screens edit it).
 *
 * A pattern is 1..8 bars of four tracks, as FL Studio's: CHORDS (chords on the chord sound), BASS (single notes on
 * the bass sound; empty, the bass follows the chords as it does under the hands), LAYER (chords or notes, whatever
 * the keys play, on a sound of its own: part 2) and DRUMS (eight lanes of hits). A bar is a row of steps, each
 * track's own count of them in each bar (the owner's mock-up: "one bar is one row", its STEPS its divisions; a bar of
 * 16ths can follow a bar of 8ths, the hats can run in 16ths over chords in 8ths): the grid the frame walks and new
 * steps go on. What is written keeps its time when the grid changes (the owner, 2026-10-19: STEPS turned after a take
 * moved every note, and turned to 6 and back lost some): CHORDS / BASS / LAYER keep events (where in its bar a step
 * starts and how long it lasts, in 1/96 of a bar, its notes), DRUMS a bit for each 1/48 of a bar for each lane. The
 * song is a row of patterns played in turn. Included by cf_out.c (the audio side plays it; the UI edits it between
 * blocks). */

#define SQ_NPAT 8u                               /* patterns A..H */
#define SQ_BARS 8u                               /* bars a pattern can have */
#define SQ_MAXST 32u                             /* steps a bar can have */
#define SQ_NLANE 11u                             /* drum lanes, one an upper key: kick, snare, clap, hat, open hat,
                                                  * three toms, rim, bell, crash (the owner, 2026-10-19: one tom, no
                                                  * rim shot, no crash was too few) */
#define SQ_NEV 96u                               /* chord, bass and layer steps in a pattern */
#define SQ_SONG 32u                              /* patterns in the song */
enum { SQ_CHORDS, SQ_BASS, SQ_LAYER, SQ_DRUMS, SQ_NTRK };

/* time in a bar: 96 parts (every count of steps a bar takes, 4 .. 32, divides it), the drums' hits 48 (their counts,
 * 4 .. 16, divide it) */
#define SQ_U 96u
#define SQ_H 48u
typedef struct {
    uint8_t trk, bar, pos, n;                    /* SQ_CHORDS / BASS / LAYER; the bar; where in it it starts (1/96 of a
                                                  * bar); notes */
    uint16_t len;                                /* how long it lasts (1/96 of a bar; on over the bar lines after) */
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
    uint8_t bars;                                /* bars it plays, 1..8; 0: empty (never written) */
    uint8_t start;                               /* the first of them: the bars start .. start + bars - 1 play, the
                                                  * others keep their steps (the screen's green side bars; K4 moves
                                                  * them up and down, the owner's mock-up) */
    uint8_t steps[SQ_NTRK][SQ_BARS];             /* each track's steps in each bar (its grid there) */
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
    uint64_t hit[SQ_NLANE][SQ_BARS];             /* lane l, bar b: bit h, a hit at h/48 of the bar */
    sq_ev_t ev[SQ_NEV];
} sq_pat_t;

typedef struct {
    uint8_t n;                                   /* patterns in the song */
    uint8_t pat[SQ_SONG];                        /* their indices */
    uint8_t mute;                                /* bit t: track t is silent (every pattern) */
} sq_song_t;

/* all of it in one block, as flash keeps it (cf_sequi.c: six objects of it) */
#define SQ_MAGIC 0x32514643u                     /* "CFQ2" (CFQ1: positions in steps, a grid a bar) */
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

/* the steps divisions a bar takes (K2): halves of a beat, thirds, quarters, ... */
static const uint8_t SQ_DIVS[] = {4, 6, 8, 12, 16, 24, 32};
#define SQ_NDIVS ((int)(sizeof SQ_DIVS))

static int sq_empty(const sq_pat_t *p) { return !p->bars; }

/* a pattern written for the first time: 4 bars; chords and bass 2 steps a beat (the owner's default: the
 * metronome's beats x 2), the drums 4 (16 in 4/4, as a drum machine's); it follows the key */
static void sq_fresh(sq_pat_t *p, uint32_t beats)
{
    p->follow = 1;
    uint32_t b, l, st = beats * 2u, sd = beats * 4u;
    st = st < 4u ? 4u : st > SQ_MAXST ? SQ_MAXST : st;
    sd = sd > 16u ? beats * 2u : sd;             /* (the drums' steps are the 16 lower keys) */
    p->bars = 4;
    p->start = 0;
    p->nev = 0;
    p->kit = 0;
    p->swing = 0;
    for (l = 0; l < SQ_NLANE; l++)
        p->dkit[l] = 0;
    for (b = 0; b < SQ_BARS; b++) {
        p->steps[SQ_CHORDS][b] = p->steps[SQ_BASS][b] = p->steps[SQ_LAYER][b] = (uint8_t)st;
        p->steps[SQ_DRUMS][b] = (uint8_t)(sd > 16u ? 16u : sd);
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
/* the event of track `trk` on step `s` of bar `b` of its grid: the first that begins in it, else the one that lasts
 * over it (from a step before, a bar above too), -1 none */
static int sq_at(const sq_pat_t *p, uint32_t trk, uint32_t b, uint32_t s)
{
    uint32_t i, u = SQ_U / p->steps[trk][b], s0 = b * SQ_U + s * u, s1 = s0 + u, first = 0xFFFFu;
    int in = -1, over = -1;
    for (i = 0; i < p->nev; i++) {
        const sq_ev_t *e = &p->ev[i];
        uint32_t es;
        if (e->trk != trk)
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
/* DRUMS' step s of bar b: its bits on the 1/48 grid (what a lane has there: hit[l][b] & this) */
static uint64_t sq_span(const sq_pat_t *p, uint32_t b, uint32_t s)
{
    uint32_t w = SQ_H / p->steps[SQ_DRUMS][b];
    return (((uint64_t)1 << w) - 1u) << (s * w);
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
    uint32_t l, b;
    uint64_t n = 0;
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

/* time in the pattern, in 1/96 of a bar: where an event starts and ends. It may run on into the bars after (the
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
        if (o->trk != e->trk) {
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

/* how long an event can last (1/96 of a bar): to the next event of its track, in this bar or a later one, or the
 * grid's end */
static uint32_t sq_room(const sq_pat_t *p, const sq_ev_t *e)
{
    uint32_t i, s = sq_ev_start(p, e), end = SQ_BARS * SQ_U;
    for (i = 0; i < p->nev; i++) {
        const sq_ev_t *o = &p->ev[i];
        uint32_t os;
        if (o == e || o->trk != e->trk)
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
    uint32_t su = SQ_U / p->steps[e->trk][e->bar], n = (e->len + su / 2u) / su;
    return n ? n : 1u;
}

/* a track's steps in a bar (K2): its grid there. What is written stays where it is in time (the frame and the steps
 * written next go by the new grid; a step off it stands between its marks) */
static void sq_set_steps(sq_pat_t *p, uint32_t trk, uint32_t b, uint32_t st) { p->steps[trk][b] = (uint8_t)st; }

static int sq_plays(const sq_pat_t *p, uint32_t b) { return b >= p->start && b < (uint32_t)p->start + p->bars; }

/* the bars played (K3): the window grows at its foot (at the grid's end, at its top); a bar that comes in for the
 * first time (nothing in it) is a copy of the phrase the window had when the turn began (base bars: 1-4 turned to 8
 * gives 5-8 a copy of 1-4, the owner's "duplicate the bars"), so a pattern grows by repeating itself and is then
 * changed (the bars let go keep what they had) */
static void sq_copy_bar(sq_pat_t *p, uint32_t to, uint32_t from)
{
    uint32_t l, i, n = p->nev;
    for (l = 0; l < SQ_NLANE; l++)
        if (p->hit[l][to])
            return;
    for (i = 0; i < p->nev; i++)
        if (p->ev[i].bar == to)
            return;
    for (l = 0; l < SQ_NTRK; l++)
        p->steps[l][to] = p->steps[l][from];
    for (l = 0; l < SQ_NLANE; l++)
        p->hit[l][to] = p->hit[l][from];
    for (i = 0; i < n; i++)
        if (p->ev[i].bar == from && p->nev < SQ_NEV) {
            sq_ev_t e = p->ev[i];
            e.bar = (uint8_t)to;
            p->ev[p->nev++] = e;
        }
}
static void sq_set_bars(sq_pat_t *p, uint32_t n, uint32_t base)
{
    n = n < 1u ? 1u : n > SQ_BARS ? SQ_BARS : n;
    base = base < 1u ? 1u : base;
    while (p->bars < n) {
        if (p->start + p->bars < SQ_BARS) {      /* (at its foot: the phrase again) */
            sq_copy_bar(p, p->start + p->bars, p->start + p->bars % base);
        } else {                                 /* (the grid's end: at its top) */
            p->start--;
            sq_copy_bar(p, p->start, p->start + 1u);
        }
        p->bars++;
    }
    p->bars = (uint8_t)n;
}
/* the window moved (K4): up or down a bar, its length kept */
static void sq_set_start(sq_pat_t *p, int s)
{
    int hi = (int)SQ_BARS - p->bars;
    p->start = (uint8_t)(s < 0 ? 0 : s > hi ? hi : s);
}
