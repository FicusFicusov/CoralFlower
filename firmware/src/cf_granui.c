/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 CoralFlower contributors */
/* CoralFlower: the GRAIN controls, three pages at the end of GLO (cf_gran.c runs the layer). Page 5 Grain: MIX (0 =
 * off), POS, SIZE, DENS. Page 6 Pitch: PITCH, RAND, SPREAD, DIR. Page 7 Hold: FREEZE. Every change goes to the audio
 * side at once (cf_gran_apply). Not saved with the settings: a power cycle returns the defaults (MIX 0, off).
 * Included by cf_ui.c after its helpers. */

#define CG_PAGE0 5u                              /* GLO's first GRAIN page (Display, MIDI, Channels, Play, Reset before it) */
#define CG_NPAGE 3u

static cf_gr_t cgr = {0, 25, 45, 70, 24, 20, 60, 0, 0};   /* (= CG_DEFAULT) */

static const char *const CG_PAGE_NAME[CG_NPAGE] = {"Grain", "Pitch", "Hold"};
static const char *const CG_DIR_NAME[3] = {"Fwd", "Rev", "Either"};

static void cgr_pct(cf_knob_t *o, const char *name, uint32_t v)
{
    kfmt(o->name, 12, name);
    kint(o->val, (int)v, "%");
    o->bar = (int16_t)(v * 255u / 100u);
}

static void cg_knob_info(uint32_t page, int k, cf_knob_t *o)
{
    o->dis = k > 0 && page < 2u && !cgr.mix;     /* (dimmed while MIX is 0: the layer is off) */
    switch (page) {
    case 0:
        if (k == 0) {
            cgr_pct(o, "MIX", cgr.mix);
            if (!cgr.mix)
                kfmt(o->val, 14, "Off");
        } else if (k == 1) {
            cgr_pct(o, "POSITION", cgr.pos);
        } else if (k == 2) {
            kfmt(o->name, 12, "SIZE");
            kint(o->val, (int)((CG_LEN[cgr.size > 100u ? 100u : cgr.size] * 1000u + 22050u) / 44100u), " ms");
            o->bar = (int16_t)(cgr.size * 255u / 100u);
        } else {
            uint32_t iv = CG_INT[cgr.dens > 100u ? 100u : cgr.dens];
            kfmt(o->name, 12, "DENSITY");
            kint(o->val, (int)((44100u + iv / 2u) / iv), " /s");
            o->bar = (int16_t)(cgr.dens * 255u / 100u);
        }
        break;
    case 1:
        if (k == 0) {
            int st = (int)(cgr.pitch > 48u ? 48u : cgr.pitch) - 24;
            kfmt(o->name, 12, "PITCH");
            o->val[0] = 0;
            if (st > 0)
                str_cpy(o->val, "+", 2);
            kint(o->val + str_len(o->val), st, " st");
            o->bar = (int16_t)(cgr.pitch * 255u / 48u);
        } else if (k == 1) {
            cgr_pct(o, "RANDOM", cgr.rnd);
        } else if (k == 2) {
            cgr_pct(o, "SPREAD", cgr.spread);
        } else {
            kfmt(o->name, 12, "DIR");
            kfmt(o->val, 14, CG_DIR_NAME[cgr.dir > 2u ? 2u : cgr.dir]);
            o->bar = -1;
        }
        break;
    default:
        if (k == 0) {
            kfmt(o->name, 12, "FREEZE");
            kfmt(o->val, 14, cgr.freeze ? "Frozen" : "Live");
            o->bar = -1;
        }
        break;
    }
}

static void cg_knob_turn(uint32_t page, int k, int d)
{
    if (page == 0u) {
        if (k == 0)
            cgr.mix = (uint8_t)clampi(cgr.mix + d, 0, 100);
        else if (k == 1)
            cgr.pos = (uint8_t)clampi(cgr.pos + d, 0, 100);
        else if (k == 2)
            cgr.size = (uint8_t)clampi(cgr.size + d, 0, 100);
        else
            cgr.dens = (uint8_t)clampi(cgr.dens + d, 0, 100);
    } else if (page == 1u) {
        if (k == 0)
            cgr.pitch = (uint8_t)clampi(cgr.pitch + d, 0, 48);
        else if (k == 1)
            cgr.rnd = (uint8_t)clampi(cgr.rnd + d, 0, 100);
        else if (k == 2)
            cgr.spread = (uint8_t)clampi(cgr.spread + d, 0, 100);
        else
            cgr.dir = (uint8_t)clampi(cgr.dir + (d > 0 ? 1 : -1), 0, 2);
    } else if (k == 0) {
        cgr.freeze = (uint8_t)(d > 0);
    }
    cf_gran_apply(&cgr);
}
