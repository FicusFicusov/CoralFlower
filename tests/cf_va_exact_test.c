/* SPDX-License-Identifier: GPL-3.0-only */
/* Deterministic actual VA render/state stream for cf_va_exact_test.py.
 * This is a DSP oracle and native benchmark, not device timing or allocation. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#define __attribute__(x)
#define FELUCCA_VA 1
#define VA_NPART 3u
#define CF_NTAIL 1u
#include "felucca_tables.h"
#include "../firmware/src/core.h"
#include "../firmware/src/dsp.c"
static uint64_t va_test_output_multiplies, va_test_lp_samples;
#ifndef VA_ENGINE_SOURCE
#define VA_ENGINE_SOURCE "../firmware/src/eng_va.c"
#endif
#include VA_ENGINE_SOURCE

#define FACTORY_CASES (VA_NPRESETS * 3u)
#define EXTRA_CASES 12u
#define TEST_TICKS 1536u
#define CYCLE_TICKS 32768u
static uint32_t rendered, audible, side_blocks, finished;
static int32_t output[CTL], stereo[CTL];
static volatile int32_t bench_sink;
static uint32_t active_slot;
static track_t *part;

static void note_on(uint32_t i, uint32_t note, uint32_t vel)
{
    voice_t *v = &part->v[i];
    if (!v->active)
        v->env = v->env_out = 0;
    v->active = v->gate = 1;
    v->note = (uint8_t)note;
    v->vel = v->mvel = (uint8_t)vel;
    v->mrnd = (int16_t)(i * 7001u - 30000u);
    v->pitch16 = v->pitch_cur = (int32_t)note * 16;
    v->age = i + 1u;
    part->m_vi = (uint8_t)i;
    va_note_on(part, v);
}

static void reset_case(uint32_t id)
{
    uint32_t k, mode = id < FACTORY_CASES ? id / VA_NPRESETS : id - FACTORY_CASES;
    static const uint32_t tracks[] = {0, 1, 2, NTRK};
    memset(trk, 0, sizeof trk);
    memset(va_vs, 0, sizeof va_vs);
    memset(va_lfo, 0, sizeof va_lfo);
    memset(va_patch, 0, sizeof va_patch);
    memset(va_mlast, 0, sizeof va_mlast);
    memset(va_pan_off, 0, sizeof va_pan_off);
    memset(va_rate_off, 0, sizeof va_rate_off);
    part = &trk[tracks[id % NELEM(tracks)]];
    active_slot = va_tr(part);
    int8_t *p = va_patch[active_slot];
    song.g[G_BPM] = 137;
    part->p[P_VOICE] = mode == 1 ? V_UNISON : V_POLY;
    if (id < FACTORY_CASES) {
        va_preset_patch(id % VA_NPRESETS, p);
        if (mode == 1)
            p[VA_USPREAD] = 127;
    } else {
        static const uint8_t types[EXTRA_CASES] = {0, 0, 0, 1, 32, 64, 96, 127, 0, 0, 0, 0};
        va_init_patch(p);
        p[VA_FTYPE] = types[mode];
        p[VA_FSPREAD] = mode == 8 ? 0 : 80;
        p[VA_USPREAD] = mode == 8 ? 127 : 0;
        part->p[P_VOICE] = mode == 8 ? V_UNISON : V_POLY;
        p[VA_CUT] = 90;
        p[VA_RES] = mode == 10 ? 127 : 45;
        p[VA_DRIVE] = mode == 10 ? 127 : 30;
        p[VA_SYNC2] = mode == 9;
        p[VA_RING4] = mode == 9;
        for (k = 0; k < 4; k++) {
            p[VA_OMODE0 + k] = mode < 3 ? (int8_t)mode : (int8_t)(k % VOM_N);
            p[VA_OSC(k, VO_LEVEL)] = 90;
            p[VA_OSC(k, VO_WAVE)] = (int8_t)((mode + k) % VW_N);
            p[VA_OSC(k, VO_SHAPE)] = (int8_t)(17 + 31 * k);
            p[VA_OSC(k, VO_COARSE)] = (int8_t)((int32_t)k * 12 - 24);
            p[VA_ENV(k, VE_ATK)] = 8;
            p[VA_ENV(k, VE_HOLD)] = 10;
            p[VA_ENV(k, VE_DEC)] = 20;
            p[VA_ENV(k, VE_SUS)] = 80;
            p[VA_ENV(k, VE_REL)] = 12;
            p[VA_LFO(k, VL_SYNC)] = k & 1u;
            p[VA_LFO(k, VL_RATE)] = (int8_t)(60 + 10 * k);
            p[VA_MOD(k, VM_SRC)] = VS_LFO1 + k;
            p[VA_MOD(k, VM_DST)] = VD_SHP1 + k;
            p[VA_MOD(k, VM_AMT)] = k & 1u ? -32 : 40;
        }
        p[VA_MOD(4, VM_SRC)] = VS_MODW;
        p[VA_MOD(4, VM_DST)] = mode == 11 ? VD_FTYPE : VD_SPREAD;
        p[VA_MOD(4, VM_AMT)] = mode == 11 ? 63 : -30;
        p[VA_MOD(5, VM_SRC)] = VS_ENV1 + 1;
        p[VA_MOD(5, VM_DST)] = VD_CUT;
        p[VA_MOD(5, VM_AMT)] = 24;
        p[VA_MOD(6, VM_SRC)] = VS_LFO1 + 3;
        p[VA_MOD(6, VM_DST)] = VD_PITCH;
        p[VA_MOD(6, VM_AMT)] = 2;
        p[VA_MOD(7, VM_SRC)] = VS_VEL;
        p[VA_MOD(7, VM_DST)] = VD_DEP1;
        p[VA_MOD(7, VM_AMT)] = -30;
    }
    va_macros_out(part);
    for (k = 0; k < VA_POLY; k++)
        note_on(k, 36 + k * 7, 32 + k * 13);
    rendered = audible = side_blocks = finished = 0;
    va_test_output_multiplies = va_test_lp_samples = 0;
}

static void tick(uint32_t id, uint32_t tick_no, int varying)
{
    uint32_t k, n = varying && id >= FACTORY_CASES ? 1u + tick_no % CTL : CTL;
    int mono = id < FACTORY_CASES && id / VA_NPRESETS == 2;
    int8_t *p = va_patch[active_slot];
    if (varying == 2) {
        uint32_t phase = tick_no % (CYCLE_TICKS / 2u);
        if (phase == 13627) {                    /* at least 2 s completely silent before the next onset */
            for (k = 0; k < VA_POLY; k++)
                if (part->v[k].active) {
                    fprintf(stderr, "FAIL release unfinished: case %u tick %u voice %u\n", id, tick_no, k);
                    exit(5);
                }
        }
        if (!phase && tick_no)
            for (k = 0; k < VA_POLY; k++)
                note_on(k, 41 + k * 7, 110 - k * 9);
        if (phase == 1024)
            for (k = 0; k < VA_POLY; k++)
                part->v[k].gate = 0;
    } else if (varying) {
        part->mw = (uint8_t)((tick_no / 13u) & 127u);
        if (tick_no == 384)
            for (k = 0; k < VA_POLY; k += 2)
                note_on(k, 43 + k * 7, 110 - k * 9);
        if (tick_no == 512 && id >= FACTORY_CASES)
            p[VA_FSPREAD] = 0;
        if (tick_no == 768 && id >= FACTORY_CASES)
            p[VA_FSPREAD] = 100;
        if (tick_no == 1024)
            for (k = 0; k < VA_POLY; k++)
                part->v[k].gate = 0;
    }
    memset(output, 0, sizeof output);
    memset(stereo, 0, sizeof stereo);
    va_block(part);
    for (k = 0; k < VA_POLY; k++) {
        voice_t *v = &part->v[k];
        if (!v->active)
            continue;
        if (va_done(part, v)) {
            v->active = 0;
            v->env = v->env_out = 0;
            finished++;
            continue;
        }
        vmod_t m = {0};
        v->env = 1 << 24;
        m.amp0 = v->env_out;
        m.amp1 = v->env_out = varying ? 26000 + (int32_t)(tick_no % 67u) * 80 : 32767;
        m.pitch16 = v->pitch_cur + (varying ? (int32_t)(tick_no % 129u) - 64 : 0);
        m.fine = varying ? (int32_t)(tick_no % 11u) - 5 : 0;
        m.inc = pitch_inc((uint32_t)m.pitch16);
        m.shape = (64 << 8) + (varying ? ((int32_t)(tick_no % 19u) - 9) * 256 : 0);
        m.cutoff = varying ? ((int32_t)(tick_no % 97u) - 48) * 16 : 0;
        side_blocks += va_render(part, v, output, mono ? NULL : stereo, n, &m) != 0;
        rendered++;
    }
    for (k = 0; k < CTL; k++)
        audible += output[k] != 0 || stereo[k] != 0;
    bench_sink = output[tick_no & (CTL - 1u)] ^ stereo[tick_no & (CTL - 1u)];
}

static void bytes(const void *p, size_t n)
{
    if (fwrite(p, 1, n, stdout) != n) {
        fprintf(stderr, "FAIL output write\n");
        exit(3);
    }
}

static uint32_t state_size(void)
{
    return sizeof part->v + sizeof va_vs[active_slot] + sizeof va_lfo[active_slot] +
           sizeof va_patch[active_slot] + sizeof va_mlast[active_slot] +
           sizeof va_pan_off[active_slot] + sizeof va_rate_off[active_slot];
}

static void state(void)
{
    bytes(part->v, sizeof part->v);
    bytes(va_vs[active_slot], sizeof va_vs[active_slot]);
    bytes(&va_lfo[active_slot], sizeof va_lfo[active_slot]);
    bytes(va_patch[active_slot], sizeof va_patch[active_slot]);
    bytes(va_mlast[active_slot], sizeof va_mlast[active_slot]);
    bytes(&va_pan_off[active_slot], sizeof va_pan_off[active_slot]);
    bytes(va_rate_off[active_slot], sizeof va_rate_off[active_slot]);
}

int main(int argc, char **argv)
{
    if (argc == 2 && (!strcmp(argv[1], "verify") || !strcmp(argv[1], "cycles"))) {
        static const uint32_t cycle_presets[] = {20, 22, 24, 27, 29};
        uint32_t cycles = !strcmp(argv[1], "cycles");
        uint32_t cases = cycles ? NELEM(cycle_presets) : FACTORY_CASES + EXTRA_CASES;
        uint32_t ticks = cycles ? CYCLE_TICKS : TEST_TICKS;
        const uint32_t header[] = {0x56414551u, cases, ticks,
                                   2u * sizeof output, state_size()};
        bytes(header, sizeof header);
        for (uint32_t test = 0; test < cases; test++) {
            uint32_t id = cycles ? cycle_presets[test] : test;
            reset_case(id);
            for (uint32_t n = 0; n < ticks; n++) {
                tick(id, n, cycles ? 2 : 1);
                bytes(output, sizeof output); bytes(stereo, sizeof stereo);
                state();
                const uint32_t counts[] = {rendered, audible, side_blocks, finished};
                bytes(counts, sizeof counts);
            }
            const uint64_t ops[] = {va_test_output_multiplies, va_test_lp_samples};
            bytes(ops, sizeof ops);
            if (!rendered || !audible || (cycles && finished != 2 * VA_POLY)) {
                fprintf(stderr, "FAIL case %u rendered=%u audible=%u\n", id, rendered, audible);
                return 4;
            }
        }
        return fflush(stdout) ? 3 : 0;
    }
    if (argc == 4 && !strcmp(argv[1], "bench")) {
        uint32_t id = (uint32_t)strtoul(argv[2], NULL, 10), ticks = (uint32_t)strtoul(argv[3], NULL, 10);
        if (id >= FACTORY_CASES + EXTRA_CASES || !ticks)
            return 2;
        reset_case(id);
        for (uint32_t n = 0; n < 4096; n++) tick(id, n, 0);
        uint32_t rendered_before = rendered, audible_before = audible;
        clock_t start = clock();
        for (uint32_t n = 0; n < ticks; n++) tick(id, n, 0);
        double seconds = (double)(clock() - start) / CLOCKS_PER_SEC;
        printf("case=%u ticks=%u voices=%u rendered=%u audible=%u seconds=%.6f checksum=%d\n",
               id, ticks, VA_POLY, rendered - rendered_before, audible - audible_before, seconds, bench_sink);
        return rendered - rendered_before != ticks * VA_POLY || audible == audible_before || seconds <= 0;
    }
    fprintf(stderr, "usage: %s verify | cycles | bench case ticks\n", argv[0]);
    return 2;
}
