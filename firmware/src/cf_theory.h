/* SPDX-License-Identifier: GPL-3.0-only
 * CoralFlower: music theory - scales, spelling, chord forms, chord names and labels.
 *
 * Pure functions and const tables: no firmware state, no libc beyond <stdint.h>. Used by the chord engine (audio
 * side) and the screens (UI side); host-tested by tests/cf_theory_test.c.
 *
 * Pitch classes are 0..11 with C = 0. A chord is a root plus an interval mask: bit i = a tone i semitones above
 * the root, i = 0..23 (two octaves, so 9ths, 11ths and 13ths keep their register).
 */
#ifndef CF_THEORY_H
#define CF_THEORY_H

#include <stdint.h>

/* ------------------------------------------------------------------ scales --- */
typedef struct {
    const char *name;   /* "Natural Minor" */
    const char *shrt;   /* "MIN": 5 characters at most */
    uint16_t mask;      /* bit pc = in the scale, tonic = bit 0 */
} cf_scale_t;

enum {
    CF_SC_MAJOR = 0, CF_SC_MINOR, CF_SC_MAJ_PENT, CF_SC_MIN_PENT, CF_SC_DORIAN, CF_SC_MIXOLYDIAN, CF_SC_LYDIAN,
    CF_SC_PHRYGIAN, CF_SC_LOCRIAN, CF_SC_HARM_MINOR, CF_SC_MEL_MINOR, CF_SC_BLUES, CF_SC_MAJ_BLUES, CF_SC_HARM_MAJOR,
    CF_SC_PHRYG_DOM, CF_SC_LYD_DOM, CF_SC_ALTERED, CF_SC_WHOLE, CF_SC_DIM_WH, CF_SC_DIM_HW, CF_SC_HUNGARIAN,
    CF_SC_DOUBLE_HARM, CF_SC_HIRAJOSHI, CF_SC_IN_SEN, CF_SC_IWATO, CF_SC_KUMOI, CF_SC_CHROMATIC,
    CF_NSCALES
};
extern const cf_scale_t CF_SCALES[CF_NSCALES];

/* tones in the scale (5..12) */
int cf_scale_size(int scale);
/* the n-th tone of the scale above (n < 0: below) the tonic, as semitones from the tonic (n = size: an octave) */
int cf_scale_step(int scale, int n);
/* the scale degree index (0..size-1) of semitone offset s from the tonic, -1 when s is not in the scale */
int cf_scale_index(int scale, int s);
/* a MIDI note moved to the nearest note of the scale on tonic (pc); a tie goes up when up_on_tie */
int cf_snap(int scale, int tonic, int note, int up_on_tie);

/* --------------------------------------------------------------- spelling --- */
/* how the key spells each pitch class: a letter 0..6 (C D E F G A B) and an accidental -2..+2 */
typedef struct {
    int8_t letter[12];
    int8_t acc[12];
} cf_spell_t;
void cf_key_spell(int tonic, int scale, cf_spell_t *sp);
/* "C", "F#", "Bb", "Ebb": 4 bytes with the terminator. The sharp and flat are ASCII '#' and 'b'; screens may
 * draw them as glyphs. */
void cf_pc_name(int pc, const cf_spell_t *sp, char out[4]);
/* the spelling a key uses for its own tonic: D# major is spelled Eb major and so on */
int cf_key_prefers_flats(int tonic, int scale);
/* Roman numeral of a scale degree (0-based) with the case of the chord built on it in the scale ("I", "ii",
 * "vii\xB0" with \xB0 the diminished ring); a non-diatonic root is prefixed with b or #. 8 bytes. */
void cf_numeral(int scale, int tonic, int root_pc, char out[8]);

/* ------------------------------------------------------------ chord forms --- */
enum {                     /* the chord type: what the third and fifth are */
    CF_T_NONE = 0,         /* the root alone (plus the added degrees): "single degrees" */
    CF_T_MAJ, CF_T_MIN, CF_T_DIM, CF_T_AUG, CF_T_SUS2, CF_T_SUS4, CF_T_POWER,
    CF_NTYPES
};
enum {                     /* the added degrees (cf_form_t.add bits) */
    CF_A_6 = 1u << 0,      /* 9 semitones */
    CF_A_b7 = 1u << 1,     /* 10: a seventh, dominant or minor */
    CF_A_7 = 1u << 2,      /* 11: the major seventh */
    CF_A_9 = 1u << 3,      /* 14 */
    CF_A_b9 = 1u << 4,     /* 13 */
    CF_A_s9 = 1u << 5,     /* 15 */
    CF_A_11 = 1u << 6,     /* 17 */
    CF_A_s11 = 1u << 7,    /* 18 */
    CF_A_13 = 1u << 8,     /* 21 */
    CF_A_2 = 1u << 9,      /* 2: an added second, close */
    CF_A_4 = 1u << 10,     /* 5: an added fourth, close */
    CF_A_bb7 = 1u << 11    /* 9 in a diminished seventh chord */
};
#define CF_INV_AUTO (-1)   /* the inversion closest to the previous chord */

typedef struct {
    uint8_t type;          /* CF_T_* */
    uint16_t add;          /* CF_A_* */
    int8_t inv;            /* 0 root position, 1..3, CF_INV_AUTO */
    uint8_t bass;          /* the bass degree: 0 the root, 1 the 2nd tone of the chord, ... (counted root up) */
} cf_form_t;

/* the add-degree combinations SEL's K2 offers, in order of use */
#define CF_NADDS 19
extern const uint16_t CF_ADDS[CF_NADDS];
int cf_adds_index(uint16_t add);                  /* its position in CF_ADDS, -1 when it is not one of them */
const char *cf_adds_name(int i);                  /* "None", "7", "Maj7", "6", "Add9", "9", "Maj9", "6/9" ... */
const char *cf_type_name(int type);               /* "Major" ... */

/* the interval mask of a form (no inversion: the tones as stacked above the root) */
uint32_t cf_form_mask(const cf_form_t *f);
/* the tones of a mask, low to high, as semitones above the root; returns how many (at most 12) */
int cf_mask_tones(uint32_t mask, int8_t out[12]);
/* a mask moved into a scale: every tone snapped to the scale on tonic (the root itself stays) */
uint32_t cf_mask_snap(uint32_t mask, int root_pc, int scale, int tonic);
/* the triad (or with sevenths, as wanted) a scale builds on root_pc by stacking every other scale tone; n tones */
uint32_t cf_diatonic_mask(int scale, int tonic, int root_pc, int n);
/* the chord type a mask reads as (for names and the plant): CF_T_* */
int cf_mask_type(uint32_t mask);

/* lead-sheet name of root + mask (+ a different bass pitch class, or -1): "Cmaj9", "F#m7b5", "Bbsus4", "C6/9",
 * "Cadd9", "Am/E". 16 bytes. */
void cf_chord_name(int root_pc, uint32_t mask, int bass_pc, const cf_spell_t *sp, char out[16]);
/* the compact key-cap label of a form: at most 5 glyphs. Small (raised) digits for added degrees are written as
 * the bytes 0x10..0x19 ('0'..'9' + 0x10 - '0'); everything else is ASCII. "M", "m7", "M\x19" = Madd9. 8 bytes. */
void cf_form_label(const cf_form_t *f, char out[8]);

/* --------------------------------------------------------------- voicing --- */
/* the notes of a chord: root note (MIDI), mask, inversion 0..n-1 (each step moves the lowest tone up an octave);
 * returns how many, low to high (at most 12) */
int cf_voice(int root_note, uint32_t mask, int inv, uint8_t out[12]);
/* the inversion of mask on root_note closest to the previous chord (prev, n_prev notes): least total motion */
int cf_auto_inversion(int root_note, uint32_t mask, const uint8_t *prev, int n_prev);
/* the glide's voice matching: two sorted note sets paired along the cheapest monotone path (lowest with lowest;
 * one note may feed two: a chord that grows or shrinks). Returns the pairs (p index, q index), at most n + m - 1 */
int cf_match(const uint8_t *p, int n, const uint8_t *q, int m, uint8_t pi[24], uint8_t qj[24]);
/* the bass degree's pitch class: the deg-th tone of the mask counted from the root (wraps), -1 for an empty mask */
int cf_bass_pc(int root_pc, uint32_t mask, int deg);

#endif
