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
    if (s->loop.bars >= CF_LOOP_NBARS || s->loop.mode > 1u || s->loop.quant > 2u || s->loop.count_in > 1u)
        s->loop = d.loop;
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
    for (n = 0; n < 3; n++) {                     /* .. the sounds' edits' objects too (put back later: cfk_load) */
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

/* ------------------------------------------------------ the sounds' unsaved edits --- */
/* The owner, 2026-10-21: "Огибающая адср для баса не сохранилась", then "Как оставил": a part's sound edited on ENV or
 * EDIT and not SAVEd comes back after a power-off as it was left. Each part (chord, bass, LAYER) keeps one record in
 * an object of its own (storage.c OBJ_CFEDIT0..2, a log of 512-byte entries: 13 edits in a row with no erase): the
 * sound the edits were made on (engine, kind, factory preset, user slot, name) and the sound as edited (the
 * parameters a sound holds, cr_bank.c param_kept's left out, and its engine's patch, engine_t.blob_get), or "not
 * edited". At power-on, once the sounds are loaded (cf_ui.c cf_ui_init), a record made on the sound the part has
 * again is put back on it; one made on another sound (a user sound deleted, or replaced by a restored backup) is
 * dropped. Another sound loaded, or the sound SAVEd, the record says "not edited" (cf_sound.c cf_snd_gen). Written as
 * the settings are: half a second after a change, no erase while it sounds, a quiet moment tidies */
#define CFK_MAGIC 0x314B4643u                     /* "CFK1" */
#define CFK_OBJ(part) ((uint32_t)OBJ_CFEDIT0 + (part))
typedef struct {
    uint32_t magic;
    uint8_t part, edited, engine, kind, fk, slot, np, blen;   /* np: P_COUNT; blen: the engine's blob_size */
    char name[13];
    uint8_t rsv[3];
    int16_t p[P_COUNT];                           /* (param_kept's: 0) */
    uint8_t blob[ENG_BLOB_MAX];
    uint32_t check;                               /* FNV-1a of what is before it */
} cfk_rec_t;
_Static_assert(sizeof(cfk_rec_t) <= ST_LMAX(OBJ_CFEDIT0), "edits: a record fits one log entry");

static cfk_rec_t cfk_rec;                         /* (one, built and checked in turn) */
static uint8_t cfk_on[3], cfk_gen[3];             /* an edit (cf_ui.c cf_edited), on the sound load cfk_gen */
static uint8_t cfk_tidy[3];                       /* the part's object is tidy (storage.c st_tidy) */
static uint32_t cfk_saved[3], cfk_seen[3], cfk_changed_ms[3], cfk_retry_ms;

static int cfk_edited(uint32_t part) { return cfk_on[part] && cfk_gen[part] == cf_snd_gen[part]; }
static const eng_deep_t *cfk_deep(const track_t *t)   /* the engine's patch blob (FM6, VA), 0 none */
{
    const eng_deep_t *d = ENGINES[eng_idx(t->eng_req)]->deep;
    return d && d->blob_get && d->blob_set && d->blob_size && d->blob_size <= ENG_BLOB_MAX ? d : 0;
}
static void cfk_build(uint32_t part, cfk_rec_t *r)   /* the part's record as it stands */
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
static int cfk_valid(const cfk_rec_t *r, uint32_t part)
{
    return r->magic == CFK_MAGIC && r->part == part && r->np == P_COUNT && r->blen <= ENG_BLOB_MAX &&
           r->check == cfs_fnv(r, (uint32_t)__builtin_offsetof(cfk_rec_t, check));
}
/* power-on, the sounds loaded (cf_ui.c cf_ui_init): each part's unsaved edits back on its sound (reads only: the
 * audio runs already, an erase waits for a quiet moment) */
static void cfk_load(void)
{
    uint32_t part, i;
    if (cr_safe)
        return;
    for (part = 0; part < 3u; part++) {
        track_t *t = &trk[part];
        const eng_deep_t *d = cfk_deep(t);
        int n;
        cfk_on[part] = 0;
        cfk_gen[part] = cf_snd_gen[part];
        st_check_blank(CFK_OBJ(part));
        n = st_load(CFK_OBJ(part), &cfk_rec, sizeof cfk_rec);
        if (n < 0) {                              /* (nothing kept: as "not edited") */
            cfk_build(part, &cfk_rec);
            cfk_saved[part] = cfk_seen[part] = cfk_rec.check;
            cfk_tidy[part] = 1;
            continue;
        }
        cfk_saved[part] = n == (int)sizeof cfk_rec && cfk_valid(&cfk_rec, part) ? cfk_rec.check : 0u;
        if (!cfk_saved[part] || !cfk_rec.edited || cfk_rec.engine != cf_snd[part].engine ||
            cfk_rec.kind != cf_snd[part].kind || cfk_rec.fk != cf_snd[part].fk || cfk_rec.slot != cf_snd[part].slot ||
            memcmp(cfk_rec.name, cf_snd[part].name, sizeof cfk_rec.name) || cfk_rec.blen != (d ? d->blob_size : 0u))
            continue;                             /* (made on another sound: the poll writes "not edited") */
        fm1_irq_off();
        for (i = 0; i < P_COUNT; i++)
            if (!param_kept(i))
                t->p[i] = cfk_rec.p[i];
        fm1_irq_on();
        if (d)
            d->blob_set(t, cfk_rec.blob);
        cfk_on[part] = 1;
        cu.edited[part] = 1;                      /* (SAVE offers to keep it over itself, as before the power-off) */
    }
}
/* a part whose record changed: written half a second after (quiet: an erase may go), one write a frame; nothing to
 * write, a quiet moment tidies */
static void cfk_poll(uint32_t now, int silent)
{
    uint32_t part;
    int rc, quiet;
    if (cfk_retry_ms && (int32_t)(now - cfk_retry_ms) < 0)
        return;
    cfk_retry_ms = 0;
    for (part = 0; part < 3u; part++) {
        cfk_build(part, &cfk_rec);
        if (cfk_rec.check != cfk_seen[part]) {
            cfk_seen[part] = cfk_rec.check;
            cfk_changed_ms[part] = now;
        }
        quiet = silent && now - cfk_changed_ms[part] >= CF_SET_QUIET_MS;
        if (cfk_rec.check == cfk_saved[part]) {
            if (!cfk_tidy[part] && quiet) {
                rc = st_tidy(CFK_OBJ(part));
                cfk_tidy[part] = rc == 0;
                if (rc < 0)
                    cfk_retry_ms = now + 1000u;
                return;
            }
            continue;
        }
        if (now - cfk_changed_ms[part] < CF_SET_QUICK_MS)
            continue;
        if (!quiet && felucca_dbg.last_us * 20u > HALF_FRAMES * 1000000u / FS * 17u)
            return;                               /* (over 85 %: the write waits for room) */
        rc = st_put(CFK_OBJ(part), &cfk_rec, sizeof cfk_rec, quiet ? 0u : ST_NOERASE);
        if (rc == 0) {
            cfk_saved[part] = cfk_rec.check;
            cfk_tidy[part] = 0;
        } else {
            cfk_retry_ms = now + 1000u;           /* (the log full while it sounds, or a flash error) */
        }
        return;
    }
}
/* a backup's LIST: what is not written yet, now */
static void cfk_flush(void)
{
    uint32_t part;
    for (part = 0; part < 3u; part++) {
        cfk_build(part, &cfk_rec);
        if (cfk_rec.check != cfk_saved[part] && st_put(CFK_OBJ(part), &cfk_rec, sizeof cfk_rec, 0u) == 0) {
            cfk_saved[part] = cfk_rec.check;
            cfk_tidy[part] = 0;
        }
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
    h = cfs_fnv(&cs, sizeof cs);
    if (h != cfs_seen) {
        cfs_seen = h;
        cfs_changed_ms = now;
    }
    cfk_poll(now, now - cfs_sound_ms >= CF_SET_SILENT_MS);   /* (the sounds' edits) */
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
