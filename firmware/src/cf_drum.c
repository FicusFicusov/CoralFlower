/* SPDX-License-Identifier: GPL-3.0-only
 * CoralFlower: the drum machine (0.6; the owner, 2026-10-28: "Да, подходят"). Ready rhythms of 1 or 2 bars, each for
 * one TIME SIG, played on the drums' part (the DRUM engine and the second one's kits, as a pattern's DRUMS) while the
 * keys play chords on HOME. The audio side: the rhythms and their player. Included by cf_out.c after cf_seqp.c: a
 * rhythm and the sequencer never play together (one thing plays: the player stops the other as it starts).
 *
 * A rhythm is its drums' hits as text, one string a drum, its bars one after another ('x' a hit, '.' none): the
 * drums' grid of the metronome's beats x 4 steps (x 2 in a bar of more than 4 beats, sq_dsteps), as a pattern's
 * DRUMS. Written for CoralFlower from the common grooves of each style, no other instrument's patterns. */

enum { DM_K, DM_S, DM_C, DM_H, DM_O, DM_LT, DM_MT, DM_HT, DM_R, DM_B, DM_X };   /* (the lanes: cf_seqd.c SQ_NLANE) */
typedef struct {
    const char *name;            /* 9 letters at most: "Breakcore" fits the page's title and the plaque */
    uint8_t id;                  /* its own number, never reused: what the user's KIT and SWING and the rhythm stood on
                                  * are kept by (cf_dmui.c); rc3's rhythms kept theirs (0 .. 28), new ones go on from 29 */
    uint8_t beats, bars;         /* its TIME SIG (cs.beats), its length */
    uint8_t kit, swing;          /* its own kit (SQ_KITS) and SWING (0..100 %, as a pattern's) */
    uint8_t bpm;                 /* the tempo its style lives at (shown, never set: the tempo is the user's) */
    const char *lane[SQ_NLANE];  /* each drum's hits, bar after bar; 0: the drum is not in it */
    uint8_t lev[SQ_NLANE];       /* .. its level, 0..100 % (a pattern's dlev) */
} dm_rhythm_t;

/* the kits by name (SQ_KITS' order) */
enum { DMK_STD, DMK_808, DMK_909, DMK_TIGHT, DMK_BOOM, DMK_LOFI, DMK_HAND, DMK_CRUSH, DMK_XCLUB, DMK_XDEEP, DMK_XLOFI,
       DMK_XLIVE, DMK_XTECH, DMK_XBREAK };

/* 4/4: the owner's list for rc4 ("моя прошивка больше ориентирована на более современные жанры"; rc3's Rock, Bossa,
 * Reggae, Swing, Motown, Dub, Afro, Ballad, Punk, Disco, Trap and Samba gone; "Все, решу на слух": every one proposed
 * is in, the owner cuts by ear before the release). Pop and HipHop on X Lo-Fi, Funk on X Club with SWING 40 %, DnB on
 * X Tech with more in it than two steps (the owner's) */
static const dm_rhythm_t DM_RHY[] = {
    /* 4/4: 16 steps a bar */
    {"Pop", 1, 4, 1, DMK_XLOFI, 0, 110,
     {[DM_K] = "x.....x.x.......", [DM_S] = "....x.......x...", [DM_C] = "....x.......x...",
      [DM_H] = "x.x.x.x.x.x.x.x."},
     {[DM_K] = 100, [DM_S] = 80, [DM_C] = 60, [DM_H] = 50}},
    {"PopRock", 29, 4, 2, DMK_XLIVE, 0, 120,
     {[DM_K] = "x.......x.x....." "x.....x.x.......", [DM_S] = "....x.......x..." "....x.......x...",
      [DM_H] = "x.x.x.x.x.x.x.x." "x.x.x.x.x.x.x.x.", [DM_X] = "x..............." "................"},
     {[DM_K] = 100, [DM_S] = 90, [DM_H] = 55, [DM_X] = 45}},
    {"Funk", 2, 4, 2, DMK_XCLUB, 40, 105,
     {[DM_K] = "x..x..x...x....." "x.x...x..x..x...", [DM_S] = "....x.......x..." "....x.......x...",
      [DM_R] = ".......x.x....x." "..x....x.....x..", [DM_H] = "xxxxxxxxxxxxxxxx" "xxxxxxxxxxxxxx..",
      [DM_O] = "................" "..............x."},
     {[DM_K] = 100, [DM_S] = 90, [DM_R] = 35, [DM_H] = 40, [DM_O] = 50}},
    {"HipHop", 7, 4, 2, DMK_XLOFI, 30, 90,
     {[DM_K] = "x......x.x......" "x.........x.....", [DM_S] = "....x.......x..." "....x.......x...",
      [DM_H] = "x.x.x.x.x.x.x.x." "x.x.x.x.x.x.x.x."},
     {[DM_K] = 100, [DM_S] = 90, [DM_H] = 50}},
    {"Lo-fi", 30, 4, 2, DMK_XLOFI, 45, 80,
     {[DM_K] = "x.......x.x....." "x......x..x.....", [DM_S] = "....x.......x..." "....x.......x...",
      [DM_H] = "x.x.x.x.x.x.x.x." "x.x.x.x.x.x.xx.."},
     {[DM_K] = 90, [DM_S] = 75, [DM_H] = 35}},
    {"House", 4, 4, 1, DMK_909, 0, 124,
     {[DM_K] = "x...x...x...x...", [DM_C] = "....x.......x...", [DM_H] = ".x.x.x.x.x.x.x.x",
      [DM_O] = "..x...x...x...x."},
     {[DM_K] = 100, [DM_C] = 80, [DM_H] = 35, [DM_O] = 60}},
    {"Garage", 18, 4, 1, DMK_XCLUB, 55, 132,
     {[DM_K] = "x......x..x.....", [DM_S] = "....x.......x...", [DM_H] = "x.xxx.xxx.xxx.xx"},
     {[DM_K] = 100, [DM_S] = 85, [DM_H] = 40}},
    {"Techno", 5, 4, 1, DMK_XTECH, 0, 132,
     {[DM_K] = "x...x...x...x...", [DM_C] = "....x.......x...", [DM_H] = "xx..xx..xx..xx..",
      [DM_O] = "..x...x...x...x.", [DM_R] = "...x.....x....x."},
     {[DM_K] = 100, [DM_C] = 70, [DM_H] = 40, [DM_O] = 55, [DM_R] = 45}},
    {"Break", 13, 4, 2, DMK_XBREAK, 10, 130,
     {[DM_K] = "x.x.......xx...." "x.x.......x.....", [DM_S] = "....x..x.x..x..x" "....x..x.x....x.",
      [DM_H] = "x.x.x.x.x.x.x.x." "x.x.x.x.x.x.x.x."},
     {[DM_K] = 100, [DM_S] = 90, [DM_H] = 50}},
    {"Jungle", 31, 4, 2, DMK_XBREAK, 0, 165,
     {[DM_K] = "x.........x....." "..x.......x.....", [DM_S] = "....x.x....x.x.." "....x..x.x..x.x.",
      [DM_R] = ".x....x.x......x" "x....x......x...", [DM_H] = "xxxxxxxxxxxxxxxx" "xxxxxxxxxxxxxxxx",
      [DM_X] = "x..............." "................"},
     {[DM_K] = 100, [DM_S] = 90, [DM_R] = 35, [DM_H] = 30, [DM_X] = 40}},
    {"DnB", 14, 4, 2, DMK_XTECH, 0, 174,
     {[DM_K] = "x.........x....." "x.x.......x..x..", [DM_S] = "....x.......x..." "....x.......x...",
      [DM_R] = ".......x.x....x." "..x....x.......x", [DM_H] = "x.xxx.x.x.xxx.x." "x.x.x.xxx.x.x.x.",
      [DM_X] = "x..............." "................"},
     {[DM_K] = 100, [DM_S] = 95, [DM_R] = 35, [DM_H] = 45, [DM_X] = 40}},
    {"Breakcore", 39, 4, 2, DMK_XBREAK, 0, 180,
     {[DM_K] = "x.x...x...x.x..." "x...x.x..x...x..", [DM_S] = "....x..xx..x.x.x" "..x.x.x..x.xx.xx",
      [DM_H] = "xxxxxxxxxxxxxxxx" "xxxxxxxxxxxxxxxx", [DM_X] = "x..............." "................"},
     {[DM_K] = 100, [DM_S] = 90, [DM_H] = 35, [DM_X] = 40}},
    {"Footwork", 33, 4, 2, DMK_808, 0, 160,
     {[DM_K] = "x..x..x...x..x.." "x..x..x..x..x...", [DM_C] = "....x.......x..." "....x.......x...",
      [DM_H] = "x.xx.xx.x.xx.xx." "x.xx.xx.x.xx.xx.", [DM_LT] = "......x......x.." "........x....x.."},
     {[DM_K] = 100, [DM_C] = 80, [DM_H] = 40, [DM_LT] = 70}},
    {"Jersey", 32, 4, 2, DMK_XCLUB, 0, 140,
     {[DM_K] = "x..x..x.x..x.x.." "x..x..x.x.xx.x.x", [DM_C] = "....x.......x..." "....x.......x.xx",
      [DM_H] = "..x...x...x...x." "..x...x...x...x."},
     {[DM_K] = 100, [DM_C] = 85, [DM_H] = 40}},
    {"Hyperpop", 34, 4, 2, DMK_CRUSH, 0, 150,
     {[DM_K] = "x.....x...x....." "x.....x.x.x.....", [DM_C] = "....x.......x..." "....x.......x...",
      [DM_H] = "x.x.x.x.x.x.xxxx" "x.x.x.x.xxxxxxxx", [DM_X] = "x..............." "................"},
     {[DM_K] = 100, [DM_C] = 90, [DM_H] = 40, [DM_X] = 35}},
    {"Drill", 35, 4, 2, DMK_808, 15, 142,
     {[DM_K] = "x......x...x...." "..x....x..x.x...", [DM_S] = "........x......." "........x.....x.",
      [DM_H] = "x..x..x.x..x..x." "x..x..x.x.xx..x."},
     {[DM_K] = 100, [DM_S] = 90, [DM_H] = 45}},
    {"Grime", 36, 4, 2, DMK_TIGHT, 0, 140,
     {[DM_K] = "x.....x...x..x.." "x..x......x.....", [DM_S] = "........x......." "........x.......",
      [DM_H] = "x...x...x...x..." "x...x...x...x.x."},
     {[DM_K] = 100, [DM_S] = 90, [DM_H] = 40}},
    {"Halftime", 38, 4, 2, DMK_XDEEP, 0, 140,
     {[DM_K] = "x.........x....." "x......x.x......", [DM_S] = "........x......." "........x.......",
      [DM_H] = "x.x.x.x.x.x.x.x." "x.x.x.x.x.x.x.x."},
     {[DM_K] = 100, [DM_S] = 95, [DM_H] = 35}},
    {"Phonk", 37, 4, 1, DMK_808, 0, 130,
     {[DM_K] = "x......x..x.....", [DM_C] = "....x.......x...", [DM_H] = "x.x.x.x.x.x.x.x.",
      [DM_B] = "x..x..x.x..x..x."},
     {[DM_K] = 100, [DM_C] = 85, [DM_H] = 40, [DM_B] = 55}},
    {"Amapiano", 40, 4, 2, DMK_XDEEP, 0, 112,
     {[DM_K] = "x...x...x...x..." "x...x...x...x...", [DM_LT] = "...x..x...x..x.." "..x..x....x...x.",
      [DM_R] = "....x.......x..." "....x.......x...", [DM_H] = "xxxxxxxxxxxxxxxx" "xxxxxxxxxxxxxxxx"},
     {[DM_K] = 85, [DM_LT] = 90, [DM_R] = 60, [DM_H] = 30}},
    {"Baile", 41, 4, 2, DMK_808, 0, 130,
     {[DM_K] = "x.....x...x....." "x.....x...x.x...", [DM_MT] = "...x.....x...x.." "...x.....x...x..",
      [DM_LT] = "..x.....x.....x." "..x.....x.....x.", [DM_C] = "....x.......x..." "....x.......x..."},
     {[DM_K] = 100, [DM_MT] = 75, [DM_LT] = 80, [DM_C] = 80}},
    {"Dembow", 42, 4, 1, DMK_XCLUB, 0, 95,
     {[DM_K] = "x...x...x...x...", [DM_S] = "...x..x....x..x.", [DM_H] = "x.x.x.x.x.x.x.x."},
     {[DM_K] = 100, [DM_S] = 85, [DM_H] = 35}},
    /* the other TIME SIGs: modern takes (the owner's pick B for rc4: "3/4 trap-waltz, 6/8 afro-house, 5/4 and 7/4
     * math-rock, 2/4 baile"; rc3's Waltz, Jazz3, March, Polka, Five, Odd5, Blues, Bembe and Seven gone). A 6/8 bar is
     * 6 beats of 8ths: its tempo counts 8ths */
    /* 3/4: 12 steps */
    {"TrapWaltz", 43, 3, 2, DMK_808, 0, 140,
     {[DM_K] = "x.....x....." "x..x.....x..", [DM_C] = "........x..." "........x...",
      [DM_H] = "x.x.x.x.xxxx" "x.x.xxx.x.x."},
     {[DM_K] = 100, [DM_C] = 85, [DM_H] = 40}},
    /* 6/8: 12 steps */
    {"AfroHouse", 44, 6, 1, DMK_XDEEP, 0, 200,
     {[DM_K] = "x.....x.....", [DM_B] = "x.x.xx.x.x.x", [DM_H] = "xxxxxxxxxxxx", [DM_LT] = "....x.....x."},
     {[DM_K] = 95, [DM_B] = 55, [DM_H] = 30, [DM_LT] = 70}},
    /* 2/4: 8 steps */
    {"Tamborzao", 47, 2, 2, DMK_808, 0, 130,
     {[DM_K] = "x..x..x." "x..x..x.", [DM_MT] = ".x...x.." ".x...x.x", [DM_LT] = "..x....x" "..x.....",
      [DM_C] = "....x..." "....x..."},
     {[DM_K] = 100, [DM_MT] = 75, [DM_LT] = 80, [DM_C] = 80}},
    /* 5/4: 10 steps (8ths) */
    {"MathRock", 45, 5, 1, DMK_XLIVE, 0, 140,
     {[DM_K] = "x..x..x...", [DM_S] = "....x...x.", [DM_H] = "x.x.x.x.x.", [DM_X] = "x........."},
     {[DM_K] = 95, [DM_S] = 90, [DM_H] = 45, [DM_X] = 35}},
    /* 7/4: 14 steps (8ths) */
    {"MathRock", 46, 7, 1, DMK_XLIVE, 0, 130,
     {[DM_K] = "x...x...x..x..", [DM_S] = "..x...x...x..x", [DM_H] = "xxxxxxxxxxxxxx", [DM_X] = "x............."},
     {[DM_K] = 95, [DM_S] = 90, [DM_H] = 35, [DM_X] = 35}},
};
#define DM_N ((uint32_t)(sizeof DM_RHY / sizeof DM_RHY[0]))

/* the rhythms of a TIME SIG: how many, the k-th (DM_N: none) */
static uint32_t dm_count(uint32_t beats)
{
    uint32_t i, n = 0;
    for (i = 0; i < DM_N; i++)
        n += DM_RHY[i].beats == beats;
    return n;
}
static uint32_t dm_nth(uint32_t beats, uint32_t k)
{
    uint32_t i;
    for (i = 0; i < DM_N; i++)
        if (DM_RHY[i].beats == beats && !k--)
            return i;
    return DM_N;
}
static uint32_t dm_place(uint32_t r)             /* .. its place in its TIME SIG's list */
{
    uint32_t i, k = 0;
    for (i = 0; i < r && i < DM_N; i++)
        k += DM_RHY[i].beats == DM_RHY[r % DM_N].beats;
    return k;
}
/* a bar's drum steps in a TIME SIG of so many beats (as cf_seqd.c sq_dsteps for the one now) */
static uint32_t dm_steps(uint32_t beats) { return beats * 4u <= 16u ? beats * 4u : beats * 2u; }
/* drum l's hits in bar b of rhythm r: a pattern's DRUMS bits (bit i, step i), in the rhythm's own TIME SIG */
static uint32_t dm_mask(uint32_t r, uint32_t l, uint32_t b)
{
    const dm_rhythm_t *y = &DM_RHY[r % DM_N];
    const char *s = y->lane[l % SQ_NLANE];
    uint32_t sd = dm_steps(y->beats), i, m = 0;
    if (!s || b >= y->bars)
        return 0;
    s += b * sd;
    for (i = 0; i < sd && s[i]; i++)
        if (s[i] != '.')
            m |= 1u << i;
    return m;
}

/* ------------------------------------------------------------ the player --- */
/* PLAY in the drum machine (or on HOME, the rhythm last chosen): its bars round, on the sequencer's clock (96 ticks a
 * beat from the tempo; MIDI clock in: its pulses), the metronome's beat 1 at its start. Another rhythm picked while it
 * plays comes in at the next bar line, from its first bar. REC in the SEQUENCER while it plays: it ends its bar (the
 * next too, with less than a beat of it left: the player gets ready), then the pattern on screen plays and the take
 * goes into it (rin) */
static struct {
    uint8_t on;                  /* a rhythm plays */
    uint8_t r, kit, swing;       /* .. which, its kit and SWING as played (its own or the user's) */
    uint8_t next, nkit, nswing;  /* .. the one coming in at the next bar line (0xFF none) */
    uint8_t bar;                 /* .. its bar, 0 .. its bars - 1 */
    uint32_t nbar;               /* .. bars since it began */
    uint32_t t, acc;             /* .. ticks into the bar; the clock's samples x bpm */
    uint8_t nh;                  /* .. the next step not struck yet */
    uint8_t rin, rtrk, rbars;    /* REC in the SEQUENCER: pattern + 1 (0 none), its track, bar lines to wait */
} dmp = {.next = 0xFF};

static uint32_t dmp_tick_of(uint32_t i)          /* where step i sounds: every second one late by SWING */
{
    uint32_t bt = sqp_bar_ticks(), sd = sq_dsteps(), t = i * bt / sd;
    if (i & 1u)
        t += dmp.swing * (bt / sd) / 200u;
    return t;
}
static void dmp_fire(uint32_t h)
{
    const dm_rhythm_t *y = &DM_RHY[dmp.r % DM_N];
    uint32_t l;
    for (l = 0; l < SQ_NLANE; l++)
        if ((dm_mask(dmp.r, l, dmp.bar) >> h) & 1u) {
            sqp_lane_kit(l, dmp.kit, y->lev[l]);
            trk_note_on(&trk[CF_PART_DRUM], SQ_LANE_NOTE[l], 100);
            cfp_midi(0x90u, cfp_ch(CF_PART_DRUM), SQ_LANE_NOTE[l], 100);
            sqp.doff |= (uint16_t)(1u << l);
            sqp.lane_t[l] = cf_now_ms;
        }
}
static void dmp_now(void)
{
    while (dmp.nh < sq_dsteps() && dmp.t >= dmp_tick_of(dmp.nh))
        dmp_fire(dmp.nh++);
}
static void dmp_stop(void)
{
    if (!dmp.on)
        return;
    dmp.on = 0;
    dmp.next = 0xFF;
    dmp.rin = 0;
    sqp_drum_offs();
    if (!sqp.on)
        cfm.running = 0;
}
static void dmp_start(uint32_t r, uint32_t kit, uint32_t swing)
{
    sqp_stop();
    dmp.r = (uint8_t)(r % DM_N);
    dmp.kit = (uint8_t)kit;
    dmp.swing = (uint8_t)swing;
    dmp.next = 0xFF;
    dmp.rin = 0;
    dmp.bar = 0;
    dmp.nbar = 0;
    dmp.t = 0;
    dmp.acc = 0;
    dmp.nh = 0;
    dmp.on = 1;
    cfm_restart();
    cfm.running = 1;
    dmp_now();
}
/* REC in the SEQUENCER while a rhythm plays: its bar ends first, then pattern pat plays from its top, the take into
 * track trk */
static void dmp_rec_in(uint32_t pat, uint32_t trk)
{
    uint32_t bt = sqp_bar_ticks();
    if (!dmp.on) {
        sqp_rec_start(pat, trk);
        return;
    }
    dmp.rin = (uint8_t)(pat + 1u);
    dmp.rtrk = (uint8_t)trk;
    dmp.rbars = (uint8_t)(bt - dmp.t < SQ_PPQN ? 2u : 1u);
}
static void dmp_next_bar(void)
{
    const dm_rhythm_t *y;
    dmp.t = 0;
    dmp.nh = 0;
    if (dmp.rin && !--dmp.rbars) {               /* (REC in: the pattern takes over at this bar line) */
        uint32_t pat = dmp.rin - 1u, trk = dmp.rtrk;
        dmp_stop();
        sqp_begin(pat, 0, 0);
        sqp_rec_set(trk);
        return;
    }
    if (dmp.next != 0xFFu) {
        dmp.r = dmp.next;
        dmp.kit = dmp.nkit;
        dmp.swing = dmp.nswing;
        dmp.next = 0xFF;
        dmp.bar = 0;
        dmp.nbar = 0;
        if (DM_RHY[dmp.r % DM_N].beats != cfm.beats) {   /* (a rhythm of another TIME SIG: it comes with it, here at
                                                  * the bar line; the UI takes it into cs.beats, cf_dmui.c dm_poll) */
            cfm.beats = DM_RHY[dmp.r % DM_N].beats;
            sq_beats = cfm.beats;
        }
        return;
    }
    y = &DM_RHY[dmp.r % DM_N];
    dmp.nbar++;
    dmp.bar = (uint8_t)(dmp.nbar % (y->bars ? y->bars : 1u));
}
static void dmp_tick1(void)
{
    sqp_drum_offs();
    if (++dmp.t >= sqp_bar_ticks())
        dmp_next_bar();
    if (dmp.on)
        dmp_now();
}
static void dmp_tick(uint32_t n, uint32_t pulses)   /* every audio block, as sqp_tick */
{
    uint32_t k;
    if (!dmp.on)
        return;
    if (cfx_in.active) {
        for (k = 0; k < pulses * (SQ_PPQN / 24u) && dmp.on; k++)
            dmp_tick1();
        return;
    }
    dmp.acc += n * cfm.bpm * SQ_PPQN;
    if (dmp.acc < FS * 60u)
        return;
    dmp.acc -= FS * 60u;
    dmp_tick1();
}
/* PLAY on a rhythm, or SELECT in the drum machine while one plays: the next comes in at the bar line */
static void dmp_queue(uint32_t r, uint32_t kit, uint32_t swing)
{
    if (!dmp.on) {
        dmp_start(r, kit, swing);
        return;
    }
    if (r == dmp.r) {                            /* (back to the one playing: it plays on) */
        dmp.next = 0xFF;
        dmp.kit = (uint8_t)kit;
        dmp.swing = (uint8_t)swing;
        return;
    }
    dmp.next = (uint8_t)r;
    dmp.nkit = (uint8_t)kit;
    dmp.nswing = (uint8_t)swing;
}
/* REC under a rhythm playing (on HOME, the drum machine): the new pattern, its DRUMS this rhythm from the bar playing,
 * takes over at once where the bar is, its take into CHORDS (8 bars at most, as from HOME) */
static void dmp_take_over(uint32_t pat)
{
    uint32_t t = dmp.on ? dmp.t : 0u, acc = dmp.acc, bt = sqp_bar_ticks();
    int run = dmp.on;
    dmp.on = 0;
    dmp.next = 0xFF;
    dmp.rin = 0;
    if (!run) {                                  /* (stopped meanwhile: a count-in, as from silence) */
        sqp_rec_start(pat, SQ_CHORDS);
        sqp.rlim = SQ_BARS;
        return;
    }
    sqp_setup(pat, 0);                           /* (the beat goes on where it is) */
    sqp_rec_set(SQ_CHORDS);
    sqp.rlim = SQ_BARS;
    sqp.t = t < bt ? t : 0u;
    sqp.acc = acc;
    while (sqp.nu < SQ_U && sqp.nu * bt / SQ_U < sqp.t)   /* (what lay before is not struck now) */
        sqp.nu++;
    while (sqp.nh < sq_dsteps() && sqp_drum_tick(&sq_st.pat[pat % SQ_NPAT], sqp.nh) < sqp.t)
        sqp.nh++;
    cfm.running = 1;
    sqp_now();
}
