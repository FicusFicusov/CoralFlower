/* SPDX-License-Identifier: GPL-3.0-only
 * CoralFlower: the drum machine's page and what it changes elsewhere (0.6; cf_drum.c plays the rhythms, cf_dmv.c
 * draws them). Included by cf_ui.c after cf_sequi.c.
 *
 * The owner's rules (roles/drum-machine-journeys.md, 2026-10-28: "Да, подходят"):
 * - one thing plays: a rhythm, a pattern or the song. PLAY stops what plays; when nothing does, PLAY starts the
 *   rhythm on screen in the drum machine, the pattern on screen in the SEQUENCER, and elsewhere what was chosen last:
 *   the rhythm, pattern or song stood on last, even only looked at;
 * - TEMPO and TIME SIG are one for all; the drum machine shows the rhythms of the TIME SIG now, and remembers for each
 *   TIME SIG the one stood on;
 * - a rhythm keeps the user's KIT and SWING (over a power-off too); SEL gives it its own back. LEVEL is the drums'
 *   VOLUME (one for the rhythms and the patterns, as TEMPO);
 * - the plaque (top right: what plays) on HOME, the METRONOME, the SEQUENCER and the drum machine; on HOME and the
 *   METRONOME, pale when nothing plays: what PLAY would start ("Показывать").
 * The METRONOME's carousel picks where its SEQ goes: the SEQUENCER, as before, or the drum machine. It starts on the
 * SEQUENCER each time the METRONOME opens, so SEQ twice still opens the SEQUENCER. */

/* ------------------------------------------------------------- flash --- */
/* what the drum machine keeps: an object of its own, the old looper's (storage.c OBJ_CFLOOP, a log as the settings':
 * a change written as what changed, no erase while anything sounds). 0.5 and older look there only for a loop of
 * theirs (cf_loop.c CFL_MAGIC): this record is passed over, a downgrade keeps everything else */
#define DM_MAGIC 0x32444643u                     /* "CFD2" (rc4: kept by each rhythm's id) */
#define DM_MAGIC1 0x31444643u                    /* "CFD1": rc3's, kept by the place in its list (read in: dm_from1) */
#define DM_NMAX 64u                              /* rhythm ids 0 .. 63 */
typedef struct {
    uint32_t magic;
    uint8_t kit[DM_NMAX], swing[DM_NMAX];        /* each rhythm's KIT and SWING the user set (by its id), 0xFF its own */
    uint8_t pos[8];                              /* the rhythm stood on for each TIME SIG (cs.beats): its id, 0xFF none */
    uint8_t last;                                /* 1: a rhythm was chosen last (PLAY on HOME plays it) */
    uint8_t rsv[3];
    uint32_t check;                              /* FNV-1a over what comes before */
} dm_rec_t;
typedef struct {                                 /* rc3's (0.6-rc3 only, a test build) */
    uint32_t magic;
    uint8_t kit[32], swing[32], pos[8], last, rsv[3];
    uint32_t check;
} dm_rec1_t;
typedef char dm_fits[sizeof(dm_rec_t) <= 224u && sizeof(dm_rec1_t) == 84u ? 1 : -1];
static dm_rec_t dmr;
static uint32_t dm_saved, dm_seen, dm_changed_ms, dm_retry_ms;
static uint8_t dm_tidy, dm_store;
static uint8_t dm_restore_lock;                  /* a backup restored the record (cf_backup.c): nothing kept till the restart */
static uint8_t dm_seq_to;                        /* the METRONOME's carousel: 1, its SEQ opens the drum machine */

static uint32_t dm_check(void) { return cfs_fnv(&dmr, (uint32_t)__builtin_offsetof(dm_rec_t, check)); }
static void dm_defaults(void)
{
    memset(&dmr, 0, sizeof dmr);
    memset(dmr.kit, 0xFF, sizeof dmr.kit);
    memset(dmr.swing, 0xFF, sizeof dmr.swing);
    memset(dmr.pos, 0xFF, sizeof dmr.pos);
    dmr.magic = DM_MAGIC;
}
/* rc3's record: its rhythms had the ids 0 .. 28 in its list's order (cf_drum.c kept them), so its tweaks are by id
 * already; the rhythm stood on was a place in a TIME SIG's list: 4/4's 0 .. 19, then two each in 3/4, 2/4, 5/4, 6/8
 * from 20, 7/4's 28. A rhythm rc4 no longer has: its tweaks wait unused, a place on it falls back (dm_cur) */
static void dm_from1(const dm_rec1_t *o)
{
    static const uint8_t BASE[8] = {0xFF, 0xFF, 22, 20, 0, 24, 26, 28};
    uint32_t i;
    for (i = 0; i < 32u; i++) {
        dmr.kit[i] = o->kit[i];
        dmr.swing[i] = o->swing[i];
    }
    for (i = 2; i < 8u; i++)
        dmr.pos[i] = BASE[i] != 0xFFu && o->pos[i] < 20u ? (uint8_t)(BASE[i] + o->pos[i]) : 0xFFu;
    dmr.last = o->last;
}
/* power-on, after sq_from_loop (cf_ui.c cf_ui_init): a loop of 0.3.1 still there (no free pattern for it yet) keeps
 * the object; then the drum machine's settings stay in RAM till the next power-on */
static void dm_load(void)
{
    static dm_rec_t r;
    static dm_rec1_t r1;
    st_hdr_t h;
    uint32_t i, m = 0;
    dm_defaults();
    dm_saved = dm_seen = dm_check();
    dm_tidy = 0;
    dm_store = 0;
    if (cr_safe)
        return;
    if (st_current(OBJ_CFLOOP, &h) >= 0 && h.len > 16u)
        memcpy(&m, st_buf, 4);
    if (m == CFL_MAGIC)
        return;
    st_loop_log = 1;
    dm_store = 1;
    st_check_blank(OBJ_CFLOOP);
    r.magic = 0;
    if (st_load(OBJ_CFLOOP, &r, sizeof r) == (int)sizeof r && r.magic == DM_MAGIC &&
        r.check == cfs_fnv(&r, (uint32_t)__builtin_offsetof(dm_rec_t, check))) {
        dmr = r;
    } else if (st_load(OBJ_CFLOOP, &r1, sizeof r1) == (int)sizeof r1 && r1.magic == DM_MAGIC1 &&
               r1.check == cfs_fnv(&r1, (uint32_t)__builtin_offsetof(dm_rec1_t, check))) {
        r.magic = 0;
        dm_from1(&r1);                           /* (written as rc4's at the first quiet moment) */
    } else {
        return;
    }
    for (i = 0; i < DM_NMAX; i++) {              /* (a value out of range: the rhythm's own) */
        if (dmr.kit[i] != 0xFFu && dmr.kit[i] >= SQ_NKIT)
            dmr.kit[i] = 0xFF;
        if (dmr.swing[i] != 0xFFu && dmr.swing[i] > 100u)
            dmr.swing[i] = 0xFF;
    }
    dmr.last = dmr.last == 1u;
    dm_saved = dm_seen = dm_check();
    if (r.magic != DM_MAGIC)                     /* (rc3's: rc4's written once, when it is quiet) */
        dm_saved = 0;
}
/* every frame: a change written half a second after it, as what changed (no erase) while anything sounds; the log
 * tidied in a quiet moment (cf_settings.c cfps_poll) */
static void dm_poll_store(uint32_t now)
{
    uint32_t h;
    int rc, quiet;
    if (!dm_store || cfs_restore_lock || dm_restore_lock || !cu.booted)
        return;
    if (dm_retry_ms && (int32_t)(now - dm_retry_ms) < 0)
        return;
    dm_retry_ms = 0;
    h = dm_check();
    if (h != dm_seen) {
        dm_seen = h;
        dm_changed_ms = now;
    }
    quiet = now - cfs_sound_ms >= CF_SET_SILENT_MS && now - dm_changed_ms >= CF_SET_QUIET_MS;
    if (h == dm_saved) {
        if (!dm_tidy && quiet) {
            rc = st_tidy(OBJ_CFLOOP);
            dm_tidy = rc == 0;
            if (rc < 0)
                dm_retry_ms = now + 1000u;
        }
        return;
    }
    if (now - dm_changed_ms < CF_SET_QUICK_MS)
        return;
    if (!quiet && felucca_dbg.last_us * 20u > HALF_FRAMES * 1000000u / FS * 17u)
        return;
    dmr.check = h;
    rc = st_put(OBJ_CFLOOP, &dmr, sizeof dmr, quiet ? 0u : ST_NOERASE);
    if (rc == 0) {
        dm_saved = h;
        dm_tidy = 0;
    } else {
        dm_retry_ms = now + 1000u;
    }
}
static void dm_flush(void)                       /* a backup's LIST: what is not written yet, now */
{
    uint32_t h = dm_check();
    dmr.check = h;
    if (dm_store && !dm_restore_lock && h != dm_saved && st_put(OBJ_CFLOOP, &dmr, sizeof dmr, 0u) == 0) {
        dm_saved = h;
        dm_tidy = 0;
    }
}

static int dm_rec_ok(const void *raw, uint32_t len)   /* a backup's record of the rhythm machine (cf_backup.c), rc3's too */
{
    const dm_rec_t *r = (const dm_rec_t *)raw;
    const dm_rec1_t *r1 = (const dm_rec1_t *)raw;
    return (len == sizeof(dm_rec_t) && r->magic == DM_MAGIC &&
            r->check == cfs_fnv(r, (uint32_t)__builtin_offsetof(dm_rec_t, check))) ||
           (len == sizeof(dm_rec1_t) && r1->magic == DM_MAGIC1 &&
            r1->check == cfs_fnv(r1, (uint32_t)__builtin_offsetof(dm_rec1_t, check)));
}

/* ----------------------------------------------------------- the rhythms --- */
static uint32_t dm_beats(void) { return cs.beats >= 2u && cs.beats <= 7u ? cs.beats : 4u; }
/* the rhythm on screen: one picked in another TIME SIG while a pattern plays (dm_view: it waits for PLAY to switch
 * TIME SIG), else the one stood on in this TIME SIG (by its id); one no longer there, or none yet: the first */
static uint8_t dm_view = 0xFF;
static uint32_t dm_cur(void)
{
    uint32_t b = dm_beats(), i;
    if (dm_view < DM_N)
        return dm_view;
    for (i = 0; i < DM_N; i++)
        if (DM_RHY[i].beats == b && DM_RHY[i].id == dmr.pos[b])
            return i;
    return dm_nth(b, 0);
}
static uint32_t dm_uid(uint32_t r) { return DM_RHY[r % DM_N].id % DM_NMAX; }   /* (the user's are kept by the id) */
static uint32_t dm_kit(uint32_t r) { return dmr.kit[dm_uid(r)] != 0xFFu ? dmr.kit[dm_uid(r)] : DM_RHY[r % DM_N].kit; }
static uint32_t dm_swing(uint32_t r)
{
    return dmr.swing[dm_uid(r)] != 0xFFu ? dmr.swing[dm_uid(r)] : DM_RHY[r % DM_N].swing;
}
static int dm_tweaked(uint32_t r) { return dmr.kit[dm_uid(r)] != 0xFFu || dmr.swing[dm_uid(r)] != 0xFFu; }
static int16_t dm_v(uint32_t r) { return (int16_t)(dm_kit(r) | dm_swing(r) << 8); }
static void dm_play(uint32_t r) { cf_post(CFE_DRUM, 1, (uint8_t)r, dm_v(r)); }
static int dm_plays(uint32_t r) { return cf_sn.dm_on && cf_sn.dm_r == r; }

/* PLAY anywhere: what plays stops (a rhythm, a pattern or the song, the beat alone) */
static int dm_stop_all(void)
{
    if (cf_sn.dm_on) {
        cf_post(CFE_DRUM, 0, 0, 0);
        return 1;
    }
    if (cf_sn.sq_on) {
        cf_post(CFE_SEQ, 0, 0, 0);
        return 1;
    }
    if (cf_sn.running) {
        cf_post(CFE_TRANSPORT, 0, 0, 0);
        return 1;
    }
    return 0;
}

/* a pattern's DRUMS made of rhythm r: its hits from bar b0 of it (the bar playing when REC came), its kit on every
 * drum, its levels and SWING; the row as long as the rhythm */
static void dm_fill(sq_pat_t *p, uint32_t r, uint32_t b0)
{
    const dm_rhythm_t *y = &DM_RHY[r % DM_N];
    uint32_t l, b, n = y->bars ? y->bars : 1u;
    for (l = 0; l < SQ_NLANE; l++) {
        for (b = 0; b < SQ_BARS; b++)
            p->hit[l][b] = b < n ? dm_mask(r, l, (b0 + b) % n) : 0u;
        p->dkit[l] = (uint8_t)dm_kit(r);
        p->dlev[l] = y->lev[l] ? y->lev[l] : 100u;   /* (a drum the rhythm leaves out: full, when one is added) */
    }
    p->kit = (uint8_t)dm_kit(r);
    p->swing = (uint8_t)dm_swing(r);
    p->ts[SQ_DRUMS] = 0;
    p->tb[SQ_DRUMS] = (uint8_t)n;
}

/* ------------------------------------------------------------ the page --- */
static void dm_knob_info(int k, cf_knob_t *o)
{
    uint32_t r = dm_cur(), s = dm_swing(r);
    switch (k) {
    case 0: kfmt(o->name, 12, "KIT"); kfmt(o->val, 14, SQ_KITS[dm_kit(r) % SQ_NKIT].name); break;
    case 1:                                      /* (the drums' VOLUME, the Drums row's K1: one name for one setting) */
        kfmt(o->name, 12, "VOLUME");
        kint(o->val, (int)(cs.level_drums * 100u / 127u), "%");
        o->bar = (int16_t)(cs.level_drums * 255u / 127u);
        break;
    case 2:
        kfmt(o->name, 12, "TEMPO");
        kint(o->val, cs.bpm, " BPM");
        o->dis = cf_sn.ext_clock;                /* (MIDI clock in: the clock's tempo) */
        break;
    default:
        kfmt(o->name, 12, "SWING");
        if (s)
            kint(o->val, (int)s, "%");
        else
            kfmt(o->val, 14, "Off");
        o->bar = (int16_t)(s * 255u / 100u);
        break;
    }
    if (sqk.on && (k == 0 || k == 3))            /* (a take under the rhythm: as it was when REC came) */
        o->dis = 1;
}
static void dm_knob_turn(int k, int d)
{
    uint32_t r = dm_cur();
    if (k == 1) {                                /* (VOLUME: the drums', the SEQUENCER's K1 on Drums) */
        sq_level_set(SQ_DRUMS, cs.level_drums + d * 2);
        return;
    }
    if (k == 2) {
        if (!cf_sn.ext_clock)
            cs.bpm = (uint16_t)clampi(cs.bpm + d, 30, 300);
        cf_send_metro();
        cf_send_config();
        return;
    }
    if (sqk.on)
        return;
    if (k == 0) {                                /* (in the kits' list order: X Lo-Fi first, sq_kit_step) */
        uint32_t v = sq_kit_step(dm_kit(r), d);
        dmr.kit[dm_uid(r)] = (uint8_t)(v == DM_RHY[r].kit ? 0xFFu : v);
    } else {
        int v = clampi((int)dm_swing(r) + d * 5, 0, 100);
        dmr.swing[dm_uid(r)] = (uint8_t)(v == DM_RHY[r].swing ? 0xFFu : (uint32_t)v);
    }
    if (dm_plays(r))
        cf_post(CFE_DRUM, 2, 0, dm_v(r));
}
/* TIME SIG to rhythm r's, said (the owner's pick G1: one list of every rhythm, its TIME SIG under its name; picking one
 * of another TIME SIG switches TIME SIG, with a message). The patterns play in it too then, as after K4 on METRONOME */
static void dm_sig_for(uint32_t r)
{
    uint32_t b = DM_RHY[r % DM_N].beats;
    char m[16];
    if (b == cs.beats)
        return;
    cs.beats = (uint8_t)b;
    cf_send_metro();
    cf_send_config();
    str_cpy(m, "TIME SIG ", sizeof m);
    kint(m + 9, (int)b, 0);
    str_cpy(m + str_len(m), b == 6u ? "/8" : "/4", sizeof m - str_len(m));
    cf_message(m);
}
/* SELECT: the rhythm, in one list (4/4's first, then the other TIME SIGs'). One playing: the next comes in at the bar
 * line, its TIME SIG with it. A pattern or the song playing: it only shows the rhythm; TIME SIG switches when PLAY
 * starts it. Nothing playing: TIME SIG switches at once */
static void dm_select(int d)
{
    uint32_t i = dm_cur();
    int v;
    const dm_rhythm_t *y;
    if (sqk.on)
        return;
    v = clampi((int)i + d, 0, (int)DM_N - 1);
    if ((uint32_t)v == i)
        return;
    y = &DM_RHY[v];
    dmr.pos[y->beats & 7u] = y->id;              /* (kept by its id, for its TIME SIG) */
    dmr.last = 1;
    cv_turned(d);
    if (y->beats == cs.beats) {
        dm_view = 0xFF;
        if (cf_sn.dm_on)
            dm_play((uint32_t)v);
    } else if (cf_sn.dm_on) {
        dm_view = (uint8_t)v;                    /* (it waits for the bar line: dm_poll takes its TIME SIG then) */
        dm_play((uint32_t)v);
    } else if (cf_sn.sq_on) {
        dm_view = (uint8_t)v;
    } else {
        dm_sig_for((uint32_t)v);
        dm_view = 0xFF;
    }
}
static void dm_sel(void)                         /* SEL: the rhythm's own KIT and SWING back */
{
    uint32_t r = dm_cur();
    char m[32];
    if (sqk.on || !dm_tweaked(r))
        return;
    dmr.kit[dm_uid(r)] = dmr.swing[dm_uid(r)] = 0xFF;
    str_cpy(m, DM_RHY[r].name, sizeof m);
    str_cpy(m + str_len(m), ": its own KIT, SWING", sizeof m - str_len(m));
    cf_message(m);
    if (dm_plays(r))
        cf_post(CFE_DRUM, 2, 0, dm_v(r));
}
static void sq_home_rec_r(uint32_t r);
static void dm_page_play(void)                   /* PLAY in the drum machine */
{
    if (sqk.on) {                                /* (taking: it is kept, and all stops) */
        sq_home_end(0);
        cf_post(CFE_SEQ, 0, 0, 0);
        return;
    }
    if (dm_stop_all())
        return;
    dmr.last = 1;
    dm_sig_for(dm_cur());                        /* (picked in another TIME SIG while a pattern played: now) */
    dm_play(dm_cur());
}

/* ---------------------------------------------- HOME and the other pages --- */
/* PLAY on HOME (and every page but the SEQUENCER's and the drum machine's), nothing playing: a rhythm chosen last
 * plays (1); else the pattern or the song, as before (0) */
static int dm_home_play(void)
{
    if (!dmr.last)
        return 0;
    dm_sig_for(dm_cur());
    dm_play(dm_cur());
    return 1;
}
/* REC there: under the rhythm playing (it goes into the new pattern from the bar it plays, no count-in), or, nothing
 * playing, under the rhythm chosen last (a bar counted in first). A pattern or the song playing: as before (1: done
 * here) */
static int dm_home_rec(void)
{
    if (sqk.on || cf_sn.sq_rec != 0xFFu || cf_sn.sq_on)
        return 0;
    if (cf_sn.dm_on) {                           /* (its beat runs too: cf_sn.running) */
        sq_home_rec_r(cf_sn.dm_r);
        return 1;
    }
    if (cf_sn.running || !dmr.last)
        return 0;
    dm_sig_for(dm_cur());
    sq_home_rec_r(dm_cur());
    return 1;
}
static void dm_page_rec(void)                    /* REC in the drum machine: always under the rhythm on screen */
{
    if (sqk.on || cf_sn.sq_rec != 0xFFu) {
        sq_home_rec();                           /* (a take under way: REC ends it, as everywhere) */
        return;
    }
    if (!cf_sn.dm_on)
        dm_sig_for(dm_cur());                    /* (a pattern or the song stops for the count-in: TIME SIG first) */
    sq_home_rec_r(cf_sn.dm_on ? cf_sn.dm_r : dm_cur());
}

/* what PLAY on HOME plays now: a rhythm (1) or what the SEQUENCER keeps (0) */
static void dm_chosen(int rhythm) { dmr.last = (uint8_t)(rhythm != 0); }

/* EDIT in the drum machine: the rhythm on screen becomes a new pattern (the first empty letter), with its KIT and
 * SWING, and its DRUMS open there to change. What plays stops (as the drums' EDIT hold does). Hold REC undoes it (rc5:
 * the owner on rc4, "потерялась функция чтобы вставить барабаны из драм-машины в паттерны", picked "[EDIT] make it a
 * pattern") */
static void dm_make_pattern(void)
{
    uint32_t r = dm_cur();
    sq_pat_t *p;
    char m[48];
    int f;
    if (sqk.on || cf_sn.sq_rec != 0xFFu || cf_sn.dm_rin)
        return;                                  /* (a take under way: REC ends it first) */
    f = sq_home_free();
    if (f < 0) {
        cf_message("All 8 patterns are used:\nclear one in the SEQUENCER");   /* (as REC says it) */
        return;
    }
    if (cf_sn.dm_on)
        cf_post(CFE_DRUM, 0, 0, 0);
    if (cf_sn.sq_on)
        cf_post(CFE_SEQ, 0, 0, 0);
    dm_sig_for(r);                               /* (its TIME SIG, as a take under it) */
    squ.pat = (uint8_t)f;
    squ.trk = SQ_DRUMS;
    sqh.busy = 0;
    sq_undo_poll();                              /* (the undo: the pattern as it was, empty) */
    p = &sq_st.pat[f];
    sq_fresh(p, cs.beats);
    p->tonic = cs.chord.tonic;
    p->scale = cs.chord.scale;
    dm_fill(p, r, 0);
    sq_touch();
    sqk.last = (uint8_t)f;                       /* (PLAY on HOME plays it now) */
    sqk.song = 0;
    dm_chosen(0);
    squ.bar = squ.step = 0;
    if (sq_open())
        ui_open(SCR_SQED);
    str_cpy(m, DM_RHY[r % DM_N].name, sizeof m);
    str_cpy(m + str_len(m), " as Pattern ", sizeof m - str_len(m));
    m[str_len(m) + 1u] = 0;
    m[str_len(m)] = (char)('A' + f);
    cf_message(m);
}

static void dm_hold_poll(void);
/* every frame */
static void dm_poll(void)
{
    static uint8_t was = 0xFF;
    if (cu.scr == SCR_DRUM && was != SCR_DRUM && !sqk.on)   /* (opened, even only looked at: what PLAY on HOME plays) */
        dmr.last = 1;
    else if (sq_screen(cu.scr) && ((squ.pat < SQ_NPAT && sq_has(squ.pat)) ||
                                   (cu.scr == SCR_SQ && squ.pat == SQ_SONGPG && sq_st.song.n)))
        dmr.last = 0;
    was = cu.scr;
    if (cf_sn.dm_on && DM_RHY[cf_sn.dm_r % DM_N].beats != cs.beats)
        dm_sig_for(cf_sn.dm_r);                  /* (a rhythm of another TIME SIG came in at the bar line: cf_drum.c) */
    {
        static uint8_t seen;                     /* (TIME SIG changed, by a rhythm or on METRONOME: the list stands on
                                                  * that TIME SIG's rhythm) */
        if (cs.beats != seen) {
            dm_view = 0xFF;
            seen = cs.beats;
        }
    }
    dm_hold_poll();
    dm_poll_store(cu.now);
}

/* ------------------------------------------------------------ the plaque --- */
/* what plays now, as the plaque says it (HOME's top line, and the METRONOME's, the SEQUENCER's, the drum machine's):
 * its kind and words; DMP_PALE: nothing plays, what PLAY would start */
enum { DMP_NONE, DMP_SEQ, DMP_DRUM, DMP_REC, DMP_RECIN, DMP_PALE };
/* a rhythm on the plaque: its name after a little drum (the owner's pick, rc4: "значок"; "RHYTHM Breakcore" left HOME's
 * chord sound one letter) */
static uint8_t dm_plaque_rhythm;                 /* (the plaque says a rhythm: cf_dmv.c draws the drum) */
static uint32_t dm_plaque(char *b, uint32_t n)
{
    b[0] = 0;
    dm_plaque_rhythm = 0;
    if (sqk.on && sqk.rolled) {                  /* a take from HOME or the drum machine: its bar of 8 */
        str_cpy(b, "REC ", n);
        kint(b + 4, (int)(sqk.nbar % SQ_BARS) + 1, "/8");
        return DMP_REC;
    }
    if (sqk.on || (cf_sn.sq_rec != 0xFFu && cf_sn.sq_cin)) {   /* .. its count-in */
        str_cpy(b, "REC in ", n);
        b[7] = (char)('0' + (cf_sn.sq_cin + SQ_PPQN - 1u) / SQ_PPQN % 10u);
        b[8] = 0;
        return DMP_RECIN;
    }
    if (cf_sn.dm_rin) {                          /* REC in the SEQUENCER: the rhythm ends its bar first */
        uint32_t k = (cf_sn.dm_left + SQ_PPQN - 1u) / SQ_PPQN;
        str_cpy(b, "REC in ", n);
        b[7] = (char)('0' + (k > 9u ? 9u : k));
        b[8] = 0;
        return DMP_RECIN;
    }
    if (cf_sn.sq_rec != 0xFFu) {
        str_cpy(b, "REC", n);
        return DMP_REC;
    }
    if (cf_sn.dm_on) {
        str_cpy(b, "", n);
        str_cpy(b + str_len(b), DM_RHY[cf_sn.dm_r % DM_N].name, n - str_len(b));
        dm_plaque_rhythm = 1;
        return DMP_DRUM;
    }
    if (cf_sn.sq_on) {
        str_cpy(b, cf_sn.sq_song ? "SONG " : "SEQ ", n);
        b[str_len(b) + 1] = 0;
        b[str_len(b)] = (char)('A' + cf_sn.sq_pat % SQ_NPAT);   /* (the letter alone: "A3" read as a note) */
        return DMP_SEQ;
    }
    if (dmr.last) {
        str_cpy(b, "", n);
        str_cpy(b + str_len(b), DM_RHY[dm_cur()].name, n - str_len(b));
        dm_plaque_rhythm = 1;
    } else if (sqk.song && sq_st.song.n) {
        str_cpy(b, "SONG", n);
    } else if (sqk.last < SQ_NPAT && sq_has(sqk.last)) {
        str_cpy(b, "SEQ ", n);
        b[4] = (char)('A' + sqk.last);
        b[5] = 0;
    } else {
        return DMP_NONE;
    }
    return DMP_PALE;
}
/* .. shown on this page? HOME and the METRONOME: all of it; the SEQUENCER: a rhythm or a take (the pattern it shows);
 * the drum machine: a pattern, the song or a take (its rhythm it shows) */
static int dm_plaque_here(uint32_t kind)
{
    if (kind == DMP_NONE)
        return 0;
    if (cu.scr == SCR_HOME || cu.scr == SCR_SEQ)
        return 1;
    if (sq_screen(cu.scr))
        return kind == DMP_DRUM || kind == DMP_REC || kind == DMP_RECIN;
    if (cu.scr == SCR_DRUM)
        return kind == DMP_SEQ || kind == DMP_REC || kind == DMP_RECIN;
    return 0;
}

/* -------------------------------------------- EDIT held in the DRUMS editor --- */
/* A rhythm into a pattern (the owner: "задерживать EDIT и крутить PRESETS"). EDIT held, PRESETS turns: first Keep (the
 * pattern's own drums), then the rhythms of this TIME SIG in the drum machine's order. On a rhythm the pattern plays
 * with it instead of its drums, as it would lie there: its DRUMS as long as the longest of the other tracks (the
 * rhythm round and round, from the top with the chords), as long as the rhythm with none or shorter ones. The first
 * step onto a rhythm starts this pattern from its top if something else played or nothing did; the pattern itself
 * playing plays on where it is. EDIT let go on a rhythm: its drums stay (REC held undoes it, as any edit); on Keep
 * nothing changed. What was started for it stops after; PLAY or REC while EDIT is held calls it all off */
static struct {
    uint8_t on, k, moved, started, pat;          /* held; PRESETS' place (0 Keep); left Keep; this started the
                                                  * pattern; which */
    uint32_t hit[SQ_NLANE][SQ_BARS];             /* the pattern's own drums, as they were */
    uint8_t dkit[SQ_NLANE], dlev[SQ_NLANE], kit, swing, ts, tb, bars;
} dmh;
static int dm_holding(void) { return dmh.on; }
static void dm_hold_say(void)
{
    char m[60], a[6];
    if (!dmh.k) {
        cf_message("Keep: your drums\nturn [PRESETS]: a rhythm");
    } else {
        uint32_t r = dm_nth(dm_beats(), dmh.k - 1u);
        str_cpy(m, DM_RHY[r % DM_N].name, sizeof m);
        str_cpy(m + str_len(m), " ", sizeof m - str_len(m));   /* ("Pop 1/22": every counter N/M, rc6) */
        kint(a, dmh.k, 0);
        str_cpy(m + str_len(m), a, sizeof m - str_len(m));
        str_cpy(m + str_len(m), "/", sizeof m - str_len(m));
        kint(a, (int)dm_count(dm_beats()), 0);
        str_cpy(m + str_len(m), a, sizeof m - str_len(m));
        str_cpy(m + str_len(m), "\nlet go of [EDIT]: it stays", sizeof m - str_len(m));
        cf_message(m);
    }
}
static void dm_hold_press(void)                  /* EDIT down in the DRUMS editor */
{
    sq_pat_t *p = sq_cur();
    if (squ_rec_on() || sqk.on || squ.pat >= SQ_NPAT)
        return;                                  /* (a take under way: nothing) */
    sqh.busy = 0;                                /* (one action: one undo, REC held) */
    sq_undo_poll();
    memcpy(dmh.hit, p->hit, sizeof dmh.hit);
    memcpy(dmh.dkit, p->dkit, sizeof dmh.dkit);
    memcpy(dmh.dlev, p->dlev, sizeof dmh.dlev);
    dmh.kit = p->kit;
    dmh.swing = p->swing;
    dmh.ts = p->ts[SQ_DRUMS];
    dmh.tb = p->tb[SQ_DRUMS];
    dmh.bars = p->bars;
    dmh.pat = squ.pat;
    dmh.k = dmh.moved = dmh.started = 0;
    dmh.on = 1;
    dm_hold_say();
}
static void dm_hold_apply(void)                  /* the pattern's DRUMS as PRESETS stands */
{
    sq_pat_t *p = &sq_st.pat[dmh.pat % SQ_NPAT];
    uint32_t l, b, t;
    if (!dmh.k) {                                /* Keep: its own, as they were */
        memcpy(p->hit, dmh.hit, sizeof dmh.hit);
        memcpy(p->dkit, dmh.dkit, sizeof dmh.dkit);
        memcpy(p->dlev, dmh.dlev, sizeof dmh.dlev);
        p->kit = dmh.kit;
        p->swing = dmh.swing;
        p->ts[SQ_DRUMS] = dmh.ts;
        p->tb[SQ_DRUMS] = dmh.tb;
        p->bars = dmh.bars;
    } else {
        uint32_t r = dm_nth(dm_beats(), dmh.k - 1u), rb = DM_RHY[r % DM_N].bars, n = 0;
        rb = rb ? rb : 1u;
        for (t = 0; t < SQ_DRUMS; t++)           /* (as long as the longest other track with something in it) */
            if (sq_own(p, t) && sq_tbars(p, t) > n)
                n = sq_tbars(p, t);
        n = n < rb ? rb : n > SQ_BARS ? SQ_BARS : n;
        dm_fill(p, r, 0);
        for (l = 0; l < SQ_NLANE; l++)           /* (round and round, from the top with the chords: 1-2-1 under 3) */
            for (b = 0; b < SQ_BARS; b++)
                p->hit[l][b] = b < n ? dm_mask(r, l, b % rb) : 0u;
        p->tb[SQ_DRUMS] = (uint8_t)n;
        if (!p->bars)
            p->bars = (uint8_t)n;
    }
    sq_touch();
    if (cf_sn.sq_on)                             /* (the bars each track plays: read again now) */
        cf_post(CFE_SEQ, 7, 0, 0);
}
static void dm_hold_turn(int d)                  /* PRESETS while EDIT is held */
{
    int n = (int)dm_count(dm_beats()), k = clampi((int)dmh.k + d, 0, n);
    if ((uint32_t)k == dmh.k)
        return;
    dmh.k = (uint8_t)k;
    dm_hold_apply();
    if (k && !dmh.moved) {                       /* (the first rhythm: this pattern plays, from its top unless it played) */
        dmh.moved = 1;
        if (!(cf_sn.sq_on && !cf_sn.sq_song && cf_sn.sq_pat == dmh.pat)) {
            cf_post(CFE_SEQ, 1, dmh.pat, 0);
            dmh.started = 1;
        }
    }
    dm_hold_say();
}
static void dm_hold_cancel(void)                 /* PLAY or REC while EDIT is held: all as it was, all stops */
{
    if (!dmh.on)
        return;
    dmh.k = 0;
    dm_hold_apply();
    dmh.on = 0;
    cf_post(CFE_SEQ, 0, 0, 0);
    cf_post(CFE_DRUM, 0, 0, 0);
    cf_message("The drums as they were");
}
static void dm_hold_poll(void)                   /* every frame: EDIT let go */
{
    if (!dmh.on)
        return;
    if ((fm1_in.buttons >> panel.btn[B_EDIT]) & 1u && cu.scr == SCR_SQED) {
        if (cu.pop == POP_MSG)
            cu.pop_until = cu.now + 400u;        /* (the window stays while it is held) */
        return;
    }
    dmh.on = 0;
    if (dmh.k) {
        char m[24];
        str_cpy(m, "Drums: ", sizeof m);
        str_cpy(m + 7, DM_RHY[dm_nth(dm_beats(), dmh.k - 1u) % DM_N].name, sizeof m - 7u);
        cf_message(m);
    } else if (cu.pop == POP_MSG) {
        cu.pop = POP_NONE;
    }
    if (dmh.started)                             /* (started for it: it stops) */
        cf_post(CFE_SEQ, 0, 0, 0);
}
