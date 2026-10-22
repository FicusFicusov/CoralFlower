/* SPDX-License-Identifier: GPL-3.0-only
 * CoralFlower: the settings in flash.
 *
 * One record keeps what the screens and GLO set (cf_set_t cs, the parts' sounds as places in the sound row with it).
 * It is read at power-on, before the first picture (cf_splash), and written half a second after the last change, even
 * while the unit sounds and never stops: the object keeps a log (storage.c ST_LOG_OBJ), so a change is two page
 * programs of about 1 ms each, made when the audio has room, and no erase (a flash erase stops the audio for about
 * 45 ms). A change switched off a second later is kept (the owner, 2026-10-21: "Бас почему-то выбранный не
 * сохраняется"; BOOT SND set On, HOME, off: gone, its 5 s preview held the old save back; "Сохранять всё сразу").
 * About 29 changes fit without a pause; a quiet moment (nothing sounding for a second, no change for 1.5 s) tidies the
 * flash: the newest record where 0.4.1 and older read it, the other copy erased for the next ones (storage.c
 * st_tidy). Only when the log is full does a change wait for that quiet moment. SAFE MODE neither reads nor writes it.
 *
 * Where: OBJ_PROJECT0 + 3 (0x9D000 / 0x9E000, storage.c's A/B pair), ChoralRoot's CZ-1 tone store, which CoralFlower
 * (no CZ engine) does not use. Anything there that is not our record (another magic, version or size, a bad check)
 * is ignored, and the first save replaces it. A record of 0.4.1 or older is the log's entry 0: read as it is.
 * Included after cf_ui.c, before cf_splash.c. */

#define CF_SET_OBJ (OBJ_PROJECT0 + 3u)
#define CF_SET_MAGIC 0x31534643u          /* "CFS1" */
#define CF_SET_VERSION 2u                 /* bump when cf_set_t changes: an older record is then ignored */
#define CF_SET_QUICK_MS 500u              /* a change rests this long, then it is written */
#define CF_SET_QUIET_MS 1500u             /* a quiet moment: no change for this long .. */
#define CF_SET_SILENT_MS 1000u            /* .. and nothing has sounded for this long (an erase may go: st_tidy) */

typedef struct {
    uint32_t magic;
    uint16_t version, size;               /* CF_SET_VERSION, sizeof (cf_set_t) */
    uint32_t check;                       /* FNV-1a of s */
    cf_set_t s;
} cf_rec_t;
_Static_assert(sizeof(cf_rec_t) <= ST_LOG_MAX, "settings: a record fits one log entry");
_Static_assert(sizeof(cf_set_t) == 188u, "settings: 0.4.1 takes a record of this size (a new field fits the padding, "
               "or a downgrade loses the settings)");
_Static_assert(__builtin_offsetof(cf_set_t, notes_in) == 172u && __builtin_offsetof(cf_set_t, mark) == 174u &&
               __builtin_offsetof(cf_set_t, rsv_ci) == 175u,
               "settings: NOTES IN, the ARP's byte and 0.5's mark sit where 0.4.x keeps the looper's four");
_Static_assert(OBJ_PROJECT0 + 3u == ST_LOG_OBJ, "settings: the object that keeps the log");

static cf_rec_t cf_rec;
static uint32_t cfs_saved, cfs_seen;      /* the check of what flash holds, of cs at the last poll */
static uint32_t cfs_changed_ms, cfs_sound_ms, cfs_retry_ms;
static uint8_t cfs_loaded;                /* cs came from flash at power-on */
static uint8_t cfs_restore_lock;          /* a backup restored the record (cf_backup.c): no save until the restart */
static uint8_t cfs_tidy;                  /* the flash is tidy (storage.c st_tidy): nothing to do in a quiet moment */

static uint32_t cfs_fnv(const void *p, uint32_t n)
{
    const uint8_t *b = (const uint8_t *)p;
    uint32_t h = 2166136261u;
    while (n--)
        h = (h ^ *b++) * 16777619u;
    return h;
}

/* a stored value out of range (a record from a build with other limits): the default instead */
static void cfs_sanitize(cf_set_t *s)
{
    cf_set_t d;
    uint32_t i;
    cs_defaults(&d);
    if (s->chord.algo >= CF_NALGS) s->chord.algo = d.chord.algo;
    if (s->chord.quantize > CF_Q_FULL) s->chord.quantize = d.chord.quantize;
    if (s->chord.tonic > 11u) s->chord.tonic = d.chord.tonic;
    if (s->chord.scale >= CF_NSCALES) s->chord.scale = d.chord.scale;
    if (s->chord.octave < -3 || s->chord.octave > 3) s->chord.octave = 0;
    for (i = 0; i < NELEM(s->chord.top); i++)
        if (s->chord.top[i].type >= CF_NTYPES || s->chord.top[i].inv < CF_INV_AUTO || s->chord.top[i].inv > 3)
            s->chord.top[i] = d.chord.top[i];
    for (i = 0; i < NELEM(s->chord.side); i++)
        if (s->chord.side[i].type >= CF_NTYPES || s->chord.side[i].inv < CF_INV_AUTO || s->chord.side[i].inv > 3)
            s->chord.side[i] = d.chord.side[i];
    {   /* the Top buttons still in the first builds' default order (Off M m | s2 s4 | M6 M7 M9 | m6 m7 | m9), never
         * changed: the new default order. A record with no Off button at all: the defaults too. */
        static const cf_form_t OLD[CF_TOP_NBTN] = {
            {CF_T_NONE, CF_OFF_ADD, 0, 0}, {CF_T_MAJ, 0, 0, 0}, {CF_T_MIN, 0, 0, 0}, {CF_T_SUS2, 0, 0, 0},
            {CF_T_SUS4, 0, 0, 0}, {CF_T_MAJ, CF_A_6, 0, 0}, {CF_T_MAJ, CF_A_7, 0, 0}, {CF_T_MAJ, CF_A_9, 0, 0},
            {CF_T_MIN, CF_A_6, 0, 0}, {CF_T_MIN, CF_A_b7, 0, 0}, {CF_T_MIN, CF_A_9, 0, 0},
        };
        uint32_t same = 1, offs = 0;
        for (i = 0; i < CF_TOP_NBTN; i++) {
            const cf_form_t *f = &s->chord.top[i];
            if (f->type != OLD[i].type || f->add != OLD[i].add || f->inv != OLD[i].inv || f->bass != OLD[i].bass)
                same = 0;
            offs += f->add == CF_OFF_ADD;
        }
        if (same || !offs)
            for (i = 0; i < CF_TOP_NBTN; i++)
                s->chord.top[i] = d.chord.top[i];
    }
    if (s->glide_ms > 2000u) s->glide_ms = d.glide_ms;
    if (!s->vel || s->vel > 127u) s->vel = d.vel;
    if (s->bass_oct < -2 || s->bass_oct > 2) s->bass_oct = 0;
    if (s->bass_level > 127u) s->bass_level = d.bass_level;
    if (s->metro_level > 100u) s->metro_level = d.metro_level;
    if (s->beats < 1u || s->beats > 12u) s->beats = d.beats;
    if (s->bpm < 30u || s->bpm > 300u) s->bpm = d.bpm;
    if (s->accent >= (uint8_t)CF_NTHEMES) s->accent = d.accent;
    if (s->ch_chord > 15u) s->ch_chord = d.ch_chord;
    if (s->ch_bass > 15u) s->ch_bass = d.ch_bass;
    if (s->ch_layer > 15u) s->ch_layer = d.ch_layer;
    if (s->ch_drums > 15u) s->ch_drums = d.ch_drums;
    if (s->click >= CF_NCLICKS) s->click = d.click;
    if (s->backdrop >= FL_NBD) s->backdrop = d.backdrop;
    s->glide_always = s->glide_always != 0;
    s->bass_on = s->bass_on != 0;
    s->octaver = s->octaver != 0;
    s->env_link = s->env_link != 0;
    s->midi_out = s->midi_out != 0;
    s->lights = s->lights != 0;
    if (s->retrig >= CF_NRT) s->retrig = d.retrig;
    if (s->clock_in > 2u) s->clock_in = 0;      /* (0 Off, 1 In, 2 Out) */
    s->boot_sound = s->boot_sound != 0;
    if (!(s->resume & CF_RESUME_ON))
        s->resume = 0;
    if (s->arp.mode >= CF_ARP_NMODES || s->arp.oct < 1u || s->arp.oct > 4u || s->arp.rate >= CF_ARP_NRATES ||
        s->arp.gate < 20u || s->arp.gate > 100u || s->arp.swing < 50u || s->arp.swing > 75u || s->arp.latch > 1u ||
        s->arp.strum_ms < 5u || s->arp.strum_ms > 250u)
        s->arp = d.arp;
    if (s->mark != CF_SET_MARK05) {               /* (an older build's record: these two bytes were its looper's BARS
                                                   * and MODE, set in 0.3.1 or never) */
        s->notes_in = d.notes_in;
        s->arp_keep = d.arp_keep;
    }
    if (s->notes_in >= CF_NI_N)
        s->notes_in = d.notes_in;
    s->arp_keep = s->arp_keep != 0;
    s->mark = CF_SET_MARK05;
    s->rsv_ci = 1;                                /* (0.4.x reads it as the looper's COUNT-IN) */
    {                                     /* the chain: each effect once, in some order; the values in range */
        uint32_t seen = 0, e;
        for (i = 0; i < CFX_N; i++)
            seen |= s->fx.order[i] < CFX_N ? 1u << s->fx.order[i] : 0u;
        if (seen != (1u << CFX_N) - 1u)
            s->fx = d.fx;
        for (e = 0; e < CFX_N; e++)
            for (i = 0; i < 3u; i++)
                if (s->fx.v[e][i] > 100u || ((e == CFX_FILTER || e == CFX_REVERB) && !i && s->fx.v[e][i] > 2u))
                    s->fx.v[e][i] = d.fx.v[e][i];
        s->fx.on &= (1u << CFX_N) - 1u;
    }
}

/* power-on: cs from flash, if a good record is there (1), else cs stays as it is (the defaults) */
static int cf_settings_load(void)
{
    int n, rc = 0;
    uint32_t k;
    cfs_loaded = 0;
    if (cr_safe)
        return 0;
    st_check_blank(CF_SET_OBJ);
    for (k = 0; k < 3u; k++) {                    /* (power-on, the audio not running yet: an erase here is silent) */
        rc = st_tidy(CF_SET_OBJ);
        fm1_wdt_feed();
        if (rc <= 0)
            break;
    }
    cfs_tidy = rc == 0;
    for (n = 0; n < (int)(OBJ_CFTWK7 - OBJ_CFEDIT0 + 1u); n++) {   /* .. the sounds' edits', the pattern sounds' and
                                                   * the tweaked pattern sounds' objects too (read later: cfk_load,
                                                   * cfps_load) */
        st_check_blank((uint32_t)OBJ_CFEDIT0 + (uint32_t)n);
        for (k = 0; k < 3u && st_tidy((uint32_t)OBJ_CFEDIT0 + (uint32_t)n) > 0; k++)
            fm1_wdt_feed();
    }
    n = st_load(CF_SET_OBJ, &cf_rec, sizeof cf_rec);
    /* an older build's record may be shorter (fields are only ever added at the end of cf_set_t): what it has is
     * taken, the rest stays at the defaults */
    if (n < (int)__builtin_offsetof(cf_rec_t, s) || cf_rec.magic != CF_SET_MAGIC || cf_rec.version != CF_SET_VERSION ||
        cf_rec.size > sizeof(cf_set_t) || n < (int)(__builtin_offsetof(cf_rec_t, s) + cf_rec.size) ||
        cf_rec.check != cfs_fnv(&cf_rec.s, cf_rec.size))
        return 0;
    cs_defaults(&cs);
    memcpy(&cs, &cf_rec.s, cf_rec.size);
    cfs_sanitize(&cs);
    cfs_saved = cfs_seen = cfs_fnv(&cs, sizeof cs);
    cfs_loaded = 1;
    return 1;
}

/* cs to flash (storage.c st_put; how: ST_NOERASE while the audio plays): 1 written, 0 not now (the log full: a
 * quiet moment makes room), -1 a flash error */
static int cf_settings_write(uint32_t how)
{
    int rc;
    cf_rec.magic = CF_SET_MAGIC;
    cf_rec.version = CF_SET_VERSION;
    cf_rec.size = sizeof(cf_set_t);
    cf_rec.s = cs;
    cf_rec.check = cfs_fnv(&cf_rec.s, sizeof cf_rec.s);
    if ((rc = st_put(CF_SET_OBJ, &cf_rec, sizeof cf_rec, how)) != 0)
        return rc == 1 ? 0 : -1;
    cfs_saved = cf_rec.check;
    cfs_tidy = 0;
    return 1;
}

/* something sounds: a voice of any part (the chords, the bass, the sequencer's LAYER and DRUMS: a crash's tail
 * was cut when only the first two counted), the metronome's clock, the transport, anything at the effects' output
 * (a reverb's or a delay's tail: the owner heard holes in it where a save stopped the audio) */
static int cfs_sounding(void)
{
    uint32_t p, i;
    if (cf_sn.metro_on || cf_sn.running || cfx_quiet < FS / CTL)
        return 1;
    for (p = 0; p < NTRK; p++)
        for (i = 0; i < NVOICE; i++)
            if (trk[p].v[i].active)
                return 1;
    return 0;
}

/* ------------------------------------------------------- the patterns' own sounds --- */
/* 0.5 (the owner, 2026-10-21: "в каждый паттерн можно было свой звук подгрузить на каждый инструмент"): each pattern's
 * CHORDS, BASS and LAYER sound its own, or HOME's ("HOME": it follows what HOME plays). A sound is kept by what it is
 * (its engine, factory preset, user slot and a check of its name), so the row moving or a user sound gone is found
 * out (gone: the track follows HOME again). An object of its own (storage.c OBJ_CFPSND, a log as the settings'): the
 * patterns' store keeps its format, so 0.4.x reads the patterns. Written as the settings are */
#define CFPS_MAGIC 0x31524643u                     /* "CFR1" */
typedef struct {
    uint8_t own;                                  /* 1: the pattern's own sound; 0: HOME's */
    uint8_t engine, kind, fk, slot, nck;          /* (cf_snd_t's; nck: a user sound's name, summed) */
    uint16_t pos;                                 /* (an INIT: its place) */
} cf_sref_t;
typedef struct {
    uint32_t magic;
    uint8_t npat, ntrk, rsv[2];
    cf_sref_t r[SQ_NPAT][3];                      /* the tracks: 0 CHORDS (part 0), 1 BASS (part 1), 2 LAYER (part 2) */
    uint32_t check;
} cfps_rec_t;
_Static_assert(sizeof(cfps_rec_t) <= ST_LMAX(OBJ_CFPSND), "pattern sounds: the record fits one log entry");
static cfps_rec_t cfps;                             /* (RAM: what the patterns have) */
static uint32_t cfps_saved, cfps_seen, cfps_changed_ms, cfps_retry_ms;
static uint8_t cfps_tidy;

static uint8_t cfps_nck(const char *n)
{
    uint32_t h = 0;
    while (*n)
        h = h * 31u + (uint8_t)*n++;
    return (uint8_t)(h ^ h >> 8 ^ h >> 16);
}
static void cfps_ref_of(cf_sref_t *r, const cf_snd_t *m)   /* m 0: HOME */
{
    memset(r, 0, sizeof *r);
    if (!m)
        return;
    r->own = 1;
    r->engine = m->engine;
    r->kind = m->kind;
    r->fk = m->fk;
    r->slot = m->slot;
    r->nck = cfps_nck(m->name);
    r->pos = m->pos;
}
static void cfps_set(uint32_t pat, uint32_t t3, const cf_snd_t *m) { cfps_ref_of(&cfps.r[pat % SQ_NPAT][t3 % 3u], m); }
/* a pattern's track's own sound as a place in its engine's pool: 1, or 0 (HOME's, or its sound gone) */
static int cfps_ref_where(const cf_sref_t *r, uint32_t *e, uint32_t *pos)   /* a kept sound's place now: 1 */
{
    uint32_t en = r->engine % NENGINES;
    if (!r->own)
        return 0;
    *e = en;
    if (r->kind == PK_USER) {
        char nm[13];
        if (!up_used(r->slot))
            return 0;
        up_name(r->slot, nm);
        if (cfps_nck(nm) != r->nck)
            return 0;
        *pos = cb_bound(r->slot) != PF_NONE ? pool_pos_factory(en, cb_bound(r->slot)) : pool_pos_slot(en, r->slot);
        return 1;
    }
    if (r->kind == PK_FACTORY) {
        *pos = pool_pos_factory(en, r->fk);
        return 1;
    }
    *pos = 0;                                     /* (INIT: the pool's first place) */
    return 1;
}
static int cfps_where(uint32_t pat, uint32_t t3, uint32_t *e, uint32_t *pos)
{
    return cfps_ref_where(&cfps.r[pat % SQ_NPAT][t3 % 3u], e, pos);
}
static uint32_t cfps_check(void)
{
    cfps.magic = CFPS_MAGIC;
    cfps.npat = SQ_NPAT;
    cfps.ntrk = 3;
    return cfs_fnv(&cfps, (uint32_t)__builtin_offsetof(cfps_rec_t, check));
}
static int cfps_valid(const cfps_rec_t *r)
{
    return r->magic == CFPS_MAGIC && r->npat == SQ_NPAT && r->ntrk == 3u &&
           r->check == cfs_fnv(r, (uint32_t)__builtin_offsetof(cfps_rec_t, check));
}
/* power-on, before the sounds load (cf_ui.c cf_ui_init): what the patterns have (none kept: every track HOME's) */
static void cfps_load(void)
{
    static cfps_rec_t r;
    memset(&cfps, 0, sizeof cfps);
    cfps_saved = cfps_seen = cfps_check();
    cfps_tidy = 0;
    if (cr_safe)
        return;
    st_check_blank(OBJ_CFPSND);
    if (st_load(OBJ_CFPSND, &r, sizeof r) == (int)sizeof r && cfps_valid(&r)) {
        cfps = r;
        cfps_saved = cfps_seen = r.check;
    }
}
static void cfps_poll(uint32_t now, int silent)
{
    uint32_t h;
    int rc, quiet;
    if (cfps_retry_ms && (int32_t)(now - cfps_retry_ms) < 0)
        return;
    cfps_retry_ms = 0;
    h = cfps_check();
    if (h != cfps_seen) {
        cfps_seen = h;
        cfps_changed_ms = now;
    }
    quiet = silent && now - cfps_changed_ms >= CF_SET_QUIET_MS;
    if (h == cfps_saved) {
        if (!cfps_tidy && quiet) {
            rc = st_tidy(OBJ_CFPSND);
            cfps_tidy = rc == 0;
            if (rc < 0)
                cfps_retry_ms = now + 1000u;
        }
        return;
    }
    if (now - cfps_changed_ms < CF_SET_QUICK_MS)
        return;
    if (!quiet && felucca_dbg.last_us * 20u > HALF_FRAMES * 1000000u / FS * 17u)
        return;
    cfps.check = h;
    rc = st_put(OBJ_CFPSND, &cfps, sizeof cfps, quiet ? 0u : ST_NOERASE);
    if (rc == 0) {
        cfps_saved = h;
        cfps_tidy = 0;
    } else {
        cfps_retry_ms = now + 1000u;
    }
}
static void cfps_flush(void)                       /* a backup's LIST: what is not written yet, now */
{
    uint32_t h = cfps_check();
    cfps.check = h;
    if (h != cfps_saved && st_put(OBJ_CFPSND, &cfps, sizeof cfps, 0u) == 0) {
        cfps_saved = h;
        cfps_tidy = 0;
    }
}

/* ------------------------------------------------------------ the sounds' tweaks --- */
/* A sound tweaked on ENV or EDIT keeps its tweak where the sound belongs, with no SAVE (0.4.2, the owner: "Огибающая
 * адср для баса не сохранилась", "Как оставил"; 0.5: "Сама в паттерн"): HOME's sound on each part (chord, bass, LAYER)
 * in a store of its own (storage.c OBJ_CFEDIT0..2), a pattern's own sound in one of eight stores (OBJ_CFTWK0..7, a
 * pattern and a track each); a ninth asks for a SAVE. A store's record: whose it is (HOME's: pat1 0, else the pattern
 * + 1, and the part), the sound tweaked (engine, kind, factory preset, user slot, name), the sound as tweaked (the
 * parameters a sound holds, cr_bank.c param_kept's left out, and its engine's patch, eng_deep_t.blob_get), or "not
 * tweaked". A part loading a sound takes its owner's tweak with it, when made on that very sound (cfk_take; the bar
 * line's ready sound too: cf_sequi.c sq_snd_poll); one made on another sound (a user sound deleted or replaced) is
 * left. The tweak goes when its owner's sound changes (PRESETS, K2, SAVE: cfk_drop). Each store a log of 512-byte
 * entries (13 tweaks in a row with no erase), written as the settings are */
static void cf_message(const char *s);           /* (cf_ui.c) */
static uint32_t sq_cur_pat(void);                /* (cf_sequi.c) */
#define CFK_MAGIC 0x314B4643u                     /* "CFK1" */
#define CFK_NSLOT 8u                              /* tweaked pattern sounds kept at once */
#define CFK_NST (3u + CFK_NSLOT)                  /* the stores: 0..2 HOME's sound on part 0..2, then the slots */
typedef struct {
    uint32_t magic;
    uint8_t part, edited, engine, kind, fk, slot, np, blen;   /* np: P_COUNT; blen: the engine's blob_size */
    char name[13];
    uint8_t pat1, rsv[2];                         /* whose: 0 HOME's sound, else the pattern + 1 (0.4.2's: 0) */
    int16_t p[P_COUNT];                           /* (param_kept's: 0) */
    uint8_t blob[ENG_BLOB_MAX];
    uint32_t check;                               /* FNV-1a of what is before it */
} cfk_rec_t;
_Static_assert(sizeof(cfk_rec_t) <= ST_LMAX(OBJ_CFEDIT0) && sizeof(cfk_rec_t) <= ST_LMAX(OBJ_CFTWK0),
               "tweaks: a record fits one log entry");
#define CFK_OBJ(part) ((uint32_t)OBJ_CFEDIT0 + (part))
static uint32_t cfk_obj(uint32_t st) { return st < 3u ? (uint32_t)OBJ_CFEDIT0 + st : (uint32_t)OBJ_CFTWK0 + st - 3u; }

static cfk_rec_t cfk_rec;                         /* (one, built and checked in turn) */
static struct {                                   /* each store's record, as far as finding it goes */
    uint8_t edited, pat1, part, engine, kind, fk, slot, tidy, drop;   /* drop: "not tweaked" owed to flash */
    char name[13];
    uint32_t saved;                               /* the check of what flash holds */
} cfk_st[CFK_NST];
static uint8_t cfk_on[3], cfk_gen[3];             /* the live part tweaked since its load (cf_ui.c cf_edited), on load cfk_gen */
static uint8_t cfk_own[3];                        /* .. whose its sound is: 0 HOME's, the pattern + 1 */
static uint8_t cfk_full[3];                       /* .. a ninth pattern sound tweaked: told once */
static uint32_t cfk_seen[3], cfk_changed_ms[3], cfk_retry_ms;

static int cfk_edited(uint32_t part) { return cfk_on[part] && cfk_gen[part] == cf_snd_gen[part]; }
static const eng_deep_t *cfk_deep(const track_t *t)   /* the engine's patch blob (FM6, VA), 0 none */
{
    const eng_deep_t *d = ENGINES[eng_idx(t->eng_req)]->deep;
    return d && d->blob_get && d->blob_set && d->blob_size && d->blob_size <= ENG_BLOB_MAX ? d : 0;
}
static int cfk_valid(const cfk_rec_t *r, uint32_t part)
{
    return r->magic == CFK_MAGIC && r->part == part && r->np == P_COUNT && r->blen <= ENG_BLOB_MAX &&
           r->check == cfs_fnv(r, (uint32_t)__builtin_offsetof(cfk_rec_t, check));
}
static void cfk_head(uint32_t st, const cfk_rec_t *r)   /* the store's index from its record */
{
    cfk_st[st].edited = r->edited;
    cfk_st[st].pat1 = r->pat1;
    cfk_st[st].part = r->part;
    cfk_st[st].engine = r->engine;
    cfk_st[st].kind = r->kind;
    cfk_st[st].fk = r->fk;
    cfk_st[st].slot = r->slot;
    memcpy(cfk_st[st].name, r->name, sizeof cfk_st[st].name);
}
static int cfk_same(uint32_t st, const cf_snd_t *m)   /* the store's tweak was made on sound m */
{
    return cfk_st[st].edited && cfk_st[st].engine == m->engine && cfk_st[st].kind == m->kind &&
           cfk_st[st].fk == m->fk && cfk_st[st].slot == m->slot && !memcmp(cfk_st[st].name, m->name, sizeof m->name);
}
static int cfk_find(uint32_t pat1, uint32_t part)   /* the store of an owner's tweak: -1 none */
{
    uint32_t st;
    if (!pat1)
        return (int)part;
    for (st = 3u; st < CFK_NST; st++)
        if (cfk_st[st].edited && cfk_st[st].pat1 == pat1 && cfk_st[st].part == part)
            return (int)st;
    return -1;
}
static void cfk_build(uint32_t part, cfk_rec_t *r)   /* the live part's record (tweaked or not) */
{
    const track_t *t = &trk[part];
    const eng_deep_t *d = cfk_deep(t);
    uint32_t i;
    memset(r, 0, sizeof *r);
    r->magic = CFK_MAGIC;
    r->part = (uint8_t)part;
    r->np = (uint8_t)P_COUNT;
    if (cfk_edited(part)) {
        r->edited = 1;
        r->pat1 = cfk_own[part];
        r->engine = cf_snd[part].engine;
        r->kind = cf_snd[part].kind;
        r->fk = cf_snd[part].fk;
        r->slot = cf_snd[part].slot;
        memcpy(r->name, cf_snd[part].name, sizeof r->name);
        for (i = 0; i < P_COUNT; i++)
            r->p[i] = param_kept(i) ? 0 : t->p[i];
        if (d) {
            r->blen = (uint8_t)d->blob_size;
            d->blob_get(t, r->blob);
        }
    }
    r->check = cfs_fnv(r, (uint32_t)__builtin_offsetof(cfk_rec_t, check));
}
/* the tweak of store st as it is kept, read into cfk_rec (made on sound m, for part): 1 */
static int cfk_read(uint32_t st, uint32_t part, const cf_snd_t *m)
{
    if (st >= CFK_NST || !cfk_same(st, m))
        return 0;
    return st_load(cfk_obj(st), &cfk_rec, sizeof cfk_rec) == (int)sizeof cfk_rec && cfk_valid(&cfk_rec, part) &&
           cfk_rec.edited;
}
/* a part has loaded a sound of owner pat1 (cf_sequi.c sq_snd_poll; power-on): its tweak back on it, if made on it */
static void cfk_take(uint32_t part, uint32_t pat1)
{
    track_t *t = &trk[part];
    const eng_deep_t *d = cfk_deep(t);
    int st = cfk_find(pat1, part);
    uint32_t i;
    cfk_own[part] = (uint8_t)pat1;
    cfk_on[part] = 0;
    cfk_gen[part] = cf_snd_gen[part];
    cfk_full[part] = 0;
    if (st < 0 || !cfk_read((uint32_t)st, part, &cf_snd[part]) || cfk_rec.blen != (d ? d->blob_size : 0u))
        return;
    fm1_irq_off();
    for (i = 0; i < P_COUNT; i++)
        if (!param_kept(i))
            t->p[i] = cfk_rec.p[i];
    fm1_irq_on();
    if (d)
        d->blob_set(t, cfk_rec.blob);
    cfk_on[part] = 1;
    cu.edited[part] = 1;                          /* (SAVE offers to keep it over itself) */
    cfk_build(part, &cfk_rec);
    cfk_seen[part] = cfk_rec.check;               /* (as kept: nothing to write) */
}
/* an owner's sound changed (PRESETS, K2, SAVE): its tweak goes ("not tweaked" written by the poll) */
static void cfk_drop(uint32_t pat1, uint32_t part)
{
    int st = cfk_find(pat1, part);
    if (st < 0 || !cfk_st[st].edited)
        return;
    cfk_st[st].edited = 0;
    cfk_st[st].drop = 1;
    if (cfk_own[part] == pat1)
        cfk_on[part] = 0;
}
/* power-on, before the sounds load: what each store holds (put on the parts by their loads: cfk_take) */
static void cfk_load(void)
{
    uint32_t st;
    memset(cfk_st, 0, sizeof cfk_st);
    for (st = 0; st < CFK_NST; st++) {
        int n;
        cfk_st[st].part = (uint8_t)(st < 3u ? st : 0u);
        if (cr_safe)
            continue;
        st_check_blank(cfk_obj(st));
        n = st_load(cfk_obj(st), &cfk_rec, sizeof cfk_rec);
        if (n == (int)sizeof cfk_rec && cfk_valid(&cfk_rec, cfk_rec.part) && (st < 3u ? cfk_rec.part == st &&
            !cfk_rec.pat1 : cfk_rec.pat1 != 0u && cfk_rec.pat1 <= SQ_NPAT && cfk_rec.part < 3u)) {
            cfk_head(st, &cfk_rec);
            cfk_st[st].saved = cfk_rec.check;
        } else {
            cfk_st[st].saved = n < 0 ? 1u : 0u;   /* (nothing there: nothing owed; a bad record: one owed) */
            cfk_st[st].drop = n >= 0;
        }
    }
}
/* the store a tweak of the live part goes to: HOME's, or its pattern's slot (a free one taken: one not tweaked, or
 * one whose pattern has another sound now); -1 none free */
static int cfk_store_for(uint32_t part)
{
    uint32_t pat1 = cfk_own[part], st, e, pos;
    int f = cfk_find(pat1, part);
    cf_snd_t m;
    if (f >= 0)
        return f;
    for (st = 3u; st < CFK_NST; st++) {
        uint32_t pp = cfk_st[st].pat1;
        if (!cfk_st[st].edited)
            break;
        if (pp && pp <= SQ_NPAT && cfps_where(pp - 1u, cfk_st[st].part, &e, &pos)) {
            cf_snd_meta(e, pos, &m);
            if (cfk_same(st, &m))
                continue;
        }
        break;                                    /* (its pattern plays another sound now: the tweak is lost anyway) */
    }
    if (st >= CFK_NST)
        return -1;
    cfk_st[st].edited = 0;
    return (int)st;
}
static int cfk_write(uint32_t st, const cfk_rec_t *r, uint32_t how)
{
    int rc = st_put(cfk_obj(st), r, sizeof *r, how);
    if (rc == 0) {
        cfk_head(st, r);
        cfk_st[st].saved = r->check;
        cfk_st[st].tidy = 0;
        cfk_st[st].drop = 0;
    }
    return rc;
}
/* every frame: a tweak, once it has rested, to its owner's store; "not tweaked" owed; a quiet moment tidies. One
 * write a frame */
static void cfk_poll(uint32_t now, int silent)
{
    uint32_t part, st;
    int rc, quiet;
    if (cfk_retry_ms && (int32_t)(now - cfk_retry_ms) < 0)
        return;
    cfk_retry_ms = 0;
    for (part = 0; part < 3u; part++) {
        int s;
        cfk_build(part, &cfk_rec);
        if (cfk_rec.check != cfk_seen[part]) {
            cfk_seen[part] = cfk_rec.check;
            cfk_changed_ms[part] = now;
        }
        if (!cfk_rec.edited || now - cfk_changed_ms[part] < CF_SET_QUICK_MS)
            continue;
        s = cfk_store_for(part);
        if (s < 0) {
            if (!cfk_full[part]) {
                cfk_full[part] = 1;
                cf_message("8 pattern sounds tweaked:\nSAVE one to keep this");
            }
            continue;
        }
        if (cfk_rec.check == cfk_st[s].saved)
            continue;
        quiet = silent && now - cfk_changed_ms[part] >= CF_SET_QUIET_MS;
        if (!quiet && felucca_dbg.last_us * 20u > HALF_FRAMES * 1000000u / FS * 17u)
            return;                               /* (over 85 %: the write waits for room) */
        if (cfk_write((uint32_t)s, &cfk_rec, quiet ? 0u : ST_NOERASE) != 0)
            cfk_retry_ms = now + 1000u;           /* (the log full while it sounds, or a flash error) */
        return;
    }
    for (st = 0; st < CFK_NST; st++) {
        quiet = silent && now - cfk_changed_ms[0] >= CF_SET_QUIET_MS && now - cfk_changed_ms[1] >= CF_SET_QUIET_MS &&
                now - cfk_changed_ms[2] >= CF_SET_QUIET_MS;
        if (cfk_st[st].drop) {                    /* ("not tweaked") */
            memset(&cfk_rec, 0, sizeof cfk_rec);
            cfk_rec.magic = CFK_MAGIC;
            cfk_rec.part = cfk_st[st].part;
            cfk_rec.np = (uint8_t)P_COUNT;
            cfk_rec.check = cfs_fnv(&cfk_rec, (uint32_t)__builtin_offsetof(cfk_rec_t, check));
            if (cfk_write(st, &cfk_rec, quiet ? 0u : ST_NOERASE) != 0)
                cfk_retry_ms = now + 1000u;
            return;
        }
        if (!cfk_st[st].tidy && quiet) {
            rc = st_tidy(cfk_obj(st));
            cfk_st[st].tidy = rc == 0;
            if (rc < 0)
                cfk_retry_ms = now + 1000u;
            if (rc)
                return;
        }
    }
}
/* a backup's LIST: what is not written yet, now */
static void cfk_flush(void)
{
    uint32_t part;
    for (part = 0; part < 3u; part++) {
        int s;
        cfk_build(part, &cfk_rec);
        if (cfk_rec.edited && (s = cfk_store_for(part)) >= 0 && cfk_rec.check != cfk_st[s].saved)
            (void)cfk_write((uint32_t)s, &cfk_rec, 0u);
    }
}

/* every UI frame (main.c's main loop): a change, once it has rested, goes to flash; a quiet moment tidies it */
static void settings_poll(void)
{
    uint32_t now = cu.now, h;
    int quiet, rc;
    if (cr_safe || !cu.booted || cfs_restore_lock)
        return;
    if (cfs_sounding())
        cfs_sound_ms = now;
    if (cs.arp_keep != cu.arp_on)                 /* (the ARP's on / off, kept: cf_ui.c cf_ui_init) */
        cs.arp_keep = cu.arp_on;
    h = cfs_fnv(&cs, sizeof cs);
    if (h != cfs_seen) {
        cfs_seen = h;
        cfs_changed_ms = now;
    }
    cfk_poll(now, now - cfs_sound_ms >= CF_SET_SILENT_MS);   /* (the sounds' edits) */
    cfps_poll(now, now - cfs_sound_ms >= CF_SET_SILENT_MS);   /* (the patterns' own sounds) */
    if (cfs_retry_ms && (int32_t)(now - cfs_retry_ms) < 0)
        return;                                   /* (a flash error, or the log full: once more a second later) */
    cfs_retry_ms = 0;
    quiet = now - cfs_changed_ms >= CF_SET_QUIET_MS && now - cfs_sound_ms >= CF_SET_SILENT_MS;
    if (h == cfs_saved) {
        if (!cfs_tidy && quiet) {
            rc = st_tidy(CF_SET_OBJ);
            cfs_tidy = rc == 0;
            if (rc < 0)
                cfs_retry_ms = now + 1000u;
        }
        return;
    }
    if (now - cfs_changed_ms < CF_SET_QUICK_MS)
        return;
    if (!quiet && felucca_dbg.last_us * 20u > HALF_FRAMES * 1000000u / FS * 17u)
        return;                                   /* (over 85 %: audio.c sheds voices; the write waits for room) */
    if (cf_settings_write(quiet ? 0u : ST_NOERASE) <= 0)
        cfs_retry_ms = now + 1000u;
}

/* main.c, after the power-on calibration: at once */
static void settings_save(void)
{
    if (!cr_safe && !cfs_restore_lock)
        cf_settings_write(0);
}
