/* SPDX-License-Identifier: GPL-3.0-only
 * CoralFlower: editing the sounds - the ENV screen's envelope and the EDIT screen's pages.
 *
 * ENV is one Attack / Decay / Sustain / Release for any engine, shown as heard (ms and s, the sustain in %), turned
 * in the engine's own steps:
 *   FM6  the carriers' envelopes (the operators that sound): Attack moves R1, Decay R2 and R3, Sustain L3, Release
 *        R4, each carrier by the same step, so a patch keeps its character (a DX7 rate is a speed: the knob shows
 *        99 - rate, a time);
 *   VA   its amplitude envelope (ENV 1 of its deep pages);
 *   else the part's own ADSR (P_ATK ..).
 * Linked (ENV's "chord + bass", the default) the bass's envelope follows the chord's: on every turn and every sound
 * change it is set to the chord's times and sustain (cf_env_follow).
 * EDIT follows the stock FM-1 firmware: the upper keys OP1..OP6, PIT and GLO pick a section, SELECT turns its pages,
 * KNOB 1-4 the values. FM6: per operator Rates, Levels, Tuning, Sens & Level, Scaling, Curves; PIT the pitch
 * envelope; GLO the algorithm, feedback, osc sync, transpose and the LFO. The VA's own deep pages fill the same
 * sections (OSC 1-4, FILTER, ENV, LFO, MOD). Included after cf_sound.c and cf_state.h. */

/* ------------------------------------------------------------- envelope --- */
static int cfe_fm6(uint32_t part) { return trk[part].eng_req == ENGI_FM6; }
#if FELUCCA_VA
static int cfe_va(uint32_t part) { return trk[part].eng_req == ENGI_VA; }
#else
static int cfe_va(uint32_t part) { (void)part; return 0; }
#endif
static const eng_deep_t *cfe_deep(uint32_t part)
{
    const eng_deep_t *d = ENGINES[eng_idx(trk[part].eng_req)]->deep;
    return d && d->pages && d->npages && d->get && d->set ? d : 0;
}

/* the DX7 bytes ENV k moves on an operator (offsets in its 21) */
static const uint8_t CFE_FM6_BYTE[4] = {FP_R1, FP_R1 + 1, FP_L1 + 2, FP_R1 + 3};
static const uint8_t CFE_FM6_INV[4] = {1, 1, 0, 1};        /* shown as a time (99 - rate) */

static int cf_env_get(uint32_t part, int k)
{
    if (cfe_fm6(part)) {
        const uint8_t *v = fm6_patch[part];
        uint32_t alg = v[FP_ALG] & 31u, op, n = 0;
        int32_t sum = 0;
        for (op = 1; op <= 6u; op++)
            if (fm6_carrier(alg, op)) {
                sum += v[(6u - op) * FP_OP + CFE_FM6_BYTE[k]];
                n++;
            }
        sum = n ? sum / (int32_t)n : 0;
        return CFE_FM6_INV[k] ? 99 - sum : sum;
    }
    if (cfe_va(part) && cfe_deep(part))
        return cfe_deep(part)->get(&trk[part], 10u, (uint32_t)k) * 99 / 127;
    {
        static const uint8_t P[4] = {P_ATK, P_DEC, P_SUS, P_REL};
        return trk[part].p[P[k]] * 99 / 127;
    }
}

static void cf_env_step(uint32_t part, int k, int d)
{
    if (cfe_fm6(part)) {
        uint8_t v[FP_SIZE + 1u];
        uint32_t alg, op;
        int step = CFE_FM6_INV[k] ? -d : d;        /* a longer time is a slower rate */
        memcpy(v, fm6_patch[part], FP_SIZE + 1u);
        alg = v[FP_ALG] & 31u;
        for (op = 1; op <= 6u; op++)
            if (fm6_carrier(alg, op)) {
                uint8_t *o = &v[(6u - op) * FP_OP];
                o[CFE_FM6_BYTE[k]] = (uint8_t)clamp(o[CFE_FM6_BYTE[k]] + step * 2, 0, 99);
                if (k == 1)                        /* Decay: both decay stages */
                    o[FP_R1 + 2] = (uint8_t)clamp(o[FP_R1 + 2] + step * 2, 0, 99);
            }
        fm6_put_patch(part, v, 0);
        return;
    }
    if (cfe_va(part) && cfe_deep(part)) {
        const eng_deep_t *dp = cfe_deep(part);
        int32_t x = dp->get(&trk[part], 10u, (uint32_t)k) + d * 3;
        dp->set(&trk[part], 10u, (uint32_t)k, clamp(x, 0, 127));
        return;
    }
    {
        static const uint8_t P[4] = {P_ATK, P_DEC, P_SUS, P_REL};
        fm1_irq_off();
        trk[part].p[P[k]] = (int16_t)clamp(trk[part].p[P[k]] + d * 3, 0, 127);
        fm1_irq_on();
    }
}

/* ---------------------------------------------------- the envelope heard --- */
/* ENV as the ear hears it: the times in 0.1 ms, the sustain in %. FM6: the loudest carrier's envelope, run by the
 * engine's own code (fm6_eg_go / fm6_eg_step) on C4 at velocity 100; VA and the rest: the time table of their
 * F_TIME values (TIME_MS_X10, the one params.c shows) */
#define CFE_CAP ((uint32_t)(60u * 44100u / FM6_N))     /* a minute: as good as endless */

static const uint8_t *cfe_fm6_main(const uint8_t *v)   /* the loudest carrier's 21 bytes */
{
    uint32_t alg = v[FP_ALG] & 31u, op;
    const uint8_t *best = &v[5u * FP_OP];
    int ol = -1;
    for (op = 1; op <= 6u; op++)
        if (fm6_carrier(alg, op) && v[(6u - op) * FP_OP + FP_OL] > ol) {
            ol = v[(6u - op) * FP_OP + FP_OL];
            best = &v[(6u - op) * FP_OP];
        }
    return best;
}

/* the blocks (FM6_N samples) a carrier's segment takes: seg 0 the attack, 1 the decay to the sustain, 3 the release
 * from the peak (L1: a key let go early, the tail at its longest; from a sustain of 0 there would be nothing to
 * time). A segment whose level does not move counts nothing (the DX7 waits there, nobody hears it) */
static uint32_t cfe_fm6_blocks(const uint8_t *op, int seg)
{
    fm6_eg_t e;
    int32_t ol = fm6_outlevel(op, 60u, 100u), rs = fm6_rscale(op, 60u);
    uint32_t n = 0, guard = 0;
    memset(&e, 0, sizeof e);
    if (seg == 3) {
        fm6_eg_go(&e, op, ol, rs, 0);                    /* (its target: the peak) */
        e.level = e.target;
        fm6_eg_go(&e, op, ol, rs, 3);
    } else {
        fm6_eg_go(&e, op, ol, rs, 0);
    }
    while (n < CFE_CAP && guard++ < 4u * CFE_CAP) {
        uint32_t ix = e.ix, counted = seg == 3 ? ix < 4u : seg == 1 ? ix >= 1u && ix < 3u : ix == 0u;
        if (seg == 3 ? ix >= 4u : seg == 1 ? ix >= 3u : ix != 0u)
            break;
        if (e.hold) {
            e.hold = 0;
            fm6_eg_go(&e, op, ol, rs, ix + 1u);
            continue;
        }
        fm6_eg_step(&e, op, ol, rs, seg != 3);
        n += counted;
    }
    return n;
}
static uint32_t cfe_blocks_ms10(uint32_t n) { return n * (FM6_N * 100u) / 441u; }

static struct { uint8_t key[FP_OP + 1u]; uint32_t t[4]; uint8_t ok; } cfe_tc[3];   /* chord, bass, layer */
static uint32_t cfe_fm6_val(uint32_t part, int k)
{
    const uint8_t *v = fm6_patch[part], *op = cfe_fm6_main(v);
    if (part > 2u)
        return 0;
    if (!cfe_tc[part].ok || memcmp(cfe_tc[part].key, op, FP_OP) || cfe_tc[part].key[FP_OP] != v[FP_ALG]) {
        memcpy(cfe_tc[part].key, op, FP_OP);
        cfe_tc[part].key[FP_OP] = v[FP_ALG];
        cfe_tc[part].t[0] = cfe_blocks_ms10(cfe_fm6_blocks(op, 0));
        cfe_tc[part].t[1] = cfe_blocks_ms10(cfe_fm6_blocks(op, 1));
        cfe_tc[part].t[2] = (uint32_t)op[FP_L1 + 2] * 100u / 99u;
        cfe_tc[part].t[3] = cfe_blocks_ms10(cfe_fm6_blocks(op, 3));
        cfe_tc[part].ok = 1;
    }
    return cfe_tc[part].t[k & 3];
}

/* ENV k of a part as heard: ATTACK, DECAY, RELEASE in 0.1 ms, SUSTAIN in % */
static uint32_t cf_env_val(uint32_t part, int k)
{
    int32_t raw;
    if (cfe_fm6(part))
        return cfe_fm6_val(part, k);
    if (cfe_va(part) && cfe_deep(part))
        raw = cfe_deep(part)->get(&trk[part], 10u, (uint32_t)k);
    else {
        static const uint8_t P[4] = {P_ATK, P_DEC, P_SUS, P_REL};
        raw = trk[part].p[P[k]];
    }
    raw = clamp(raw, 0, 127);
    return k == 2 ? (uint32_t)(raw * 100 + 63) / 127u : TIME_MS_X10[raw];
}

/* "0.4 ms", "35 ms", "1.25 s", "12.5 s"; the sustain "80%" */
static void cf_env_fmt(char *b, int k, uint32_t val)
{
    const char *unit = " ms";
    if (k == 2)
        fmt_int(b, (int32_t)val), unit = "%";
    else if (val < 10u)                                 /* (fmt_fix writes ".4") */
        b[0] = '0', fmt_fix(b + 1, (int32_t)val, 1);
    else if (val < 100u)
        fmt_fix(b, (int32_t)val, 1);
    else if (val < 10000u)
        fmt_int(b, (int32_t)((val + 5u) / 10u));
    else if (val < 100000u)
        fmt_fix(b, (int32_t)((val + 50u) / 100u), 2), unit = " s";
    else if (val < 600000u)
        fmt_fix(b, (int32_t)((val + 500u) / 1000u), 1), unit = " s";
    else {
        str_cpy(b, "1 min+", 7);
        return;
    }
    str_cpy(b + str_len(b), unit, 4);
}

/* ENV k of a part set to what is heard (val as cf_env_val gives it): the nearest the engine can do */
static void cf_env_set_val(uint32_t part, int k, uint32_t val)
{
    if (cfe_fm6(part)) {
        uint8_t v[FP_SIZE + 1u], tmp[FP_OP];
        uint32_t alg, op;
        int r = 0;
        memcpy(v, fm6_patch[part], FP_SIZE + 1u);
        if (k == 2) {
            r = (int)clamp((int32_t)((val * 99u + 50u) / 100u), 0, 99);
        } else {                                       /* a time: the rate whose time is nearest (time falls as the
                                                        * rate rises) */
            int lo = 0, hi = 99, seg = k == 3 ? 3 : k;
            const uint8_t *m = cfe_fm6_main(v);
            uint32_t t_lo, t_hi;
            memcpy(tmp, m, FP_OP);
            while (lo < hi) {
                int mid = (lo + hi) / 2;
                tmp[CFE_FM6_BYTE[k]] = (uint8_t)mid;
                if (k == 1)
                    tmp[FP_R1 + 2] = (uint8_t)mid;
                if (cfe_blocks_ms10(cfe_fm6_blocks(tmp, seg)) > val)
                    lo = mid + 1;
                else
                    hi = mid;
            }
            r = lo;
            if (r > 0) {                               /* the one below may be nearer */
                tmp[CFE_FM6_BYTE[k]] = (uint8_t)r;
                if (k == 1)
                    tmp[FP_R1 + 2] = (uint8_t)r;
                t_lo = cfe_blocks_ms10(cfe_fm6_blocks(tmp, seg));
                tmp[CFE_FM6_BYTE[k]] = (uint8_t)(r - 1);
                if (k == 1)
                    tmp[FP_R1 + 2] = (uint8_t)(r - 1);
                t_hi = cfe_blocks_ms10(cfe_fm6_blocks(tmp, seg));
                if (t_hi - val < val - t_lo)
                    r--;
            }
        }
        alg = v[FP_ALG] & 31u;
        for (op = 1; op <= 6u; op++)
            if (fm6_carrier(alg, op)) {
                uint8_t *o = &v[(6u - op) * FP_OP];
                o[CFE_FM6_BYTE[k]] = (uint8_t)r;
                if (k == 1)
                    o[FP_R1 + 2] = (uint8_t)r;
            }
        fm6_put_patch(part, v, 0);
        return;
    }
    {
        int32_t raw = 0, i;
        if (k == 2)
            raw = (int32_t)((val * 127u + 50u) / 100u);
        else                                           /* the table rises: the first entry not shorter, or the one
                                                        * before it when nearer */
            for (i = 0; i < 128; i++) {
                raw = i;
                if (TIME_MS_X10[i] >= val) {
                    if (i && val - TIME_MS_X10[i - 1] < TIME_MS_X10[i] - val)
                        raw = i - 1;
                    break;
                }
            }
        raw = clamp(raw, 0, 127);
        if (cfe_va(part) && cfe_deep(part)) {
            cfe_deep(part)->set(&trk[part], 10u, (uint32_t)k, raw);
        } else {
            static const uint8_t P[4] = {P_ATK, P_DEC, P_SUS, P_REL};
            fm1_irq_off();
            trk[part].p[P[k]] = (int16_t)raw;
            fm1_irq_on();
        }
    }
}

/* ENV linked (chord + bass): the bass's envelope is the chord's, as near as its engine can make it */
static void cf_env_follow(void)
{
    int k;
    for (k = 0; k < 4; k++)
        cf_env_set_val(CF_PART_BASS, k, cf_env_val(CF_PART_CHORD, k));
}

/* ----------------------------------------------------------------- EDIT --- */
/* a page column: what it edits and how it shows */
enum { EV_NUM, EV_DET, EV_MODE, EV_CURVE, EV_NOTE, EV_ALG, EV_ONOFF, EV_TRNSP, EV_LWAVE, EV_NONE };
typedef struct { const char *name; uint16_t byte; int16_t min, max; uint8_t view; } cfe_col_t;
typedef struct { const char *title; cfe_col_t col[4]; } cfe_page_t;
#define OPB 0x100u                                 /* byte is per operator (+ (6 - n) x FP_OP). (Above every patch
                                                    * byte: 0x80 was one, and the voice's bytes from 128 on, PIT's
                                                    * levels and GLO's, read and wrote outside the patch) */
static const cfe_page_t CFE_OP[6] = {
    {"Envelope Rate", {{"RATE 1", OPB | FP_R1, 0, 99, EV_NUM}, {"RATE 2", OPB | (FP_R1 + 1), 0, 99, EV_NUM},
                       {"RATE 3", OPB | (FP_R1 + 2), 0, 99, EV_NUM}, {"RATE 4", OPB | (FP_R1 + 3), 0, 99, EV_NUM}}},
    {"Envelope Level", {{"LEVEL 1", OPB | FP_L1, 0, 99, EV_NUM}, {"LEVEL 2", OPB | (FP_L1 + 1), 0, 99, EV_NUM},
                        {"LEVEL 3", OPB | (FP_L1 + 2), 0, 99, EV_NUM}, {"LEVEL 4", OPB | (FP_L1 + 3), 0, 99, EV_NUM}}},
    {"Tuning", {{"DETUNE", OPB | FP_DET, 0, 14, EV_DET}, {"COARSE", OPB | FP_FC, 0, 31, EV_NUM},
                {"FINE", OPB | FP_FF, 0, 99, EV_NUM}, {"MODE", OPB | FP_MODE, 0, 1, EV_MODE}}},
    {"Sens & Level", {{"AM SENS", OPB | FP_AMS, 0, 3, EV_NUM}, {"VEL SENS", OPB | FP_KVS, 0, 7, EV_NUM},
                      {"LEVEL", OPB | FP_OL, 0, 99, EV_NUM}, {"", 0, 0, 0, EV_NONE}}},
    {"Scaling", {{"L DEPTH", OPB | FP_LD, 0, 99, EV_NUM}, {"R DEPTH", OPB | FP_RD, 0, 99, EV_NUM},
                 {"BREAK", OPB | FP_BP, 0, 99, EV_NOTE}, {"", 0, 0, 0, EV_NONE}}},
    {"Curves", {{"L CURVE", OPB | FP_LC, 0, 3, EV_CURVE}, {"R CURVE", OPB | FP_RC, 0, 3, EV_CURVE},
                {"RATE SCL", OPB | FP_RS, 0, 7, EV_NUM}, {"", 0, 0, 0, EV_NONE}}},
};
static const cfe_page_t CFE_PIT[2] = {
    {"Pitch Rate", {{"RATE 1", FP_PR1, 0, 99, EV_NUM}, {"RATE 2", FP_PR1 + 1, 0, 99, EV_NUM},
                    {"RATE 3", FP_PR1 + 2, 0, 99, EV_NUM}, {"RATE 4", FP_PR1 + 3, 0, 99, EV_NUM}}},
    {"Pitch Level", {{"LEVEL 1", FP_PL1, 0, 99, EV_NUM}, {"LEVEL 2", FP_PL1 + 1, 0, 99, EV_NUM},
                     {"LEVEL 3", FP_PL1 + 2, 0, 99, EV_NUM}, {"LEVEL 4", FP_PL1 + 3, 0, 99, EV_NUM}}},
};
static const cfe_page_t CFE_GLO[3] = {
    {"Voice", {{"ALGO", FP_ALG, 0, 31, EV_ALG}, {"FEEDBACK", FP_FB, 0, 7, EV_NUM},
               {"OSC SYNC", FP_OKS, 0, 1, EV_ONOFF}, {"TRANSP", FP_TRNSP, 0, 48, EV_TRNSP}}},
    {"LFO", {{"WAVE", FP_LFW, 0, 5, EV_LWAVE}, {"SPEED", FP_LFS, 0, 99, EV_NUM}, {"PMD", FP_LPMD, 0, 99, EV_NUM},
             {"AMD", FP_LAMD, 0, 99, EV_NUM}}},
    {"LFO 2", {{"DELAY", FP_LFD, 0, 99, EV_NUM}, {"PMS", FP_LPMS, 0, 7, EV_NUM},
               {"LFO SYNC", FP_LKS, 0, 1, EV_ONOFF}, {"", 0, 0, 0, EV_NONE}}},
};

/* sections: 0..5 OP1..OP6, 6 PIT, 7 GLO (FM6); the VA's sections map onto the same keys */
#define CFE_NSEC 8
static const char *const CFE_SEC_KEY[CFE_NSEC] = {"OP1", "OP2", "OP3", "OP4", "OP5", "OP6", "PIT", "GLO"};
/* the VA: per section, its first deep page and how many */
static const uint8_t CFE_VA_SEC[CFE_NSEC][2] = {{0, 2}, {2, 2}, {4, 2}, {6, 2}, {8, 2}, {10, 8}, {18, 6}, {24, 8}};
static const char *const CFE_VA_KEY[CFE_NSEC] = {"OS1", "OS2", "OS3", "OS4", "FLT", "ENV", "LFO", "MOD"};

/* the LAYER's sound has one page more, its last (the last section's, after the engine's own): Glide, the sound's
 * voice. VOICE poly / mono / legato / unison, GLIDE its own glide (portamento: voice.c, TIME mode), PRIORITY the note
 * mono keeps; saved with the sound (the owner, 2026-10-19: a glide of the LAYER's own). The chords and the bass have
 * none: the keys' player plays them polyphonic (cf_sound.c cf_part_voice) and glides them with HOME's GLIDE */
static uint32_t cfe_npages(uint32_t part, uint32_t sec)
{
    uint32_t g = part == CF_PART_LAYER && sec == CFE_NSEC - 1u;
    if (cfe_fm6(part))
        return (sec < 6u ? 6u : sec == 6u ? 2u : 3u) + g;   /* (GLO: Voice, LFO, LFO 2, the LAYER's Glide) */
    if (cfe_va(part))
        return CFE_VA_SEC[sec % CFE_NSEC][1] + g;          /* (MOD's pages, the LAYER's Glide) */
    return 1u;
}
static int cfe_glide_pg(uint32_t part, uint32_t sec, uint32_t pg)
{
    return part == CF_PART_LAYER && (cfe_fm6(part) || cfe_va(part)) && sec == CFE_NSEC - 1u &&
           pg + 1u == cfe_npages(part, sec);
}
static uint32_t cfe_glide_ms(int g)              /* voice.c glide_set's TIME: 1 + g^2 / 8 control blocks */
{
    return (1u + (uint32_t)(g * g) / 8u) * CTL * 1000u / FS;
}
static int cfe_glide_info(uint32_t part, uint32_t col, char *name, char *val, int16_t *bar)
{
    static const char *const VC[4] = {"Poly", "Mono", "Legato", "Unison"};
    static const char *const PR[3] = {"Last", "Low", "High"};
    const int16_t *p = trk[part].p;
    if (col == 0) {
        str_cpy(name, "VOICE", 12);
        str_cpy(val, VC[clamp(p[P_VOICE], 0, 3)], 12);
    } else if (col == 1) {
        str_cpy(name, "GLIDE", 12);
        if (p[P_GLIDE] > 0)
            kint(val, (int)cfe_glide_ms(p[P_GLIDE]), "ms");
        else
            str_cpy(val, "Off", 12);
        *bar = (int16_t)(clamp(p[P_GLIDE], 0, 127) * 255 / 127);
    } else if (col == 2) {
        str_cpy(name, "PRIORITY", 12);
        str_cpy(val, PR[clamp(p[P_PRIO], 0, 2)], 12);
    } else {
        return 0;
    }
    return 1;
}
static void cfe_glide_turn(uint32_t part, uint32_t col, int d)
{
    int16_t *p = trk[part].p;
    fm1_irq_off();
    if (col == 0) {
        int16_t v = (int16_t)clamp(p[P_VOICE] + d, V_POLY, V_UNISON);
        if (v != p[P_VOICE])
            panic_req |= (uint8_t)(1u << part);  /* (another voice: what sounds goes, none left held) */
        p[P_VOICE] = v;
    } else if (col == 1) {                       /* (finer at the short end) */
        int g = p[P_GLIDE], st = g < 16 ? 1 : g < 48 ? 2 : 4;
        p[P_GLIDE] = (int16_t)clamp(g + d * st, 0, 127);
        p[P_GLMODE] = 1;                         /* (TIME: the same time for any interval, as the value says) */
    } else if (col == 2) {
        p[P_PRIO] = (int16_t)clamp(p[P_PRIO] + d, 0, 2);
    }
    fm1_irq_on();
}
static const char *cfe_key_name(uint32_t part, uint32_t sec)
{
    return cfe_va(part) ? CFE_VA_KEY[sec % CFE_NSEC] : CFE_SEC_KEY[sec % CFE_NSEC];
}
static const cfe_page_t *cfe_fm6_page(uint32_t sec, uint32_t pg)
{
    return sec < 6u ? &CFE_OP[pg % 6u] : sec == 6u ? &CFE_PIT[pg % 2u] : &CFE_GLO[pg % 3u];
}
static uint32_t cfe_byte(uint32_t sec, const cfe_col_t *c)
{
    return (c->byte & OPB) ? (5u - sec) * FP_OP + (c->byte & 0xFFu) : c->byte;
}

/* a detent of a deep column: about 5 % of the range, enums one by one (ChoralRoot's cr_pages.c cp_dstep) */
static int32_t cfe_dstep(const param_desc_t *d, int32_t v0, int32_t s)
{
    int32_t r = d->max - d->min, step = d->fmt == F_ENUM ? 1 : (r + 10) / 20, v;
    if (step < 1)
        step = 1;
    v = clamp(v0 + s * step, d->min, d->max);
    if (d->fmt == F_ENUM && v != v0)
        v = enum_step(d, v0, v);
    return v;
}

/* a column's label, value text and position (0..255); returns 0 for an empty column */
static int cfe_info(uint32_t part, uint32_t sec, uint32_t pg, uint32_t col, char *name, char *val, int16_t *bar)
{
    *bar = -1;
    if (cfe_glide_pg(part, sec, pg))
        return cfe_glide_info(part, col, name, val, bar);
    if (cfe_fm6(part)) {
        const cfe_col_t *c = &cfe_fm6_page(sec, pg)->col[col];
        int32_t v;
        static const char *const CURVE[4] = {"-LIN", "-EXP", "+EXP", "+LIN"};
        static const char *const LW[6] = {"Tri", "Saw Dn", "Saw Up", "Square", "Sine", "S&H"};
        static const char *const NN[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
        if (c->view == EV_NONE)
            return 0;
        v = fm6_patch[part][cfe_byte(sec, c)];
        str_cpy(name, c->name, 12);
        switch (c->view) {
        case EV_DET: kint(val, v - 7, 0); if (v > 7) { char t[8]; kint(t, v - 7, 0); val[0] = '+'; str_cpy(val + 1, t, 12); } break;
        case EV_MODE: str_cpy(val, v ? "Fixed" : "Ratio", 12); break;
        case EV_CURVE: str_cpy(val, CURVE[v & 3], 12); break;
        case EV_NOTE: {                            /* break point 0 = A-1 */
            int n = v + 21;
            str_cpy(val, NN[n % 12], 12);
            kint(val + str_len(val), n / 12 - 1, 0);
            break;
        }
        case EV_ALG: kint(val, v + 1, 0); break;
        case EV_ONOFF: str_cpy(val, v ? "On" : "Off", 12); break;
        case EV_TRNSP: kint(val, v - 24, 0); if (v > 24) { char t[8]; kint(t, v - 24, 0); val[0] = '+'; str_cpy(val + 1, t, 12); } break;
        case EV_LWAVE: str_cpy(val, LW[v % 6], 12); break;
        default: kint(val, v, 0); break;
        }
        if (c->max > c->min)
            *bar = (int16_t)((v - c->min) * 255 / (c->max - c->min));
        return 1;
    }
    if (cfe_deep(part)) {
        const eng_deep_t *d = cfe_deep(part);
        uint32_t page = CFE_VA_SEC[sec % CFE_NSEC][0] + pg;
        const param_desc_t *pd;
        const char *unit;
        char t[12];
        int32_t v;
        uint32_t i;
        if (page >= d->npages)
            return 0;
        pd = d->desc ? d->desc(&trk[part], page, col) : 0;
        if (!pd)
            pd = &d->pages[page].col[col];
        if (!pd->label || !pd->label[0])
            return 0;
        for (i = 0; pd->label[i] && i < 11u; i++)
            name[i] = pd->label[i] >= 'a' && pd->label[i] <= 'z' ? (char)(pd->label[i] - 32) : pd->label[i];
        name[i] = 0;
        v = d->get(&trk[part], page, col);
        param_format(pd, v, t, &unit);
        str_cpy(val, t, 14);
        if (str_len(val) + str_len(unit) < 13u)
            str_cpy(val + str_len(val), unit, 14 - str_len(val));
        if (pd->max > pd->min && pd->fmt != F_ENUM)
            *bar = (int16_t)((v - pd->min) * 255 / (pd->max - pd->min));
        return 1;
    }
    return 0;
}

static void cfe_turn(uint32_t part, uint32_t sec, uint32_t pg, uint32_t col, int d)
{
    if (cfe_glide_pg(part, sec, pg)) {
        cfe_glide_turn(part, col, d);
        return;
    }
    if (cfe_fm6(part)) {
        const cfe_col_t *c = &cfe_fm6_page(sec, pg)->col[col];
        uint8_t v[FP_SIZE + 1u];
        uint32_t b;
        int step = (c->max - c->min) >= 60 ? 2 : 1;
        if (c->view == EV_NONE)
            return;
        b = cfe_byte(sec, c);
        memcpy(v, fm6_patch[part], FP_SIZE + 1u);
        v[b] = (uint8_t)clamp(v[b] + d * step, c->min, c->max);
        if (b == FP_ALG || b == FP_FB) {           /* the patch's own algorithm and feedback (no macro over them) */
            fm1_irq_off();
            trk[part].p[P_E0 + (b == FP_ALG ? 0 : 1)] = 0;
            fm1_irq_on();
        }
        fm6_put_patch(part, v, 0);
        return;
    }
    if (cfe_deep(part)) {
        const eng_deep_t *dp = cfe_deep(part);
        uint32_t page = CFE_VA_SEC[sec % CFE_NSEC][0] + pg;
        const param_desc_t *pd;
        if (page >= dp->npages)
            return;
        pd = dp->desc ? dp->desc(&trk[part], page, col) : 0;
        if (!pd)
            pd = &dp->pages[page].col[col];
        if (!pd->label || !pd->label[0])
            return;
        dp->set(&trk[part], page, col, cfe_dstep(pd, dp->get(&trk[part], page, col), d));
    }
}

static const char *cfe_page_title(uint32_t part, uint32_t sec, uint32_t pg)
{
    if (cfe_glide_pg(part, sec, pg))
        return "Glide";
    if (cfe_fm6(part))
        return cfe_fm6_page(sec, pg)->title;
    if (cfe_deep(part)) {
        uint32_t page = CFE_VA_SEC[sec % CFE_NSEC][0] + pg;
        return page < cfe_deep(part)->npages ? cfe_deep(part)->pages[page].title : "";
    }
    return "";
}
