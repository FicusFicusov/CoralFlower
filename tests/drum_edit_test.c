/* SPDX-License-Identifier: GPL-3.0-only
 * CoralFlower: the three drum engines (drum_voice.c, drum_synth.c, drum_smp.c) and the drum edits (de.c) on the host.
 *   cc -std=gnu11 -O1 -w -Ibuild/gen -Ifirmware/src -o build/host/drum_edit_test tests/drum_edit_test.c -lm
 *   build/host/drum_edit_test            the checks (exit 1 on a failure)
 *   build/host/drum_edit_test dump FILE  every sound of every kit with no edits, as raw 16-bit: two builds' files
 *                                        must be equal when the drums themselves did not change
 * -DNO_DE: a tree without de.c (the dump alone). */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#define __attribute__(x)
#include "felucca_tables.h"
typedef struct { int32_t amp0, amp1; } vmod_t;
#include "dsp.c"
#include "drum_voice.c"
#include "drum_synth.c"
#include "drum_smp.c"
#ifndef DSM_HAVE
#define DSM_HAVE 1                               /* (a tree before the recordings were optional) */
#endif

#define LEN (FS * 3 / 2)
static int16_t buf[LEN];
static int fails;
enum { E_STD, E_X, E_S };
static const char *EN[3] = {"Std", "X", "S"};
static const uint8_t SND_ROLE[DSS_N] = {DVT_PUNCH, DVT_SNARE, DVT_CLAP, DVT_HATC, DVT_HATO, DVT_TOM, DVT_RIM, DVT_BELL,
                                        DVT_CYM};

static int16_t sat(int32_t x) { return x > 32767 ? 32767 : x < -32768 ? -32768 : (int16_t)x; }
/* one hit of sound sn on engine e (its kit k), as eng_drum.c plays it, into buf */
#ifndef NO_DE
static de_fx_t fx;
#define POST(sn) de_post(&fx, sn, y, CTL)
#else
#define POST(sn)
#endif
static void hit(int e, uint32_t k, uint32_t sn)
{
    int32_t y[CTL], m[CTL];
    uint32_t i, j;
#ifndef NO_DE
    de_fx_strike(&fx);
#endif
    if (e == E_X) {
        ds_voice_t v; memset(&v, 0, sizeof v);
        ds_rng = 0x2F6B1D3;                              /* (VARY's dice: the same hit every time) */
        ds_strike(&v, &DS_KITS[k], sn, 0, 100);
        for (i = 0; i < LEN; i += CTL) {
            ds_run(&v, y, CTL); POST(sn);
            for (j = 0; j < CTL && i + j < LEN; j++) buf[i + j] = sat(y[j]);
        }
    } else if (e == E_S) {
        dsm_voice_t v;
        dsm_strike(&v, k, sn, 0, 100);
        for (i = 0; i < LEN; i += CTL) {
            dsm_run(&v, y, CTL); POST(sn);
            for (j = 0; j < CTL && i + j < LEN; j++) buf[i + j] = sat(y[j]);
        }
    } else {
        dv_param_t p; dv_coef_t c; dv_voice_t v; dv_metal_t mt; uint32_t r = SND_ROLE[sn];
        memset(&mt, 0, sizeof mt);
        dv_default(&p, r);
#ifndef NO_DE
        p.tune = (int16_t)(p.tune + de_v[DE_PIT][sn] * 16);    /* (eng_drum.c drum_render's lines) */
        p.decay = (uint8_t)clamp(p.decay + de_v[DE_DEC][sn] * 3, 0, 127);
        p.level = (uint8_t)clamp((int32_t)((p.level * de_gain(sn)) >> 12), 0, 127);
#endif
        dv_setup(&c, &p); dv_init(&v, r); dv_trigger(&v);
        if (dv_uses_metal(r)) dv_metal_tune(&mt, &c);
        for (i = 0; i < LEN; i += CTL) {
            if (dv_uses_metal(r)) dv_metal_run(&mt, m, CTL);
            dv_run(&c, &v, dv_uses_metal(r) ? m : 0, y, CTL); POST(sn);
            for (j = 0; j < CTL && i + j < LEN; j++) buf[i + j] = sat(y[j]);
        }
    }
}
static double peakto(int b)                      /* the loudest sample before b */
{
    double p = 0; int i;
    for (i = 0; i < b; i++) if (fabs(buf[i]) > p) p = fabs(buf[i]);
    return p;
}
static double peak(void) { return peakto(LEN); }
static double energy(int a, int b)
{
    double s = 0; int i;
    for (i = a; i < b && i < LEN; i++) s += (double)buf[i] * buf[i];
    return s;
}
static double crossings(int a, int b)
{
    int n = 0, i;
    for (i = a + 1; i < b; i++) n += (buf[i - 1] < 0) != (buf[i] < 0);
    return n;
}
static double high(void)                         /* the highs: the energy of the sample to sample steps */
{
    double s = 0; int i;
    for (i = 1; i < LEN; i++) { double d = buf[i] - buf[i - 1]; s += d * d; }
    return s;
}
static double held(void)                         /* samples equal to the one before, in the first 50 ms */
{
    int n = 0, i;
    for (i = 1; i < FS / 20; i++) n += buf[i] == buf[i - 1];
    return n;
}
static int length(void) { int i; for (i = LEN - 1; i > 0 && abs(buf[i]) < 8; i--); return i; }
static void check(int ok, const char *what, int e, uint32_t sn, double got)
{
    if (!ok) fails++;
    printf("  %s  %-3s sound %u: %s (%.3f)\n", ok ? "ok  " : "FAIL", EN[e], sn, what, got);
}

int main(int argc, char **argv)
{
    static const uint32_t NK[3] = {1, DS_NKIT, DSM_NKIT};
    int e; uint32_t k, sn;
    if (argc > 2 && !strcmp(argv[1], "dump")) {
        FILE *f = fopen(argv[2], "wb");
        for (e = 0; e < 3; e++)
            for (k = 0; k < NK[e]; k++)
                for (sn = 0; sn < DSS_N; sn++) { hit(e, k, sn); fwrite(buf, 2, LEN, f); }
        fclose(f);
        return 0;
    }
#ifndef NO_DE
    for (e = 0; e < 3; e++) {
        static int16_t ref[LEN];
        if (e == E_S && !DSM_HAVE) {                     /* (built without recordings: no S kit to check) */
            printf("  skip S kits: no firmware/src/drum_smp_data.h\n");
            continue;
        }
        double p0, e0, l0, t0, z0, r;
        k = e == E_X ? 1 : 0;                            /* (X Deep; S 808; Std) */
        /* every sound: no edit is no change; -6 dB halves it */
        for (sn = 0; sn < DSS_N; sn++) {
            de_reset(); hit(e, k, sn); memcpy(ref, buf, sizeof ref); p0 = peak();
            check(p0 > 1000, "sounds", e, sn, p0);
            de_v[DE_PIT][sn] = 3; de_reset(); hit(e, k, sn);
            check(!memcmp(ref, buf, sizeof ref), "edits cleared: the same hit", e, sn, 0);
            de_v[DE_LVL][sn] = -6; hit(e, k, sn);
            check(peak() / p0 > 0.42 && peak() / p0 < 0.62, "LEVEL -6 dB: about half", e, sn, peak() / p0);
            de_reset();
            { int q; for (q = 0; q < DE_NP; q++) de_v[q][(sn + 1) % DSS_N] = q & 1 || !DE_LO[q] ? DE_HI[q] : DE_LO[q]; }
            hit(e, k, sn);
            check(!memcmp(ref, buf, sizeof ref), "another drum's edits: no change", e, sn, 0);
        }
        /* the kick: PITCH and DECAY */
        de_reset(); hit(e, k, DSS_KICK);
        l0 = length(); t0 = energy(FS * 15 / 100, LEN); e0 = energy(0, LEN); z0 = crossings(FS / 50, FS / 5);
        de_v[DE_PIT][DSS_KICK] = 12; hit(e, k, DSS_KICK);
        r = e == E_S ? length() / l0 : crossings(FS / 50, FS / 5) / z0;
        if (e == E_S) check(r > 0.45 && r < 0.56, "PITCH +12: half as long", e, 0, r);
        else check(r > 1.5, "PITCH +12: more cycles", e, 0, r);
        de_reset(); de_v[DE_PIT][DSS_KICK] = -12; hit(e, k, DSS_KICK);
        r = e == E_S ? length() / l0 : crossings(FS / 50, FS / 5) / z0;
        if (e == E_S) check(r > 1.3, "PITCH -12: longer", e, 0, r);
        else check(r < 0.75, "PITCH -12: fewer cycles", e, 0, r);
        de_reset(); de_v[DE_DEC][DSS_KICK] = -25; hit(e, k, DSS_KICK);
        r = energy(FS * 15 / 100, LEN) / t0;
        check(r < 0.25, "DECAY -25: the tail after 150 ms mostly gone", e, 0, r);
        check(peak() > 1000, "DECAY -25: still sounds", e, 0, peak());
        de_reset(); de_v[DE_DEC][DSS_KICK] = 25; hit(e, k, DSS_KICK);
        r = e == E_S ? energy(0, LEN) / e0 : energy(FS * 15 / 100, LEN) / t0;
        if (e == E_S) check(fabs(r - 1.0) < 0.001, "DECAY +25: a recording stays as it is", e, 0, r);
        else check(r > 1.5, "DECAY +25: a longer tail", e, 0, r);
        /* the chain and BEND, on the snare and the kick */
        de_reset(); hit(e, k, DSS_SNARE);
        { double hi0 = high(), lo0, r0 = sqrt(energy(0, LEN) / LEN), a0 = peakto(FS / 200), held0 = held();
          de_v[DE_TON][DSS_SNARE] = -30; hit(e, k, DSS_SNARE);
          check(high() / hi0 < 0.2, "TONE -30: the highs mostly gone", e, DSS_SNARE, high() / hi0);
          de_reset(); de_v[DE_DRV][DSS_SNARE] = 30; hit(e, k, DSS_SNARE);
          r = sqrt(energy(0, LEN) / LEN) / r0;
          check(r > 1.15 && peak() < 32768, "DRIVE 30: denser, not over full scale", e, DSS_SNARE, r);
          de_reset(); de_v[DE_CRU][DSS_SNARE] = 30; hit(e, k, DSS_SNARE);
          check(held() > held0 + FS / 40, "CRUSH 30: samples held", e, DSS_SNARE, held());
          de_reset(); de_v[DE_ATT][DSS_SNARE] = 30; hit(e, k, DSS_SNARE);
          r = peakto(FS / 200) / a0;
          check(r < 0.4, "ATTACK 30 ms: the first 5 ms much quieter", e, DSS_SNARE, r);
          de_reset(); hit(e, k, DSS_KICK); lo0 = energy(0, LEN); z0 = crossings(0, FS / 25);
          de_v[DE_TON][DSS_KICK] = 30; hit(e, k, DSS_KICK);
          check(energy(0, LEN) / lo0 < 0.3, "TONE +30: the kick's lows mostly gone", e, DSS_KICK, energy(0, LEN) / lo0);
          de_reset(); de_v[DE_BND][DSS_KICK] = 20; hit(e, k, DSS_KICK);
          r = crossings(0, FS / 25) / (z0 + 1e-9);
          if (e != E_STD) check(r > 1.0, "BEND +20: more cycles in the first 40 ms", e, DSS_KICK, r);
        }
        de_reset();
    }
#endif
    printf(fails ? "drum_edit_test: %d FAILED\n" : "drum_edit_test: all passed\n", fails);
    return fails != 0;
}
