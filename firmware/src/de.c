/* SPDX-License-Identifier: GPL-3.0-only
 * CoralFlower: the drum edits, each drum's own settings on top of whatever kit plays it (the rhythm machine's ENV
 * pages, cf_dmui.c, which also keeps them in flash). One set for the nine sounds (DSS_KICK .. DSS_CYM), read by all
 * three drum engines: drum_voice.c's kits (eng_drum.c drum_render), the X kits (ds_strike) and the sampled kits
 * (dsm_strike).
 *   LEVEL  dB                         PITCH  semitones
 *   DECAY  a tenth of an octave of time a step (a recording: shorter only)
 *   BEND   the pitch's fall into the hit, half a semitone a step (X kits: their own bend; sampled kits: the
 *          playback's pitch falls over 30 ms; drum_voice.c's kits: none)
 *   TONE   below 0 darker (a low-pass, 8.5 kHz .. 300 Hz), above brighter (a high-pass, 50 Hz .. 3.7 kHz)
 *   DRIVE  x1 .. x8 into the soft clip, the level kept        CRUSH  fewer samples and bits (lo-fi)
 *   ATTACK the hit fades in over as many ms
 * TONE, DRIVE, CRUSH and ATTACK are a chain after the drum's voice, the same for every engine (de_post).
 * 0 is the kit's own everywhere: all zero changes nothing, to the bit. Included by drum_synth.c after ds_snd_t. */
enum { DE_LVL, DE_PIT, DE_DEC, DE_BND, DE_TON, DE_DRV, DE_CRU, DE_ATT, DE_NP };
static const int8_t DE_LO[DE_NP] = {-24, -12, -40, -20, -40, 0, 0, 0};
static const int8_t DE_HI[DE_NP] = {12, 12, 40, 20, 40, 40, 40, 40};
static int8_t de_v[DE_NP][DSS_N];
static uint8_t de_pad;                           /* the drum the pages edit */

typedef struct {                                 /* a lane's chain: its state from one hit to the next */
    int32_t lp, hv, coef;                        /* TONE's one-pole and its coefficient (Q15); CRUSH's held sample */
    uint32_t att;                                /* ATTACK: the fade in, Q16 */
    uint8_t hc;                                  /* CRUSH: the count to the next sample taken */
    int8_t ton;                                  /* the TONE coef was set for */
    uint8_t pad[2];
} de_fx_t;

static void de_reset(void)
{
    uint32_t p, i;
    for (p = 0; p < DE_NP; p++)
        for (i = 0; i < DSS_N; i++)
            de_v[p][i] = 0;
}
static int de_any(void)
{
    uint32_t p, i;
    for (p = 0; p < DE_NP; p++)
        for (i = 0; i < DSS_N; i++)
            if (de_v[p][i])
                return 1;
    return 0;
}
static void de_clamp(void)                       /* (values read from flash: into their ranges) */
{
    uint32_t p, i;
    for (p = 0; p < DE_NP; p++)
        for (i = 0; i < DSS_N; i++)
            de_v[p][i] = (int8_t)clamp(de_v[p][i], DE_LO[p], DE_HI[p]);
}
/* sound sn's LEVEL as a gain, Q12 (4096: the kit's own) */
static uint32_t de_gain(uint32_t sn)
{
    return dv_exp2((uint32_t)(clamp(de_v[DE_LVL][sn % DSS_N], -24, 12) + 24) * 4096u / 6u) >> 8;
}
/* an X kit's sound (drum_synth.c), morphed, before its hit */
static void de_apply(ds_snd_t *s, uint32_t sn)
{
    int32_t p = de_v[DE_PIT][sn % DSS_N], d = de_v[DE_DEC][sn % DSS_N];
    s->level = (uint8_t)clamp(s->level + de_v[DE_LVL][sn % DSS_N] * 2, 0, 127);   /* (LEVEL: 0.5 dB a step) */
    if (p) {
        if (s->olev)
            s->pitch = (int16_t)clamp(s->pitch + p * 16, 0, 2047);
        if (s->mpitch)
            s->mpitch = (int16_t)clamp(s->mpitch + p * 16, 0, 2047);
        if (s->src)                                                /* (the noise: its filter moves with it) */
            s->cut = (uint8_t)clamp(s->cut + p, 0, 127);
    }
    if (d) {
        s->odec = (uint8_t)clamp(s->odec + d, 0, 127);
        s->ndec = (uint8_t)clamp(s->ndec + d, 0, 127);
    }
    s->bend = (uint8_t)clamp(s->bend + de_v[DE_BND][sn % DSS_N] * 2, 0, 127);   /* (BEND: p16 / 4 a step) */
}

static void de_fx_strike(de_fx_t *f)
{
    f->lp = f->hv = 0;
    f->att = 0;
    f->hc = 1;
    f->ton = 127;                                /* (no TONE is 127: the coefficient is set at the first block) */
}
/* the chain on a block of sound sn's voice (Q15, n <= CTL) */
static void de_post(de_fx_t *f, uint32_t sn, int32_t *y, uint32_t n)
{
    int32_t t = de_v[DE_TON][sn % DSS_N], dr = de_v[DE_DRV][sn % DSS_N], cr = de_v[DE_CRU][sn % DSS_N];
    int32_t at = de_v[DE_ATT][sn % DSS_N];
    uint32_t i;
    if (!(t | dr | cr | at))
        return;
    if (t) {
        int32_t lp = f->lp, c;
        if (f->ton != t) {
            f->coef = dv_pole(t < 0 ? 1900 + t * 22 : 500 + t * 30);
            f->ton = (int8_t)t;
        }
        c = f->coef;
        for (i = 0; i < n; i++) {
            lp += ((y[i] - lp) * c) >> 15;
            y[i] = t < 0 ? lp : y[i] - lp;
        }
        f->lp = lp;
    }
    if (dr) {
        int32_t g = 4096 + dr * 7 * 4096 / 40, mk = (int32_t)((19661u << 15) / (uint32_t)softclip((19661 * g) >> 12));
        for (i = 0; i < n; i++)
            y[i] = (softclip((clamp(y[i], -65536, 65536) * (g >> 2)) >> 10) * mk) >> 15;
    }
    if (cr) {
        int32_t hv = f->hv, mask = -(int32_t)(1u << (cr / 4));
        uint32_t hc = f->hc, hn = 1u + (uint32_t)cr / 5u;
        for (i = 0; i < n; i++) {
            if (!--hc) {
                hv = (y[i] + ((-mask) >> 1)) & mask;
                hc = hn;
            }
            y[i] = hv;
        }
        f->hv = hv, f->hc = (uint8_t)hc;
    }
    if (at && f->att < 65536u) {
        uint32_t a = f->att, inc = 65536u / ((uint32_t)at * (FS / 1000u));
        for (i = 0; i < n && a < 65536u; i++) {
            y[i] = (clamp(y[i], -65536, 65536) * (int32_t)(a >> 2)) >> 14;
            a += inc;
        }
        f->att = a;
    }
}
