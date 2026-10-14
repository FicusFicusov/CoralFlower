/* SPDX-License-Identifier: GPL-3.0-only
 * CoralFlower: the FX screen's controls (cf_fx.c runs the chain). SELECT picks an effect along the chain, ALGORITHM
 * moves it there (one place a detent), KNOB 1 turns it on (right) or off (left), KNOB 2-4 its values (the owner: On/Off
 * first; the stock FM-1 has it on KNOB 4). Every change goes to the audio side at once (cf_fx_apply) and is kept with the settings.
 * Included by cf_ui.c after its helpers. */

static const char *const CFX_NAME[CFX_N] = {"Filter", "Reverb", "Delay", "Distortion", "Chorus", "Phaser"};
static const char *const CFX_SHORT[CFX_N] = {"FLT", "REV", "DLY", "DST", "CHO", "PHS"};
static const char *const CFX_KNOB[CFX_N][3] = {
    {"TYPE", "CUTOFF", "RESO"}, {"TYPE", "DECAY", "MIX"}, {"TIME", "FDBK", "MIX"},
    {"DRIVE", "TONE", "LEVEL"}, {"RATE", "DEPTH", "MIX"}, {"RATE", "DEPTH", "MIX"}};
static const char *const CFX_FTYPE[3] = {"Low pass", "Band pass", "High pass"};
static const char *const CFX_FTYPE_S[3] = {"LPF", "BPF", "HPF"};   /* (a knob cell holds 7 characters) */
static const char *const CFX_RTYPE[3] = {"Room", "Hall", "Plate"};

static uint32_t cfx_sel(void) { return cs.fx.order[cu.fx_pos % CFX_N] % CFX_N; }
static int cfx_is_on(uint32_t e) { return (int)((cs.fx.on >> e) & 1u); }

/* "1004 Hz", "2.5 kHz", "12 kHz": 7 characters at most */
static void cfx_hz(char *b, uint32_t hz)
{
    if (hz < 1000u) {
        kint(b, (int)hz, " Hz");
    } else if (hz >= 9950u) {
        kint(b, (int)((hz + 500u) / 1000u), " kHz");
    } else {
        fmt_fix(b, (int32_t)((hz + 50u) / 100u), 1);
        str_cpy(b + str_len(b), " kHz", 5);
    }
}
/* a slow rate: "0.35 Hz", "4.2 Hz" (x100 in) */
static void cfx_rate(char *b, uint32_t hz100)
{
    if (hz100 < 100u)                            /* (fmt_fix writes ".35") */
        b[0] = '0', fmt_fix(b + 1, (int32_t)hz100, 2);
    else if (hz100 < 1000u)
        fmt_fix(b, (int32_t)hz100, 2);
    else
        fmt_fix(b, (int32_t)(hz100 / 10u), 1);
    str_cpy(b + str_len(b), " Hz", 4);
}

static void cfx_knob_info(int knob, cf_knob_t *o)
{
    uint32_t e = cfx_sel();
    const uint8_t *v = cs.fx.v[e];
    int k = knob - 1;                            /* (KNOB 1 the switch, KNOB 2-4 the three values) */
    if (knob == 0) {
        kfmt(o->name, 12, "ON/OFF");
        kfmt(o->val, 14, cfx_is_on(e) ? "On" : "Off");
        return;
    }
    kfmt(o->name, 12, CFX_KNOB[e][k]);
    o->dis = !cfx_is_on(e);
    o->bar = (int16_t)(v[k] * 255 / 100);
    if (k == 0 && (e == CFX_FILTER || e == CFX_REVERB)) {
        kfmt(o->val, 14, (e == CFX_FILTER ? CFX_FTYPE_S : CFX_RTYPE)[v[0] > 2u ? 2u : v[0]]);
        o->bar = -1;
    } else if (e == CFX_FILTER && k == 1) {
        cfx_hz(o->val, CFX_HZ[v[1] > 99u ? 99u : v[1]]);
        o->bar = (int16_t)(v[1] * 255 / 99);
    } else if (e == CFX_DELAY && k == 0) {
        kint(o->val, (int)xf_ms(v[0] * 127u / 100u), " ms");
    } else if (e == CFX_CHORUS && k == 0) {
        cfx_rate(o->val, LFO_HZ_X100[(8u + v[0]) & 127u]);
    } else if (e == CFX_PHASER && k == 0) {
        cfx_rate(o->val, (uint32_t)(((uint64_t)CFX_PRATE[v[0] > 100u ? 100u : v[0]] * (FS / 32u) * 100u) >> 32));
    } else {
        kint(o->val, v[k], "%");
    }
}

static void cfx_knob_turn(int knob, int d)
{
    uint32_t e = cfx_sel();
    uint8_t *v = cs.fx.v[e];
    int k = knob - 1;
    if (knob == 0)
        cs.fx.on = (uint8_t)(d > 0 ? cs.fx.on | (1u << e) : cs.fx.on & ~(1u << e));
    else if (k == 0 && (e == CFX_FILTER || e == CFX_REVERB))
        v[0] = (uint8_t)clampi(v[0] + (d > 0 ? 1 : -1), 0, 2);
    else
        v[k] = (uint8_t)clampi(v[k] + d, 0, e == CFX_FILTER && k == 1 ? 99 : 100);
    cf_fx_apply(&cs.fx);
}

/* ALGORITHM on FX: the selected effect one place along the chain, the cursor with it */
static void cfx_move(int d)
{
    int p = (int)(cu.fx_pos % CFX_N), q = clampi(p + (d > 0 ? 1 : -1), 0, CFX_N - 1);
    uint8_t t;
    if (q == p)
        return;
    t = cs.fx.order[p];
    cs.fx.order[p] = cs.fx.order[q];
    cs.fx.order[q] = t;
    cu.fx_pos = (uint8_t)q;
    cf_fx_apply(&cs.fx);
}
