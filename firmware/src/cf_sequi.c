/* SPDX-License-Identifier: GPL-3.0-only
 * CoralFlower: the SEQUENCER screens' controls (the data: cf_seqd.c; the pictures: cf_seqv.c). Included by cf_ui.c.
 *
 *   SEQ         METRONOME, again the SEQUENCER, again HOME
 *   SEQUENCER   a pattern's four tracks, or the song. ALGORITHM turns the pattern (A..H, then the song), SELECT
 *               picks the track, EDIT opens it; the knobs are the picked track's (the owner, 2026-10-18: the
 *               tracks' controls at hand, the metronome on its own screen): K1 VOLUME, K2 SOUND (DRUMS: KIT),
 *               K3 KEY (the pattern follows HOME's key or stays), K4 MUTE.
 *   the song    a row of patterns, typed: the upper keys are the patterns A..H, a key puts its pattern after the
 *               frame (the owner, 2026-10-18: three knobs to put B after A "непонятно, неинтуитивно"). SELECT moves
 *               the frame, K1 changes the pattern in it, K2 moves it along, SEL takes it out (the pattern stays),
 *               EDIT goes to that pattern, PLAY plays from it.
 *   a track     CHORDS / BASS: SELECT walks the steps, what the keys play is written on the step under the frame
 *               (while a key is down it is the same step, so a root then a button is one chord; the frame stays
 *               there: SELECT moves it), SEL clears it; K1 LENGTH, K2 STEPS (the frame's bar's grid; what is
 *               written stays where it is in time), K3 BARS, K4 PLAYS. DRUMS: the upper keys pick the drum,
 *               the lower keys are the bar's 16 steps, on and off; SELECT the bar, K1 the drum's kit (PRESETS the
 *               whole kit), K2 SWING.
 *               SAVE goes back to the tracks. Everywhere here GLO held shows what each control does. */

static void ui_open(uint32_t scr);
static void cf_pop(uint32_t kind, uint32_t ms);
#define SQ_SONGPG SQ_NPAT                        /* squ.pat: the song */
static const char *const SQ_TRK_NAME[SQ_NTRK] = {"Chords", "Bass", "Layer", "Drums"};

static struct {
    uint8_t pat;                 /* the pattern shown, SQ_SONGPG: the song */
    uint8_t trk;                 /* the track picked */
    uint8_t bar, step;           /* the cursor in the open track */
    uint8_t otrk, tbar[SQ_NTRK], tstep[SQ_NTRK];   /* .. whose it is; every track keeps its own */
    uint8_t len;                 /* the length a step gets when written */
    uint8_t lane;                /* DRUMS: the drum picked */
    uint8_t slot;                /* the song: the place picked */
    uint8_t writing;             /* a gesture writes the cursor's step, until every key is up */
    uint32_t serial;             /* the chord model's serial last seen */
    uint32_t glo_down;           /* when GLO went down on these screens (0: up) */
    uint8_t help;                /* GLO tapped: what every control does, over the screen, till GLO again */
    uint8_t help_hold;           /* .. GLO held: while it is down */
    uint32_t rr;                 /* the take's queue: read so far (cf_seqp.c sqp_rw) */
    uint8_t rtrk;                /* .. the track the take writes */
    uint8_t ropen, rbar, rpos;   /* .. where its sounding chord was written (its bar, its place in it) */
    uint32_t sel_down;           /* SEL down in a track (0: up): a tap clears the step, held with SELECT turned erases
                                  * all the frame passes over (the owner, 2026-10-19) */
    uint8_t cpy;                 /* .. SELECT turned while it is down: erasing */
    uint32_t oct_down;           /* OCT+ down in a track (0: up): a tap is OCT+, held with SELECT turned it copies the
                                  * frame's bar to where the frame goes (the owner's pick, 2026-10-19) */
    uint8_t ocpy, ofrom;         /* .. SELECT turned while it is down; the bar copied */
    uint8_t k3_base;             /* K3 turned: the bars the window had when it began (the bars it adds repeat them) */
    uint32_t k3_ms;              /* .. when it was last turned (a pause of 0.7 s: a new turn, a new phrase) */
} squ = {.len = 1};

#define SQ_ST(p) sq_steps((p), squ.trk, squ.bar)    /* the frame's bar's steps in the open track */
#define SQ_SU(p) (SQ_U / SQ_ST(p))               /* .. a step's time there (1/24 of a beat) */
/* ------------------------------------------------------------- flash --- */
/* the patterns and the song in flash, six objects of sq_st, written once a change has rested and nothing sounds
 * (an erase stops the audio ~45 ms: never while the sequencer plays), only the objects that changed */
#define SQ_CHUNK 3840u
#define SQ_NCHUNK 6u
static uint8_t sq_dirty;
static uint32_t sq_changed_ms, sq_retry_ms;
static uint32_t sq_saved[SQ_NCHUNK];             /* each object's FNV as it was last written or read */
typedef char sq_fits_flash[sizeof(sq_store_t) <= SQ_CHUNK * SQ_NCHUNK ? 1 : -1];

/* undo (REC held, as the looper's): the pattern and the song as they were before the last action, swapped back in;
 * held again, swapped out again (redo). An action: the edits that follow one another within 0.7 s (a knob turned on,
 * a chord written and its length set), or a whole take (REC). pre is the state at the start of each frame's input
 * while no action is under way; the first edit of an action keeps it as the undo */
static struct {
    uint8_t busy, has, pat, prepat, redo;
    uint32_t last;
    sq_pat_t pre, undo;
    sq_song_t spre, sundo;
} sqh;
static uint8_t squ_rec_on(void) { return cf_sn.sq_rec != 0xFFu || squ.ropen; }   /* (a take under way) */
static void sq_undo_poll(void)                   /* every frame, before the input */
{
    if (sqh.busy && !squ_rec_on() && !squ.sel_down && cu.now - sqh.last > 700u)
        sqh.busy = 0;
    if (!sqh.busy) {
        sqh.prepat = (uint8_t)(squ.pat < SQ_NPAT ? squ.pat : 0u);
        sqh.pre = sq_st.pat[sqh.prepat];
        sqh.spre = sq_st.song;
    }
}
static void sq_touch(void)
{
    if (!sqh.busy) {                             /* a new action: what was before it is the undo */
        sqh.undo = sqh.pre;
        sqh.sundo = sqh.spre;
        sqh.pat = sqh.prepat;
        sqh.has = 1;
        sqh.redo = 0;
        sqh.busy = 1;
    }
    sqh.last = cu.now;
    sq_dirty = 1;
    sq_changed_ms = cu.now;
}
/* a pattern given its first bars (opened for the first time): not an action to undo; it is where the next one
 * starts from */
static void sq_fresh_kept(sq_pat_t *p)
{
    sq_fresh(p, cs.beats);
    sqh.busy = 0;
    sqh.prepat = (uint8_t)(p - sq_st.pat);
    sqh.pre = *p;
    sqh.spre = sq_st.song;
    sq_dirty = 1;
    sq_changed_ms = cu.now;
}
/* an action on another pattern than the one shown (SAVE > Pattern: a copy, a clearing): its state before */
static void sq_touch_pat(uint32_t pat)
{
    sqh.undo = sq_st.pat[pat % SQ_NPAT];
    sqh.sundo = sq_st.song;
    sqh.pat = (uint8_t)(pat % SQ_NPAT);
    sqh.has = 1;
    sqh.redo = 0;
    sqh.busy = 1;
    sqh.last = cu.now;
    sq_dirty = 1;
    sq_changed_ms = cu.now;
}
static void sq_undo(void)                        /* REC held */
{
    sq_pat_t t;
    sq_song_t g;
    if (!sqh.has) {
        cf_message("Nothing to undo");
        return;
    }
    t = sq_st.pat[sqh.pat];
    sq_st.pat[sqh.pat] = sqh.undo;
    sqh.undo = t;
    g = sq_st.song;
    sq_st.song = sqh.sundo;
    sqh.sundo = g;
    sqh.redo ^= 1u;
    sqh.busy = 0;
    sq_dirty = 1;
    sq_changed_ms = cu.now;
    if (squ.slot >= sq_st.song.n)
        squ.slot = (uint8_t)(sq_st.song.n ? sq_st.song.n - 1u : 0u);
    cf_message(sqh.redo ? "Undone (hold REC: redo)" : "Redone");
}
static uint32_t sq_chunk_len(uint32_t k)
{
    uint32_t sz = sizeof sq_st, o = k * SQ_CHUNK;
    return o >= sz ? 0u : sz - o < SQ_CHUNK ? sz - o : SQ_CHUNK;
}
static void sq_defaults(void)
{
    uint32_t i;
    uint8_t *b = (uint8_t *)&sq_st;
    for (i = 0; i < sizeof sq_st; i++)
        b[i] = 0;
    sq_st.song.n = 0;                            /* (an empty song: the page says how to add to it) */
}
static void sq_load(void)                        /* power-on */
{
    uint32_t k, got = 0;
    int ok = 1;
    for (k = 0; k < SQ_NCHUNK && sq_chunk_len(k); k++) {
        int n = st_load(OBJ_CFSEQ0 + k, (uint8_t *)&sq_st + k * SQ_CHUNK, sq_chunk_len(k));
        ok &= n == (int)sq_chunk_len(k);
        got |= (uint32_t)(n == (int)sq_chunk_len(k)) << k;
    }
    if (!ok || sq_st.magic != SQ_MAGIC || sq_st.size != sizeof sq_st ||
        sq_st.check != cfs_fnv(&sq_st.song, sizeof sq_st - __builtin_offsetof(sq_store_t, song)) ||
        sq_st.song.n > SQ_SONG)
        sq_defaults();
    for (k = 0; k < SQ_NCHUNK; k++) {            /* (an object not in flash yet: written at the first save, whatever) */
        sq_saved[k] = sq_chunk_len(k) ? cfs_fnv((uint8_t *)&sq_st + k * SQ_CHUNK, sq_chunk_len(k)) : 0u;
        if (!ok || !((got >> k) & 1u))
            sq_saved[k] = ~sq_saved[k];
    }
}
static void sq_save_poll(void)                   /* every frame */
{
    uint32_t k, now = cu.now;
    int err = 0;
    if (!sq_dirty || cr_safe || !cu.booted || now - sq_changed_ms < CF_SET_QUIET_MS || cf_sn.sq_on ||
        now - cfs_sound_ms < CF_SET_SILENT_MS || (sq_retry_ms && (int32_t)(now - sq_retry_ms) < 0))
        return;
    sq_st.magic = SQ_MAGIC;
    sq_st.size = sizeof sq_st;
    sq_st.check = cfs_fnv(&sq_st.song, sizeof sq_st - __builtin_offsetof(sq_store_t, song));
    for (k = 0; k < SQ_NCHUNK && sq_chunk_len(k); k++) {
        uint32_t h = cfs_fnv((uint8_t *)&sq_st + k * SQ_CHUNK, sq_chunk_len(k));
        if (h == sq_saved[k])
            continue;
        if (st_save(OBJ_CFSEQ0 + k, (uint8_t *)&sq_st + k * SQ_CHUNK, sq_chunk_len(k)))
            err = 1;
        else
            sq_saved[k] = h;
    }
    sq_dirty = (uint8_t)err;
    sq_retry_ms = err ? now + 1000u : 0u;        /* (a flash error: once more a second later) */
}

/* where the keys' chords sound: the LAYER's part while the LAYER is open, else the chord part */
static uint8_t sq_live_part(void)
{
    return (uint8_t)((cu.scr == SCR_SQED && squ.trk == SQ_LAYER) || cf_sn.sq_rec == SQ_LAYER ? CF_PART_LAYER :
                     CF_PART_CHORD);
}
static int sq_screen(uint32_t s) { return s == SCR_SQ || s == SCR_SQED; }
static sq_pat_t *sq_cur(void) { return &sq_st.pat[squ.pat < SQ_NPAT ? squ.pat : 0]; }

/* the drums' names as the kit plays them (eng_drum.c drum_lane_name's), short for a knob */
static const char *sq_lane_name(uint32_t l)
{
    static const char *const N[SQ_NLANE] = {"Kick", "Snare", "Clap", "Hat", "Open hat", "Low tom", "Mid tom",
                                            "High tom", "Rim", "Bell", "Crash"};
    static const char *const H[4] = {"Low conga", "Mid conga", "High conga", "Clave"};   /* (KIT 1: hand drums) */
    l %= SQ_NLANE;
    return (SQ_KITS[sq_cur()->dkit[l] % SQ_NKIT].e[0] & 1u) && l >= 5u && l <= 8u ? H[l - 5u] : N[l];
}
/* .. in capitals, 8 letters at most (the knob that turns its kit names it) */
static const char *sq_lane_label(uint32_t l)
{
    static const char *const N[SQ_NLANE] = {"KICK", "SNARE", "CLAP", "HAT", "OPEN HAT", "LOW TOM", "MID TOM",
                                            "HIGH TOM", "RIM", "BELL", "CRASH"};
    static const char *const H[4] = {"CONGA L", "CONGA M", "CONGA H", "CLAVE"};
    l %= SQ_NLANE;
    return (SQ_KITS[sq_cur()->dkit[l] % SQ_NKIT].e[0] & 1u) && l >= 5u && l <= 8u ? H[l - 5u] : N[l];
}
/* the kit's name, "Mixed" when a drum plays another kit */
static const char *sq_kit_name(const sq_pat_t *p)
{
    uint32_t l;
    for (l = 0; l < SQ_NLANE; l++)
        if (p->dkit[l] != p->kit)
            return "Mixed";
    return SQ_KITS[p->kit % SQ_NKIT].name;
}
/* the kit turned whole (PRESETS on DRUMS, KIT on SEQUENCER): every drum in the next kit, the drum picked heard in it
 * while nothing plays */
static void sq_kit_turn(sq_pat_t *p, int d)
{
    uint32_t l;
    p->kit = (uint8_t)clampi(p->kit + d, 0, (int)SQ_NKIT - 1);
    for (l = 0; l < SQ_NLANE; l++)
        p->dkit[l] = p->kit;
    sq_touch();
    if (!cf_sn.sq_on)
        cf_post(CFE_SEQ, 2, squ.lane, p->kit);
}
/* the drum picked in another kit (K1 in DRUMS: kits mixed, a kick of 808 under a snare of 909), heard */
static void sq_drum_kit_turn(sq_pat_t *p, int d)
{
    uint32_t l = squ.lane % SQ_NLANE;
    p->dkit[l] = (uint8_t)clampi(p->dkit[l] + d, 0, (int)SQ_NKIT - 1);
    sq_touch();
    if (!cf_sn.sq_on)
        cf_post(CFE_SEQ, 2, l, p->dkit[l]);
}

static void sq_clamp(void)
{
    sq_pat_t *p = sq_cur();
    if (squ.bar >= SQ_BARS)                      /* (the frame goes over every bar: the ones outside the window are
                                                  * written ahead) */
        squ.bar = SQ_BARS - 1u;
    if (squ.step >= SQ_ST(p))
        squ.step = (uint8_t)(SQ_ST(p) - 1u);
}

/* EDIT on SEQUENCER: the track opens (a pattern never written gets its first bars); on the song: its pattern */
static int sq_open(void)                         /* returns 1: the track's screen opens */
{
    sq_pat_t *p;
    if (squ.pat == SQ_SONGPG) {
        if (sq_st.song.n)
            squ.pat = sq_st.song.pat[squ.slot % sq_st.song.n] % SQ_NPAT;
        return 0;
    }
    p = sq_cur();
    if (sq_empty(p))
        sq_fresh_kept(p);
    squ.tbar[squ.otrk] = squ.bar;                /* (the cursor: the track left keeps its own) */
    squ.tstep[squ.otrk] = squ.step;
    squ.bar = squ.tbar[squ.trk];
    squ.step = squ.tstep[squ.trk];
    squ.otrk = squ.trk;
    sq_clamp();
    squ.writing = 0;
    squ.serial = cf_sn.out.serial;
    return 1;
}

static void sq_erase_here(void);
static void sq_materialize(sq_pat_t *p, uint32_t trk, uint32_t b);
/* SELECT in a track: CHORDS / BASS walk the steps (over the bars, round), DRUMS the bars */
static void sq_select(int d)
{
    sq_pat_t *p = sq_cur();
    if (squ.oct_down) {                          /* OCT+ held: a copy of the frame's bar, its target bars along */
        if (!squ.ocpy) {
            squ.ocpy = 1;
            squ.ofrom = squ.bar;
        }
        squ.bar = (uint8_t)wrapi(squ.bar + d, SQ_BARS);
        sq_clamp();
        return;
    }
    if (squ.sel_down) {                          /* SEL held: an eraser, what the frame is on and all it passes
                                                  * over goes (CHORDS / BASS / LAYER: the steps; DRUMS: the picked
                                                  * drum in the bars) */
        int k, n = d < 0 ? -d : d;
        if (!squ.cpy) {
            squ.cpy = 1;
            sq_erase_here();
        }
        for (k = 0; k < n; k++) {
            if (squ.trk == SQ_DRUMS) {
                squ.bar = (uint8_t)wrapi(squ.bar + (d > 0 ? 1 : -1), SQ_BARS);
            } else {
                int s = squ.step + (d > 0 ? 1 : -1);
                if (s >= SQ_ST(p)) {
                    squ.bar = (uint8_t)((squ.bar + 1u) % SQ_BARS);
                    s = 0;
                } else if (s < 0) {
                    squ.bar = (uint8_t)((squ.bar + SQ_BARS - 1u) % SQ_BARS);
                    s = SQ_ST(p) - 1;
                }
                squ.step = (uint8_t)s;
            }
            sq_erase_here();
        }
        sq_clamp();
        return;
    }
    if (squ.trk == SQ_DRUMS) {
        squ.bar = (uint8_t)wrapi(squ.bar + d, SQ_BARS);
        sq_clamp();
        return;
    }
    while (d) {
        int s = squ.step + (d > 0 ? 1 : -1);
        if (s >= SQ_ST(p)) {
            squ.bar = (uint8_t)((squ.bar + 1u) % SQ_BARS);
            s = 0;
        } else if (s < 0) {
            squ.bar = (uint8_t)((squ.bar + SQ_BARS - 1u) % SQ_BARS);
            s = SQ_ST(p) - 1;
        }
        squ.step = (uint8_t)s;
        d += d > 0 ? -1 : 1;
    }
    squ.writing = 0;
}

/* every frame in CHORDS / BASS: what the keys play goes on the cursor's step */
static void sq_write_poll(void)
{
    const cf_chord_out_t *o = &cf_sn.out;
    sq_pat_t *p = sq_cur();
    if (cu.scr != SCR_SQED || squ.trk == SQ_DRUMS)
        return;
    if (squ_rec_on()) {                          /* (a take writes by time, not into the frame) */
        squ.serial = o->serial;
        squ.writing = 0;
        return;
    }
    if (o->serial != squ.serial) {
        squ.serial = o->serial;
        if (o->sounding && o->n) {
            sq_ev_t e;
            uint32_t i, room;
            sq_materialize(p, squ.trk, squ.bar);   /* (a bar the track repeated: the repeats become its own first) */
            e.trk = squ.trk;
            e.bar = squ.bar;
            e.pos = (uint8_t)(squ.step * SQ_SU(p));
            e.len = 1;
            room = sq_room(p, &e);               /* (up to the next one: a long step does not eat the next; it may
                                                  * run on over the bar line) */
            e.len = (uint16_t)(squ.len * SQ_SU(p) < room ? squ.len * SQ_SU(p) : room);
            if (squ.trk == SQ_BASS) {
                uint8_t n = cf_sn.nb ? cf_sn.bass[0] : o->note[0];
                e.n = 1;
                e.note[0] = n;
                e.root = (uint8_t)(n % 12u);
                e.mask = 1;
                e.nb = 0;
            } else {
                uint32_t k = o->n > 6u ? o->n - 6u : 0u;   /* (more than six: the top six) */
                e.n = (uint8_t)(o->n - k);
                for (i = 0; i < e.n; i++)
                    e.note[i] = o->note[k + i];
                e.root = o->root_pc;
                e.mask = (uint16_t)(o->single ? 1u : o->mask & 0xFFFu);
                e.nb = (uint8_t)(squ.trk == SQ_LAYER ? 0u : cf_sn.nb < 2u ? cf_sn.nb : 2u);   /* (the LAYER: no bass) */
                e.bass[0] = cf_sn.bass[0];
                e.bass[1] = cf_sn.bass[1];
            }
            e.rootkey = 0xFF;                    /* (the keys that made it: the lowest root key down, the buttons) */
            for (i = 0; i < CF_NKEYS && e.rootkey == 0xFF; i++) {
                int idx;
                if (((cu.keys >> i) & 1u) && cf_key_role(cs.chord.algo, (int)i, &idx) == CF_ROLE_ROOT)
                    e.rootkey = (uint8_t)i;
            }
            e.btns = (uint16_t)o->buttons;
            e.algo_oct = (uint8_t)((cs.chord.algo & 3u) | (uint32_t)(cs.chord.octave + 3) << 2);
            p->tonic = cs.chord.tonic;           /* (the key it is written in: a fixed pattern names it) */
            p->scale = cs.chord.scale;
            if (sq_put(p, &e) < 0)
                cf_message("Pattern full");
            sq_touch();
            squ.writing = 1;
        }
    }
    if (squ.writing && !cu.keys)                 /* every key up: the gesture is over. The frame stays on the step
                                                  * (the owner, 2026-10-18: it jumped on after every chord; SELECT
                                                  * moves it, only) */
        squ.writing = 0;
}

/* K3 KEY on SEQUENCER: the pattern follows HOME's KEY and SCALE, or keeps its own. Fixed, it keeps what it plays now:
 * every step resolved under the key of the moment becomes its notes (what you hear stays) */
static cf_chord_t squ_cm;                        /* (the UI's chord model for sq_resolve: the ISR has its own) */
static void sq_follow_toggle(void)
{
    sq_pat_t *p = sq_cur();
    uint32_t i;
    if (squ.pat >= SQ_NPAT || sq_empty(p))
        return;
    if (p->follow) {
        for (i = 0; i < p->nev; i++) {
            sq_ev_t now;
            if (p->ev[i].trk != SQ_DRUMS && sq_resolve(&squ_cm, &p->ev[i], &cs.chord, &now))
                p->ev[i] = now;
        }
        p->tonic = cs.chord.tonic;
        p->scale = cs.chord.scale;
    }
    p->follow ^= 1u;
    sq_touch();
    if (p->follow) {
        cf_message("Follows HOME's key");
    } else {
        char m[24], a[4];
        cf_pc_name(p->tonic % 12u, &cu_spell, a);
        str_cpy(m, "Stays in ", sizeof m);
        str_cpy(m + str_len(m), a, 4);
        str_cpy(m + str_len(m), " ", 2);
        str_cpy(m + str_len(m), CF_SCALES[p->scale % CF_NSCALES].shrt, 8);
        cf_message(m);
    }
}

/* the eraser under the frame (SEL held, SELECT turned): the step there (what sounds on it), or on DRUMS the picked
 * drum's hits in the bar */
static void sq_erase_here(void)
{
    sq_pat_t *p = sq_cur();
    sq_materialize(p, squ.trk, squ.bar);
    if (squ.trk == SQ_DRUMS) {
        if (p->hit[squ.lane][squ.bar]) {
            p->hit[squ.lane][squ.bar] = 0;
            sq_touch();
        }
        return;
    }
    {
        int i = sq_at(p, squ.trk, squ.bar, squ.step);
        if (i >= 0) {
            sq_del(p, (uint32_t)i);
            sq_touch();
        }
    }
}
/* a track's bar copied onto another (OCT+ held, SELECT turned, OCT+ let go): what the track had there goes, its steps
 * and its count of steps come; DRUMS: every drum's hits */
static void sq_copy_track_bar(sq_pat_t *p, uint32_t trk, uint32_t from, uint32_t to)
{
    uint32_t i, l, n;
    if (from == to)
        return;
    p->steps[trk][to] = p->steps[trk][from];
    if (trk == SQ_DRUMS) {
        for (l = 0; l < SQ_NLANE; l++)
            p->hit[l][to] = p->hit[l][from];
        return;
    }
    for (i = 0; i < p->nev;)                     /* (what was there goes) */
        if (p->ev[i].trk == trk && p->ev[i].bar == to)
            sq_del(p, i);
        else
            i++;
    n = p->nev;
    for (i = 0; i < n; i++)
        if (p->ev[i].trk == trk && p->ev[i].bar == from) {
            sq_ev_t e = p->ev[i];
            e.bar = (uint8_t)to;
            if (e.len > sq_room(p, &e))
                e.len = (uint16_t)sq_room(p, &e);
            sq_put(p, &e);
        }
}
/* bar b of a track that repeats its first bars there (sq_src_bar): the repeats up to b become the track's own, so
 * an edit there changes that bar alone and everything sounds as it did (writing into a repeat lengthened the track,
 * and the bars between went silent) */
static void sq_materialize(sq_pat_t *p, uint32_t trk, uint32_t b)
{
    uint32_t n, x;
    if (sq_src_bar(p, trk, b) == b)
        return;
    n = sq_track_bars(p, trk);
    for (x = p->start + n; x <= b; x++)
        sq_copy_track_bar(p, trk, p->start + (x - p->start) % n, x);
    sq_touch();
}
static void sq_oct_poll(void)                    /* every frame: OCT+ let go in a track */
{
    char m[24];
    if (!squ.oct_down)
        return;
    if ((fm1_in.buttons >> panel.btn[B_OCTUP]) & 1u && cu.scr == SCR_SQED)
        return;
    squ.oct_down = 0;
    if (!squ.ocpy) {                             /* a tap: OCT+, as anywhere */
        cs.chord.octave = (int8_t)clampi(cs.chord.octave + 1, -3, 3);
        cf_send_config();
        cf_pop(POP_OCTAVE, 900);
        return;
    }
    squ.ocpy = 0;
    if (cu.scr != SCR_SQED || squ.ofrom == squ.bar)
        return;
    sqh.busy = 0;                                /* (an action of its own) */
    sq_undo_poll();
    sq_touch();
    sq_materialize(sq_cur(), squ.trk, squ.ofrom > squ.bar ? squ.ofrom : squ.bar);   /* (a bar repeated: real first) */
    sq_copy_track_bar(sq_cur(), squ.trk, squ.ofrom, squ.bar);
    str_cpy(m, "Bar ", sizeof m);
    kint(m + str_len(m), squ.ofrom + 1, 0);
    str_cpy(m + str_len(m), " copied to ", 12);
    kint(m + str_len(m), squ.bar + 1, 0);
    cf_message(m);
}
static void sq_clear(void);
static void sq_sel_poll(void)                    /* every frame: SEL let go in a track */
{
    char m[24];
    if (!squ.sel_down)
        return;
    if ((fm1_in.buttons >> panel.btn[B_SCL]) & 1u && cu.scr == SCR_SQED)
        return;
    squ.sel_down = 0;
    if (!squ.cpy) {                              /* a tap: the step cleared */
        if (cu.scr == SCR_SQED)
            sq_clear();
        return;
    }
    squ.cpy = 0;                                 /* (an erase: done as it went) */
    (void)m;
}

/* SEL in CHORDS / BASS: the step under the cursor cleared. DRUMS: nothing (it cleared the drum's bar at one tap, no
 * question asked; the owner, 2026-10-18: not that, not on SEL; a step goes off on its key) */
static void sq_clear(void)
{
    sq_pat_t *p = sq_cur();
    if (squ.trk == SQ_DRUMS)
        return;
    sq_materialize(p, squ.trk, squ.bar);
    sq_touch();
    {
        int i = sq_at(p, squ.trk, squ.bar, squ.step);
        if (i >= 0)
            sq_del(p, (uint32_t)i);
    }
}

/* the song: an upper key A..H puts its pattern after the frame (an empty pattern is said so, not put) */
static void sq_song_add(uint32_t pat)
{
    sq_song_t *g = &sq_st.song;
    uint32_t at = g->n ? squ.slot + 1u : 0u, i;
    char m[24];
    if (sq_empty(&sq_st.pat[pat])) {
        str_cpy(m, "Pattern ", sizeof m);
        m[8] = (char)('A' + pat);
        str_cpy(m + 9, " is empty", 10);
        cf_message(m);
        return;
    }
    if (g->n >= SQ_SONG) {
        cf_message("The song is full");
        return;
    }
    for (i = g->n; i > at; i--)
        g->pat[i] = g->pat[i - 1u];
    g->pat[at] = (uint8_t)pat;
    g->n++;
    squ.slot = (uint8_t)at;
    sq_touch();
}
/* SEL on the song: the pattern in the frame leaves the song (the pattern itself stays) */
static void sq_song_remove(void)
{
    sq_song_t *g = &sq_st.song;
    uint32_t i;
    if (!g->n)
        return;
    for (i = squ.slot; i + 1u < g->n; i++)
        g->pat[i] = g->pat[i + 1u];
    g->n--;
    if (squ.slot && squ.slot >= g->n)
        squ.slot--;
    sq_touch();
}

/* the keys in DRUMS: the upper ones pick the drum, the lower ones are the bar's steps (they play nothing else); on the
 * song the upper ones are the patterns, the lower ones nothing */
static int sq_key(int key, int down)             /* returns 1: the key is the sequencer's */
{
    sq_pat_t *p = sq_cur();
    int k, i = 0;
    if (cu.scr == SCR_SQ && squ.pat == SQ_SONGPG) {
        for (k = 0; k < key; k++)
            i += cf_key_is_upper(k);
        if (down && cf_key_is_upper(key) && i < (int)SQ_NPAT)
            sq_song_add((uint32_t)i);
        return 1;
    }
    if (cu.scr != SCR_SQED || squ.trk != SQ_DRUMS)
        return 0;
    if (!down)
        return 1;
    for (k = 0; k < key; k++)
        i += cf_key_is_upper(k) == cf_key_is_upper(key);
    if (cf_key_is_upper(key)) {
        if (i < (int)SQ_NLANE) {
            squ.lane = (uint8_t)i;
            cf_post(CFE_SEQ, 2, squ.lane, p->dkit[squ.lane % SQ_NLANE]);   /* (the drum heard as it is picked) */
        }
    } else if (i < SQ_ST(p)) {                   /* (a step: on, off) */
        uint32_t *h = &p->hit[squ.lane][squ.bar], m = 1u << i;
        sq_materialize(p, SQ_DRUMS, squ.bar);
        *h ^= m;
        sq_touch();
        if ((*h & m) && !cf_sn.sq_on)
            cf_post(CFE_SEQ, 2, squ.lane, p->dkit[squ.lane % SQ_NLANE]);   /* (a step set: heard, while nothing
                                                                             * plays) */
    }
    return 1;
}

/* REC tapped on these screens: a take into the track in the frame (from SEQUENCER: its track opens, so the notes are
 * seen as they come), or the take ends. On the song: nothing (a take is a track's) */
static int sq_open(void);
static void sq_rec_tap(void)
{
    sq_pat_t *p;
    if (cf_sn.sq_rec != 0xFFu) {
        cf_post(CFE_SEQ, 5, 0, 0);
        return;
    }
    if (squ.pat >= SQ_NPAT)
        return;
    p = sq_cur();
    if (cu.scr == SCR_SQ) {
        if (!sq_open())
            return;
        ui_open(SCR_SQED);
    }
    if (sq_empty(p))
        sq_fresh_kept(p);
    sqh.busy = 0;                                /* (a take is an action of its own, all of it one undo) */
    sq_undo_poll();
    sq_touch();
    squ.rtrk = squ.trk;
    squ.ropen = 0;
    squ.rr = sqp_rw;                             /* (what the queue held before is not this take's) */
    cf_post(CFE_SEQ, 4, squ.pat, squ.trk);
    cf_send_config();                            /* (a LAYER take: the keys play its sound) */
}

/* every frame: the take's queue into the pattern. A chord goes on the track's step nearest to where it began (late in
 * a bar: the next bar's first); it lasts to the step nearest to where it ended. DRUMS: a hit on the nearest step. The
 * writer of the patterns stays the UI (the ISR only says what and when) */
static void sq_rec_poll(void)
{
    uint32_t bt = (uint32_t)cs.beats * 96u;
    while (squ.rr != sqp_rw) {
        sqr_ev_t e = sqp_rq[squ.rr % SQR_N];
        sq_pat_t *p = &sq_st.pat[e.pat % SQ_NPAT];
        uint32_t trk = squ.rtrk, st, step, bar = e.bar;
        squ.rr++;
        if (sq_empty(p) || bar >= SQ_BARS || !bt)
            continue;
        st = sq_steps(p, trk, bar);
        step = (e.t * st + bt / 2u) / bt;
        if (e.kind == 2) {                       /* a chord ended: its length */
            int i;
            if (!squ.ropen)
                continue;
            squ.ropen = 0;
            for (i = 0; i < (int)p->nev; i++)
                if (p->ev[i].trk == trk && p->ev[i].bar == squ.rbar && p->ev[i].pos == squ.rpos)
                    break;
            if (i < (int)p->nev) {               /* (to where it was let go, over bar lines too; past the window's
                                                  * end round to its top) */
                uint32_t s0 = sq_ev_start(p, &p->ev[i]), su = SQ_U / sq_steps(p, trk, squ.rbar), len;
                int32_t d = (int32_t)(bar * SQ_U + e.t * SQ_U / bt) - (int32_t)s0;
                if (d <= 0)
                    d += (int32_t)(p->bars * SQ_U);
                len = ((uint32_t)d + su / 2u) / su * su;
                if (len > sq_room(p, &p->ev[i]))
                    len = sq_room(p, &p->ev[i]);
                p->ev[i].len = (uint16_t)(len ? len : su);
                sq_touch();
            }
            continue;
        }
        if (step >= st) {                        /* (late in the bar: the next bar's first step) */
            step = 0;
            bar = bar + 1u < (uint32_t)p->start + p->bars ? bar + 1u : p->start;
            st = sq_steps(p, trk, bar);
        }
        if (e.kind == 3) {
#ifdef CF_TRACE
            fprintf(stderr, "take: drum %u bar %u step %u of %u (t %u)\n", (unsigned)e.n, (unsigned)bar + 1u,
                    (unsigned)step, (unsigned)st, (unsigned)e.t);
#endif
            if (trk == SQ_DRUMS && step < st) {
                sq_materialize(p, trk, bar);
                p->hit[e.n % SQ_NLANE][bar] |= 1u << step;
                sq_touch();
            }
            continue;
        }
        if (trk == SQ_DRUMS)
            continue;
        {
            sq_ev_t v;
            uint32_t i, k;
            sq_materialize(p, trk, bar);
            v.trk = (uint8_t)trk;
            v.bar = (uint8_t)bar;
            v.pos = (uint8_t)(step * (SQ_U / st));
            v.len = (uint16_t)(SQ_U / st);
            if (trk == SQ_BASS) {
                v.n = 1;
                v.note[0] = e.nb ? e.bass[0] : e.note[0];
                v.root = (uint8_t)(v.note[0] % 12u);
                v.mask = 1;
                v.nb = 0;
            } else {
                v.n = e.n;
                for (i = 0; i < e.n; i++)
                    v.note[i] = e.note[i];
                v.root = e.root;
                v.mask = (uint16_t)(e.single ? 1u : e.mask);
                v.nb = (uint8_t)(trk == SQ_LAYER ? 0u : e.nb);
                v.bass[0] = e.bass[0];
                v.bass[1] = e.bass[1];
            }
            v.btns = e.btns;
            v.rootkey = 0xFF;                    /* (the root key down now: the take's chord's, near enough) */
            for (k = 0; k < CF_NKEYS && v.rootkey == 0xFF; k++) {
                int idx;
                if (((cu.keys >> k) & 1u) && cf_key_role(cs.chord.algo, (int)k, &idx) == CF_ROLE_ROOT)
                    v.rootkey = (uint8_t)k;
            }
            v.algo_oct = (uint8_t)((cs.chord.algo & 3u) | (uint32_t)(cs.chord.octave + 3) << 2);
#ifdef CF_TRACE
            fprintf(stderr, "take: %c bar %u step %u of %u (t %u)\n", 'A' + e.pat % SQ_NPAT, (unsigned)bar + 1u,
                    (unsigned)step, (unsigned)st, (unsigned)e.t);
#endif
            if (sq_put(p, &v) >= 0) {
                squ.ropen = 1;
                squ.rbar = (uint8_t)bar;
                squ.rpos = v.pos;
                sq_touch();
            }
        }
    }
}

/* PLAY on these screens: stop, or play what is shown (a track open: its pattern) */
static void sq_play(void)
{
    int song = cu.scr == SCR_SQ && squ.pat == SQ_SONGPG;
    if (cf_sn.sq_on) {                           /* playing: PLAY stops, whatever is shown (it queued another pattern
                                                  * and seemed not to stop: the owner, 2026-10-19; the pattern shown
                                                  * takes over by itself: sq_algo) */
        cf_post(CFE_SEQ, 0, 0, 0);
        return;
    }
    if (cf_sn.running) {                         /* (the beat alone runs: PLAY stops it) */
        cf_post(CFE_TRANSPORT, 0, 0, 0);
        return;
    }
    if (song && !sq_st.song.n)                   /* an empty song: the beat alone (PLAY works on nothing too: the
                                                  * owner, 2026-10-19) */
        cf_post(CFE_TRANSPORT, 0, 0, 1);
    else                                         /* (an empty pattern plays too: its click, a take into it) */
        cf_post(CFE_SEQ, 1, song ? SQ_NPAT : squ.pat, squ.slot);
}

/* a track's volume: its part's P_LEVEL, kept in the settings (the bass's is the BASS screen's LEVEL too) */
static uint32_t sq_level(uint32_t t)
{
    return t == SQ_BASS ? cs.bass_level : t == SQ_LAYER ? cs.level_layer : t == SQ_DRUMS ? cs.level_drums :
           cs.level_chord;
}
static void sq_level_set(uint32_t t, int v)
{
    uint8_t x = (uint8_t)clampi(v, 0, 127);
    if (t == SQ_BASS)
        cs.bass_level = x;
    else if (t == SQ_LAYER)
        cs.level_layer = x;
    else if (t == SQ_DRUMS)
        cs.level_drums = x;
    else
        cs.level_chord = x;
    cf_send_config();
}

/* SAVE > Pattern (SAVE on the SEQUENCER): the pattern shown (the song: the frame's) copied to another letter, or
 * cleared; K1 TO the letter (an empty one first), K4 ACTION, SAVE held 1 s does it (one undo: REC held) */
static uint8_t sq_save_src, sq_save_to;
static void sq_save_open(void)
{
    uint32_t i;
    sq_save_src = (uint8_t)(squ.pat < SQ_NPAT ? squ.pat : sq_st.song.n ? sq_st.song.pat[squ.slot] % SQ_NPAT : 0u);
    sq_save_to = (uint8_t)((sq_save_src + 1u) % SQ_NPAT);
    for (i = 1; i < SQ_NPAT; i++)                /* (the next empty letter, if there is one) */
        if (sq_empty(&sq_st.pat[(sq_save_src + i) % SQ_NPAT])) {
            sq_save_to = (uint8_t)((sq_save_src + i) % SQ_NPAT);
            break;
        }
    cu.save_del = 0;
}
static void sq_save_knob_info(int k, cf_knob_t *o)
{
    if (k == 0) {
        kfmt(o->name, 12, "TO");
        o->val[0] = (char)('A' + sq_save_to);
        o->val[1] = 0;
        o->dis = cu.save_del;
    } else if (k == 3) {
        kfmt(o->name, 12, "ACTION");
        kfmt(o->val, 14, cu.save_del ? "Clear" : "Copy");
    }
}
static void sq_save_knob_turn(int k, int d)
{
    if (k == 0 && !cu.save_del) {
        int t = sq_save_to;
        do                                       /* (never onto itself) */
            t = wrapi(t + (d > 0 ? 1 : -1), SQ_NPAT);
        while (t == sq_save_src);
        sq_save_to = (uint8_t)t;
    } else if (k == 3) {
        cu.save_del = (uint8_t)(d > 0);
    }
}
static void sq_save_do(void)
{
    char m[24];
    if (sq_empty(&sq_st.pat[sq_save_src])) {
        cf_message("Empty pattern");
        return;
    }
    if (cu.save_del) {
        sq_touch_pat(sq_save_src);
        sq_st.pat[sq_save_src].bars = 0;         /* (empty: its steps go with it on the next write) */
        sq_st.pat[sq_save_src].nev = 0;
        str_cpy(m, "Pattern   cleared", sizeof m);
        m[8] = (char)('A' + sq_save_src);
        squ.pat = sq_save_src;
    } else {
        sq_touch_pat(sq_save_to);
        sq_st.pat[sq_save_to] = sq_st.pat[sq_save_src];
        str_cpy(m, "  copied to  ", sizeof m);
        m[0] = (char)('A' + sq_save_src);
        m[12] = (char)('A' + sq_save_to);
        squ.pat = sq_save_to;                    /* (the copy shown: the next thing is to change it) */
    }
    cf_message(m);
    ui_open(SCR_SQ);
}

/* PRESETS with a track open: that track's sound (BASS the bass's, LAYER its own, DRUMS the kit), as PRESETS turns
 * the chord sound on HOME (the owner, 2026-10-19: how to change the LAYER's sound was nowhere to be seen there) */
static void sq_presets(int d)
{
    sq_pat_t *p = sq_cur();
    if (squ.trk == SQ_DRUMS) {
        char m[24];
        sq_kit_turn(p, d);
        str_cpy(m, "Kit: ", sizeof m);
        str_cpy(m + 5, sq_kit_name(p), 8);
        cf_message(m);
        return;
    }
    if (squ.trk == SQ_BASS) {
        cf_snd_step(CF_PART_BASS, d);
        cs.snd[1] = (uint16_t)cf_snd_pos(CF_PART_BASS);
        if (cs.env_link)
            cf_env_follow();
        cf_pop(POP_SOUND_BASS, 1200);
        return;
    }
    cf_snd_step(CF_PART_LAYER, d);
    cs.snd_layer = (uint16_t)cf_snd_pos(CF_PART_LAYER);
    cf_pop(POP_SOUND_LAYER, 1200);
}

/* the metronome's knobs (METRONOME) */
static void metro_knob_info(int k, cf_knob_t *o)
{
    switch (k) {
    case 0:
        kfmt(o->name, 12, "LEVEL");
        kint(o->val, cs.metro_level, "%");
        o->bar = (int16_t)(cs.metro_level * 255 / 100);
        break;
    case 1: kfmt(o->name, 12, "SOUND"); kfmt(o->val, 14, CF_CLICKS[cs.click % CF_NCLICKS].name); break;
    case 2:
        kfmt(o->name, 12, "TEMPO");
        kint(o->val, cs.bpm, " BPM");
        o->dis = cf_sn.ext_clock;                /* (MIDI clock in: the clock's tempo) */
        break;
    default: {
        char b[8];
        kfmt(o->name, 12, "TIME SIG");
        kint(b, cs.beats, 0);
        str_cpy(o->val, b, 14);
        str_cpy(o->val + str_len(o->val), cs.beats == 6 ? "/8" : "/4", 4);
        break;
    }
    }
}
static void metro_knob_turn(int k, int d)
{
    if (k == 0) {
        cs.metro_level = (uint8_t)clampi(cs.metro_level + d * 5, 0, 100);
    } else if (k == 1) {
        cs.click = (uint8_t)wrapi(cs.click + d, (int)CF_NCLICKS);
        cf_click_sound = cs.click;
    } else if (k == 2) {
        if (!cf_sn.ext_clock)
            cs.bpm = (uint16_t)clampi(cs.bpm + d, 30, 300);
    } else {
        static const uint8_t SIG[] = {2, 3, 4, 5, 6, 7};
        int i;
        for (i = 0; i < 6 && SIG[i] != cs.beats; i++)
            ;
        cs.beats = SIG[clampi((i < 6 ? i : 2) + d, 0, 5)];
    }
    cf_send_metro();
    cf_send_config();                            /* (the tempo: the arpeggiator's too) */
}

static void sq_knob_info(int k, cf_knob_t *o)
{
    sq_pat_t *p = sq_cur();
    if (cu.scr == SCR_SQ && squ.pat == SQ_SONGPG) {
        switch (k) {
        case 0:
            kfmt(o->name, 12, "PATTERN");
            if (squ.slot < sq_st.song.n) {
                o->val[0] = (char)('A' + sq_st.song.pat[squ.slot]);
                o->val[1] = 0;
            } else {
                kfmt(o->val, 14, "-");
            }
            break;
        case 1:
            kfmt(o->name, 12, "MOVE");
            if (sq_st.song.n) {
                char b[6];
                kint(o->val, squ.slot + 1, 0);
                kint(b, sq_st.song.n, 0);
                str_cpy(o->val + str_len(o->val), " of ", 5);
                str_cpy(o->val + str_len(o->val), b, 4);
            } else {
                kfmt(o->val, 14, "-");
            }
            o->dis = sq_st.song.n < 2u;
            break;
        default: metro_knob_info(k, o); break;
        }
        return;
    }
    if (cu.scr == SCR_SQ) {                      /* the picked track's: VOLUME, SOUND (KIT), KEY, MUTE */
        uint32_t t = squ.trk;
        switch (k) {
        case 0: {
            uint32_t v = sq_level(t);
            kfmt(o->name, 12, "VOLUME");
            kint(o->val, (int)(v * 100u / 127u), "%");
            o->bar = (int16_t)(v * 255u / 127u);
            break;
        }
        case 1:
            if (t == SQ_DRUMS) {
                kfmt(o->name, 12, "KIT");
                kfmt(o->val, 14, sq_empty(p) ? "-" : sq_kit_name(p));
                o->dis = (uint8_t)sq_empty(p);
            } else {
                kfmt(o->name, 12, "SOUND");
                kfmt(o->val, 14, cf_snd[t == SQ_LAYER ? CF_PART_LAYER : t == SQ_BASS ? CF_PART_BASS : CF_PART_CHORD].name);
            }
            break;
        case 2:
            kfmt(o->name, 12, "KEY");
            if (sq_empty(p) || t == SQ_DRUMS) {
                kfmt(o->val, 14, "-");
                o->dis = 1;
            } else if (p->follow) {
                kfmt(o->val, 14, "Follows");
            } else {
                char a[4];
                cf_pc_name(p->tonic % 12u, &cu_spell, a);
                kfmt(o->val, 14, a);
                str_cpy(o->val + str_len(o->val), " ", 2);
                str_cpy(o->val + str_len(o->val), CF_SCALES[p->scale % CF_NSCALES].shrt, 8);
            }
            break;
        default:
            kfmt(o->name, 12, "MUTE");
            kfmt(o->val, 14, (sq_st.song.mute >> t) & 1u ? "On" : "Off");
            break;
        }
        return;
    }
    switch (k) {
    case 0:
        if (squ.trk == SQ_DRUMS) {               /* the drum picked: its kit (PRESETS: the whole kit) */
            kfmt(o->name, 12, sq_lane_label(squ.lane));
            kfmt(o->val, 14, SQ_KITS[p->dkit[squ.lane % SQ_NLANE] % SQ_NKIT].name);
        } else {
            int i = sq_at(p, squ.trk, sq_src_bar(p, squ.trk, squ.bar), squ.step);   /* (what plays there) */
            uint32_t n = i >= 0 ? sq_len_steps(p, &p->ev[i]) : squ.len;
            kfmt(o->name, 12, "LENGTH");
            kint(o->val, (int)n, n > 1u ? " steps" : " step");
        }
        break;
    case 1:
        if (squ.trk == SQ_DRUMS) {               /* (the drums: 16 steps, the keys; their groove here) */
            kfmt(o->name, 12, "SWING");
            if (p->swing)
                kint(o->val, p->swing, "%");
            else
                kfmt(o->val, 14, "Off");
            o->bar = (int16_t)(p->swing * 255 / 100);
        } else {
            kfmt(o->name, 12, "STEPS");
            kint(o->val, SQ_ST(p), 0);
        }
        break;
    case 2: kfmt(o->name, 12, "BARS"); kint(o->val, p->bars, 0); break;
    default: {                                   /* the window that plays: its first bar to its last */
        char b[8];
        kfmt(o->name, 12, "PLAYS");
        kint(o->val, p->start + 1, 0);
        if (p->bars > 1u) {
            kint(b, p->start + p->bars, 0);
            str_cpy(o->val + str_len(o->val), "-", 2);
            str_cpy(o->val + str_len(o->val), b, 4);
        }
        break;
    }
    }
}

static void sq_knob_turn(int k, int d)
{
    sq_pat_t *p = sq_cur();
    if (k != 2)
        squ.k3_base = 0;
    if (cu.scr == SCR_SQED || (squ.pat == SQ_SONGPG && k < 2))
        sq_touch();
    if (cu.scr == SCR_SQ && squ.pat == SQ_SONGPG) {
        if (k == 0 && squ.slot < sq_st.song.n) {   /* K1: the pattern in the frame */
            sq_st.song.pat[squ.slot] = (uint8_t)wrapi(sq_st.song.pat[squ.slot] + d, SQ_NPAT);
        } else if (k == 1 && sq_st.song.n > 1u) {   /* K2: the frame's pattern moves along, the frame with it */
            int to = clampi(squ.slot + d, 0, sq_st.song.n - 1);
            uint8_t v = sq_st.song.pat[squ.slot];
            while (squ.slot != to) {
                int nx = squ.slot + (to > squ.slot ? 1 : -1);
                sq_st.song.pat[squ.slot] = sq_st.song.pat[nx];
                squ.slot = (uint8_t)nx;
            }
            sq_st.song.pat[squ.slot] = v;
        } else if (k >= 2) {
            metro_knob_turn(k, d);
        }
        return;
    }
    if (cu.scr == SCR_SQ) {
        uint32_t t = squ.trk;
        if (k == 0) {
            sq_level_set(t, (int)sq_level(t) + d * 2);
        } else if (k == 1) {
            if (t == SQ_DRUMS) {
                if (!sq_empty(p))
                    sq_kit_turn(p, d);
            } else {
                uint32_t part = t == SQ_LAYER ? CF_PART_LAYER : t == SQ_BASS ? CF_PART_BASS : CF_PART_CHORD;
                cf_snd_step(part, d);
                if (part == CF_PART_CHORD)
                    cs.snd[0] = (uint16_t)cf_snd_pos(part);
                else if (part == CF_PART_BASS)
                    cs.snd[1] = (uint16_t)cf_snd_pos(part);
                else
                    cs.snd_layer = (uint16_t)cf_snd_pos(part);
                if (cs.env_link && part != CF_PART_LAYER)   /* (as PRESETS / SELECT do) */
                    cf_env_follow();
            }
        } else if (k == 2) {                     /* KEY: to the right it follows HOME's, to the left it stays */
            if (t != SQ_DRUMS && (d > 0) != (p->follow != 0))
                sq_follow_toggle();
        } else {
            sq_st.song.mute ^= (uint8_t)(1u << t);
            sq_touch();
        }
        return;
    }
    if (k == 0) {
        if (squ.trk == SQ_DRUMS) {
            sq_drum_kit_turn(p, d);
        } else {
            int i = sq_at(p, squ.trk, sq_src_bar(p, squ.trk, squ.bar), squ.step);
            if (i >= 0) {                        /* (a step the track repeats there: its own first) */
                sq_materialize(p, squ.trk, squ.bar);
                i = sq_at(p, squ.trk, squ.bar, squ.step);
            }
            if (i >= 0) {                        /* the step under the cursor, and the next ones written (in whole
                                                  * steps of the grid; up to the next step) */
                sq_ev_t *e = &p->ev[i];
                int n = clampi((int)sq_len_steps(p, e) + d, 1, 255), r = (int)sq_room(p, e);
                int su = (int)(SQ_U / sq_steps(p, e->trk, e->bar));   /* (its bar's steps) */
                e->len = (uint16_t)(n * su < r ? n * su : r);
                squ.len = (uint8_t)sq_len_steps(p, e);
            } else {
                squ.len = (uint8_t)clampi(squ.len + d, 1, SQ_ST(p));
            }
        }
    } else if (k == 1 && squ.trk == SQ_DRUMS) {  /* SWING, 5 % a detent */
        p->swing = (uint8_t)clampi(p->swing + d * 5, 0, 100);
    } else if (k == 1) {
        int i, st = SQ_ST(p), k = p->steps[squ.trk][squ.bar];   /* (steps a beat: 1 2 3 4 6 8) */
        for (i = 0; i < SQ_NKS && SQ_KS[i] != k; i++)
            ;
        i = clampi((i < SQ_NKS ? i : 1) + d, 0, SQ_NKS - 1);
        if (SQ_KS[i] * sq_beats <= SQ_MAXST) {
            squ.step = (uint8_t)(squ.step * SQ_KS[i] * sq_beats / st);
            sq_set_steps(p, squ.trk, squ.bar, SQ_KS[i]);
        }
    } else if (k == 2) {
        if (!squ.k3_base || cu.now - squ.k3_ms > 700u)   /* (a new turn: the phrase the new bars repeat) */
            squ.k3_base = p->bars;
        squ.k3_ms = cu.now;
        sq_set_bars(p, (uint32_t)clampi(p->bars + d, 1, SQ_BARS), squ.k3_base);
    } else {                                     /* K4: the window up and down a bar (the frame goes with it while it
                                                  * is in the window: the bar being worked on stays in sight) */
        int was = p->start, st = SQ_ST(p);
        sq_set_start(p, p->start + d);
        if (sq_plays(p, (uint32_t)squ.bar - (uint32_t)(p->start - was)) && p->start != was) {
            squ.bar = (uint8_t)(squ.bar + (p->start - was));
            squ.step = (uint8_t)(squ.step * SQ_ST(p) / st);
        }
    }
    sq_clamp();
}

/* ALGORITHM on SEQUENCER: the pattern, then the song */
static void sq_algo(int d)
{
    squ.pat = (uint8_t)clampi(squ.pat + d, 0, SQ_SONGPG);
    cv_turned(d);
    if (cf_sn.sq_on && !cf_sn.sq_song && cf_sn.sq_rec == 0xFFu && squ.pat < SQ_NPAT)
        cf_post(CFE_SEQ, 3, squ.pat, 0);         /* (a pattern plays: the one shown takes over at the next bar line;
                                                  * the song's page leaves it playing) */
}
/* SELECT on SEQUENCER: the track (the song: the place) */
static void sq_pick(int d)
{
    if (squ.pat == SQ_SONGPG)
        squ.slot = (uint8_t)(sq_st.song.n ? clampi(squ.slot + d, 0, sq_st.song.n - 1) : 0);
    else
        squ.trk = (uint8_t)clampi(squ.trk + d, 0, SQ_NTRK - 1);
}

/* the drums' key caps: the upper row names the drums, the lower row is the bar's steps; the song's: the upper row the
 * patterns A..H (the one in the frame lit, an empty one dark), the lower row nothing */
static int sq_keycap(int key, cf_keycap_t *o)    /* returns 1: the cap is the sequencer's */
{
    sq_pat_t *p = sq_cur();
    int k, i = 0;
    if (cu.scr == SCR_SQ && squ.pat == SQ_SONGPG) {
        for (k = 0; k < key; k++)
            i += cf_key_is_upper(k);
        o->style = KC_OFF;
        if (cf_key_is_upper(key) && i < (int)SQ_NPAT) {
            o->top[0] = (char)('A' + i);
            o->top[1] = 0;
            o->btn = 1;
            o->style = (cu.keys >> key) & 1u ? KC_DOWN : sq_st.song.n && sq_st.song.pat[squ.slot] == i ? KC_ON :
                       sq_empty(&sq_st.pat[i]) ? KC_OFF : KC_BTN;
        }
        return 1;
    }
    if (cu.scr != SCR_SQED || squ.trk != SQ_DRUMS)
        return 0;
    for (k = 0; k < key; k++)
        i += cf_key_is_upper(k) == cf_key_is_upper(key);
    if (cf_key_is_upper(key)) {
        o->btn = 1;
        if (i < (int)SQ_NLANE) {
            o->icon = (uint8_t)(1 + i);
            o->style = i == squ.lane ? KC_ON : KC_BTN;
        } else {
            o->style = KC_OFF;
        }
    } else if (i < SQ_ST(p)) {
        int beat = (i * cs.beats) % SQ_ST(p) == 0;   /* a beat's step: the beat's number; the others a dot (as a
                                                       * drum machine's 1 . . . 2 . . .; 16 numbers ran together) */
        if (beat)
            kint(o->top, i * cs.beats / SQ_ST(p) + 1, 0);
        else
            kfmt(o->top, 8, ".");
        o->style = (cu.keys >> key) & 1u ? KC_DOWN : (p->hit[squ.lane][sq_src_bar(p, SQ_DRUMS, squ.bar)] >> i) & 1u ? KC_ON :
                   beat ? KC_ROOT : KC_OFF;
    } else {
        o->style = KC_OFF;
    }
    return 1;
}

/* GLO on these screens: the help, as the top bar's hint says (the owner tapped GLO there and got GLOBAL, 2026-10-18).
 * A tap shows it and the next tap hides it; held, it shows while GLO is down. GLOBAL: from any other screen */
static void sq_glo_poll(void)
{
    int held;
    if (!squ.glo_down)
        return;
    held = (fm1_in.buttons >> panel.btn[B_GLO]) & 1u;
    if (held) {
        if (cu.now - squ.glo_down >= 350u)
            squ.help_hold = 1;
        return;
    }
    if (!squ.help_hold)
        squ.help ^= 1u;
    squ.help_hold = 0;
    squ.glo_down = 0;
}
