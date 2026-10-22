/* SPDX-License-Identifier: GPL-3.0-only
 * CoralFlower: the chord input model (cf_chord.h). */
#include "cf_chord.h"

static const uint8_t IS_UPPER_PC[12] = {0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0};

int cf_key_is_upper(int key) { return key >= 0 && key < CF_NKEYS && IS_UPPER_PC[(53 + key) % 12]; }

static int cfc_upper_index(int key)
{
    int k, n = 0;
    for (k = 0; k < key; k++)
        n += cf_key_is_upper(k);
    return n;
}
static int cfc_lower_index(int key) { return key - cfc_upper_index(key); }

int cf_key_role(int algo, int key, int *index)
{
    int up, idx;
    if (key < 0 || key >= CF_NKEYS)
        return CF_ROLE_NONE;
    up = cf_key_is_upper(key);
    idx = up ? cfc_upper_index(key) : cfc_lower_index(key);
    if (algo == CF_ALG_TOP || algo == CF_ALG_TOP_LOCK) {
        if (index)
            *index = idx;
        return up ? CF_ROLE_BUTTON : CF_ROLE_ROOT;
    }
    if (up && idx < 3) {
        if (index)
            *index = idx;
        return CF_ROLE_BUTTON;
    }
    if (!up && idx < 4) {
        if (index)
            *index = 3 + idx;
        return CF_ROLE_BUTTON;
    }
    if (index)
        *index = key;
    return CF_ROLE_ROOT;
}

static int cfc_is_lock(int algo) { return algo == CF_ALG_TOP_LOCK || algo == CF_ALG_SIDE_LOCK; }
static int cfc_is_top(int algo) { return algo == CF_ALG_TOP || algo == CF_ALG_TOP_LOCK; }
static int cfc_clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

int cf_key_root_note(const cf_chord_cfg_t *cfg, int key)
{
    int idx, n;
    if (key >= CF_KEY_EXT && key < CF_KEY_EXT + 128) {   /* a root from outside: where the FM-1's key of that name
                                                          * sounds (its octave too), snapped as a piano key */
        n = key - CF_KEY_EXT;
        if (cfg->quantize != CF_Q_OFF)
            n = cf_snap(cfg->scale, cfg->tonic, n, !IS_UPPER_PC[n % 12]);
        return cfc_clampi(n + 12 * (cfg->octave + CF_OCT_BASE), 0, 127);
    }
    if (cf_key_role(cfg->algo, key, &idx) != CF_ROLE_ROOT)
        return -1;
    if (cfc_is_top(cfg->algo)) {
        /* the lower keys: the tonic on the fifth key, one semitone (Quantize Off) or one scale step a key */
        if (cfg->quantize == CF_Q_OFF) {
            n = 60 + (idx - 4);
        } else {
            int tonic_note = 60 + cfg->tonic - (cfg->tonic > 6 ? 12 : 0);
            n = tonic_note + cf_scale_step(cfg->scale, idx - 4);
        }
    } else {
        /* a piano: the key's own pitch, snapped into the scale (white keys up, black keys down on a tie) */
        n = 53 + key;
        if (cfg->quantize != CF_Q_OFF)
            n = cf_snap(cfg->scale, cfg->tonic, n, !cf_key_is_upper(key));
    }
    return cfc_clampi(n + 12 * (cfg->octave + CF_OCT_BASE), 0, 127);
}

static const cf_form_t OFF_FORM = {CF_T_NONE, CF_OFF_ADD, 0, 0};

const cf_form_t *cf_button_form(const cf_chord_cfg_t *cfg, int btn)
{
    if (cfc_is_top(cfg->algo))
        return &cfg->top[btn >= 0 && btn < CF_TOP_NBTN ? btn : 0];
    return &cfg->side[btn >= 0 && btn < CF_SIDE_NBTN ? btn : 0];
}

/* Off is a form, not a place: the Top button that holds it is HOLD (since 0.5; it played single notes before),
 * wherever it is (the default puts it on the last upper key; a settings record from before keeps it on the first) */
int cf_is_off(const cf_chord_cfg_t *cfg, int btn)
{
    return cfc_is_top(cfg->algo) && btn >= 0 && btn < CF_TOP_NBTN && cfg->top[btn].add == CF_OFF_ADD;
}

static uint16_t cfc_off_mask(const cf_chord_cfg_t *cfg)
{
    uint16_t m = 0;
    int b;
    for (b = 0; b < CF_TOP_NBTN; b++)
        if (cf_is_off(cfg, b))
            m |= (uint16_t)(1u << b);
    return m;
}

void cf_chord_defaults(cf_chord_cfg_t *cfg)
{
    /* the upper keys in their groups of 3 2 3 2 1: M6 M7 M9 | s2 s4 | m6 m7 m9 | M m | Off (the owner's order) */
    static const cf_form_t TOP[CF_TOP_NBTN] = {
        {CF_T_MAJ, CF_A_6, 0, 0}, {CF_T_MAJ, CF_A_7, 0, 0}, {CF_T_MAJ, CF_A_9, 0, 0},
        {CF_T_SUS2, 0, 0, 0}, {CF_T_SUS4, 0, 0, 0},
        {CF_T_MIN, CF_A_6, 0, 0}, {CF_T_MIN, CF_A_b7, 0, 0}, {CF_T_MIN, CF_A_9, 0, 0},
        {CF_T_MAJ, 0, 0, 0}, {CF_T_MIN, 0, 0, 0},
        {CF_T_NONE, CF_OFF_ADD, 0, 0},
    };
    static const cf_form_t SIDE[CF_SIDE_NBTN] = {
        {CF_T_MAJ, 0, 0, 0}, {CF_T_MIN, 0, 0, 0}, {CF_T_DIM, 0, 0, 0},
        {CF_T_SUS2, 0, 0, 0}, {CF_T_SUS4, 0, 0, 0}, {CF_T_NONE, CF_A_b7, 0, 0}, {CF_T_NONE, CF_A_9, 0, 0},
    };
    int i;
    cfg->algo = CF_ALG_TOP_LOCK;                 /* (the first on the ALGORITHM knob: cf_ui.c) */
    cfg->quantize = CF_Q_BASIC;                  /* (a chord button plays its own type on any root; the owner, 2026-10-21:
                                                  * "квантайз я бы на бейсик поставил всё же по-умолчанию", after Full
                                                  * since 2026-10-18) */
    cfg->tonic = 0;
    cfg->scale = CF_SC_MAJOR;
    cfg->octave = 0;
    for (i = 0; i < CF_TOP_NBTN; i++)
        cfg->top[i] = TOP[i];
    for (i = 0; i < CF_SIDE_NBTN; i++)
        cfg->side[i] = SIDE[i];
}

/* ------------------------------------------------------------------- merge --- */
static int cfc_is_typed(int t) { return t == CF_T_MAJ || t == CF_T_MIN || t == CF_T_DIM || t == CF_T_AUG || t == CF_T_POWER; }

cf_form_t cf_merge_forms(const cf_form_t *forms, int n)
{
    cf_form_t r = {CF_TYPE_DEFAULT, 0, 0, 0};
    int count[CF_NTYPES], last[CF_NTYPES], i, win = -1, sus2 = 0, sus4 = 0, src = -1;
    for (i = 0; i < CF_NTYPES; i++) {
        count[i] = 0;
        last[i] = -1;
    }
    for (i = 0; i < n; i++) {
        int t = forms[i].type;
        if (t < CF_NTYPES) {
            count[t]++;
            last[t] = i;
        }
    }
    /* the type held most wins; on a tie the one pressed last */
    for (i = 0; i < CF_NTYPES; i++)
        if (cfc_is_typed(i) && count[i] &&
            (win < 0 || count[i] > count[win] || (count[i] == count[win] && last[i] > last[win])))
            win = i;
    for (i = 0; i < n; i++) {
        int t = forms[i].type;
        if (win >= 0 && cfc_is_typed(t) && t != win)
            continue;                         /* incompatible with the winners: left out */
        if (t == CF_T_SUS2)
            sus2 = 1;
        if (t == CF_T_SUS4)
            sus4 = 1;
        r.add |= forms[i].add;
        /* inversion and bass degree: from the last typed (or sus) form pressed, else the last one */
        if (t != CF_T_NONE || src < 0 || forms[src].type == CF_T_NONE)
            src = i;
    }
    if (win >= 0) {
        r.type = (uint8_t)win;
        if (sus2)
            r.add |= CF_A_2;                  /* a sus with a third: an added 2nd / 4th (Madd4, m(add2)) */
        if (sus4)
            r.add |= CF_A_4;
    } else if (sus2 && sus4) {
        r.type = CF_T_SUS2;
        r.add |= CF_A_4;
    } else if (sus2) {
        r.type = CF_T_SUS2;
    } else if (sus4) {
        r.type = CF_T_SUS4;
    }
    if (src >= 0) {
        r.inv = forms[src].inv;
        r.bass = forms[src].bass;
    }
    return r;
}

/* ------------------------------------------------------------------- state --- */
void cf_chord_init(cf_chord_t *c, const cf_chord_cfg_t *cfg)
{
    uint8_t *p = (uint8_t *)c;
    unsigned i;
    for (i = 0; i < sizeof(*c); i++)
        p[i] = 0;
    c->cfg = *cfg;
    c->out.bass_pc = -1;
}

void cf_chord_reset(cf_chord_t *c)
{
    c->held = c->gesture = c->locked = 0;
    c->toggle = 0;
    c->nroots = 0;
    c->hold = 0;
}

static uint16_t cfc_effective(const cf_chord_t *c)
{
    if (!cfc_is_lock(c->cfg.algo))
        return c->held;
    return c->held ? c->gesture : c->locked;
}

static void cfc_sort_u8(uint8_t *a, int n)
{
    int i, j;
    for (i = 1; i < n; i++) {
        uint8_t v = a[i];
        for (j = i - 1; j >= 0 && a[j] > v; j--)
            a[j + 1] = a[j];
        a[j + 1] = v;
    }
}

/* the lit buttons are the ones in the chord: a major button and a minor one never join (the type held most wins, on a
 * tie the last pressed: cf_merge_forms), a sus or a single degree joins either; one left out goes dark */
static uint16_t cfc_used(const cf_chord_t *c, uint16_t eff, int nb)
{
    cf_form_t held[CF_TOP_NBTN], f;
    int order[CF_TOP_NBTN], nh = 0, b, i, j;
    uint16_t used = 0;
    for (b = 0; b < nb; b++)
        if ((eff >> b) & 1u)
            order[nh++] = b;
    for (i = 1; i < nh; i++) {
        int v = order[i];
        for (j = i - 1; j >= 0 && c->stamp[order[j]] > c->stamp[v]; j--)
            order[j + 1] = order[j];
        order[j + 1] = v;
    }
    for (i = 0; i < nh; i++)
        held[i] = *cf_button_form(&c->cfg, order[i]);
    f = cf_merge_forms(held, nh);
    for (i = 0; i < nh; i++)
        if (!cfc_is_typed(held[i].type) || held[i].type == f.type)
            used |= (uint16_t)(1u << order[i]);
    return used;
}

static int cfc_any_down(const cf_chord_t *c)  /* a root key physically down */
{
    int i;
    for (i = 0; i < c->nroots; i++)
        if (c->root_down[i])
            return 1;
    return 0;
}

static int cfc_resolve(cf_chord_t *c)
{
    cf_chord_out_t o = c->out, *old = &c->out;
    const cf_chord_cfg_t *g = &c->cfg;
    uint16_t eff = cfc_effective(c), use, off;
    int i, nb = cfc_is_top(g->algo) ? CF_TOP_NBTN : CF_SIDE_NBTN, changed;
    /* the lights show the buttons; the notes come from `play`, which follows them except while kept (cfc_root takes
     * them) */
    if (!(c->keep && c->nroots && !c->bare && (!c->hold || cfc_any_down(c))))
        c->play = eff;                            /* (HOLD with no key down: a type tapped changes the held chord) */
    use = c->play;
    off = cfc_is_top(g->algo) ? cfc_off_mask(g) : 0u;
    /* single notes: Off held, or no chord button at all (the owner's rule, 2026-10-15: keys alone play notes, as many
     * as are held; a chord sounds only while a chord button is down, or latched in Lock) */
    o.single = (uint8_t)((use & off) != 0 || use == 0);
    o.buttons = (eff & off) ? eff : cfc_used(c, eff, nb);
    o.sounding = c->nroots > 0;
    o.hold = c->hold;
    if (!o.sounding) {
        o.n = 0;
    } else if (o.single) {
        int k = 0, j;
        for (i = 0; i < c->nroots && k < CF_MAX_NOTES; i++) {
            int nt = cf_key_root_note(g, c->root_key[i]);
            for (j = 0; j < k && o.note[j] != nt; j++)
                ;
            if (nt >= 0 && j == k)
                o.note[k++] = (uint8_t)nt;
        }
        cfc_sort_u8(o.note, k);
        o.n = (uint8_t)k;
        o.root_note = (uint8_t)cf_key_root_note(g, c->root_key[c->nroots - 1]);
        o.root_pc = (uint8_t)(o.root_note % 12);
        o.mask = 1u;
        o.type = CF_T_NONE;
        o.bass_pc = (int8_t)o.root_pc;
        o.inv = 0;
        o.form = OFF_FORM;
    } else {
        cf_form_t held[CF_TOP_NBTN], f;
        int order[CF_TOP_NBTN], nh = 0, b, j, base, inv;
        uint32_t mask;
        /* the buttons in press order, oldest first */
        for (b = 0; b < nb; b++)
            if ((use >> b) & 1u)
                order[nh++] = b;
        for (i = 1; i < nh; i++) {
            int v = order[i];
            for (j = i - 1; j >= 0 && c->stamp[order[j]] > c->stamp[v]; j--)
                order[j + 1] = order[j];
            order[j + 1] = v;
        }
        for (i = 0; i < nh; i++)
            held[i] = *cf_button_form(g, order[i]);
        f = cf_merge_forms(held, nh);
        o.root_note = (uint8_t)cf_key_root_note(g, c->root_key[c->nroots - 1]);
        o.root_pc = (uint8_t)(o.root_note % 12);
        if (f.type == CF_TYPE_DEFAULT) {
            mask = g->quantize == CF_Q_OFF ? cf_form_mask(&(cf_form_t){CF_T_MAJ, 0, 0, 0})
                                           : cf_diatonic_mask(g->scale, g->tonic, o.root_pc, 3);
            {
                cf_form_t only_adds = {CF_T_NONE, f.add, 0, 0};
                mask |= cf_form_mask(&only_adds);
            }
        } else {
            mask = cf_form_mask(&f);
        }
        if (g->quantize == CF_Q_FULL || (g->quantize == CF_Q_BASIC && f.type == CF_TYPE_DEFAULT && f.add))
            mask = cf_mask_snap(mask, o.root_pc, g->scale, g->tonic);
        base = o.root_note;
        inv = f.inv < 0 ? 0 : f.inv;
        if (f.inv == CF_INV_AUTO) {
            int a = cf_auto_inversion(o.root_note, mask, c->prev, c->prev_n), nt;
            int8_t t[12];
            nt = cf_mask_tones(mask, t);
            if (a >= nt) {
                base -= 12;
                inv = a - nt;
            } else {
                inv = a;
            }
        }
        o.n = (uint8_t)cf_voice(base, mask, inv, o.note);
        o.mask = mask;
        o.type = (uint8_t)cf_mask_type(mask);
        o.inv = (uint8_t)inv;
        o.bass_pc = (int8_t)cf_bass_pc(o.root_pc, mask, f.bass);
        o.form = f;
    }
    changed = o.sounding != old->sounding || o.single != old->single || o.n != old->n ||
              o.bass_pc != old->bass_pc || o.buttons != old->buttons || o.hold != old->hold;
    for (i = 0; !changed && i < o.n; i++)
        changed = o.note[i] != old->note[i];
    if (changed)
        o.serial = old->serial + 1u;
    c->out = o;
    if (o.sounding && !o.single && o.n) {
        c->prev_n = o.n;
        for (i = 0; i < o.n; i++)
            c->prev[i] = o.note[i];
    }
    return changed;
}

void cf_chord_config(cf_chord_t *c, const cf_chord_cfg_t *cfg)
{
    if (cfg->algo != c->cfg.algo) {
        c->held = c->gesture = c->locked = 0;
        c->toggle = 0;
        c->nroots = 0;
        c->hold = 0;                              /* (Side has no HOLD key: nothing held over) */
    }
    c->cfg = *cfg;
    cfc_resolve(c);
}

static void cfc_button(cf_chord_t *c, int btn, int down)
{
    uint16_t bit = (uint16_t)(1u << btn);
    if (down) {
        if (cfc_is_lock(c->cfg.algo)) {
            if (!c->held) {                   /* a new press: it replaces the latch */
                c->toggle = (uint8_t)(c->locked == bit);
                c->gesture = bit;
            } else {
                c->gesture |= bit;
                c->toggle = 0;
            }
        }
        c->held |= bit;
        c->stamp[btn] = ++c->clock;
    } else {
        c->held &= (uint16_t)~bit;
        if (cfc_is_lock(c->cfg.algo) && !c->held) {
            c->locked = c->toggle ? 0 : c->gesture;
            c->gesture = 0;
            c->toggle = 0;
        }
    }
}

/* HOLD tapped: on, or off (the roots kept only by it go; the keys still down stay) */
static void cfc_hold(cf_chord_t *c)
{
    int i, k = 0;
    c->hold = (uint8_t)!c->hold;
    if (c->hold)
        return;
    for (i = 0; i < c->nroots; i++)
        if (c->root_down[i]) {
            c->root_key[k] = c->root_key[i];
            c->root_down[k++] = 1;
        }
    c->nroots = (uint8_t)k;
}

static void cfc_root(cf_chord_t *c, int key, int down)
{
    int i, k = 0;
    if (!down && c->hold) {                       /* (HOLD: the root stays, no longer down) */
        for (i = 0; i < c->nroots; i++)
            if (c->root_key[i] == key)
                c->root_down[i] = 0;
        return;
    }
    if (down && c->hold && !cfc_any_down(c))
        c->nroots = 0;                            /* (HOLD: a press with no key down replaces what it held) */
    for (i = 0; i < c->nroots; i++)
        if (c->root_key[i] != key) {
            c->root_down[k] = c->root_down[i];
            c->root_key[k++] = c->root_key[i];
        }
    c->nroots = (uint8_t)k;
    if (down) {
        if (c->nroots >= CF_MAX_ROOTS) {
            for (i = 1; i < c->nroots; i++) {
                c->root_key[i - 1] = c->root_key[i];
                c->root_down[i - 1] = c->root_down[i];
            }
            c->nroots--;
        }
        c->root_down[c->nroots] = 1;
        c->root_key[c->nroots++] = (uint8_t)key;
        c->play = cfc_effective(c);           /* (a root down: the chord takes the latch as it is now) */
        c->bare = !c->play;                   /* (no button with it: the buttons pressed now apply at once) */
    }
}

int cf_chord_key(cf_chord_t *c, int key, int down)
{
    int idx, role = cf_key_role(c->cfg.algo, key, &idx);
    if (key >= CF_KEY_EXT && key < CF_KEY_EXT + 128) {
        cfc_root(c, key, down);                   /* (a root from outside: NOTES IN Roots) */
        return cfc_resolve(c);
    }
    if (role == CF_ROLE_BUTTON && cf_is_off(&c->cfg, idx)) {
        if (down)
            cfc_hold(c);                          /* (HOLD: a tap; never a chord button, nor in the latch) */
    } else if (role == CF_ROLE_BUTTON)
        cfc_button(c, idx, down);
    else if (role == CF_ROLE_ROOT)
        cfc_root(c, key, down);
    else
        return 0;
    return cfc_resolve(c);
}
