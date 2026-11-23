/* SPDX-License-Identifier: GPL-3.0-only
 * CoralFlower: the third drum engine, sampled kits (recorded drum machines' one-shots), next to drum_voice.c and
 * drum_synth.c.
 * A kit is nine sounds in drum_synth.c's order (DSS_KICK .. DSS_CYM), IMA ADPCM (4 bit, eng_sample.c's coding) in
 * flash, each at its own rate (drum_smp_data.h, written by tools/gen_drum_samples.py from assets/drum-kits: the
 * recordings are the builder's own, not in the repository).
 * A hit plays its sound once from the start, st semitones from its pitch (the toms' GM map), linearly interpolated.
 * Integers only, 32-bit products. Use: dsm_strike at a hit, dsm_run per block (n <= CTL) into y, Q15 at its level. */
#if defined(__has_include) && __has_include("drum_smp_data.h")
#include "drum_smp_data.h"
#define DSM_HAVE 1
#else                                            /* no recordings built (assets/drum-kits/README.md): no S kits in the
                                                  * kit lists (cf_seqd.c); one silent kit keeps the code whole */
#define DSM_HAVE 0
#define DSM_NKIT 1u
#define DSM_NSND 9u
typedef struct { uint32_t off, n, rate; } dsm_zone_t;
static const uint16_t DSM_GAIN[DSM_NKIT] = {4096};
static const dsm_zone_t DSM_ZONE[DSM_NKIT * DSM_NSND] = {{0, 0, 65536}, {0, 0, 65536}, {0, 0, 65536}, {0, 0, 65536},
                                                         {0, 0, 65536}, {0, 0, 65536}, {0, 0, 65536}, {0, 0, 65536},
                                                         {0, 0, 65536}};
static const uint8_t DSM_DATA[1] = {0};
#endif

typedef struct {
    const dsm_zone_t *z;
    uint32_t pos, frac, step;                    /* samples decoded; the read point's fraction and its step (Q16) */
    int32_t pred, idx, s0, s1;                   /* the decoder's state; the samples around the read point */
    int32_t gain;                                /* Q12 */
    uint32_t ce, kc;                             /* a choke: its envelope (Q16) and decay a sample (Q15) */
    uint32_t step0, kbb;                         /* BEND (de.c): the step without it, its envelope's decay a block */
    int32_t qb, bend;                            /* .. its envelope (Q30, 0: none), its depth (half semitones) */
    int32_t qf;                                  /* a shorter DECAY (de.c): its envelope (Q30), 0: none */
    uint32_t kbf;                                /* .. its decay a block (Q16) */
    uint8_t live, choke, pad[2];
} dsm_voice_t;

static const int16_t DSM_STEP[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97,
    107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
    876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428,
    4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350,
    22385, 24623, 27086, 29794, 32767};
static const int8_t DSM_IDX[8] = {-1, -1, -1, -1, 2, 4, 6, 8};

static void dsm_strike(dsm_voice_t *v, uint32_t kit, uint32_t snd, int32_t st, uint32_t lvl)
{
    uint32_t i, ratio;
    for (i = 0; i < sizeof *v / 4u; i++)
        ((uint32_t *)v)[i] = 0;
    v->z = &DSM_ZONE[(kit % DSM_NKIT) * DSM_NSND + snd % DSM_NSND];
    st += de_v[DE_PIT][snd % DSM_NSND];                /* (the user's edits, de.c: PITCH, LEVEL below, a shorter DECAY) */
    ratio = dv_exp2((uint32_t)(clamp(st, -24, 24) + 24) * 4096u / 12u) >> 2;   /* 2^(st / 12), Q16: 0.25 .. 4 */
    v->step = (v->z->rate * (ratio >> 4)) >> 12;
    v->gain = (int32_t)(DSM_GAIN[kit % DSM_NKIT] * (lvl < 100u ? lvl : 100u) / 100u);
    v->gain = (int32_t)(((uint32_t)v->gain * de_gain(snd % DSM_NSND)) >> 12);
    if (de_v[DE_DEC][snd % DSM_NSND] < 0) {      /* (a recording cannot ring longer: shorter only, 0.6 s .. 2.4 ms) */
        v->qf = 1 << 30;
        v->kbf = dv_kb(ds_tau((uint32_t)(110 + de_v[DE_DEC][snd % DSM_NSND] * 2)));
    }
    v->step0 = v->step;
    v->bend = de_v[DE_BND][snd % DSM_NSND];      /* (BEND: the playback's pitch falls into the hit over 30 ms) */
    v->qb = v->bend ? 1 << 30 : 0;
    v->kbb = dv_kb(30000);
    v->ce = 65536u;
    v->kc = dv_k(1500);                          /* a choke: 1.5 ms */
    v->frac = 65536u;                            /* (the first sample is read at once) */
    v->live = 1;
}

static void dsm_choke(dsm_voice_t *v) { v->choke = 1; }

static inline int32_t dsm_next(dsm_voice_t *v)
{
    uint32_t pos = v->pos, b, code;
    int32_t step, vd;
    if (pos >= v->z->n)
        return 0;
    b = DSM_DATA[v->z->off + (pos >> 1)];
    code = (pos & 1u) ? (b >> 4) : (b & 15u);
    step = DSM_STEP[v->idx];
    vd = step >> 3;
    if (code & 4u)
        vd += step;
    if (code & 2u)
        vd += step >> 1;
    if (code & 1u)
        vd += step >> 2;
    v->pred = clamp(v->pred + ((code & 8u) ? -vd : vd), -32768, 32767);
    v->idx = clamp(v->idx + DSM_IDX[code & 7u], 0, 88);
    v->pos = pos + 1u;
    return v->pred;
}

static __attribute__((noinline)) void dsm_run(dsm_voice_t *v, int32_t *y, uint32_t n)
{
    uint32_t i, frac = v->frac, step = v->step, ce = v->ce;
    int32_t s0 = v->s0, s1 = v->s1, g = v->gain, fa = 32767 << 5, fd = 0;
    if (!v->live) {
        for (i = 0; i < n; i++)
            y[i] = 0;
        return;
    }
    if (v->qf)
        dv_slow(&v->qf, v->kbf, &fa, &fd);
    if (v->qb) {                                 /* BEND: this block's step, bend x envelope half semitones above
                                                  * (in 1/16 semitones, from two octaves down: dv_exp2 takes no minus) */
        uint32_t r = dv_exp2((uint32_t)(((v->bend * (v->qb >> 18)) >> 9) + 24 * 16) * 4096u / 192u) >> 2;
        step = (v->step0 >> 4) * (r >> 4) >> 8;
        v->qb = mulq16(v->qb, v->kbb);
        if (v->qb < (1 << 14))
            v->qb = 0, step = v->step0;
        v->step = step;
    }
    for (i = 0; i < n; i++) {
        int32_t s;
        while (frac >= 65536u) {
            frac -= 65536u;
            s0 = s1;
            s1 = dsm_next(v);
        }
        s = s0 + (((s1 - s0) * (int32_t)(frac >> 1)) >> 15);
        frac += step;
        if (v->kbf) {                            /* (before the gain: within 32 bits) */
            fa += fd;
            s = (s * (fa >> 5)) >> 15;
        }
        s = (s * g) >> 12;
        if (v->choke) {
            s = (s * (int32_t)(ce >> 1)) >> 15;
            ce = (ce * v->kc) >> 15;
        }
        y[i] = s;
    }
    v->frac = frac, v->s0 = s0, v->s1 = s1, v->ce = ce;
    if ((v->pos >= v->z->n && frac >= 65536u) || ce < 16u || (v->kbf && v->qf < (1 << 16)))   /* (played out, choked or
                                                  * decayed to nothing) */
        v->live = 0;
}

/* the hit's level now (Q15): what is left of its sound (a recording has no envelope to read) */
static int32_t dsm_level(const dsm_voice_t *v)
{
    uint32_t n = v->z ? v->z->n : 0;
    if (!v->live || v->pos >= n)
        return 0;
    return (int32_t)(((n - v->pos) >> 4) * 32767u / ((n >> 4) | 1u));
}
