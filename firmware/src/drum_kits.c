/* SPDX-License-Identifier: GPL-3.0-only
 * CoralFlower: the second drum engine's kits (drum_synth.c), voiced by ear and named by the owner. Each kit keeps its
 * own balance of drums; only its overall loudness is matched to X Club's (a shared pattern's).
 * Written by a script from the kits' data (the developer's kits.py, levels.json): edit there, or here by hand.
 * VARY (each kit's last number) is the owner's pick by ear from A/B pairs (2026-10-27): every kit's hits a little
 * different, but X Tech's all the same, a machine's
 *
 * A row: wave, noise, filter, teeth, route, pitch (p16), metal pitch (p16), ratio (Q12), then BEND BTAU ODEC O2 OLEV
 * CUT RES NDEC NLEV FM GAP CLICK DRIVE LEVEL (0..127; LEVEL 0.5 dB a step, 96 as is). Times (BTAU ODEC NDEC): 0.3 ms x
 * 2^(v / 10), so 40 is 4.8 ms, 60 19 ms, 80 77 ms, 90 154 ms, 100 307 ms, 110 614 ms. CUT: 60 is 0.6 kHz, 70 1 kHz,
 * 80 1.6 kHz, 90 2.6 kHz, 100 4.3 kHz, 110 7.1 kHz, 120 11.6 kHz. Pitches: 491 49 Hz, 528 55 Hz, 720 110 Hz, 864
 * 180 Hz, 894 205 Hz, 1139 500 Hz. B is the harder hit: more bend, click, noise and drive, 2 dB louder */

#define DS(w, n, f, t, r, p, mp, ra, bend, btau, odec, o2, olev, cut, res, ndec, nlev, fm, gap, click, drive, level) \
    {w, n, f, t, r, p, mp, ra, bend, btau, odec, o2, olev, cut, res, ndec, nlev, fm, gap, click, drive, level}

static const ds_kit_t DS_KITS[] = {
    {"X Club",                                  /* club 909: a punchy kick, a bright snare, metallic cymbals */
     {
        DS(DSW_ADD, DSN_WHITE, DSF_LP, 0, 0, 528, 0, 12288, 80, 58, 84, 50, 127, 95, 10, 30, 30, 0, 0, 70, 70, 96),
        DS(DSW_ADD, DSN_WHITE, DSF_BP, 0, 0, 864, 0, 7496, 12, 50, 66, 90, 110, 106, 0, 76, 110, 0, 0, 40, 20, 101),
        DS(DSW_SINE, DSN_WHITE, DSF_BP, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 82, 40, 82, 115, 0, 40, 0, 15, 106),
        DS(DSW_SINE, DSN_FM, DSF_HP, 0, 0, 0, 1383, 0, 0, 0, 0, 0, 0, 104, 20, 55, 110, 70, 20, 50, 0, 96),
        DS(DSW_SINE, DSN_FM, DSF_HP, 0, 0, 0, 1383, 0, 0, 0, 0, 0, 0, 100, 20, 88, 105, 70, 25, 40, 0, 78),
        DS(DSW_SINE, DSN_WHITE, DSF_LP, 0, 0, 720, 0, 0, 28, 70, 90, 0, 127, 85, 10, 40, 25, 0, 0, 25, 15, 90),
        DS(DSW_ADD, DSN_WHITE, DSF_BP, 0, 0, 1139, 0, 13312, 4, 30, 38, 120, 120, 106, 30, 30, 50, 0, 0, 30, 80, 104),
        DS(DSW_SQ2, DSN_NONE, DSF_BP, 0, 1, 1157, 0, 6062, 0, 0, 80, 127, 110, 70, 60, 0, 0, 0, 0, 0, 0, 102),
        DS(DSW_SINE, DSN_FM, DSF_HP, 0, 0, 0, 1187, 0, 0, 0, 0, 0, 0, 80, 0, 112, 110, 80, 35, 40, 0, 78),
     },
     {
        DS(DSW_ADD, DSN_WHITE, DSF_LP, 0, 0, 528, 0, 12288, 90, 62, 86, 60, 127, 100, 10, 33, 40, 0, 0, 90, 85, 100),
        DS(DSW_ADD, DSN_WHITE, DSF_BP, 0, 0, 864, 0, 7496, 16, 50, 68, 100, 118, 109, 0, 79, 122, 0, 0, 60, 30, 105),
        DS(DSW_SINE, DSN_WHITE, DSF_BP, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 84, 40, 85, 125, 0, 38, 0, 25, 110),
        DS(DSW_SINE, DSN_FM, DSF_HP, 0, 0, 0, 1383, 0, 0, 0, 0, 0, 0, 106, 20, 58, 123, 78, 20, 65, 0, 100),
        DS(DSW_SINE, DSN_FM, DSF_HP, 0, 0, 0, 1383, 0, 0, 0, 0, 0, 0, 102, 20, 91, 117, 78, 25, 55, 0, 82),
        DS(DSW_SINE, DSN_WHITE, DSF_LP, 0, 0, 720, 0, 0, 34, 70, 92, 0, 127, 88, 10, 40, 35, 0, 0, 35, 15, 94),
        DS(DSW_ADD, DSN_WHITE, DSF_BP, 0, 0, 1139, 0, 13312, 4, 30, 40, 120, 120, 108, 30, 30, 60, 0, 0, 40, 95, 108),
        DS(DSW_SQ2, DSN_NONE, DSF_BP, 0, 1, 1157, 0, 6062, 0, 0, 82, 127, 120, 72, 60, 0, 0, 0, 0, 0, 0, 106),
        DS(DSW_SINE, DSN_FM, DSF_HP, 0, 0, 0, 1187, 0, 0, 0, 0, 0, 0, 82, 0, 115, 120, 88, 35, 50, 0, 82),
     },
     32, 40, 1, 0, 0, 0},
    {"X Deep",                                  /* deep 808: a long sub boom, a two-tone snare, square-wave cymbals */
     {
        DS(DSW_ADD, DSN_NONE, DSF_LP, 0, 0, 485, 0, 12288, 24, 62, 106, 35, 127, 64, 0, 0, 0, 0, 0, 15, 45, 87),
        DS(DSW_ADD, DSN_WHITE, DSF_HP, 0, 0, 934, 0, 8192, 4, 50, 70, 90, 115, 102, 10, 70, 90, 0, 0, 10, 0, 85),
        DS(DSW_SINE, DSN_WHITE, DSF_BP, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 72, 70, 94, 120, 0, 30, 0, 0, 89),
        DS(DSW_SINE, DSN_METAL, DSF_HP, 0, 0, 0, 894, 0, 0, 0, 0, 0, 0, 108, 30, 50, 110, 0, 8, 20, 0, 98),
        DS(DSW_SINE, DSN_METAL, DSF_HP, 0, 0, 0, 894, 0, 0, 0, 0, 0, 0, 106, 30, 92, 105, 0, 8, 15, 0, 82),
        DS(DSW_SINE, DSN_NONE, DSF_LP, 0, 0, 670, 0, 0, 10, 85, 96, 0, 127, 64, 0, 0, 0, 0, 0, 5, 0, 79),
        DS(DSW_ADD, DSN_WHITE, DSF_HP, 0, 0, 1113, 0, 15319, 0, 0, 32, 110, 120, 104, 0, 30, 25, 0, 0, 0, 50, 92),
        DS(DSW_SQ2, DSN_NONE, DSF_BP, 0, 1, 1157, 0, 6062, 0, 0, 86, 127, 110, 72, 70, 0, 0, 0, 0, 0, 0, 85),
        DS(DSW_SINE, DSN_METAL, DSF_HP, 0, 0, 0, 894, 0, 0, 0, 0, 0, 0, 100, 20, 116, 110, 0, 12, 15, 0, 81),
     },
     {
        DS(DSW_ADD, DSN_NONE, DSF_LP, 0, 0, 485, 0, 12288, 34, 64, 108, 35, 127, 64, 0, 0, 0, 0, 0, 35, 57, 91),
        DS(DSW_ADD, DSN_WHITE, DSF_HP, 0, 0, 934, 0, 8192, 8, 50, 72, 100, 123, 105, 10, 73, 102, 0, 0, 30, 10, 89),
        DS(DSW_SINE, DSN_WHITE, DSF_BP, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 74, 70, 97, 127, 0, 28, 0, 10, 93),
        DS(DSW_SINE, DSN_METAL, DSF_HP, 0, 0, 0, 894, 0, 0, 0, 0, 0, 0, 110, 30, 53, 123, 0, 8, 35, 0, 102),
        DS(DSW_SINE, DSN_METAL, DSF_HP, 0, 0, 0, 894, 0, 0, 0, 0, 0, 0, 108, 30, 95, 117, 0, 8, 30, 0, 86),
        DS(DSW_SINE, DSN_NONE, DSF_LP, 0, 0, 670, 0, 0, 16, 85, 98, 0, 127, 64, 0, 0, 0, 0, 0, 15, 0, 83),
        DS(DSW_ADD, DSN_WHITE, DSF_HP, 0, 0, 1113, 0, 15319, 0, 0, 34, 110, 120, 106, 0, 30, 35, 0, 0, 0, 65, 96),
        DS(DSW_SQ2, DSN_NONE, DSF_BP, 0, 1, 1157, 0, 6062, 0, 0, 88, 127, 120, 74, 70, 0, 0, 0, 0, 0, 0, 89),
        DS(DSW_SINE, DSN_METAL, DSF_HP, 0, 0, 0, 894, 0, 0, 0, 0, 0, 0, 102, 20, 119, 120, 0, 12, 25, 0, 85),
     },
     24, 30, 1, 0, 0, 0},
    {"X Lo-Fi",                                 /* lo-fi: dusty and dull, short, soft cymbals, an old sampler's grit */
     {
        DS(DSW_ADD, DSN_WHITE, DSF_LP, 0, 0, 552, 0, 12288, 30, 55, 80, 35, 127, 70, 0, 40, 15, 0, 0, 0, 70, 98),
        DS(DSW_ADD, DSN_WHITE, DSF_LP, 0, 0, 912, 0, 6554, 6, 50, 72, 70, 115, 93, 20, 74, 100, 0, 0, 0, 50, 92),
        DS(DSW_SINE, DSN_WHITE, DSF_BP, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 68, 30, 74, 110, 0, 55, 0, 40, 108),
        DS(DSW_SINE, DSN_RING, DSF_BP, 0, 0, 0, 1270, 0, 0, 0, 0, 0, 0, 102, 20, 55, 110, 0, 16, 15, 30, 90),
        DS(DSW_SINE, DSN_RING, DSF_BP, 0, 0, 0, 1270, 0, 0, 0, 0, 0, 0, 100, 20, 80, 105, 0, 16, 0, 30, 86),
        DS(DSW_SINE, DSN_WHITE, DSF_LP, 0, 0, 740, 0, 0, 15, 70, 82, 0, 127, 75, 0, 40, 20, 0, 0, 0, 40, 89),
        DS(DSW_ADD, DSN_WHITE, DSF_BP, 0, 0, 1139, 0, 11878, 0, 0, 34, 90, 120, 90, 0, 30, 30, 0, 0, 0, 70, 98),
        DS(DSW_SQ2, DSN_NONE, DSF_BP, 0, 1, 1157, 0, 6062, 0, 0, 72, 127, 110, 64, 40, 0, 0, 0, 0, 0, 30, 101),
        DS(DSW_SINE, DSN_RING, DSF_BP, 0, 0, 0, 1270, 0, 0, 0, 0, 0, 0, 97, 10, 100, 110, 0, 16, 0, 30, 86),
     },
     {
        DS(DSW_ADD, DSN_WHITE, DSF_LP, 0, 0, 552, 0, 12288, 40, 57, 82, 35, 127, 70, 0, 40, 25, 0, 0, 0, 82, 102),
        DS(DSW_ADD, DSN_WHITE, DSF_LP, 0, 0, 912, 0, 6554, 10, 50, 74, 80, 123, 96, 20, 77, 112, 0, 0, 0, 60, 96),
        DS(DSW_SINE, DSN_WHITE, DSF_BP, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 70, 30, 77, 120, 0, 53, 0, 50, 112),
        DS(DSW_SINE, DSN_RING, DSF_BP, 0, 0, 0, 1270, 0, 0, 0, 0, 0, 0, 104, 20, 58, 123, 0, 16, 30, 30, 94),
        DS(DSW_SINE, DSN_RING, DSF_BP, 0, 0, 0, 1270, 0, 0, 0, 0, 0, 0, 102, 20, 83, 117, 0, 16, 0, 30, 90),
        DS(DSW_SINE, DSN_WHITE, DSF_LP, 0, 0, 740, 0, 0, 21, 70, 84, 0, 127, 75, 0, 40, 30, 0, 0, 0, 40, 93),
        DS(DSW_ADD, DSN_WHITE, DSF_BP, 0, 0, 1139, 0, 11878, 0, 0, 36, 90, 120, 92, 0, 30, 40, 0, 0, 0, 85, 102),
        DS(DSW_SQ2, DSN_NONE, DSF_BP, 0, 1, 1157, 0, 6062, 0, 0, 74, 127, 120, 66, 40, 0, 0, 0, 0, 0, 30, 105),
        DS(DSW_SINE, DSN_RING, DSF_BP, 0, 0, 0, 1270, 0, 0, 0, 0, 0, 0, 99, 10, 103, 120, 0, 16, 0, 30, 90),
     },
     40, 50, 3, 8, 0, 0},
    {"X Live",                                  /* acoustic-like: a beater kick, wired snare, natural toms, a room */
     {
        DS(DSW_ADD, DSN_WHITE, DSF_LP, 0, 0, 574, 0, 6144, 18, 50, 74, 40, 127, 92, 0, 44, 60, 0, 0, 110, 0, 106),
        DS(DSW_ADD, DSN_WHITE, DSF_BP, 0, 0, 885, 0, 6513, 6, 45, 70, 70, 105, 104, 0, 86, 120, 0, 0, 90, 10, 88),
        DS(DSW_SINE, DSN_WHITE, DSF_BP, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 86, 20, 66, 110, 0, 75, 0, 0, 117),
        DS(DSW_SINE, DSN_RING, DSF_BP, 0, 0, 0, 1300, 0, 0, 0, 0, 0, 0, 104, 10, 60, 110, 40, 12, 50, 0, 97),
        DS(DSW_SINE, DSN_RING, DSF_BP, 0, 0, 0, 1300, 0, 0, 0, 0, 0, 0, 102, 10, 96, 105, 40, 12, 40, 0, 88),
        DS(DSW_ADD, DSN_WHITE, DSF_LP, 0, 0, 700, 0, 6513, 8, 85, 92, 45, 127, 90, 0, 40, 35, 0, 0, 60, 0, 99),
        DS(DSW_ADD, DSN_WHITE, DSF_BP, 0, 0, 1050, 0, 10650, 0, 0, 40, 80, 120, 100, 0, 36, 60, 0, 0, 70, 20, 84),
        DS(DSW_FM, DSN_NONE, DSF_LP, 0, 0, 1170, 0, 9889, 0, 0, 98, 90, 110, 64, 0, 0, 0, 0, 0, 0, 0, 67),
        DS(DSW_SINE, DSN_RING, DSF_BP, 0, 0, 0, 1270, 0, 0, 0, 0, 0, 0, 100, 10, 118, 110, 40, 12, 40, 0, 97),
     },
     {
        DS(DSW_ADD, DSN_WHITE, DSF_LP, 0, 0, 574, 0, 6144, 28, 52, 76, 40, 127, 92, 0, 44, 70, 0, 0, 127, 12, 110),
        DS(DSW_ADD, DSN_WHITE, DSF_BP, 0, 0, 885, 0, 6513, 10, 45, 72, 80, 113, 107, 0, 89, 127, 0, 0, 110, 20, 92),
        DS(DSW_SINE, DSN_WHITE, DSF_BP, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 88, 20, 69, 120, 0, 73, 0, 10, 121),
        DS(DSW_SINE, DSN_RING, DSF_BP, 0, 0, 0, 1300, 0, 0, 0, 0, 0, 0, 106, 10, 63, 123, 48, 12, 65, 0, 101),
        DS(DSW_SINE, DSN_RING, DSF_BP, 0, 0, 0, 1300, 0, 0, 0, 0, 0, 0, 104, 10, 99, 117, 48, 12, 55, 0, 92),
        DS(DSW_ADD, DSN_WHITE, DSF_LP, 0, 0, 700, 0, 6513, 14, 85, 94, 45, 127, 90, 0, 40, 45, 0, 0, 70, 0, 103),
        DS(DSW_ADD, DSN_WHITE, DSF_BP, 0, 0, 1050, 0, 10650, 0, 0, 42, 80, 120, 102, 0, 36, 70, 0, 0, 80, 35, 88),
        DS(DSW_FM, DSN_NONE, DSF_LP, 0, 0, 1170, 0, 9889, 0, 0, 100, 90, 120, 66, 0, 0, 0, 0, 0, 0, 0, 71),
        DS(DSW_SINE, DSN_RING, DSF_BP, 0, 0, 0, 1270, 0, 0, 0, 0, 0, 0, 102, 10, 121, 120, 48, 12, 50, 0, 101),
     },
     50, 60, 1, 0, 20, 94},
    {"X Tech",                                  /* raw techno: a hard distorted kick, a ringing clap, metallic hats */
     {
        DS(DSW_ADD, DSN_WHITE, DSF_LP, 0, 0, 502, 0, 12288, 112, 42, 80, 60, 127, 100, 0, 34, 50, 0, 0, 110, 127, 93),
        DS(DSW_ADD, DSN_WHITE, DSF_BP, 0, 0, 960, 0, 7496, 10, 50, 58, 60, 90, 96, 40, 72, 125, 0, 0, 60, 70, 90),
        DS(DSW_SINE, DSN_WHITE, DSF_BP, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 82, 80, 96, 120, 0, 26, 0, 70, 80),
        DS(DSW_SINE, DSN_FM, DSF_HP, 0, 0, 0, 1550, 0, 0, 0, 0, 0, 0, 110, 60, 44, 110, 40, 10, 30, 50, 87),
        DS(DSW_SINE, DSN_FM, DSF_HP, 0, 0, 0, 1550, 0, 0, 0, 0, 0, 0, 108, 60, 90, 105, 40, 15, 0, 50, 61),
        DS(DSW_FM, DSN_NONE, DSF_LP, 0, 0, 650, 0, 6144, 40, 55, 80, 80, 127, 64, 0, 0, 0, 0, 0, 0, 80, 82),
        DS(DSW_ADD, DSN_WHITE, DSF_BP, 0, 0, 1331, 0, 9830, 0, 0, 32, 100, 120, 104, 40, 30, 50, 0, 0, 0, 120, 105),
        DS(DSW_FM, DSN_NONE, DSF_LP, 0, 0, 1190, 0, 14336, 0, 0, 78, 110, 110, 64, 0, 0, 0, 0, 0, 0, 60, 69),
        DS(DSW_SINE, DSN_FM, DSF_HP, 0, 0, 0, 1443, 0, 0, 0, 0, 0, 0, 92, 30, 102, 110, 70, 15, 30, 60, 68),
     },
     {
        DS(DSW_ADD, DSN_WHITE, DSF_LP, 0, 0, 502, 0, 12288, 122, 44, 82, 60, 127, 100, 0, 34, 60, 0, 0, 127, 127, 97),
        DS(DSW_ADD, DSN_WHITE, DSF_BP, 0, 0, 960, 0, 7496, 14, 50, 60, 70, 98, 99, 40, 75, 127, 0, 0, 80, 80, 94),
        DS(DSW_SINE, DSN_WHITE, DSF_BP, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 84, 80, 99, 127, 0, 24, 0, 80, 84),
        DS(DSW_SINE, DSN_FM, DSF_HP, 0, 0, 0, 1550, 0, 0, 0, 0, 0, 0, 112, 60, 47, 123, 48, 10, 45, 50, 91),
        DS(DSW_SINE, DSN_FM, DSF_HP, 0, 0, 0, 1550, 0, 0, 0, 0, 0, 0, 110, 60, 93, 117, 48, 15, 0, 50, 65),
        DS(DSW_FM, DSN_NONE, DSF_LP, 0, 0, 650, 0, 6144, 46, 55, 82, 80, 127, 64, 0, 0, 0, 0, 0, 0, 80, 86),
        DS(DSW_ADD, DSN_WHITE, DSF_BP, 0, 0, 1331, 0, 9830, 0, 0, 34, 100, 120, 106, 40, 30, 60, 0, 0, 0, 127, 109),
        DS(DSW_FM, DSN_NONE, DSF_LP, 0, 0, 1190, 0, 14336, 0, 0, 80, 110, 120, 66, 0, 0, 0, 0, 0, 0, 60, 73),
        DS(DSW_SINE, DSN_FM, DSF_HP, 0, 0, 0, 1443, 0, 0, 0, 0, 0, 0, 94, 30, 105, 120, 78, 15, 40, 60, 72),
     },
     64, 0, 1, 0, 0, 0},
    {"X Break",                                 /* breaks: a punchy kick, a fat crunchy snare, soft cymbals, a room */
     {
        DS(DSW_ADD, DSN_WHITE, DSF_LP, 0, 0, 595, 0, 8192, 40, 50, 78, 60, 127, 94, 0, 40, 50, 0, 0, 90, 90, 96),
        DS(DSW_ADD, DSN_WHITE, DSF_BP, 0, 0, 874, 0, 6554, 8, 50, 74, 100, 120, 98, 10, 82, 125, 0, 0, 90, 90, 86),
        DS(DSW_SINE, DSN_WHITE, DSF_BP, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 82, 30, 76, 115, 0, 45, 0, 50, 101),
        DS(DSW_SINE, DSN_RING, DSF_BP, 0, 0, 0, 1300, 0, 0, 0, 0, 0, 0, 103, 20, 62, 110, 30, 12, 40, 40, 91),
        DS(DSW_SINE, DSN_RING, DSF_BP, 0, 0, 0, 1300, 0, 0, 0, 0, 0, 0, 101, 20, 90, 105, 30, 12, 30, 40, 82),
        DS(DSW_SINE, DSN_WHITE, DSF_LP, 0, 0, 720, 0, 0, 20, 70, 88, 0, 127, 88, 0, 40, 35, 0, 0, 0, 60, 86),
        DS(DSW_ADD, DSN_WHITE, DSF_BP, 0, 0, 1127, 0, 13312, 0, 0, 38, 110, 120, 104, 30, 30, 50, 0, 0, 0, 80, 79),
        DS(DSW_SQ2, DSN_NONE, DSF_BP, 0, 1, 1157, 0, 6062, 0, 0, 80, 127, 110, 74, 50, 0, 0, 0, 0, 0, 0, 98),
        DS(DSW_SINE, DSN_RING, DSF_BP, 0, 0, 0, 1270, 0, 0, 0, 0, 0, 0, 98, 10, 112, 110, 30, 12, 30, 30, 90),
     },
     {
        DS(DSW_ADD, DSN_WHITE, DSF_LP, 0, 0, 595, 0, 8192, 50, 52, 80, 60, 127, 94, 0, 40, 60, 0, 0, 110, 102, 100),
        DS(DSW_ADD, DSN_WHITE, DSF_BP, 0, 0, 874, 0, 6554, 12, 50, 76, 110, 127, 101, 10, 85, 127, 0, 0, 110, 100, 90),
        DS(DSW_SINE, DSN_WHITE, DSF_BP, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 84, 30, 79, 125, 0, 43, 0, 60, 105),
        DS(DSW_SINE, DSN_RING, DSF_BP, 0, 0, 0, 1300, 0, 0, 0, 0, 0, 0, 105, 20, 65, 123, 38, 12, 55, 40, 95),
        DS(DSW_SINE, DSN_RING, DSF_BP, 0, 0, 0, 1300, 0, 0, 0, 0, 0, 0, 103, 20, 93, 117, 38, 12, 45, 40, 86),
        DS(DSW_SINE, DSN_WHITE, DSF_LP, 0, 0, 720, 0, 0, 26, 70, 90, 0, 127, 88, 0, 40, 45, 0, 0, 0, 60, 90),
        DS(DSW_ADD, DSN_WHITE, DSF_BP, 0, 0, 1127, 0, 13312, 0, 0, 40, 110, 120, 106, 30, 30, 60, 0, 0, 0, 95, 83),
        DS(DSW_SQ2, DSN_NONE, DSF_BP, 0, 1, 1157, 0, 6062, 0, 0, 82, 127, 120, 76, 50, 0, 0, 0, 0, 0, 0, 102),
        DS(DSW_SINE, DSN_RING, DSF_BP, 0, 0, 0, 1270, 0, 0, 0, 0, 0, 0, 100, 10, 115, 120, 38, 12, 40, 30, 94),
     },
     48, 55, 2, 12, 15, 88},
};
#define DS_NKIT ((uint32_t)(sizeof DS_KITS / sizeof DS_KITS[0]))
