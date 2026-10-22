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
 *               written stays where it is in time), K3 BARS, K4 POSITION. DRUMS: the upper keys pick the drum,
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
    uint32_t edit_down;          /* EDIT down in a CHORDS / BASS / LAYER track (0: up): held, SELECT turned moves the
                                  * frame's chord (the owner, 2026-10-20: "если наводишь на цветочек, зажимаешь EDIT и
                                  * крутишь SELECT, то можно перемещать выбранный аккорд") */
    uint8_t mv, mvi;             /* .. a chord carried: its index in the pattern (it rides with the frame) */
    uint8_t nofr;                /* the frame hidden (CHORDS / BASS / LAYER): a take began, the keys only sound; a
                                  * SELECT turn, PLAY's stop or EDIT opening a track bring it back (the owner,
                                  * 2026-10-20: "Всё же во время проигрывания нужно, чтобы можно было прописывать",
                                  * after a take "после PLAY-паузы можно снова дописывать", "SELECT тоже
                                  * возвращает") */
    uint32_t sel_down;           /* SEL down in a track (0: up): a tap clears the step, held with SELECT turned erases
                                  * all the frame passes over (the owner, 2026-10-19) */
    uint8_t cpy;                 /* .. SELECT turned while it is down: erasing */
    uint32_t oct_down;           /* OCT+ down in a track (0: up): a tap is OCT+, held with SELECT turned it copies the
                                  * frame's bar to where the frame goes (the owner's pick, 2026-10-19) */
    uint8_t ocpy, ofrom;         /* .. SELECT turned while it is down; the bar copied */
} squ = {.len = 1};

/* a take from HOME (cf_sequi.c sq_home_rec, below) */
static struct {
    uint8_t on;                  /* a take under way (its pattern: pat) */
    uint8_t pat, last;           /* .. its pattern; the pattern last recorded or opened (0xFF none) */
    uint8_t song;                /* .. the song's page was opened after it (its song not empty): PLAY on HOME plays
                                  * the song (the owner, 2026-10-20: "Когда включён режим песни, выхожу на главный,
                                  * ставлю стоп, начинаю плей — то он не идёт по песне почему-то") */
    uint8_t seen, rolled;        /* the audio side has the take; .. its count-in is over */
    uint32_t nbar, t, bt;        /* where it was last seen: bars since its first, ticks into the bar, of the bar */
} sqk = {.last = 0xFF};
static void sq_home_end(int drop);
static void sq_home_undo(void);
/* what the first take teaches: one line at a time, each when it is needed, once on a unit (cs.seq_hint, kept with the
 * settings). The owner, 2026-10-20: a four-page tour was "тонна обозначений за раз", and of four hints "3 лишнее, это
 * из интерфейса понятно" (the SEQUENCER page's [EDIT] chip says it): after the first take where it is, back on HOME
 * how to come back, in the first track opened the way out. The rest is in GLO's help */
enum { SQH_TAKE = 1, SQH_HOME = 2, SQH_TRACK = 4 };   /* (cs.seq_hint's bits) */
static uint8_t sqh_scr = 0xFF;                   /* the screen a hint is up on (0xFF: none): it goes with the screen */
static void sq_hint_once(uint32_t bit);

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
    uint8_t busy, has, pat, prepat, redo, pend;  /* pend: an action under way, its undo kept only if it changed
                                                  * something (a SEL on an empty step, a knob at its end took the
                                                  * last undo away: a review, 2026-10-19) */
    uint32_t last;
    sq_pat_t pre, undo;
    sq_song_t spre, sundo;
} sqh;
static uint8_t squ_rec_on(void) { return cf_sn.sq_rec != 0xFFu || squ.ropen; }   /* (a take under way) */
static void sq_commit(void)                      /* an action ended: the state before it is the undo, if it changed */
{
    if (!sqh.pend)
        return;
    sqh.pend = 0;
    if (!memcmp(&sq_st.pat[sqh.prepat], &sqh.pre, sizeof sqh.pre) && !memcmp(&sq_st.song, &sqh.spre, sizeof sqh.spre))
        return;
    sqh.undo = sqh.pre;
    sqh.sundo = sqh.spre;
    sqh.pat = sqh.prepat;
    sqh.has = 1;
    sqh.redo = 0;
}
static void sq_undo_poll(void)                   /* every frame, before the input */
{
    if (sqh.busy && !squ_rec_on() && !squ.sel_down && !squ.edit_down && cu.now - sqh.last > 700u)
        sqh.busy = 0;
    if (!sqh.busy) {
        sq_commit();
        sqh.prepat = (uint8_t)(squ.pat < SQ_NPAT ? squ.pat : 0u);
        sqh.pre = sq_st.pat[sqh.prepat];
        sqh.spre = sq_st.song;
    }
}
static void sq_touch(void)
{
    if (!sqh.busy) {                             /* a new action: what was before it becomes the undo when it ends,
                                                  * if it changed something (sq_commit) */
        sqh.pend = 1;
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
    sq_commit();
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
    sq_commit();
    sqh.pend = 0;
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
    sq_commit();                                 /* (the action under way first: it is what goes back) */
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
    sq_st.song.mute = SQ_MUTE_OWN;               /* (each pattern its own mutes, none yet) */
}
/* a CFQ5 store read in (0.3-rc5 .. rc8: no drum levels, each pattern 12 bytes shorter): the patterns moved to their
 * places, the last first, every drum at 100 %, written again at the next save. 1: it was one */
static int sq_from_v5(uint32_t len)
{
    uint8_t *b = (uint8_t *)sq_st.pat;
    uint32_t i, l, j, osz = __builtin_offsetof(sq_store_t, pat) + SQ_NPAT * SQ_PAT5;
    if (len != osz || sq_st.magic != SQ_MAGIC5 || sq_st.size != osz ||
        sq_st.check != cfs_fnv(&sq_st.song, osz - __builtin_offsetof(sq_store_t, song)) || sq_st.song.n > SQ_SONG)
        return 0;
    for (i = SQ_NPAT; i-- > 0;) {
        for (j = SQ_PAT5; j-- > 0;)              /* (the bytes moved up, from the top: no memmove on the device) */
            b[i * sizeof(sq_pat_t) + j] = b[i * SQ_PAT5 + j];
        for (j = SQ_PAT5; j < sizeof(sq_pat_t); j++)
            b[i * sizeof(sq_pat_t) + j] = 0;
        for (l = 0; l < SQ_NLANE; l++)
            sq_st.pat[i].dlev[l] = 100;
    }
    sq_st.magic = SQ_MAGIC;
    sq_st.size = sizeof sq_st;
    sq_dirty = 1;
    return 1;
}
static void sq_load(void)                        /* power-on */
{
    uint32_t k, got = 0, len = 0;
    int ok = 1;
    for (k = 0; k < SQ_NCHUNK && sq_chunk_len(k); k++) {
        int n = st_load(OBJ_CFSEQ0 + k, (uint8_t *)&sq_st + k * SQ_CHUNK, sq_chunk_len(k));
        ok &= n == (int)sq_chunk_len(k);
        got |= (uint32_t)(n == (int)sq_chunk_len(k)) << k;
        if (n > 0 && len == k * SQ_CHUNK)        /* (read in a row from the start: an older, shorter store too) */
            len += (uint32_t)n;
    }
    if (!ok && sq_from_v5(len))                  /* (the patterns kept; every object written again) */
        got = 0;
    else if (!ok || sq_st.magic != SQ_MAGIC || sq_st.size != sizeof sq_st ||
        sq_st.check != cfs_fnv(&sq_st.song, sizeof sq_st - __builtin_offsetof(sq_store_t, song)) ||
        sq_st.song.n > SQ_SONG)
        sq_defaults();
    for (k = 0; k < SQ_NCHUNK; k++) {            /* (an object not in flash yet: written at the first save, whatever) */
        sq_saved[k] = sq_chunk_len(k) ? cfs_fnv((uint8_t *)&sq_st + k * SQ_CHUNK, sq_chunk_len(k)) : 0u;
        if (!ok || !((got >> k) & 1u))
            sq_saved[k] = ~sq_saved[k];
    }
    if (!(sq_st.song.mute & SQ_MUTE_OWN)) {      /* (the mutes of before, every pattern's: each pattern takes them, and
                                                  * the objects that changed are written at the next save) */
        for (k = 0; k < SQ_NPAT; k++)
            sq_st.pat[k].mute = (uint8_t)(sq_st.song.mute & ((1u << SQ_NTRK) - 1u));
        sq_st.song.mute = SQ_MUTE_OWN;
        sq_dirty = 1;
    }
}
static void sq_save_write(void)                  /* the objects that changed, now (cf_backup.c's LIST too) */
{
    uint32_t k, now = cu.now;
    int err = 0;
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
static void sq_save_poll(void)                   /* every frame */
{
    uint32_t now = cu.now;
    if (!sq_dirty || cr_safe || cfs_restore_lock || !cu.booted || now - sq_changed_ms < CF_SET_QUIET_MS || cf_sn.sq_on ||
        now - cfs_sound_ms < CF_SET_SILENT_MS || (sq_retry_ms && (int32_t)(now - sq_retry_ms) < 0))
        return;
    sq_save_write();
}

/* where the keys' chords sound: the LAYER's part while the LAYER is open, else the chord part */
static uint32_t env_part(void);
static uint32_t cf_save_part(void);
/* the keys play the sound of the track in the frame: the SEQUENCER's row (a pattern's page) or the track open (the
 * owner, 2026-10-20: "Когда наводишь на Layer, хочется уже его звук слышать, когда играешь, а то невозможно выбрать
 * звук"): CHORDS the chords and HOME's bass, LAYER its sound, BASS the bass alone, DRUMS the drums on the upper keys
 * (sq_key) */
static int sq_frame_trk(uint32_t t) { return (cu.scr == SCR_SQED || (cu.scr == SCR_SQ && squ.pat < SQ_NPAT)) && squ.trk == t; }
static uint8_t sq_live_bass(void) { return (uint8_t)(sq_frame_trk(SQ_BASS) || cf_sn.sq_rec == SQ_BASS); }
static uint8_t sq_live_part(void)                /* (and while the LAYER's sound is edited: EDIT, ENV, SAVE) */
{
    return (uint8_t)(sq_frame_trk(SQ_LAYER) || cf_sn.sq_rec == SQ_LAYER ||
                     (cu.scr == SCR_EDIT && cu.edit_part == CF_PART_LAYER) ||
                     (cu.scr == SCR_ENV && env_part() == CF_PART_LAYER) ||
                     (cu.scr == SCR_SAVE && cu.page[SCR_SAVE] == CF_PART_LAYER) ? CF_PART_LAYER : CF_PART_CHORD);
}
static int sq_screen(uint32_t s) { return s == SCR_SQ || s == SCR_SQED; }
/* ARP in a track's editor (CHORDS, LAYER): its own arpeggiator on / off, the ARP page's settings (the owner,
 * 2026-10-19: ARP and the strum did nothing in the sequencer); the steps stay chords. The keys there play as the track
 * does: ARP its own, none on BASS and DRUMS */
static uint8_t sq_keys_arp(void)
{
    if ((cu.scr != SCR_SQED && cu.scr != SCR_SQ) || squ.pat >= SQ_NPAT)
        return cu.arp_on;
    return (uint8_t)((squ.trk == SQ_CHORDS || squ.trk == SQ_LAYER) && (sq_st.pat[squ.pat].arp >> squ.trk) & 1u);
}
static void sq_undo_poll(void);
static void sq_touch(void);
static void sq_arp_toggle(void)
{
    sq_pat_t *p = &sq_st.pat[squ.pat % SQ_NPAT];
    char m[24];
    if (squ.trk != SQ_CHORDS && squ.trk != SQ_LAYER)
        return;
    sqh.busy = 0;                                /* (an action of its own) */
    sq_undo_poll();
    sq_touch();
    p->arp ^= (uint8_t)(1u << squ.trk);
    str_cpy(m, (p->arp >> squ.trk) & 1u ? "ARP on: " : "ARP off: ", sizeof m);
    str_cpy(m + str_len(m), SQ_TRK_NAME[squ.trk], 8);
    cf_message(m);
    cf_send_config();
}
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
/* drum l heard now (its kit, its level) */
static void sq_hear(const sq_pat_t *p, uint32_t l)
{
    l %= SQ_NLANE;
    if (sq_empty(p))                             /* (a pattern never opened: the first kit, full level, as it starts) */
        cf_post(CFE_SEQ, 2, l, (int)(100u << 8));
    else
        cf_post(CFE_SEQ, 2, l, (int)(p->dkit[l] | (uint32_t)(p->dlev[l] > 100u ? 100u : p->dlev[l]) << 8));
}
static void sq_kit_turn(sq_pat_t *p, int d)
{
    uint32_t l;
    p->kit = (uint8_t)clampi(p->kit + d, 0, (int)SQ_NKIT - 1);
    for (l = 0; l < SQ_NLANE; l++)
        p->dkit[l] = p->kit;
    sq_touch();
    if (!cf_sn.sq_on)
        sq_hear(p, squ.lane);
}
/* the drum picked in another kit (K1 in DRUMS: kits mixed, a kick of 808 under a snare of 909), heard */
static void sq_drum_kit_turn(sq_pat_t *p, int d)
{
    uint32_t l = squ.lane % SQ_NLANE;
    p->dkit[l] = (uint8_t)clampi(p->dkit[l] + d, 0, (int)SQ_NKIT - 1);
    sq_touch();
    if (!cf_sn.sq_on)
        sq_hear(p, l);
}
/* the drum picked louder or softer (K2 in DRUMS, 5 % a detent: the owner, 2026-10-19), heard */
static void sq_drum_level_turn(sq_pat_t *p, int d)
{
    uint32_t l = squ.lane % SQ_NLANE;
    p->dlev[l] = (uint8_t)clampi((p->dlev[l] > 100u ? 100 : p->dlev[l]) + d * 5, 0, 100);
    sq_touch();
    if (!cf_sn.sq_on)
        sq_hear(p, l);
}

/* the bar d bars from b in the open track's own bars, round (the frame walks only the bars the track plays: below
 * them it went into rows that do not play and can hardly be seen, and seemed to jump up and down at random; the
 * owner, 2026-10-19: "то вверх перемещает то влево вправо — кринж") */
static uint32_t sq_own_bar(const sq_pat_t *p, int b, int d)
{
    int lo = (int)sq_ts(p, squ.trk), n = (int)sq_tbars(p, squ.trk);
    return (uint32_t)(lo + wrapi(b - lo + d, n));
}
static void sq_clamp(void)
{
    sq_pat_t *p = sq_cur();
    if (!sq_trk_plays(p, squ.trk, squ.bar))      /* (the frame stays in the track's own bars) */
        squ.bar = (uint8_t)(squ.bar < sq_ts(p, squ.trk) ? sq_ts(p, squ.trk) : sq_ts(p, squ.trk) + sq_tbars(p, squ.trk) - 1u);
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
/* SELECT in a track: CHORDS / BASS walk the steps (over the bars, round), DRUMS the bars */
static void sq_select(int d)
{
    sq_pat_t *p = sq_cur();
    if (squ.nofr && squ.trk != SQ_DRUMS) {       /* (the frame hidden: the first turn shows it where it was) */
        squ.nofr = 0;
        return;
    }
    if (squ.oct_down) {                          /* OCT+ held: a copy of the frame's bar, its target bars along */
        if (!squ.ocpy) {
            squ.ocpy = 1;
            squ.ofrom = squ.bar;
        }
        squ.bar = (uint8_t)sq_own_bar(p, squ.bar, d);
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
                squ.bar = (uint8_t)sq_own_bar(p, squ.bar, d > 0 ? 1 : -1);
            } else {
                int s = squ.step + (d > 0 ? 1 : -1);
                if (s >= (int)SQ_ST(p)) {   /* (int: -1 compared unsigned read as past the end: down a bar) */
                    squ.bar = (uint8_t)sq_own_bar(p, squ.bar, 1);
                    s = 0;
                } else if (s < 0) {
                    squ.bar = (uint8_t)sq_own_bar(p, squ.bar, -1);
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
        squ.bar = (uint8_t)sq_own_bar(p, squ.bar, d);
        sq_clamp();
        return;
    }
    while (d) {
        int s = squ.step + (d > 0 ? 1 : -1);
        if (s >= (int)SQ_ST(p)) {   /* (int: -1 compared unsigned read as past the end: down a bar) */
            squ.bar = (uint8_t)sq_own_bar(p, squ.bar, 1);
            s = 0;
        } else if (s < 0) {
            squ.bar = (uint8_t)sq_own_bar(p, squ.bar, -1);
            s = SQ_ST(p) - 1;
        }
        squ.step = (uint8_t)s;
        d += d > 0 ? -1 : 1;
    }
    squ.writing = 0;
}

/* every frame in CHORDS / BASS / LAYER, the frame shown: what the keys play goes on the cursor's step, the pattern
 * playing or stopped (0.4.1). A take writes by time and hides the frame: after it the keys only sound until a SELECT
 * turn or PLAY's stop brings the frame back (0.4: the keys wrote only while stopped, as a chord played along after a
 * take went over its first step) */
static void sq_write_poll(void)
{
    const cf_chord_out_t *o = &cf_sn.out;
    sq_pat_t *p = sq_cur();
    if (cu.scr != SCR_SQED || squ.trk == SQ_DRUMS)
        return;
    static uint8_t was_on;
    if (was_on && !cf_sn.sq_on)                  /* (PLAY's stop: the frame back) */
        squ.nofr = 0;
    was_on = cf_sn.sq_on;
    if (squ_rec_on() || squ.nofr || squ.mv) {    /* (a take writes by time, not into the frame; the frame hidden or
                                                  * a chord carried: the keys sound, nothing is written) */
        squ.serial = o->serial;
        squ.writing = 0;
        return;
    }
    if (o->serial != squ.serial) {
        squ.serial = o->serial;
        if (o->sounding && o->n) {
            sq_ev_t e;
            uint32_t i, room;
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
            fm1_irq_off();                       /* (the keys that made it: the root key the chord model built it on,
                                                  * the last one down; several single notes at once: none, they play
                                                  * as written. The lowest held made a legato change the chord before) */
            e.rootkey = (uint8_t)(o->single && o->n > 1u ? 0xFFu : cfc.nroots ? cfc.root_key[cfc.nroots - 1u] : 0xFFu);
            if (e.rootkey >= CF_NKEYS)
                e.rootkey = 0xFF;                /* (a root from outside: as written, no key's light; 0.4.x reads it) */
            fm1_irq_on();
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
    if (squ.writing && !cu.keys && !cu.xroots)  /* every key up: the gesture is over. The frame stays on the step
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
/* a pattern copied onto another letter (OCT+ held, ALGORITHM turned on the SEQUENCER, OCT+ let go): what the letter
 * had goes (REC held: back); the copy is shown */
static void sq_copy_pattern(uint32_t from, uint32_t to)
{
    char m[24];
    from %= SQ_NPAT;
    to %= SQ_NPAT;
    if (from == to)
        return;
    str_cpy(m, "Pattern   ", sizeof m);
    m[8] = (char)('A' + from);
    if (sq_empty(&sq_st.pat[from])) {
        str_cpy(m + 9, " is empty", 10);
        cf_message(m);
        return;
    }
    sq_touch_pat(to);
    sq_st.pat[to] = sq_st.pat[from];
    {                                            /* (its own sounds with it, tweaked or not: 0.5) */
        uint32_t t;
        for (t = 0; t < 3u; t++) {
            int st = cfk_find(from + 1u, t);
            cfps.r[to][t] = cfps.r[from][t];
            cfk_drop(to + 1u, t);
            if (st >= 0 && st_load(cfk_obj((uint32_t)st), &cfk_rec, sizeof cfk_rec) == (int)sizeof cfk_rec &&
                cfk_valid(&cfk_rec, t) && cfk_rec.edited) {
                uint8_t keep = cfk_own[t];
                int to_st;
                cfk_own[t] = (uint8_t)(to + 1u);   /* (a free store for the copy's owner) */
                to_st = cfk_store_for(t);
                cfk_own[t] = keep;
                cfk_rec.pat1 = (uint8_t)(to + 1u);
                cfk_rec.check = cfs_fnv(&cfk_rec, (uint32_t)__builtin_offsetof(cfk_rec_t, check));
                if (to_st < 3 || cfk_write((uint32_t)to_st, &cfk_rec, 0u) != 0)
                    cf_message("8 pattern sounds tweaked:\ncopied without its tweak");
            }
        }
    }
    str_cpy(m + 9, " copied to  ", 13);
    m[str_len(m) - 1u] = (char)('A' + to);
    cf_message(m);
}
/* hold EDIT, turn SELECT: the frame's chord moves with the frame; let go of EDIT and it lands there as a chord written
 * there would (what starts within it goes, what sounds into it is cut where it begins). One action, one undo; EDIT
 * let go without a turn changes nothing. CHORDS / BASS / LAYER. The frame hidden after a take: EDIT shows it where it
 * was; on an empty step the turn walks it to the next chord, the next turn carries that (the owner on 0.5-rc1:
 * "сочетание edit и селект не работает во время воспроизведения": after a take the hidden frame took the press, and
 * on an empty step the frame stood still) */
static void sq_edit_press(void)
{
    if (squ.trk == SQ_DRUMS || squ_rec_on())
        return;
    squ.nofr = 0;
    squ.edit_down = cu.now ? cu.now : 1u;
    squ.mv = 0;
}
static void sq_carry_turn(int d)                  /* SELECT in a track: the frame, and with EDIT held the chord */
{
    sq_pat_t *p = sq_cur();
    if (squ.edit_down && !squ.mv && !squ.nofr) {
        int i = sq_at(p, squ.trk, squ.bar, squ.step), k;
        if (i < 0 && (squ.sel_down || squ.oct_down))
            return;                              /* (SEL's eraser or OCT+'s copy held as well: none of it) */
        if (i < 0) {                             /* (no chord in the frame: a click takes it to the next chord that
                                                  * way, round the pattern; none in the track: it stays) */
            uint8_t b0 = squ.bar, s0 = squ.step;
            for (k = 0; k < 2048; k++) {
                sq_select(d < 0 ? -1 : 1);
                if (sq_at(p, squ.trk, squ.bar, squ.step) >= 0 || (squ.bar == b0 && squ.step == s0))
                    break;
            }
            return;
        }
        sqh.busy = 0;                            /* (an action of its own: the undo puts it back) */
        sq_undo_poll();
        squ.mv = 1;
        squ.mvi = (uint8_t)i;
        sq_touch();
    }
    sq_select(d);
    if (squ.mv && squ.mvi < p->nev) {            /* (it rides with the frame; what it passes over stays till it lands) */
        p->ev[squ.mvi].bar = squ.bar;
        p->ev[squ.mvi].pos = (uint8_t)(squ.step * SQ_SU(p));
        sq_touch();
    }
}
static void sq_edit_poll(void)                   /* every frame: EDIT let go in a track */
{
    sq_pat_t *p;
    if (!squ.edit_down)
        return;
    if ((fm1_in.buttons >> panel.btn[B_EDIT]) & 1u && cu.scr == SCR_SQED)
        return;
    squ.edit_down = 0;
    p = sq_cur();
    if (squ.mv && squ.mvi < p->nev) {            /* (it lands: as written there) */
        sq_ev_t v = p->ev[squ.mvi];
        int at;
        sq_del(p, squ.mvi);
        at = sq_put(p, &v);
        if (at >= 0 && p->ev[at].len > sq_room(p, &p->ev[at]))
            p->ev[at].len = (uint16_t)sq_room(p, &p->ev[at]);
        sq_touch();
    }
    squ.mv = 0;
}
static void sq_oct_poll(void)                    /* every frame: OCT+ let go in a track or on the SEQUENCER */
{
    char m[24];
    if (!squ.oct_down)
        return;
    if ((fm1_in.buttons >> panel.btn[B_OCTUP]) & 1u && (cu.scr == SCR_SQED || cu.scr == SCR_SQ))
        return;
    squ.oct_down = 0;
    if (!squ.ocpy) {                             /* a tap: OCT+, as anywhere */
        cs.chord.octave = (int8_t)clampi(cs.chord.octave + 1, -3, 3);
        cf_send_config();
        cf_pop(POP_OCTAVE, 900);
        return;
    }
    squ.ocpy = 0;
    if (cu.scr == SCR_SQ) {
        sq_copy_pattern(squ.ofrom, squ.pat);
        return;
    }
    if (cu.scr != SCR_SQED || squ.ofrom == squ.bar || !sq_trk_plays(sq_cur(), squ.trk, squ.ofrom) ||
        !sq_trk_plays(sq_cur(), squ.trk, squ.bar))   /* (both of the track's own bars: K3 or K4 turned meanwhile) */
        return;
    sqh.busy = 0;                                /* (an action of its own) */
    sq_undo_poll();
    sq_touch();
    sq_copy_track_bar(sq_cur(), squ.trk, squ.ofrom, squ.bar);
    sq_clamp();                                  /* (the bar's steps came with it: the frame on one of them) */
    str_cpy(m, "Bar ", sizeof m);
    kint(m + str_len(m), squ.ofrom + 1, 0);
    str_cpy(m + str_len(m), " copied to ", 12);
    kint(m + str_len(m), squ.bar + 1, 0);
    cf_message(m);
}
static void sq_clear(void);
static void sq_erase_track(uint32_t t);
#define SQ_CLEAR_MS 3000u                        /* SEL held on the SEQUENCER: the track in the frame clears (3 s
                                                  * and counted down on the screen: the owner, 2026-10-19) */
/* the track in the frame has something to clear */
static int sq_track_has(const sq_pat_t *p, uint32_t t)
{
    return !sq_empty(p) && (t == SQ_DRUMS ? sq_drums(p) : sq_count(p, t) > 0);
}
static void sq_sel_poll(void)                    /* every frame: SEL let go in a track */
{
    char m[24];
    if (!squ.sel_down)
        return;
    if ((fm1_in.buttons >> panel.btn[B_SCL]) & 1u && cu.scr == SCR_SQ) {   /* held a second: the track goes */
        if (!squ.cpy && squ.pat < SQ_NPAT && cu.now - squ.sel_down >= SQ_CLEAR_MS && sq_track_has(sq_cur(), squ.trk)) {
            squ.cpy = 1;
            sq_erase_track(squ.trk);
            cf_message("Cleared. Hold REC: undo");
        }
        return;
    }
    if ((fm1_in.buttons >> panel.btn[B_SCL]) & 1u && cu.scr == SCR_SQED)
        return;
    squ.sel_down = 0;
    if (!squ.cpy) {                              /* a tap: the step cleared */
        if (cu.scr == SCR_SQED && !squ.nofr)     /* (no frame: nothing to clear) */
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
    sq_touch();
    {
        int i = sq_at(p, squ.trk, squ.bar, squ.step);
        if (i >= 0)
            sq_del(p, (uint32_t)i);
    }
}

/* the song: an upper key A..H puts its pattern at the end, where the "+" waits, and the frame goes to it (an empty
 * pattern is said so, not put). It went in after the frame once, and the "+" moved with the frame: the owner read it
 * as out of place, 2026-10-19 ("плюс съехал"); a block goes elsewhere with MOVE (K2) */
static void sq_song_add(uint32_t pat)
{
    sq_song_t *g = &sq_st.song;
    uint32_t at = g->n, i;
    char m[24];
    sqh.busy = 0;                                /* (each key its own undo step: three quick ones were undone at once) */
    sq_undo_poll();
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
    sqh.busy = 0;                                /* (each tap its own undo step, as a key A..H) */
    sq_undo_poll();
    for (i = squ.slot; i + 1u < g->n; i++)
        g->pat[i] = g->pat[i + 1u];
    g->n--;
    if (squ.slot && squ.slot >= g->n)
        squ.slot--;
    sq_touch();
}

/* the keys in DRUMS: the upper ones pick the drum, the lower ones are the bar's steps (they play nothing else); on the
 * song the upper ones are the patterns, the lower ones nothing */
static uint32_t sq_eaten;                         /* the keys whose press the sequencer took (their release too); a key
                                                  * held as the frame came to the Drums let go in the chord model */
static int sq_key(int key, int down)             /* returns 1: the key is the sequencer's */
{
    sq_pat_t *p = sq_cur();
    int k, i = 0;
    uint32_t bit = 1u << (key % 27);
    if (!down) {
        if (!(sq_eaten & bit))
            return 0;
        sq_eaten &= ~bit;
        return 1;
    }
    if (cu.scr == SCR_SQ && squ.pat == SQ_SONGPG) {
        for (k = 0; k < key; k++)
            i += cf_key_is_upper(k);
        if (cf_key_is_upper(key) && i < (int)SQ_NPAT)
            sq_song_add((uint32_t)i);
        sq_eaten |= bit;
        return 1;
    }
    if (sq_frame_trk(SQ_DRUMS) && cu.scr == SCR_SQ) {   /* the SEQUENCER's Drums row: the upper keys play the drums
                                                  * (the one played picked, as in the track), the lower ones nothing */
        for (k = 0; k < key; k++)
            i += cf_key_is_upper(k);
        if (cf_key_is_upper(key) && i < (int)SQ_NLANE) {
            squ.lane = (uint8_t)i;
            sq_hear(p, squ.lane);
        }
        sq_eaten |= bit;
        return 1;
    }
    if (cu.scr != SCR_SQED || squ.trk != SQ_DRUMS)
        return 0;
    sq_eaten |= bit;
    for (k = 0; k < key; k++)
        i += cf_key_is_upper(k) == cf_key_is_upper(key);
    if (cf_key_is_upper(key)) {
        if (i < (int)SQ_NLANE) {
            squ.lane = (uint8_t)i;
            sq_hear(p, squ.lane);                /* (the drum heard as it is picked) */
        }
    } else if (i < SQ_ST(p)) {                   /* (a step: on, off) */
        uint32_t *h = &p->hit[squ.lane][squ.bar], m = 1u << i;
        *h ^= m;
        sq_touch();
        if ((*h & m) && !cf_sn.sq_on)
            sq_hear(p, squ.lane);                /* (a step set: heard, while nothing plays) */
    }
    return 1;
}

/* REC tapped on these screens: a take into the track in the frame (from SEQUENCER: its track opens, so the notes are
 * seen as they come), or the take ends. On the song: nothing (a take is a track's) */
static int sq_open(void);
/* a take's grid: 16 a bar in 4/4, twice the default STEPS, in every bar of the track left at the default; a bar whose
 * STEPS the player set keeps them, and the take lands on its marks (the owner, 2026-10-20: "Давай всё же при записи с
 * HOME (да и везде) ставить 16, ака 2х по умолчанию, а там уже если пользователь в секвенсоре переключил у такта, то
 * делаем как пользователь переключил"; a bar no longer goes to 32 or 24 by itself). What is written keeps its time:
 * the default's marks are every other one of the new */
static void sq_take_grid(sq_pat_t *p, uint32_t trk)
{
    uint32_t b, b0 = sq_ts(p, trk), b1 = b0 + sq_tbars(p, trk);
    for (b = b0; b < b1 && b < SQ_BARS; b++)
        if (p->steps[trk % SQ_NTRK][b] == SQ_KDEF)
            p->steps[trk % SQ_NTRK][b] = 2u * SQ_KDEF;
}
static void sq_rec_tap(void)
{
    sq_pat_t *p;
    if (sqk.on) {                                /* (a take from HOME: it ends here too) */
        sq_home_end(0);
        return;
    }
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
    sqh.busy = 0;                                /* (a take is an action of its own, all of it one undo: its grid
                                                  * too) */
    sq_undo_poll();
    sq_touch();
    if (squ.trk != SQ_DRUMS) {
        sq_take_grid(p, squ.trk);
        squ.nofr = 1;                            /* (playing along after it writes nothing: the frame hides) */
    }
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
        st = sq_steps(p, trk, bar);         /* (bar: the track's own) */
        step = (e.t * st + bt / 2u) / bt;
        if (e.kind == 2) {                       /* a chord ended: its length */
            int i;
            if (!squ.ropen)
                continue;
            squ.ropen = 0;
            for (i = 0; i < (int)p->nev; i++)
                if (p->ev[i].trk == trk && p->ev[i].bar == squ.rbar && p->ev[i].pos == squ.rpos)
                    break;
            if (i < (int)p->nev) {               /* (as long as it sounded, in whole steps of its bar, one at least:
                                                  * the time it was held, the ISR's count; worked out from where it
                                                  * ended, a chord let go before the step it was rounded up to came
                                                  * out a whole track long) */
                uint32_t su = SQ_U / sq_steps(p, trk, squ.rbar), d = e.dur * SQ_U / bt, len;
                len = (d + su / 2u) / su * su;
                if (len > sq_room(p, &p->ev[i]))
                    len = sq_room(p, &p->ev[i]);
                p->ev[i].len = (uint16_t)(len ? len : su);
                sq_touch();
            }
            continue;
        }
        if (e.kind == 3)
            step = e.h;                          /* (the drums: the step the ISR found, SWING's late ones late) */
        if (step >= st) {                        /* (late in the bar: the next bar's first step) */
            step = 0;
            bar = bar + 1u < sq_ts(p, trk) + sq_tbars(p, trk) ? bar + 1u : sq_ts(p, trk);
            st = sq_steps(p, trk, bar);
        }
        if (e.kind == 3) {
#ifdef CF_TRACE
            fprintf(stderr, "take: drum %u bar %u step %u of %u (t %u)\n", (unsigned)e.n, (unsigned)bar + 1u,
                    (unsigned)step, (unsigned)st, (unsigned)e.t);
#endif
            if (trk == SQ_DRUMS && step < st) {
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
            v.rootkey = e.rkey;                  /* (the key the chord was built on, the ISR's: the lowest held when
                                                  * the UI got there wrote a legato change as the chord before) */
            (void)k;
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

/* ------------------------------------------------------------ REC on HOME --- */
/* 0.4 has no looper (the owner, 2026-10-19: "сразу 0.4 без лупера"). REC tapped away from the SEQUENCER records into a
 * new pattern, the first one with nothing written ("в новый лучше"), its CHORDS track, after a bar counted in ("С
 * отсчёта такта"); REC again ends it in whole bars, at 8 it ends by itself, and the pattern plays on round ("До второго
 * REC, максимум 8 тактов"). PLAY there plays the pattern last recorded or opened ("Играет последний паттерн"), with
 * none the beat alone as before; REC held undoes. All 8 used: a message, nothing recorded ("Сообщение, запись не
 * идёт"). The first take ever opens a panel that says it is the SEQUENCER's and how to open it; later ones a line
 * ("Панель один раз, потом строка") */
static int sq_has(uint32_t pat)                  /* the pattern holds steps (opened and left empty: not) */
{
    const sq_pat_t *p = &sq_st.pat[pat % SQ_NPAT];
    return !sq_empty(p) && (p->nev || sq_drums(p));
}
static int sq_home_free(void)                    /* the first pattern with nothing written, -1 */
{
    uint32_t i;
    for (i = 0; i < SQ_NPAT; i++)
        if (!sq_has(i))
            return (int)i;
    return -1;
}
static void sq_home_end(int drop)                /* the take ends (drop: it goes, the pattern as it was) */
{
    sq_pat_t *p = &sq_st.pat[sqk.pat % SQ_NPAT];
    uint32_t bars, i, n = 0;
    char m[40];
    if (!sqk.on)
        return;
    sqk.on = 0;
    if (cf_sn.sq_rec != 0xFFu || !sqk.seen)
        cf_post(CFE_SEQ, 5, 0, 0);               /* (still taking: it ends now) */
    sq_rec_poll();                               /* (what the queue holds: written now) */
    for (i = 0; i < p->nev; i++)
        n += p->ev[i].trk == SQ_CHORDS;
    if (drop || !sqk.rolled || !n) {             /* (ended in its count-in, or nothing played: no pattern) */
        sq_st.pat[sqk.pat % SQ_NPAT] = sqh.pre;  /* (the state before the take: sq_home_rec's undo) */
        sqh.pend = 0;
        sqh.busy = 0;
        cf_post(CFE_SEQ, 0, 0, 0);
        if (!drop)
            cf_message("Nothing recorded");
        return;
    }
    /* whole bars: the one REC came in, unless it came in its first eighth (late for the bar line before) */
    bars = sqk.nbar + (sqk.bt && sqk.t * 8u > sqk.bt ? 1u : 0u);
    bars = bars < 1u ? 1u : bars > SQ_BARS ? SQ_BARS : bars;
    p->tb[SQ_CHORDS] = (uint8_t)bars;
    p->bars = (uint8_t)bars;
    sq_touch();
    sqk.last = sqk.pat;
    sqk.song = 0;                                /* (a take: its pattern is what PLAY on HOME plays) */
    str_cpy(m, "Recorded as Pattern ", sizeof m);   /* (not "Saved": flash takes it once playback stops and things
                                                  * rest, sq_save_poll) */
    m[20] = (char)('A' + sqk.pat % SQ_NPAT);
    m[21] = 0;
    if (!(cs.seq_hint & SQH_TAKE)) {             /* the first one ever: where it is, longer to read */
        str_cpy(m + 21, "\nin the SEQUENCER", sizeof m - 21u);
        cf_message(m);
        cu.pop_until = cu.now + 2000u;           /* (2 s, two lines; the hints 1.5 s: "подсказки слишком долго
                                                  * висят сейчас, нужно в два-три раза быстрее", the owner, 2026-10-20) */
        cs.seq_hint |= SQH_TAKE;
        sqh_scr = cu.scr;
        return;
    }
    cf_message(m);
}
/* a hint shown once, one line, when it is needed */
static void sq_hint_once(uint32_t bit)
{
    if (cs.seq_hint & bit)
        return;
    cs.seq_hint |= (uint8_t)bit;
    cf_message(bit == SQH_HOME ? "[SEQ] [SEQ] the SEQUENCER" : "[SAVE] back");
    cu.pop_until = cu.now + 1500u;
    sqh_scr = cu.scr;
}
static void sq_home_rec(void)                    /* REC tapped */
{
    sq_pat_t *p;
    int f;
    if (sqk.on) {
        sq_home_end(0);
        return;
    }
    if (cf_sn.sq_rec != 0xFFu) {                 /* (a take begun on a SEQUENCER screen: REC ends it, as there) */
        cf_post(CFE_SEQ, 5, 0, 0);
        return;
    }
    f = sq_home_free();
    if (f < 0) {
        cf_message("All 8 patterns are used:\nclear one in the SEQUENCER");
        return;
    }
    squ.pat = (uint8_t)f;
    squ.trk = SQ_CHORDS;
    sqh.busy = 0;
    sq_undo_poll();                              /* (the undo: the pattern as it was) */
    p = &sq_st.pat[f];
    sq_fresh(p, cs.beats);
    p->tb[SQ_CHORDS] = SQ_BARS;                  /* (8 bars while it records: its end cuts them to the take's) */
    sq_take_grid(p, SQ_CHORDS);                  /* (16 steps a bar, the take's grid) */
    p->tonic = cs.chord.tonic;
    p->scale = cs.chord.scale;
    if (cu.arp_on)                               /* (ARP on under the keys: on the track too, so it plays as heard) */
        p->arp |= (uint8_t)(1u << SQ_CHORDS);
    sq_touch();
    squ.rtrk = SQ_CHORDS;
    squ.ropen = 0;
    squ.rr = sqp_rw;                             /* (what the queue held before is not this take's) */
    sqk.on = 1;
    sqk.pat = (uint8_t)f;
    sqk.seen = sqk.rolled = 0;
    sqk.nbar = sqk.t = sqk.bt = 0;
    squ.bar = squ.step = 0;
    if (sq_open())                               /* the track it writes opens first, then the count-in (the owner,
                                                  * 2026-10-20: "На клик записи лучше сразу открывать окно секвенсора
                                                  * и потом отсчёт давать") */
        ui_open(SCR_SQED);
    squ.nofr = 1;                                /* (after it the keys only sound: sq_select brings the frame) */
    cf_post(CFE_SEQ, 6, (uint8_t)f, SQ_CHORDS);
}
static void sq_home_play(void)                   /* PLAY tapped */
{
    uint32_t pat = squ.pat < SQ_NPAT && sq_has(squ.pat) && sq_screen(cu.scr) ? squ.pat : sqk.last;
    if (sqk.on) {                                /* (taking: it is kept, and all stops) */
        sq_home_end(0);
        cf_post(CFE_SEQ, 0, 0, 0);
        return;
    }
    if (cf_sn.sq_on) {
        cf_post(CFE_SEQ, 0, 0, 0);
        return;
    }
    if (cf_sn.running) {                         /* (the beat alone runs: PLAY stops it) */
        cf_post(CFE_TRANSPORT, 0, 0, 0);
        return;
    }
    if (sqk.song && sq_st.song.n)                /* (the song last chosen: the song, from the block its frame was on,
                                                  * as PLAY on its page: "С блока, где стояла рамка", the owner,
                                                  * 2026-10-20) */
        cf_post(CFE_SEQ, 1, SQ_NPAT, squ.slot < sq_st.song.n ? squ.slot : 0u);
    else if (pat < SQ_NPAT && sq_has(pat))
        cf_post(CFE_SEQ, 1, (uint8_t)pat, 0);
    else
        cf_post(CFE_TRANSPORT, 0, 0, 1);         /* (no pattern yet: the beat alone, as before) */
}
static void sq_home_undo(void)                   /* REC held */
{
    uint32_t pat;
    if (sqk.on) {
        sq_home_end(1);
        cf_message("Take dropped");
        return;
    }
    pat = sqh.pat;
    sq_undo();
    if (cf_sn.sq_on && cf_sn.sq_pat == pat && !sq_has(pat))   /* (the pattern playing is gone: it stops) */
        cf_post(CFE_SEQ, 0, 0, 0);
    if (!sq_has(pat) && squ.pat == pat) {        /* (a take undone: the SEQUENCER shows the pattern before it, not an
                                                  * empty one; the last with steps) */
        uint32_t i;
        if (sqk.last == pat)
            sqk.last = 0xFF;
        for (i = SQ_NPAT; i-- > 0 && sqk.last == 0xFFu;)
            if (sq_has(i))
                sqk.last = (uint8_t)i;
        if (sqk.last < SQ_NPAT)
            squ.pat = sqk.last;
    }
}
/* a loop kept by the looper (0.3.1 and before), or one an older backup put back (cf_backup.c id 31): a pattern's
 * CHORDS track at power-on (the owner, 2026-10-19: "Перенести в паттерн"). Its keys are pressed again through a chord
 * model of its own, in the algorithm it was played in, as the looper played them (RETRIG Wait's gather window too),
 * and each chord is written where it began, as long as it sounded, its bass with it; 8 bars at most (a longer loop's
 * rest is left out). The pattern follows the key, as the loop did. Written to flash at once, then the loop leaves it.
 * No free pattern: the loop stays for the next power-on, and a message says so */
static cfl_data_t sq_loop_buf __attribute__((section(".pool")));
static int sq_bass_of(const cf_chord_out_t *o, uint8_t out[2])   /* (cf_play.c cfp_bass_notes_of, on the settings) */
{
    int n = 0, b;
    if (!cs.bass_on || !o->sounding || o->single || o->bass_pc < 0)
        return 0;
    b = (int)o->root_note - 12 + 12 * cs.bass_oct;
    b += ((o->bass_pc - (b % 12 + 12) % 12 + 18) % 12) - 6;
    while (b < 12)
        b += 12;
    while (b > 96)
        b -= 12;
    out[n++] = (uint8_t)b;
    if (cs.octaver && b + 12 <= 120)
        out[n++] = (uint8_t)(b + 12);
    return n;
}
static void sq_loop_put(sq_pat_t *p, const sq_ev_t *v, uint32_t t0, uint32_t t1)   /* a chord from t0 to t1 (1/24
                                                  * of a beat from the loop's start) */
{
    uint32_t bar = (uint32_t)cs.beats * SQ_UB;
    sq_ev_t e = *v;
    if (!bar || t0 >= SQ_BARS * bar)
        return;
    if (t1 > SQ_BARS * bar)
        t1 = SQ_BARS * bar;
    e.bar = (uint8_t)(t0 / bar);
    e.pos = (uint8_t)(t0 % bar);
    e.len = (uint16_t)(t1 > t0 ? t1 - t0 : 1u);
    (void)sq_put(p, &e);
}
static void sq_from_loop(void)
{
    cfl_data_t *d = &sq_loop_buf;
    static cf_chord_t c;                         /* (static: not on the stack) */
    cf_chord_cfg_t g = cs.chord;
    sq_pat_t *p;
    sq_ev_t v;
    int n = st_load(OBJ_CFLOOP, d, sizeof *d), f, open = 0, idx;
    uint32_t i, t0 = 0, root_t = 0, gather = 50u * CFL_PPQN * cs.bpm / 60000u, bar = (uint32_t)cs.beats * SQ_UB, len, bars;
    char m[56];
    if (n < (int)__builtin_offsetof(cfl_data_t, ev) || d->magic != CFL_MAGIC || !d->len || d->nev > CFL_MAX_EV ||
        n < (int)(__builtin_offsetof(cfl_data_t, ev) + d->nev * sizeof(cfl_ev_t)) || !bar)
        return;
    f = sq_home_free();
    if (f < 0) {
        cf_message("Your loop needs a free pattern:\nclear one in the SEQUENCER");
        return;
    }
    p = &sq_st.pat[f];
    sq_fresh(p, cs.beats);
    p->tonic = cs.chord.tonic;
    p->scale = cs.chord.scale;
    if (d->algo < CF_NALGS)
        g.algo = d->algo;
    cf_chord_init(&c, &g);
    for (i = 0; i < d->nev; i++) {
        const cfl_ev_t *e = &d->ev[i];
        int down = (e->f & CFL_DOWN) != 0u, role = cf_key_role(g.algo, e->key, &idx);
        uint32_t t = e->t / 4u;                  /* (96 ticks a beat -> 24) */
        c.keep = cs.retrig == CF_RT_WAIT && (role != CF_ROLE_BUTTON || e->t - root_t >= gather);
        if (role == CF_ROLE_ROOT && down)
            root_t = e->t;
        if (!cf_chord_key(&c, e->key, down))
            continue;
        if (open)                                /* (the chord before ends here) */
            sq_loop_put(p, &v, t0, t);
        open = c.out.sounding && c.out.n;
        if (open) {
            const cf_chord_out_t *o = &c.out;
            uint32_t k, nn = o->n > 6u ? 6u : o->n;
            for (k = 0; k < sizeof v; k++)
                ((uint8_t *)&v)[k] = 0;
            v.trk = SQ_CHORDS;
            v.n = (uint8_t)nn;
            for (k = 0; k < nn; k++)
                v.note[k] = o->note[o->n - nn + k];
            v.root = o->root_pc;
            v.mask = (uint16_t)(o->single ? 1u : o->mask & 0xFFFu);
            v.nb = (uint8_t)sq_bass_of(o, v.bass);
            v.btns = o->buttons;
            v.rootkey = (uint8_t)(o->single && o->n > 1u ? 0xFFu : c.nroots ? c.root_key[c.nroots - 1u] : 0xFFu);
            v.algo_oct = (uint8_t)((g.algo & 3u) | (uint32_t)(cs.chord.octave + 3) << 2);
            t0 = t;
        }
    }
    len = d->len / 4u;
    if (open)
        sq_loop_put(p, &v, t0, len);
    bars = (len + bar - 1u) / bar;
    bars = bars < 1u ? 1u : bars > SQ_BARS ? SQ_BARS : bars;
    p->tb[SQ_CHORDS] = (uint8_t)bars;
    p->bars = (uint8_t)bars;
    if (!p->nev)                                 /* (no chord in it: no pattern) */
        p->bars = 0;
    sq_dirty = 1;
    sq_save_write();                             /* (in flash before the loop goes: a power-off between loses nothing) */
    if (sq_dirty)                                /* (a flash error: the loop stays for the next power-on) */
        return;
    for (i = 0; i < 16u; i++)
        ((uint8_t *)d)[i] = 0;
    (void)st_save(OBJ_CFLOOP, d, 16u);           /* (as SAVE > Loop > Delete did) */
    if (p->bars) {
        sqk.last = (uint8_t)f;
        str_cpy(m, "Your saved loop is now\nPattern X in the SEQUENCER", sizeof m);
        m[31] = (char)('A' + f);
        cf_message(m);
    }
}
/* ------------------------------------------------------- the patterns' own sounds --- */
/* 0.5: what you hear is the current pattern's sound (the owner's map, 2026-10-21): the pattern playing (the song's
 * block's), else the one PLAY plays (on a pattern's page: that one). Its track's own sound (cf_settings.c cfps), else
 * HOME's. Stopped, a change loads at once; playing, the next pattern's sounds are made ready (cf_sound.c cf_snd_build)
 * and the audio side puts them in at the bar line where it comes in (cf_stage_in) */
static uint8_t sq_snd_force;                     /* (power-on: every part's sound taken again, its tweak with it) */
static uint32_t sq_cur_pat(void)                 /* 0xFF: none (HOME's sounds) */
{
    if (sqp.on)                                  /* (the player's own: cf_sn's copy is a frame behind a swap) */
        return sqp.pat % SQ_NPAT;
    if (sq_screen(cu.scr) && squ.pat < SQ_NPAT && sq_has(squ.pat))
        return squ.pat;
    if (sqk.song && sq_st.song.n)
        return sq_st.song.pat[squ.slot < sq_st.song.n ? squ.slot : 0u] % SQ_NPAT;
    return sqk.last < SQ_NPAT ? sqk.last : 0xFFu;
}
static void sq_snd_want(uint32_t pat, uint32_t part, uint32_t *e, uint32_t *pos)   /* part's sound under pat */
{
    if (pat < SQ_NPAT && cfps_where(pat, part, e, pos))
        return;
    cf_snd_at(part, part == CF_PART_LAYER ? cs.snd_layer : cs.snd[part], e, pos);
}
static uint32_t sq_next_pat(void)                /* playing: the pattern that comes in next (0xFF: the same, round) */
{
    uint32_t nx = sqp.next, i, slot;
    if (nx != 0xFFu)
        return nx < SQ_NPAT ? nx : sq_st.song.n ? sq_st.song.pat[(nx - SQ_NPAT) % sq_st.song.n] % SQ_NPAT : 0xFFu;
    if (!sqp.song || !sq_st.song.n)
        return 0xFFu;
    slot = sqp.slot;
    for (i = 0; i < sq_st.song.n; i++) {          /* (as the player goes on: an empty one passed over) */
        slot = (slot + 1u) % sq_st.song.n;
        if (!sq_empty(&sq_st.pat[sq_st.song.pat[slot] % SQ_NPAT]))
            return sq_st.song.pat[slot] % SQ_NPAT;
    }
    return 0xFFu;
}
/* every frame: the parts' sounds as the current pattern wants them; playing, the next one's made ready */
static int sq_snd_tweaked(uint32_t o1, uint32_t part)   /* owner o1 (0 HOME, pattern + 1) keeps a tweak for part */
{
    int st = cfk_find(o1, part);
    return st >= 0 && cfk_st[st].edited;
}
static void sq_snd_poll(void)
{
    uint32_t cur = sq_cur_pat(), nx = sqp.on ? sq_next_pat() : 0xFFu, part, e, pos, ne, np, follow = 0;
    for (part = 0; part < 3u; part++) {
        cf_stage_t *s = &cf_stage[part];
        uint32_t o = cur < SQ_NPAT && cfps.r[cur][part].own ? cur + 1u : 0u;
        int diff;
        if (s->taken) {                          /* (the audio side put one in at the bar line) */
            s->taken = 0;
            cf_snd[part] = s->meta;
            cf_snd_gen[part]++;
            cfk_own[part] = s->own1;             /* (its tweak came with it) */
            cfk_on[part] = s->tw;
            cfk_gen[part] = cf_snd_gen[part];
            cfk_full[part] = 0;
            if (s->tw)
                cu.edited[part] = 1;
            follow |= part < 2u;
        }
        sq_snd_want(cur, part, &e, &pos);
        diff = cf_snd[part].engine != e || cf_snd[part].pos != pos;
        /* another sound, or the same one of another owner (HOME's and a pattern's own, two patterns': their tweaks
         * are theirs) */
        if (sq_snd_force || ((diff || cfk_own[part] != o) && !(s->ready && s->pat == cur))) {
            if (diff || cfk_edited(part))
                cf_pool_load(part, e, pos);      /* (stopped, or it came in without one ready: now; the same sound
                                                  * again without the last owner's tweak) */
            cfk_take(part, o);                   /* (its owner's tweak on it; edits from now on go to it) */
#ifdef CF_TRACE
            fprintf(stderr, "%6u sound loaded now: part %u %s%s\n", (unsigned)cf_now_ms, (unsigned)part, cf_snd[part].name,
                    cf_sn.sq_on ? " (playing: none was ready)" : "");
#endif
            follow |= part < 2u;
        }
        if (nx < SQ_NPAT && nx != cur) {
            uint32_t o1 = cfps.r[nx][part].own ? nx + 1u : 0u;
            sq_snd_want(nx, part, &ne, &np);
            if (ne == cf_snd[part].engine && np == cf_snd[part].pos && !cfk_edited(part) && !sq_snd_tweaked(o1, part)) {
                s->ready = 0;                    /* (the same sound, no tweak on either: nothing to change; its owner
                                                  * is taken at the bar line, above) */
            } else if (!(s->ready && s->pat == nx && s->meta.engine == ne && s->meta.pos == np && s->own1 == o1)) {
                uint32_t i;
                int st = cfk_find(o1, part);
                s->ready = 0;
                cf_snd_build(part, ne, np, s);
                s->own1 = (uint8_t)o1;
                s->tw = 0;
                if (st >= 0 && cfk_read((uint32_t)st, part, &s->meta) && cfk_rec.blen == s->blen) {
                    for (i = 0; i < P_COUNT; i++)   /* (its owner's tweak on it, as cfk_take would put it) */
                        if (!param_kept(i))
                            s->p[i] = cfk_rec.p[i];
                    memcpy(s->blob, cfk_rec.blob, s->blen);
                    s->tw = 1;
                }
                s->pat = (uint8_t)nx;
                RING_PUBLISH();
                s->ready = 1;
            }
        } else {
            s->ready = 0;
        }
    }
    sq_snd_force = 0;
    if (follow && cs.env_link)
        cf_env_follow();                         /* (linked: the bass takes the chord sound's envelope) */
}
/* HOME's sounds as what they are, and back as places in the row once a SAVE or a delete moved it (the parts may play
 * a pattern's own: HOME's are not theirs) */
static void sq_home_keep(cf_sref_t k[3])
{
    uint32_t part, e, pos;
    cf_snd_t m;
    for (part = 0; part < 3u; part++) {
        cf_snd_at(part, part == CF_PART_LAYER ? cs.snd_layer : cs.snd[part], &e, &pos);
        cf_snd_meta(e, pos, &m);
        cfps_ref_of(&k[part], &m);
    }
}
static void sq_home_put(const cf_sref_t k[3])
{
    uint32_t part, e, pos, i;
    for (part = 0; part < 3u; part++) {
        i = cfps_ref_where(&k[part], &e, &pos) ? cf_snd_index(part, e, pos) : cf_snd_pos(part);   /* (gone: the live one) */
        if (part == CF_PART_LAYER)
            cs.snd_layer = (uint16_t)i;
        else
            cs.snd[part] = (uint16_t)i;
    }
}
/* a track's sound turned on a pattern's page (K2 on its row, PRESETS there or in its track): that pattern's own.
 * The choices go round: HOME, then HOME's own sound and the row on from it, so one turn right keeps what HOME plays
 * now for this pattern, and one left from it is HOME again */
static void sq_snd_turn(uint32_t pat, uint32_t part, int d)
{
    uint32_t n = cf_snd_count(part), home = (part == CF_PART_LAYER ? cs.snd_layer : cs.snd[part]) % (n ? n : 1u);
    uint32_t e, pos, at = 0, m = n + 1u;          /* at: 0 HOME, k the row's (home + k - 1) */
    cf_snd_t mt;
    if (!n || pat >= SQ_NPAT)
        return;
    if (cfps_where(pat, part, &e, &pos))
        at = (cf_snd_index(part, e, pos) + n - home) % n + 1u;
    at = (uint32_t)(((int32_t)at + d) % (int32_t)m + (int32_t)m) % m;
    cfk_drop(pat + 1u, part);                    /* (another sound: its tweak goes) */
    if (!at) {
        cfps_set(pat, part, 0);
        return;
    }
    cf_snd_at(part, (home + at - 1u) % n, &e, &pos);
    cf_snd_meta(e, pos, &mt);
    cfps_set(pat, part, &mt);
}
static const char *sq_snd_name(uint32_t pat, uint32_t part)   /* its K2 cell: HOME or the sound's name */
{
    static cf_snd_t mt;
    uint32_t e, pos;
    if (!cfps_where(pat, part, &e, &pos))
        return "HOME";
    cf_snd_meta(e, pos, &mt);
    return mt.name;
}

/* power-on, the patterns read in (cf_ui.c cf_ui_init): PLAY on HOME plays what it played before the power-off, the
 * pattern or the song from the block its frame was on (the owner, 2026-10-21: "Как оставил"; stopped, on HOME) */
_Static_assert(SQ_SONG <= 64u && SQ_NPAT <= 8u, "settings: cs.resume holds a block or a pattern");
static void sq_resume(void)
{
    uint32_t r = cs.resume;
    if (!(r & CF_RESUME_ON))
        return;
    if (r & CF_RESUME_SONG) {
        if (!sq_st.song.n)
            return;
        sqk.song = 1;
        squ.slot = (uint8_t)((r & 0x3Fu) < sq_st.song.n ? (r & 0x3Fu) : 0u);
    } else if ((r & 7u) < SQ_NPAT && sq_has(r & 7u)) {
        sqk.last = (uint8_t)(r & 7u);
    }
}
static void sq_home_poll(void)                   /* every frame */
{
    uint32_t i;
    if (sqk.last < SQ_NPAT && !sq_has(sqk.last))  /* (cleared or undone since: another) */
        sqk.last = 0xFF;
    for (i = 0; sqk.last == 0xFFu && i < SQ_NPAT; i++)   /* (after a power-on, before any is opened: the first one
                                                  * written, so PLAY on HOME plays a pattern, not the beat alone) */
        if (sq_has(i))
            sqk.last = (uint8_t)i;
    if (sq_screen(cu.scr) && squ.pat < SQ_NPAT && sq_has(squ.pat))
        sqk.last = squ.pat;                      /* (opened: the last pattern PLAY on HOME plays) */
    if (sq_screen(cu.scr) && squ.pat < SQ_NPAT && sq_has(squ.pat))
        sqk.song = 0;
    else if (cu.scr == SCR_SQ && squ.pat == SQ_SONGPG && sq_st.song.n)
        sqk.song = 1;                            /* (the song's page: PLAY on HOME plays the song) */
    if (sqk.song && !sq_st.song.n)               /* (its blocks all removed since) */
        sqk.song = 0;
    {                                            /* .. kept with the settings for the next power-on (sq_resume) */
        uint8_t r = sqk.song ? (uint8_t)(CF_RESUME_ON | CF_RESUME_SONG | (squ.slot < sq_st.song.n ? squ.slot : 0u))
                  : sqk.last < SQ_NPAT ? (uint8_t)(CF_RESUME_ON | sqk.last) : 0u;
        if (cs.resume != r)
            cs.resume = r;
    }
    if (sqh_scr != 0xFFu && (cu.pop != POP_MSG || cu.scr != sqh_scr)) {   /* (a hint is said where it is needed: "[SAVE]
                                                  * back" went on over HOME) */
        if (cu.pop == POP_MSG)
            cu.pop = POP_NONE;
        sqh_scr = 0xFF;
    }
    if (cu.scr == SCR_HOME && (cs.seq_hint & SQH_TAKE) && cu.pop == POP_NONE)
        sq_hint_once(SQH_HOME);                  /* (HOME after the first take: how to come back) */
    if (!sqk.on)
        return;
    if (cf_sn.sq_rec != 0xFFu) {
        sqk.seen = 1;
        if (!cf_sn.sq_cin) {                     /* (its bars: the count-in over) */
            sqk.rolled = 1;
            sqk.nbar = cf_sn.sq_n;
            sqk.t = cf_sn.sq_t;
            sqk.bt = cf_sn.sq_bt;
        }
    } else if (sqk.seen) {                       /* (it ended by itself, its 8 bars up; or REC, PLAY, a panic) */
        if (sqk.rolled && sqk.nbar + 1u >= SQ_BARS)
            sqk.t = sqk.bt;                      /* (the whole 8th bar) */
        sq_home_end(0);
    }
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
    {                                            /* the pattern's own sound (0.5): as K2 on its row */
        uint32_t part = squ.trk == SQ_LAYER ? CF_PART_LAYER : squ.trk == SQ_BASS ? CF_PART_BASS : CF_PART_CHORD;
        char m[24];
        sq_snd_turn(squ.pat, part, d);
        str_cpy(m, part == CF_PART_BASS ? "Bass: " : part == CF_PART_LAYER ? "Layer: " : "Chords: ", sizeof m);
        str_cpy(m + str_len(m), sq_snd_name(squ.pat, part), 13);
        cf_message(m);
    }
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
                kfmt(o->val, 14, sq_snd_name(squ.pat, t == SQ_LAYER ? CF_PART_LAYER : t == SQ_BASS ? CF_PART_BASS :
                                             CF_PART_CHORD));
            }
            break;
        case 2:
            if (t == SQ_DRUMS) {                 /* (DRUMS have no key: their SWING here, K2 in their editor is
                                                  * the drum's level) */
                kfmt(o->name, 12, "SWING");
                if (sq_empty(p))
                    kfmt(o->val, 14, "-"), o->dis = 1;
                else if (p->swing)
                    kint(o->val, p->swing, "%");
                else
                    kfmt(o->val, 14, "Off");
                o->bar = (int16_t)(sq_empty(p) ? -1 : p->swing * 255 / 100);
                break;
            }
            kfmt(o->name, 12, "KEY");
            if (sq_empty(p)) {
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
            kfmt(o->val, 14, (p->mute >> t) & 1u ? "On" : "Off");
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
            int i = sq_at(p, squ.trk, squ.bar, squ.step);
            uint32_t n = i >= 0 ? sq_len_steps(p, &p->ev[i]) : squ.len;
            kfmt(o->name, 12, "LENGTH");
            kint(o->val, (int)n, n > 1u ? " steps" : " step");
        }
        break;
    case 1:
        if (squ.trk == SQ_DRUMS) {               /* (the drum picked: its level, by its kit on K1; SWING is on the
                                                  * SEQUENCER's DRUMS row) */
            uint32_t v = p->dlev[squ.lane % SQ_NLANE];
            kfmt(o->name, 12, "LEVEL");
            if (v > 100u)
                v = 100u;
            if (v)
                kint(o->val, (int)v, "%");
            else
                kfmt(o->val, 14, "Off");
            o->bar = (int16_t)(v * 255u / 100u);
        } else {
            kfmt(o->name, 12, "STEPS");
            kint(o->val, SQ_ST(p), 0);
        }
        break;
    case 2: kfmt(o->name, 12, "BARS"); kint(o->val, (int)sq_tbars(p, squ.trk), 0); break;   /* (the track's own) */
    default: {                                   /* the window that plays: its first bar to the track's last */
        char b[8];
        kfmt(o->name, 12, "POSITION");   /* (PLAYS once: the owner, 2026-10-19, "Да, POSITION") */
        kint(o->val, (int)sq_ts(p, squ.trk) + 1, 0);
        if (sq_tbars(p, squ.trk) > 1u) {
            kint(b, (int)(sq_ts(p, squ.trk) + sq_tbars(p, squ.trk)), 0);
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
            } else {                             /* the pattern's own sound (0.5), HOME first */
                sq_snd_turn(squ.pat, t == SQ_LAYER ? CF_PART_LAYER : t == SQ_BASS ? CF_PART_BASS : CF_PART_CHORD, d);
            }
        } else if (k == 2 && t == SQ_DRUMS) {    /* DRUMS: SWING, 5 % a detent */
            if (!sq_empty(p)) {
                sq_touch();
                p->swing = (uint8_t)clampi(p->swing + d * 5, 0, 100);
            }
        } else if (k == 2) {                     /* KEY: to the right it follows HOME's, to the left it stays */
            if ((d > 0) != (p->follow != 0))
                sq_follow_toggle();
        } else {
            p->mute ^= (uint8_t)(1u << t);       /* (this pattern's) */
            sq_touch();
        }
        return;
    }
    if (k == 0) {
        if (squ.trk == SQ_DRUMS) {
            sq_drum_kit_turn(p, d);
        } else {
            int i = sq_at(p, squ.trk, squ.bar, squ.step);
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
    } else if (k == 1 && squ.trk == SQ_DRUMS) {  /* the drum's level */
        sq_drum_level_turn(p, d);
    } else if (k == 1) {
        int i, st = SQ_ST(p), k = (int)sq_kbeat(p, squ.trk, squ.bar);   /* (steps a beat as it plays: 1 2 3 4 6 8) */
        for (i = 0; i < SQ_NKS && SQ_KS[i] != k; i++)
            ;
        i = clampi((i < SQ_NKS ? i : 1) + d, 0, SQ_NKS - 1);
        if (SQ_KS[i] * sq_beats <= SQ_MAXST) {
            squ.step = (uint8_t)(squ.step * SQ_KS[i] * sq_beats / st);
            sq_set_steps(p, squ.trk, squ.bar, SQ_KS[i]);
        }
    } else if (k == 2) {
        sq_set_bars(p, squ.trk, (uint32_t)clampi((int)sq_tbars(p, squ.trk) + d, 1, SQ_BARS));
    } else {                                     /* K4: the window up and down a bar (the frame goes with it while it
                                                  * is in the window: the bar being worked on stays in sight) */
        int was = (int)sq_ts(p, squ.trk), st = SQ_ST(p), in = sq_trk_plays(p, squ.trk, squ.bar);   /* (in it before) */
        sq_set_start(p, squ.trk, was + d);
        if (in && (int)sq_ts(p, squ.trk) != was) {
            squ.bar = (uint8_t)(squ.bar + ((int)sq_ts(p, squ.trk) - was));
            squ.step = (uint8_t)(squ.step * SQ_ST(p) / st);
        }
    }
    sq_clamp();
}

/* ALGORITHM on SEQUENCER: the pattern, then the song */
static void sq_algo(int d)
{
    if (squ.oct_down && squ.pat < SQ_NPAT) {     /* OCT+ held: where the pattern goes (sq_oct_poll copies it) */
        if (!squ.ocpy) {
            squ.ocpy = 1;
            squ.ofrom = squ.pat;
        }
        squ.pat = (uint8_t)clampi(squ.pat + d, 0, SQ_NPAT - 1);
        cv_turned(d);
        return;
    }
    if (squ.sel_down && !squ.cpy)                /* (another pattern while SEL is held: no track cleared, as SELECT) */
        squ.sel_down = 0;
    squ.pat = (uint8_t)clampi(squ.pat + d, 0, SQ_SONGPG);
    cv_turned(d);
    cf_send_config();                            /* (the keys: the row's sound, the song's page the chords') */
    if (!cf_sn.sq_on || cf_sn.sq_rec != 0xFFu)
        return;
    /* playing: the page shown takes over at the next bar line, an empty pattern too; the song from its start (the
     * owner, 2026-10-20: the song's page left the pattern playing, "сколько бы не ждал он не начинает играть"; "пустые
     * пусть играются тоже, так консистентнее"; "чтобы при переключении игралось с начала"). An empty song: nothing */
    if (squ.pat < SQ_NPAT)
        cf_post(CFE_SEQ, 3, squ.pat, 0);
    else if (sq_st.song.n)
        cf_post(CFE_SEQ, 3, SQ_NPAT, 0);
}
/* SELECT on SEQUENCER: the track (the song: the place) */
/* a track of the pattern shown cleared whole (SEL held 1 s on the SEQUENCER: sq_sel_poll; a sweep of SEL held and
 * SELECT turned, as in a track, made no sense over four rows: the first detent took two tracks, the owner,
 * 2026-10-19) */
static void sq_erase_track(uint32_t t)
{
    sq_pat_t *p = sq_cur();
    uint32_t i, l, b;
    if (sq_empty(p))
        return;
    if (t == SQ_DRUMS) {
        for (l = 0; l < SQ_NLANE; l++)
            for (b = 0; b < SQ_BARS; b++)
                p->hit[l][b] = 0;
    } else {
        for (i = 0; i < p->nev;)
            if (p->ev[i].trk == t)
                sq_del(p, i);
            else
                i++;
    }
    sq_touch();
}
static void sq_pick(int d)
{
    if (squ.pat == SQ_SONGPG) {
        squ.slot = (uint8_t)(sq_st.song.n ? clampi(squ.slot + d, 0, sq_st.song.n - 1) : 0);
        return;
    }
    if (squ.sel_down && !squ.cpy)                /* (the frame moved while SEL is held: no track cleared) */
        squ.sel_down = 0;
    squ.trk = (uint8_t)clampi(squ.trk + d, 0, SQ_NTRK - 1);
    cf_send_config();                            /* (the keys: the row's sound) */
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
                       sq_has((uint32_t)i) ? KC_BTN : KC_OFF;
        }
        return 1;
    }
    if (sq_frame_trk(SQ_DRUMS) && cu.scr == SCR_SQ) {   /* (the Drums row: the drums on the upper keys) */
        for (k = 0; k < key; k++)
            i += cf_key_is_upper(k);
        o->style = KC_OFF;
        if (cf_key_is_upper(key) && i < (int)SQ_NLANE) {
            o->btn = 1;
            o->icon = (uint8_t)(1 + i);
            o->style = (cu.keys >> key) & 1u ? KC_DOWN : i == squ.lane ? KC_ON : KC_BTN;
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
        o->style = (cu.keys >> key) & 1u ? KC_DOWN : (p->hit[squ.lane][squ.bar] >> i) & 1u ? KC_ON :
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
