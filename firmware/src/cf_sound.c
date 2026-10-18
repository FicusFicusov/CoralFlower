/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca); CoralFlower contributors */
/* CoralFlower: the sounds of the two parts (after ChoralRoot's cr_ui.c loaders).
 *
 * A list of sounds per part, across the engines: each engine's pool (cr_bank.c: INIT, the factory presets, the
 * user's) one after the other, the chord-worthy engines only, and of those the part's own (cf_ent_part: the chord
 * sounds, the basses). PRESETS turns the chord part through its list, SELECT (on HOME) the bass part through its. A load releases the part's notes (panic_req, the audio side) and keeps what is the part's
 * own: its level, pan and effect sends (cr_bank.c param_kept). Included after cr_bank.c (pools) and cf_out.c. */

#define CF_PART_CHORD 0u
#define CF_PART_BASS 1u
#define CF_PART_LAYER 2u                         /* the sequencer's LAYER: a sound of its own, from the chord sounds */
#define CF_PART_DRUMS 3u                         /* the sequencer's DRUMS (the DRUM engine) */

typedef struct { char name[13]; uint8_t engine, kind, fk, slot; uint16_t pos; } cf_snd_t;
static cf_snd_t cf_snd[3];

static const uint8_t CF_ENGINES[] = {ENGI_FM6,
#if FELUCCA_VA
                                     ENGI_VA,
#endif
#if FELUCCA_QUAD
                                     ENGI_QUAD,
#endif
};
#define CF_NENG ((uint32_t)sizeof CF_ENGINES)

static const char *cf_engine_name(uint32_t e)
{
    if (e == ENGI_FM6)
        return "FM";
#if FELUCCA_VA
    if (e == ENGI_VA)
        return "VA";
#endif
#if FELUCCA_QUAD
    if (e == ENGI_QUAD)
        return "FM4";
#endif
    return ENGINES[e % NENGINES]->name;
}

/* whose a pool entry is: the chord part's (0), the bass part's (1), both (2: an engine's INIT, the start of a new
 * sound), or no one's (-1: a factory preset taken out of the lists). The factory presets by name: CF_BASS the bass
 * part's, CF_DROP no one's, the rest the chord part's; the user's by the part they were saved from (cr_bank.c cb_store
 * keeps a bass MONO, a chord sound POLY) */
typedef struct { uint8_t e; char name[13]; } cf_named_t;
static const cf_named_t CF_BASS[] = {
    {ENGI_FM6, "BASS"}, {ENGI_FM6, "SOLID BASS"}, {ENGI_FM6, "SAW BASS"},
#if FELUCCA_VA
    {ENGI_VA, "DEEP SUB"}, {ENGI_VA, "PUNCH BASS"}, {ENGI_VA, "RUBBER BASS"}, {ENGI_VA, "SYNC BASS"},
    {ENGI_VA, "FAT BASS"}, {ENGI_VA, "ACID BASS"}, {ENGI_VA, "WOOD BASS"}, {ENGI_VA, "REESE BASS"},
#endif
};
static const cf_named_t CF_DROP[] = {
    {ENGI_FM6, "TINE EP"}, {ENGI_FM6, "BELL"}, {ENGI_FM6, "DX MARIMBA"}, {ENGI_FM6, "DRAWBARS"},
    {ENGI_FM6, "GLASS PAD"}, {ENGI_FM6, "KALIMBA"}, {ENGI_FM6, "FLUTE"}, {ENGI_FM6, "TUBULAR"},
};
static int cf_named(const cf_named_t *t, uint32_t n, uint32_t e, const char *name)
{
    uint32_t i;
    for (i = 0; i < n; i++)
        if (t[i].e == e && cb_streq(t[i].name, name))
            return 1;
    return 0;
}
static int cf_fac_part(uint32_t e, uint32_t f)   /* factory preset f of engine e */
{
    const char *nm = ENGINES[e % NENGINES]->presets[f].name;
    if (cf_named(CF_DROP, NELEM(CF_DROP), e, nm))
        return -1;
    return cf_named(CF_BASS, NELEM(CF_BASS), e, nm);
}
static int cf_ent_part(uint32_t e, uint32_t pos)
{
    pool_ent_t en = pool_entry(e, pos);
    if (en.kind == PK_INIT)
        return 2;
    if (en.kind == PK_USER) {
        int32_t v = up_value(up_rec(en.slot), P_VOICE);
        return v == V_MONO || v == V_LEGATO;
    }
    return cf_fac_part(e, en.fk);
}

/* the lists, rebuilt when the user's sounds change (up_gen): (engine, pool position) per place */
#define CF_LIST_MAX 160u
typedef struct { uint8_t e, pos; } cf_ent_t;
static struct { uint32_t gen; uint8_t ok; uint16_t n[2]; cf_ent_t at[2][CF_LIST_MAX]; } cf_lst;
static void cf_list_build(void)
{
    uint32_t i, pos, p;
    if (cf_lst.ok && cf_lst.gen == up_gen)
        return;
    cf_lst.ok = 1;
    cf_lst.gen = up_gen;
    cf_lst.n[0] = cf_lst.n[1] = 0;
    for (i = 0; i < CF_NENG; i++) {
        uint32_t e = CF_ENGINES[i], np = pool_count(e);
        for (pos = 0; pos < np && pos < 256u; pos++) {
            int w = cf_ent_part(e, pos);
            for (p = 0; p < 2u; p++)
                if ((w == 2 || w == (int)p) && cf_lst.n[p] < CF_LIST_MAX) {
                    cf_lst.at[p][cf_lst.n[p]].e = (uint8_t)e;
                    cf_lst.at[p][cf_lst.n[p]].pos = (uint8_t)pos;
                    cf_lst.n[p]++;
                }
        }
    }
}
static uint32_t cf_snd_count(uint32_t part)
{
    cf_list_build();
    return cf_lst.n[part & 1u];
}
static void cf_snd_at(uint32_t part, uint32_t idx, uint32_t *e, uint32_t *pos)
{
    uint32_t n = cf_snd_count(part);
    if (!n) {
        *e = CF_ENGINES[0];
        *pos = 0;
        return;
    }
    *e = cf_lst.at[part & 1u][idx % n].e;
    *pos = cf_lst.at[part & 1u][idx % n].pos;
}
static uint32_t cf_eng_ord(uint32_t e)
{
    uint32_t i;
    for (i = 0; i < CF_NENG && CF_ENGINES[i] != e; i++)
        ;
    return i;
}
/* the place of (e, pos) in the part's list: its index, or (not in the list) the place it would take there */
static uint32_t cf_snd_index(uint32_t part, uint32_t e, uint32_t pos)
{
    uint32_t i, n = cf_snd_count(part), o = cf_eng_ord(e);
    for (i = 0; i < n; i++) {
        uint32_t oi = cf_eng_ord(cf_lst.at[part & 1u][i].e);
        if (oi > o || (oi == o && cf_lst.at[part & 1u][i].pos >= pos))
            break;
    }
    return i;
}

static void cf_part_voice(track_t *t, uint32_t part)
{
    /* both parts are polyphonic: the bass part for the octaver's second note (cf_play.c keeps the bass monophonic:
     * a new bass note cuts the old one's release tail) */
    t->p[P_VOICE] = V_POLY;
    (void)part;
}

/* engine e's factory preset pi into part (the sound only, not the part's own parameters) */
static void cf_load_factory(uint32_t part, uint32_t e, uint32_t pi)
{
    track_t *t = &trk[part];
    const engine_t *en = ENGINES[e % NENGINES];
    const preset_t *pr;
    uint32_t i;
    if (!en->npresets)
        return;
    pr = &en->presets[pi % en->npresets];
    fm1_irq_off();
    t->eng_req = (uint8_t)(e % NENGINES);
    t->preset = (uint8_t)(pi % en->npresets);
    t->user = 0;
    t->trim = 0;
    for (i = 0; i < P_E0; i++)
        if (!param_kept(i))
            t->p[i] = TP[i].def;
    for (i = 0; i < 8u; i++)
        t->p[P_E0 + i] = (int16_t)pr->e[i];
    t->p[P_ATK] = pr->env[0];
    t->p[P_DEC] = pr->env[1];
    t->p[P_SUS] = pr->env[2];
    t->p[P_REL] = pr->env[3];
    t->p[P_ED_FLT] = pr->fenv;
    cf_part_voice(t, part);
    fm6_track_loaded(t);
    panic_req |= (uint8_t)(1u << part);
    fm1_irq_on();
    str_cpy(cf_snd[part].name, pr->name, sizeof cf_snd[part].name);
}

/* the sequencer's drums (cf_seqp.c): part 3 on the DRUM engine, its kit (eng_drum.c's one preset). Power-on */
static void cf_drums_init(void)
{
    track_t *t = &trk[CF_PART_DRUMS];
    const engine_t *en = ENGINES[ENGI_DRUM];
    const preset_t *pr;
    uint32_t i;
    if (!en->npresets)
        return;
    pr = &en->presets[0];
    fm1_irq_off();
    t->eng_req = ENGI_DRUM;
    t->preset = 0;
    t->user = 0;
    t->trim = 0;
    for (i = 0; i < P_E0; i++)
        t->p[i] = TP[i].def;
    for (i = 0; i < 8u; i++)
        t->p[P_E0 + i] = (int16_t)pr->e[i];
    t->p[P_ATK] = pr->env[0];
    t->p[P_DEC] = pr->env[1];
    t->p[P_SUS] = pr->env[2];
    t->p[P_REL] = pr->env[3];
#ifdef DRUM_LANE_KNOBS
    {                                            /* (every lane in the kit's design until the sequencer's first hit) */
        uint32_t l;
        for (l = 0; l < DV_NLANE; l++)
            for (i = 0; i < 8u; i++)
                drum_lane_knobs[0][l][i] = (int16_t)pr->e[i];
    }
#endif
    fm1_irq_on();
}

/* user slot k into part (upreset.c's records): every parameter but the part's own */
static void cf_load_user(uint32_t part, uint32_t k)
{
    track_t *t = &trk[part];
    const up_rec_t *r;
    int16_t v[P_COUNT];
    uint32_t i;
    if (!up_used(k))
        return;
    r = up_rec(k);
    up_values(r, v);
    fm1_irq_off();
    t->eng_req = eng_ok(r->engine) ? r->engine : ENGI_FM6;
    for (i = 0; i < P_COUNT; i++)
        if (!param_kept(i))
            t->p[i] = v[i];
    t->preset = 0;
    t->user = (uint8_t)(k + 1u);
    t->trim = 0;
    cf_part_voice(t, part);
    fm6_track_loaded(t);
    panic_req |= (uint8_t)(1u << part);
    fm1_irq_on();
    up_name(k, cf_snd[part].name);
}

/* engine e's init sound */
static void cf_load_init(uint32_t part, uint32_t e)
{
    track_t *t = &trk[part];
    const engine_t *en = ENGINES[e % NENGINES];
    uint32_t i;
    fm1_irq_off();
    t->eng_req = (uint8_t)(e % NENGINES);
    t->preset = 0;
    t->user = 0;
    for (i = 0; i < P_E0; i++)
        if (!param_kept(i))
            t->p[i] = TP[i].def;
    for (i = 0; i < 8u; i++)
        t->p[P_E0 + i] = en->edit[i].def;
    t->trim = 0;
    cf_part_voice(t, part);
    fm6_track_loaded(t);
    panic_req |= (uint8_t)(1u << part);
    fm1_irq_on();
    str_cpy(cf_snd[part].name, "INIT", sizeof cf_snd[part].name);
}

/* pool position pos of engine e into the part */
static void cf_pool_load(uint32_t part, uint32_t e, uint32_t pos)
{
    pool_ent_t en;
    e %= NENGINES;
    en = pool_entry(e, pos);
    if (en.kind == PK_USER) {
        cf_load_user(part, en.slot);
        if (en.fk != PF_NONE)
            trk[part].trim = cb_trim(e, en.fk);
    } else if (en.kind == PK_FACTORY) {
        cf_load_factory(part, e, en.fk);
        trk[part].trim = cb_trim(e, en.fk);
    } else {
        cf_load_init(part, e);
    }
    cf_snd[part].engine = (uint8_t)e;
    cf_snd[part].kind = en.kind;
    cf_snd[part].fk = en.fk;
    cf_snd[part].slot = en.slot;
    cf_snd[part].pos = (uint16_t)pos;
}

/* the part's place in the one row of sounds, and a step through it */
static uint32_t cf_snd_pos(uint32_t part) { return cf_snd_index(part, cf_snd[part].engine, cf_snd[part].pos); }
static void cf_snd_load_index(uint32_t part, uint32_t idx)
{
    uint32_t e, pos;
    cf_snd_at(part, idx, &e, &pos);
    cf_pool_load(part, e, pos);
}
static void cf_snd_step(uint32_t part, int32_t d)
{
    int32_t n = (int32_t)cf_snd_count(part), i = (int32_t)cf_snd_pos(part) + d;
    if (n <= 0)
        return;
    i %= n;
    if (i < 0)
        i += n;
    cf_snd_load_index(part, (uint32_t)i);
}
/* a sound by its name in an engine's factory list (the defaults), 0 when not there */
static int cf_snd_find(uint32_t part, uint32_t e, const char *name)
{
    const engine_t *en = ENGINES[e % NENGINES];
    uint32_t k;
    for (k = 0; k < en->npresets; k++)
        if (cb_streq(en->presets[k].name, name)) {
            cf_pool_load(part, e, pool_pos_factory(e, k));
            return 1;
        }
    return 0;
}

/* the parts' places in their pools again, from what they hold (a save adds to a pool: an added sound's place can move
 * up, and every later engine's sounds move along the lists; the UI then takes cf_snd_pos into the settings) */
static void cf_snd_resync(void)
{
    uint32_t p;
    for (p = 0; p < 3u; p++) {
        cf_snd_t *s = &cf_snd[p];
        uint32_t e = s->engine % NENGINES;
        if (s->kind == PK_USER && up_used(s->slot))
            s->pos = (uint16_t)(cb_bound(s->slot) != PF_NONE ? pool_pos_factory(e, cb_bound(s->slot))
                                                             : pool_pos_slot(e, s->slot));
        else if (s->kind == PK_FACTORY)
            s->pos = (uint16_t)pool_pos_factory(e, s->fk);
    }
}

/* SAVE: the part's sound kept in a user slot (upreset.c's 32; the VA's or the FM's patch with it: va_store.c,
 * fm6_ustore.c), named name. Over itself: a user sound in its own slot; a factory one as a user record bound to it,
 * which takes its place in the row (the factory preset comes back when that record is deleted). As new: the next free
 * slot, its engine's pool's next place. The part then plays the saved sound. 0 saved, -1 no free slot, -2 INIT has
 * nothing to save over, -3 SAFE MODE, 2 a flash error */
static int cf_snd_save(uint32_t part, int as_new, const char *name)
{
    track_t *t = &trk[part];
    uint32_t e = t->eng_req % NENGINES, k, f = PF_NONE;
    int res;
    if (cr_safe)
        return -3;
    if (!as_new) {
        if (cf_snd[part].kind == PK_INIT)
            return -2;
        if (cf_snd[part].kind == PK_USER && up_used(cf_snd[part].slot)) {
            k = cf_snd[part].slot;
            f = cb_bind_raw(k);                      /* (its binding as it is) */
        } else {
            f = cf_snd[part].fk;
            k = f != PF_NONE ? pool_bound_slot(e, f) : UP_SLOTS;
            if (k >= UP_SLOTS)
                k = cb_free_slot();
        }
    } else {
        k = cb_free_slot();
    }
    if (k >= UP_SLOTS)
        return -1;
    res = cb_store(t, part, k, name, f != PF_NONE ? f + 1u : 0u);
    if (res != 0 && res != 3)                        /* (3: kept in RAM only, the emulator without flash) */
        return 2;
    t->user = (uint8_t)(k + 1u);
    cf_snd[part].engine = (uint8_t)e;
    cf_snd[part].kind = PK_USER;
    cf_snd[part].slot = (uint8_t)k;
    cf_snd[part].fk = (uint8_t)cb_bound(k);
    up_name(k, cf_snd[part].name);
    cf_snd_resync();
    return 0;
}

/* where a save would put the part's sound in the row: its index (as new: the place it is inserted at) */
static uint32_t cf_snd_save_index(uint32_t part, int as_new)
{
    uint32_t e = trk[part].eng_req % NENGINES, k;
    if (!as_new && cf_snd[part].kind != PK_INIT)
        return cf_snd_pos(part);
    k = cb_free_slot();
    return cf_snd_index(part, e, k < UP_SLOTS ? pool_pos_new(e, k) : pool_count(e));
}

/* SAVE > ACTION Delete: the part's user sound erased (its slot freed, its patch with it). One bound to a factory
 * preset gives that preset its place back (when the preset is in the part's list), and the part plays it; an added
 * one leaves the list, and the part plays what now stands in its place. 0 done, -1 not a user sound, -3 SAFE MODE, 2
 * a flash error */
static int cf_snd_delete(uint32_t part)
{
    uint32_t k = cf_snd[part].slot, pos[3], n, q;
    uint8_t other[3];
    if (cr_safe)
        return -3;
    if (part > 2u || cf_snd[part].kind != PK_USER || !up_used(k))
        return -1;
    for (q = 0; q < 3u; q++) {                       /* (the other parts that play the same user sound: chord, bass,
                                                  * the LAYER) */
        pos[q] = cf_snd_pos(q);
        other[q] = (uint8_t)(q != part && cf_snd[q].kind == PK_USER && cf_snd[q].slot == k);
    }
    if (up_put(k, 0))
        return 2;
    trk[part].user = 0;
    for (q = 0; q < 3u; q++)
        if (q == part || other[q]) {
            trk[q].user = 0;
            n = cf_snd_count(q);                     /* (the lists again: up_gen moved) */
            cf_snd_load_index(q, pos[q] < n ? pos[q] : n - 1u);
        }
    cf_snd_resync();
    return 0;
}
/* would a delete give the part a factory preset back (its own: in the part's list)? */
static int cf_snd_delete_restores(uint32_t part)
{
    uint32_t f = cf_snd[part].kind == PK_USER ? cb_bound(cf_snd[part].slot) : PF_NONE;
    return f != PF_NONE && cf_fac_part(cf_snd[part].engine, f) == (int)(part == CF_PART_LAYER ? CF_PART_CHORD : part);
}
