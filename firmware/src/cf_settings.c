/* SPDX-License-Identifier: GPL-3.0-only
 * CoralFlower: the settings in flash.
 *
 * One record keeps what the screens and GLO set (cf_set_t cs, the parts' sounds as places in the sound row with it).
 * It is read at power-on, before the first picture (cf_splash), and written a while after the last change, once
 * nothing sounds: a flash erase stops the audio for about 45 ms. SAFE MODE neither reads nor writes it.
 *
 * Where: OBJ_PROJECT0 + 3 (0x9D000 / 0x9E000, storage.c's A/B pair), ChoralRoot's CZ-1 tone store, which CoralFlower
 * (no CZ engine) does not use. Anything there that is not our record (another magic, version or size, a bad check)
 * is ignored, and the first save replaces it.
 * Included after cf_ui.c, before cf_splash.c. */

#define CF_SET_OBJ (OBJ_PROJECT0 + 3u)
#define CF_SET_MAGIC 0x31534643u          /* "CFS1" */
#define CF_SET_VERSION 2u                 /* bump when cf_set_t changes: an older record is then ignored */
#define CF_SET_QUIET_MS 1500u             /* a change rests this long before it is written */
#define CF_SET_SILENT_MS 1000u            /* .. and nothing has sounded for this long */

typedef struct {
    uint32_t magic;
    uint16_t version, size;               /* CF_SET_VERSION, sizeof (cf_set_t) */
    uint32_t check;                       /* FNV-1a of s */
    cf_set_t s;
} cf_rec_t;

static cf_rec_t cf_rec;
static uint32_t cfs_saved, cfs_seen;      /* the check of what flash holds, of cs at the last poll */
static uint32_t cfs_changed_ms, cfs_sound_ms, cfs_retry_ms;
static uint8_t cfs_loaded;                /* cs came from flash at power-on */
static uint8_t cfs_restore_lock;          /* a backup restored the record (cf_backup.c): no save until the restart */

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
    int n;
    cfs_loaded = 0;
    if (cr_safe)
        return 0;
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

static int cf_settings_write(void)
{
    cf_rec.magic = CF_SET_MAGIC;
    cf_rec.version = CF_SET_VERSION;
    cf_rec.size = sizeof(cf_set_t);
    cf_rec.s = cs;
    cf_rec.check = cfs_fnv(&cf_rec.s, sizeof cf_rec.s);
    if (st_save(CF_SET_OBJ, &cf_rec, sizeof cf_rec))
        return 0;                             /* (st_save: 0 = written) */
    cfs_saved = cf_rec.check;
    return 1;
}

/* something sounds: a voice of either part, the metronome's clock, the transport, anything at the effects' output
 * (a reverb's or a delay's tail: the owner heard holes in it where a save stopped the audio) */
static int cfs_sounding(void)
{
    uint32_t p, i;
    if (cf_sn.metro_on || cf_sn.running || cfx_quiet < FS / CTL)
        return 1;
    for (p = 0; p < 2u; p++)
        for (i = 0; i < NVOICE; i++)
            if (trk[p].v[i].active)
                return 1;
    return 0;
}

/* every UI frame (main.c's main loop): a change, once it has rested and the unit is quiet, goes to flash */
static void settings_poll(void)
{
    uint32_t now = cu.now, h;
    if (cr_safe || !cu.booted || cfs_restore_lock)
        return;
    if (cfs_sounding())
        cfs_sound_ms = now;
    h = cfs_fnv(&cs, sizeof cs);
    if (h != cfs_seen) {
        cfs_seen = h;
        cfs_changed_ms = now;
    }
    if (h == cfs_saved || now - cfs_changed_ms < CF_SET_QUIET_MS || now - cfs_sound_ms < CF_SET_SILENT_MS ||
        (cfs_retry_ms && (int32_t)(now - cfs_retry_ms) < 0))
        return;
    cfs_retry_ms = cf_settings_write() ? 0u : now + 1000u;   /* (a flash error: once more a second later) */
}

/* main.c, after the power-on calibration: at once */
static void settings_save(void)
{
    if (!cr_safe && !cfs_restore_lock)
        cf_settings_write();
}
