/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 CoralFlower contributors */
/* GRAIN: a live granular layer on the master bus. Whatever the chord part, the bass part and the sequencer play is
 * recorded into a short ring (mono, half the sample rate) and cut into grains that play back, spread across the
 * stereo field, mixed with the dry sound and then fed through the effects chain (cf_fx.c's cf_fx_chain calls
 * cf_gran_run first). Freeze stops the recording: the grains then play a held moment of the music.
 * The design follows the Nice Crystal 333 browser instrument (position, size, density, pitch, random, spread, mix,
 * freeze, reverse), scaled down for the chip: CG_NG grains, a ring of CG_N samples, integers only.
 *
 * Time: the ring holds CG_N samples at FS / 2 (22.05 kHz: 0.74 s). A grain reads it with linear interpolation, its
 * Q16 step against the ring being ratio / 2 per output sample. Where a grain may start: DELAY samples behind the
 * write head, at least its own span (so it never reads what is not written yet, frozen or not) and at most what
 * leaves its length and the head's travel inside the ring. POS sets the delay across that range, RAND scatters it.
 * A grain is shortened when pitch and size would not fit the ring (CG_N 16384: a 250 ms grain up to +12 semitones,
 * 150 ms at +24).
 *
 * Cost: per grain per output sample about 30 instructions (two reads, a lerp, a parabolic window, two pans). CG_NG 8
 * grains: ~240 a sample at most, against 1000 .. 1500 for the heaviest engines; nothing is processed while MIX is 0
 * (the ring still records, so raising MIX has sound to play at once). The memory: the ring and the state are
 * .pool, ~33 KB (CG_LOG2 sets it); the pool had ~50 KB free before this (see the build report: "POOL"). The values
 * are not saved with the settings (a power cycle returns the defaults, MIX 0 = off). The device's toolchain has no
 * 64-bit division (cf_arp.c): every divide here is 32-bit; the 64-bit parts are multiplies and shifts only. */

_Static_assert(CTL == 32 && FS == 44100u, "cf_gran.c: a block is 32 samples at 44.1 kHz");

#define CG_LOG2 14                       /* the ring: 2^14 samples at FS / 2 */
#define CG_N (1u << CG_LOG2)
#define CG_MASK (CG_N - 1u)
#define CG_NG 8                          /* grains at once */
#define CG_MARGIN 64u                    /* ring samples kept between a grain's read and the head */

/* the values, 0..100 unless noted (cf_gran_apply copies them to the audio side) */
typedef struct {
    uint8_t mix;                         /* 0 = off .. 100: all grains */
    uint8_t pos;                         /* 0: the newest sound .. 100: the oldest the ring allows */
    uint8_t size;                        /* 10 .. 250 ms, exponential */
    uint8_t dens;                        /* 1 .. 120 grains a second, exponential */
    uint8_t pitch;                       /* 0..48: -24 .. +24 semitones (24 = none) */
    uint8_t rnd;                         /* scatter of position, detune by octaves, direction flips */
    uint8_t spread;                      /* stereo width of the grains */
    uint8_t dir;                         /* 0 forward, 1 reverse, 2 either */
    uint8_t freeze;                      /* 1: the ring stops recording */
} cf_gr_t;

static const cf_gr_t CG_DEFAULT = {0, 25, 45, 70, 24, 20, 60, 0, 0};

/* 2^((s - 24) / 12) in Q16, s 0..48 */
static const uint32_t CG_RATIO[49] = {
    16384, 17358, 18390, 19484, 20643, 21870, 23170,
    24548, 26008, 27554, 29193, 30929, 32768, 34716,
    36781, 38968, 41285, 43740, 46341, 49097, 52016,
    55109, 58386, 61858, 65536, 69433, 73562, 77936,
    82570, 87480, 92682, 98193, 104032, 110218, 116772,
    123715, 131072, 138866, 147123, 155872, 165140, 174960,
    185364, 196386, 208064, 220436, 233544, 247431, 262144,
};
/* a grain's length in samples (at 44.1 kHz) for SIZE 0..100: 10 ms * 25^(v/100) */
static const uint16_t CG_LEN[101] = {
    441, 455, 470, 486, 502, 518, 535, 552, 571, 589,
    608, 628, 649, 670, 692, 715, 738, 762, 787, 813,
    840, 867, 895, 925, 955, 986, 1018, 1052, 1086, 1122,
    1158, 1196, 1235, 1276, 1317, 1361, 1405, 1451, 1498, 1548,
    1598, 1650, 1704, 1760, 1818, 1877, 1939, 2002, 2068, 2135,
    2205, 2277, 2352, 2429, 2508, 2590, 2675, 2762, 2853, 2946,
    3042, 3142, 3245, 3351, 3460, 3574, 3690, 3811, 3936, 4065,
    4198, 4335, 4477, 4623, 4774, 4931, 5092, 5258, 5430, 5608,
    5791, 5981, 6177, 6379, 6587, 6803, 7025, 7255, 7492, 7738,
    7991, 8252, 8522, 8801, 9089, 9386, 9693, 10010, 10338, 10676,
    11025,
};
/* the grain interval in samples for DENS 0..100: 44100 / (120^(v/100)) */
static const uint16_t CG_INT[101] = {
    44100, 42038, 40073, 38200, 36414, 34712, 33089, 31542, 30068, 28662,
    27323, 26045, 24828, 23667, 22561, 21506, 20501, 19542, 18629, 17758,
    16928, 16137, 15382, 14663, 13978, 13324, 12701, 12108, 11542, 11002,
    10488, 9998, 9530, 9085, 8660, 8255, 7869, 7501, 7151, 6816,
    6498, 6194, 5904, 5628, 5365, 5115, 4875, 4648, 4430, 4223,
    4026, 3838, 3658, 3487, 3324, 3169, 3021, 2879, 2745, 2617,
    2494, 2378, 2266, 2161, 2060, 1963, 1871, 1784, 1701, 1621,
    1545, 1473, 1404, 1339, 1276, 1216, 1159, 1105, 1054, 1004,
    957, 913, 870, 829, 791, 754, 718, 685, 653, 622,
    593, 565, 539, 514, 490, 467, 445, 424, 404, 386,
    368,
};

static cf_gr_t cg_set;                   /* the values as the audio side uses them (cf_gran_apply copies in, IRQ off) */
static uint8_t cg_ready;

static struct {
    int16_t buf[CG_N];                   /* the ring: mono, FS / 2 */
    uint32_t w;                          /* its write position */
    int32_t acc;                         /* the pair of input samples being averaged */
    uint32_t half;                       /* 0 / 1: which of the pair */
    uint32_t rnd;                        /* xorshift state */
    int32_t cd;                          /* output samples to the next grain */
    int32_t wg;                          /* the wet gain now (Q15, eased a block at a time) */
    int32_t lp[2];                       /* the wet's one-pole low-pass (takes the edge of the half-rate image) */
    struct {
        uint32_t pos;                    /* the read position, Q16, in ring samples (wrapped by CG_MASK) */
        int32_t inc;                     /* Q16 ring samples per output sample (negative: reverse) */
        uint32_t ph, winc;               /* the window: Q24 phase and its step; one turn = the grain */
        uint32_t age, len;               /* output samples played, the grain's length */
        int32_t gl, gr;                  /* left and right gain (Q15), the level and the window folded in at start */
        uint8_t on;
    } g[CG_NG];
    int32_t wl[CTL], wr[CTL];            /* the grains' sum for a block */
} cg __attribute__((section(".pool")));

static uint32_t cg_rand(void)
{
    uint32_t x = cg.rnd ? cg.rnd : 2463534242u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    cg.rnd = x;
    return x;
}

static uint32_t cg_isqrt(uint32_t v)
{
    uint32_t r = 0, b = 1u << 30;
    while (b > v)
        b >>= 2;
    while (b) {
        if (v >= r + b) {
            v -= r + b;
            r = (r >> 1) + b;
        } else {
            r >>= 1;
        }
        b >>= 2;
    }
    return r;
}

/* the UI's values -> the audio side's (after any change, and at power-on) */
static void cf_gran_apply(const cf_gr_t *s)
{
    fm1_irq_off();
    cg_set = *s;
    cg_ready = 1;
    fm1_irq_on();
}

/* a new grain into a free slot (none free: none starts) */
static void cg_spawn(const cf_gr_t *s)
{
    uint32_t i, len, ratio, span, dmin, dmax, d, rv, lmax;
    int32_t pan, amp, ov, nrm;
    int rev;
    for (i = 0; i < CG_NG && cg.g[i].on; i++)
        ;
    if (i == CG_NG)
        return;
    rv = cg_rand();
    {
        uint32_t semi = s->pitch > 48u ? 48u : s->pitch, r = s->rnd;
        ratio = CG_RATIO[semi];
        if (r > 40u && ((rv >> 8) & 127u) < (r - 40u) * 128u / 100u) {      /* RAND: an octave up or down */
            if ((rv >> 16) & 1u)
                ratio = ratio > (1u << 18) ? ratio : ratio * 2u;
            else
                ratio = ratio < 4096u ? ratio : ratio / 2u;
        }
    }
    rev = s->dir == 1u || (s->dir == 2u && ((rv >> 20) & 1u));
    len = CG_LEN[s->size > 100u ? 100u : s->size];
    lmax = ((CG_N - 2u * CG_MARGIN) * 2u << 16) / (ratio + 65536u);   /* L (ratio + 1) / 2 fits (32 bits: 2.1e9 at most) */
    if (len > lmax)
        len = lmax;
    if (len < 64u)
        len = 64u;
    span = (uint32_t)(((uint64_t)len * ratio) >> 17) + 1u;     /* ring samples one pass covers */
    dmin = span + CG_MARGIN;
    dmax = CG_N - len / 2u - CG_MARGIN;
    if (dmax < dmin)
        dmax = dmin;
    d = dmin + ((dmax - dmin) * (s->pos > 100u ? 100u : s->pos)) / 100u;      /* (<= 16384 x 100: 32 bits) */
    if (s->rnd) {                                              /* scatter: up to +-half the range at RAND 100 */
        int32_t sc = (int32_t)(((dmax - dmin) * (uint32_t)s->rnd) / 200u);
        int32_t j = (int32_t)((rv >> 4) & 0xFFFFu) - 32768;
        int32_t dd = (int32_t)d + (int32_t)(((int64_t)sc * j) >> 15);
        d = dd < (int32_t)dmin ? dmin : dd > (int32_t)dmax ? dmax : (uint32_t)dd;
    }
    {
        uint32_t head = cg.w;
        uint32_t start = head - d;                             /* (wraps in the ring) */
        cg.g[i].pos = (rev ? start + span : start) << 16;
        cg.g[i].inc = rev ? -(int32_t)(ratio >> 1) : (int32_t)(ratio >> 1);
    }
    cg.g[i].age = 0;
    cg.g[i].len = len;
    cg.g[i].ph = 0;
    cg.g[i].winc = (1u << 24) / len;
    /* level: 1 / sqrt(overlap), overlap = grains sounding at once = len / interval */
    ov = (int32_t)((len << 8) / CG_INT[s->dens > 100u ? 100u : s->dens]);
    ov = ov < 256 ? 256 : ov;
    nrm = (int32_t)((32768u * 256u) / cg_isqrt((uint32_t)ov << 8));
    amp = nrm;
    pan = (int32_t)(rv >> 24) - 128;                           /* -128 .. 127 */
    pan = pan * (int32_t)s->spread / 100;                      /* .. -128 .. 127 at SPREAD 100 */
    cg.g[i].gl = amp * (128 - pan) * 3 / 512;                  /* a linear pan: 0.75 each side at the middle, 1.5 on */
    cg.g[i].gr = amp * (128 + pan) * 3 / 512;                  /* the one side at the edge */
    cg.g[i].on = 1;
}

/* the master bus, in place: the grains mixed in by MIX; the ring recorded from it */
static void cf_gran_run(int32_t *l, int32_t *r, uint32_t n)
{
    const cf_gr_t *s = &cg_set;
    uint32_t i, k;
    int32_t tgt, w;
    if (!cg_ready)
        return;
    if (!s->freeze) {                                          /* record: the mean of a pair of samples a ring sample */
        for (i = 0; i < n; i++) {
            cg.acc += (l[i] + r[i]) >> 1;
            if (cg.half) {
                int32_t v = cg.acc >> 1;
                cg.buf[cg.w & CG_MASK] = (int16_t)(v < -32768 ? -32768 : v > 32767 ? 32767 : v);
                cg.w++;
                cg.acc = 0;
            }
            cg.half ^= 1u;
        }
    }
    tgt = (int32_t)s->mix * 32768 / 100;
    if (!tgt && !cg.wg) {                                      /* off and faded out: no grains, no work */
        for (k = 0; k < CG_NG; k++)
            cg.g[k].on = 0;
        cg.cd = 0;
        return;
    }
    cg.wg += (tgt - cg.wg) / 4 + (tgt > cg.wg ? 1 : tgt < cg.wg ? -1 : 0);   /* (to the target within a few blocks) */
    if (cg.wg < 0)
        cg.wg = 0;
    /* new grains: one every interval (at most one a block: the interval is >= 368 samples, a block 32) */
    cg.cd -= (int32_t)n;
    if (tgt && cg.cd <= 0) {
        int32_t iv = (int32_t)CG_INT[s->dens > 100u ? 100u : s->dens];
        int32_t j = ((int32_t)(cg_rand() >> 16) & 0xFF) - 128;      /* the interval +-10 % at RAND 100 */
        cg_spawn(s);
        cg.cd += iv + (int32_t)(((int64_t)iv * j * s->rnd) >> 17);
        if (cg.cd < 1)
            cg.cd = 1;
    }
    for (i = 0; i < n; i++)
        cg.wl[i] = cg.wr[i] = 0;
    for (k = 0; k < CG_NG; k++) {
        if (!cg.g[k].on)
            continue;
        {
            uint32_t pos = cg.g[k].pos, ph = cg.g[k].ph, age = cg.g[k].age, len = cg.g[k].len;
            int32_t inc = cg.g[k].inc, gl = cg.g[k].gl, gr = cg.g[k].gr;
            uint32_t winc = cg.g[k].winc;
            for (i = 0; i < n; i++) {
                uint32_t ix = pos >> 16, fr = pos & 0xFFFFu, p = ph >> 8;
                int32_t a = cg.buf[ix & CG_MASK], b = cg.buf[(ix + 1u) & CG_MASK], y, win;
                y = a + (int32_t)(((int64_t)(b - a) * (int32_t)fr) >> 16);
                win = (int32_t)((p * (65535u - p)) >> 15);          /* a parabola, 0 .. 32768 .. 0 (Q15) */
                y = (y * win) >> 15;
                cg.wl[i] += (y * gl) >> 15;
                cg.wr[i] += (y * gr) >> 15;
                pos += (uint32_t)inc;
                ph += winc;
                if (++age >= len) {
                    cg.g[k].on = 0;
                    break;
                }
            }
            cg.g[k].pos = pos;
            cg.g[k].ph = ph;
            cg.g[k].age = age;
        }
    }
    w = cg.wg;
    {
        int32_t dg = 32768 - w;                                 /* dry (1 - mix), the grains x mix */
        int32_t lp0 = cg.lp[0], lp1 = cg.lp[1];
        for (i = 0; i < n; i++) {
            lp0 += (cg.wl[i] - lp0) * 3 / 4;                    /* a light one-pole (~ -3 dB at 7 kHz) */
            lp1 += (cg.wr[i] - lp1) * 3 / 4;
            l[i] = (int32_t)(((int64_t)l[i] * dg + (int64_t)lp0 * w) >> 15);
            r[i] = (int32_t)(((int64_t)r[i] * dg + (int64_t)lp1 * w) >> 15);
        }
        cg.lp[0] = lp0;
        cg.lp[1] = lp1;
    }
}
