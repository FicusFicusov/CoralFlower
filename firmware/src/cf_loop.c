/* SPDX-License-Identifier: GPL-3.0-only
 * CoralFlower: the looper's saved loop, a format read once. Included by cf_out.c.
 *
 * Up to 0.3.1 a looper recorded what the keys did (each key down and up, roots and chord buttons, on a clock of
 * CFL_PPQN ticks a beat) and SAVE > Loop kept it in flash (OBJ_CFLOOP). 0.4 has no looper: REC on HOME records into a
 * new sequencer pattern (cf_sequi.c sq_home_rec; the owner, 2026-10-19, "сразу 0.4 без лупера"). A loop still in
 * flash, or restored from an older backup (cf_backup.c id 31), becomes a pattern's CHORDS track at the next power-on
 * (cf_sequi.c sq_from_loop) and leaves flash. */

#define CFL_PPQN 96u                                 /* ticks a beat */
#define CFL_MAX_EV 500u                              /* events in a loop (all its layers) */
#define CFL_MAX_LAYERS 32u
#define CFL_MAGIC 0x314C4643u                        /* "CFL1" */
#define CFL_DOWN 0x80u                               /* cfl_ev_t.f: the key went down (else up); bits 0-4: its layer */
static const uint8_t CF_LOOP_BARS[] = {0, 1, 2, 4, 8};   /* (the looper's LENGTH; the settings record keeps it) */
#define CF_LOOP_NBARS ((uint32_t)sizeof CF_LOOP_BARS)

typedef struct { uint16_t t; uint8_t key, f; } cfl_ev_t;
typedef struct {                                     /* a loop: what flash keeps (OBJ_CFLOOP) */
    uint32_t magic;
    uint16_t nev, nlayers;
    uint32_t len;                                    /* ticks; 0: empty */
    uint8_t algo, beats, rsv[2];                     /* the algorithm and the bar it was played in */
    cfl_ev_t ev[CFL_MAX_EV];                         /* in time order */
} cfl_data_t;
