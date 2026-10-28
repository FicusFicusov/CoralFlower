/* SPDX-License-Identifier: GPL-3.0-only
 * CoralFlower: power cuts on storage.c's logged objects (0.6: a change written as what changed, st_dput).
 *   cc -std=gnu11 -O2 -Wall -Wno-unused-function -Ifirmware/src -o build/host/st_log_test tests/st_log_test.c
 *   build/host/st_log_test [STEPS [SEED]]
 * With older firmware's storage.c beside it (the reader after a downgrade; tests/st_log_old.c):
 *   git show v0.5:firmware/src/storage.c > build/host/storage_v05.c
 *   cc -std=gnu11 -O2 -w -Ifirmware/src -DST_OLD -o build/host/st_log_test tests/st_log_test.c tests/st_log_old.c \
 *      -DST_OLD_C='"../build/host/storage_v05.c"' && build/host/st_log_test
 * A NOR in RAM (a program only clears bits; one that would need a bit set back is counted as a fault). Three logged
 * objects (the settings, 256-byte entries; a sound's edits, 512; the patterns' sounds, 256) get a seeded run of saves
 * (small changes while it sounds, big ones, whole records, quiet-moment tidies, power-ons). After each save the
 * object loads as the model. Then, for every flash operation of every step, the step again from its start with the
 * power cut there (the operation torn: a part of a program's bytes, a part of an erase), a power-on (sometimes cut
 * too, then another), and the object must load as before the step or as the save it was making; the next save must
 * work and nothing may be programmed over data. ST_OLD: after each step the old reader loads a state the object had;
 * a few times the old firmware saves and the new one reads what it saved. */
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FELUCCA_CZ 0
#define FELUCCA_QUAD 0
#define CF_LOOP_STORE 1
#define ST_LOG_OBJ (OBJ_PROJECT0 + 3u)

/* ------------------------------------------------------------------ the NOR --- */
#define NOR_SIZE 0x100000u
static uint8_t nor[NOR_SIZE];
static long nor_ops, nor_cut_at;                 /* the operation the power goes in (0: none) */
static long nor_over, nor_page;                  /* faults: a program over data, one across a page */
static jmp_buf nor_jb;
static uint32_t rng = 1;
static uint32_t rnd(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

int nor_read(uint32_t off, void *dst, uint32_t n)
{
    if (off > NOR_SIZE || n > NOR_SIZE - off)
        return -1;
    memcpy(dst, nor + off, n);
    return 0;
}
int nor_erase(uint32_t off)
{
    if ((off & 4095u) || off >= NOR_SIZE)
        return -8;
    if (nor_cut_at && ++nor_ops == nor_cut_at) {  /* torn: a part erased (from the start, or bytes here and there) */
        uint32_t k = rnd() % 4097u, i;
        if (rnd() & 1u)
            memset(nor + off, 0xFF, k);
        else
            for (i = 0; i < 4096u; i++)
                if (rnd() & 1u)
                    nor[off + i] = 0xFF;
        longjmp(nor_jb, 1);
    }
    memset(nor + off, 0xFF, 4096u);
    return 0;
}
int nor_prog(uint32_t off, const void *src, uint32_t n)
{
    const uint8_t *s = src;
    uint32_t i;
    if (off > NOR_SIZE || n > NOR_SIZE - off)
        return -8;
    if ((off & 255u) + n > 256u)
        nor_page++;
    for (i = 0; i < n; i++)
        if (~nor[off + i] & s[i])
            nor_over++;
    if (nor_cut_at && ++nor_ops == nor_cut_at) { /* torn: the first k bytes, the next one in part */
        uint32_t k = rnd() % (n + 1u);
        for (i = 0; i < k; i++)
            nor[off + i] &= s[i];
        if (k < n)
            nor[off + k] &= (uint8_t)(s[k] | rnd());
        longjmp(nor_jb, 1);
    }
    for (i = 0; i < n; i++)
        nor[off + i] &= s[i];
    return 0;
}
static int st_read(uint32_t off, void *dst, uint32_t n) { return nor_read(off, dst, n); }
static int st_erase(uint32_t off) { return nor_erase(off); }
static int st_prog(uint32_t off, const void *src, uint32_t n) { return nor_prog(off, src, n); }
#include "storage.c"

#ifdef ST_OLD
int old_load(uint32_t obj, void *dst, uint32_t max);
int old_put(uint32_t obj, const void *src, uint32_t len, uint32_t how);
void old_boot(uint32_t obj);
#endif

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); \
                           printf(__VA_ARGS__); printf("\n"); } } while (0)

/* ------------------------------------------------------------------ the objects --- */
#define NOBJ 3
static const uint32_t OBJ[NOBJ] = {OBJ_PROJECT0 + 3u, OBJ_CFEDIT0, OBJ_CFPSND};
static const uint32_t LEN[NOBJ] = {200u, 480u, 100u};
static uint8_t model[NOBJ][480];                 /* what each loads as */
static uint8_t has[NOBJ];                        /* .. once saved */
#define NHIST 4096
static uint32_t hist[NOBJ][NHIST], nhist[NOBJ];  /* every state saved (FNV-1a), for the old reader */

static uint32_t fnv(const uint8_t *p, uint32_t n)
{
    uint32_t h = 2166136261u;
    while (n--)
        h = (h ^ *p++) * 16777619u;
    return h;
}
static void hist_add(int o, const uint8_t *r)
{
    if (nhist[o] < NHIST)
        hist[o][nhist[o]++] = fnv(r, LEN[o]);
}

static void boot(void)                           /* power-on: what RAM knew is gone; cf_settings_load's tidies */
{
    int o;
    uint32_t k;
    memset(st_blank, 0, sizeof st_blank);
    for (o = 0; o < NOBJ; o++) {
        st_check_blank(OBJ[o]);
        for (k = 0; k < 3u && st_tidy(OBJ[o]) > 0; k++)
            ;
    }
}
static int loads_as(int o, const uint8_t *r)
{
    uint8_t b[480];
    return st_load(OBJ[o], b, sizeof b) == (int)LEN[o] && !memcmp(b, r, LEN[o]);
}

/* a step: what it does (kind, its object, the record it saves) */
enum { K_SMALL, K_BIG, K_RANDOM, K_QUIET, K_WHOLE, K_TIDY, K_BOOT };
typedef struct { int kind, o; uint8_t rec[480]; } step_t;
static long now_step;                            /* (steps 400 .. 1399 of each 2000: playing on, no quiet moment) */
static int playing(void) { return now_step % 2000 >= 400 && now_step % 2000 < 1400; }
static uint8_t refused[NOBJ];                    /* (the last save had no room: the next goes with its erase) */
static void make(step_t *s)
{
    uint32_t r = rnd() % 100u, i, n;
    if (playing())
        r = r % 72u;                              /* (small and big changes only, while it sounds) */
    s->o = (int)(rnd() % NOBJ);
    memcpy(s->rec, model[s->o], LEN[s->o]);
    s->kind = !has[s->o] ? K_RANDOM : r < 62u ? K_SMALL : r < 72u ? K_BIG : r < 76u ? K_RANDOM : r < 84u ? K_QUIET :
              r < 88u ? K_WHOLE : r < 96u ? K_TIDY : K_BOOT;
    if (has[s->o] && refused[s->o]) {             /* (cf_settings.c: refused CF_SET_FORCE_MS ago, written anyway) */
        s->kind = K_QUIET;
        refused[s->o] = 0;
    }
    if (s->kind == K_SMALL)                       /* a knob: a byte or two, and the record's check */
        for (n = 1u + rnd() % 3u; n--;)
            s->rec[rnd() % LEN[s->o]] ^= (uint8_t)(1u + rnd() % 255u);
    else if (s->kind == K_BIG)                    /* a sound loaded: a stretch */
        for (i = rnd() % LEN[s->o], n = 20u + rnd() % 80u; n-- && i < LEN[s->o]; i++)
            s->rec[i] = (uint8_t)rnd();
    else if (s->kind == K_RANDOM || s->kind == K_QUIET || s->kind == K_WHOLE)
        for (i = 0; i < LEN[s->o]; i++)
            s->rec[i] = (uint8_t)((rnd() & 3u) ? s->rec[i] ^ (uint8_t)rnd() : 0);
}
/* run it: st_put's answer for a save (0, 1 not now), 0 for the rest */
static int run(const step_t *s)
{
    uint32_t k;
    switch (s->kind) {
    case K_TIDY:
        for (k = 0; k < 3u && st_tidy(OBJ[s->o]) > 0; k++)
            ;
        return 0;
    case K_BOOT:
        boot();
        return 0;
    default:
        return st_put(OBJ[s->o], s->rec, LEN[s->o],
                      s->kind == K_QUIET ? 0u : s->kind == K_WHOLE ? ST_WHOLE : ST_NOERASE);
    }
}
static int saves(const step_t *s) { return s->kind <= K_RANDOM || s->kind == K_QUIET || s->kind == K_WHOLE; }

/* ------------------------------------------------------------------ snapshots --- */
typedef struct {
    uint8_t fl[NOBJ][2][ST_SECTOR];
    uint8_t blank[sizeof st_blank];
    uint8_t model[NOBJ][480], has[NOBJ], refused[NOBJ];
} snap_t;
static void snap_take(snap_t *p)
{
    int o, c;
    for (o = 0; o < NOBJ; o++)
        for (c = 0; c < 2; c++)
            memcpy(p->fl[o][c], nor + st_sector(OBJ[o], (uint32_t)c), ST_SECTOR);
    memcpy(p->blank, st_blank, sizeof st_blank);
    memcpy(p->model, model, sizeof model);
    memcpy(p->has, has, sizeof has);
    memcpy(p->refused, refused, sizeof refused);
}
static void snap_put(const snap_t *p)
{
    int o, c;
    for (o = 0; o < NOBJ; o++)
        for (c = 0; c < 2; c++)
            memcpy(nor + st_sector(OBJ[o], (uint32_t)c), p->fl[o][c], ST_SECTOR);
    memcpy(st_blank, p->blank, sizeof st_blank);
    memcpy(model, p->model, sizeof model);
    memcpy(has, p->has, sizeof has);
    memcpy(refused, p->refused, sizeof refused);
}

static long n_deltas, n_full, n_cuts, n_cut2, n_refused, n_old;

/* the step cut at its operation k, the power back (cut again at k2 of it, if k2), the checks */
static void cut_case(const snap_t *before, const step_t *s, long k, long k2)
{
    static step_t nx;
    uint8_t b[480];
    int o = s->o, ok, rc;
    snap_put(before);
    nor_ops = 0;
    nor_cut_at = k;
    if (!setjmp(nor_jb)) {
        run(s);
        CHECK(0, "step kind %d obj %d: cut at %ld never came", s->kind, o, k);
        nor_cut_at = 0;
        return;
    }
    n_cuts++;
    if (k2) {
        nor_ops = 0;
        nor_cut_at = k2;
        if (!setjmp(nor_jb))
            boot();
        else
            n_cut2++;
    }
    nor_cut_at = 0;
    boot();
    ok = loads_as(o, before->model[o]) || (saves(s) && loads_as(o, s->rec));
    if (!before->has[o])
        ok = ok || st_load(OBJ[o], b, sizeof b) < 0;
    CHECK(ok, "step kind %d obj %d cut at %ld (then %ld): loads as neither the state before nor the save", s->kind, o,
          k, k2);
    {
        int i;
        for (i = 0; i < NOBJ; i++)
            if (i != o && before->has[i])
                CHECK(loads_as(i, before->model[i]), "cut at %ld: object %d (not the step's) changed", k, i);
    }
    if (!ok)
        return;
    nx.o = o;                                     /* the next save works: a small change of what it holds */
    if (st_load(OBJ[o], nx.rec, sizeof nx.rec) != (int)LEN[o])
        memset(nx.rec, 0x5A, LEN[o]);
    nx.rec[rnd() % LEN[o]] ^= 0x81u;
    rc = st_put(OBJ[o], nx.rec, LEN[o], ST_NOERASE);
    if (rc == 1)
        rc = st_put(OBJ[o], nx.rec, LEN[o], 0u);
    CHECK(rc == 0 && loads_as(o, nx.rec), "cut at %ld: the next save fails (rc %d)", k, rc);
}

int main(int argc, char **argv)
{
    static snap_t before, after;
    static step_t s;
    long nsteps = argc > 1 ? atol(argv[1]) : 1500, i, k, ops;
    uint32_t seed = argc > 2 ? (uint32_t)atol(argv[2]) : 12345u;
    memset(nor, 0xFF, sizeof nor);
    rng = seed;
    boot();
    for (i = 0; i < nsteps; i++) {
        int o, rc;
        now_step = i;
        make(&s);
        o = s.o;
        snap_take(&before);
        nor_ops = 0;
        nor_cut_at = 0;
        {   /* (count the step's operations: a cut at a huge index never comes) */
            long w0 = nor_over;
            nor_cut_at = 0x7FFFFFFF;
            if (setjmp(nor_jb))
                CHECK(0, "uncut step jumped");
            rc = run(&s);
            ops = nor_ops;
            nor_cut_at = 0;
            CHECK(nor_over == w0, "step %ld kind %d obj %d: programmed over data", i, s.kind, o);
        }
        if (saves(&s)) {
            if (rc == 0) {
                memcpy(model[o], s.rec, LEN[o]);
                has[o] = 1;
                hist_add(o, s.rec);
                if (st_current(OBJ[o], &(st_hdr_t){0}) >= 0 && st_cur_d)
                    n_deltas++;
                else
                    n_full++;
            } else {
                CHECK(rc == 1 && s.kind != K_QUIET && s.kind != K_WHOLE, "step %ld kind %d: st_put %d", i, s.kind, rc);
                n_refused++;
                refused[o] = 1;
            }
        }
        for (o = 0; o < NOBJ; o++)
            if (has[o])
                CHECK(loads_as(o, model[o]), "step %ld kind %d: object %d does not load as saved", i, s.kind, o);
#ifdef ST_OLD
        for (o = 0; o < NOBJ; o++) {              /* the old reader: a state the object had */
            uint8_t b[480];
            uint32_t h, j;
            if (!has[o])
                continue;
            CHECK(old_load(OBJ[o], b, sizeof b) == (int)LEN[o], "step %ld: the old reader loads nothing (obj %d)", i, o);
            for (h = fnv(b, LEN[o]), j = 0; j < nhist[o] && hist[o][j] != h; j++)
                ;
            CHECK(j < nhist[o], "step %ld: the old reader loads a state never saved (obj %d)", i, o);
        }
#endif
        snap_take(&after);
        for (k = 1; k <= ops; k++)
            cut_case(&before, &s, k, (k % 3 == 0) ? 1 + (long)(rnd() % 4u) : 0);
        snap_put(&after);
#ifdef ST_OLD
        if (i % 97 == 96) {                       /* a downgrade, a few saves there, back: the new reads them */
            uint8_t r[480];
            int j;
            o = (int)(rnd() % NOBJ);
            if (has[o]) {
                old_boot(OBJ[o]);
                CHECK(old_load(OBJ[o], r, sizeof r) == (int)LEN[o], "old firmware: nothing after its power-on");
                for (j = 0; j < 3; j++) {
                    r[rnd() % LEN[o]] ^= 0x42u;
                    rc = old_put(OBJ[o], r, LEN[o], j ? 1u : 0u);   /* (1: its ST_NOERASE) */
                    if (rc == 1)
                        rc = old_put(OBJ[o], r, LEN[o], 0u);
                    CHECK(rc == 0, "old firmware's save: %d", rc);
                }
                boot();
                CHECK(loads_as(o, r), "after the old firmware's saves the new one loads something else (obj %d)", o);
                memcpy(model[o], r, LEN[o]);
                hist_add(o, r);
                n_old++;
            }
        }
#endif
    }
    {   /* how many small changes fit after a power-on, no erase (the owner was told: about 200) */
        int o;
        for (o = 0; o < NOBJ; o++) {
            long n = 0;
            boot();
            memcpy(s.rec, model[o], LEN[o]);
            for (;;) {
                s.rec[rnd() % LEN[o]] ^= 0x11u;   /* (a knob: one byte, and the record's check: four) */
                s.rec[(LEN[o] - 4u + rnd() % 4u)] ^= 0x22u;
                if (st_put(OBJ[o], s.rec, LEN[o], ST_NOERASE) != 0)
                    break;
                n++;
                memcpy(model[o], s.rec, LEN[o]);
            }
            CHECK(loads_as(o, model[o]), "capacity run: object %d does not load as its last save", o);
            CHECK(n >= 180, "object %d: only %ld changes fit without an erase", o, n);
            printf("object %d (%u-byte entries): %ld changes without an erase\n", o, (unsigned)ST_LSZ(OBJ[o]), n);
        }
    }
    CHECK(nor_page == 0, "%ld programs across a page", nor_page);
    CHECK(nor_over == 0, "%ld programs over data", nor_over);
    CHECK(n_deltas > n_full, "changes mostly as deltas (%ld deltas, %ld whole)", n_deltas, n_full);
    printf("st_log: %ld steps (%ld saves as changes, %ld whole, %ld refused: no room without an erase), %ld power cuts"
           " (%ld cut again at the power-on)%s, %d checks, %d failed\n", nsteps, n_deltas, n_full, n_refused, n_cuts,
           n_cut2, n_old ? ", old firmware's saves read back" : "", checks, fails);
    return fails != 0;
}
