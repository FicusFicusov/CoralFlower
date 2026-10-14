/* flora: the living plant or coral at the centre of CoralFlower's screen.
 *
 * One organism grows up from the bottom edge of its picture. Two species, chosen by the chord algorithm:
 *   - FL_SP_PLANT (the Top algorithms): a flowering plant. A leafy stem; the crown flower is the root, it has one
 *     petal per chord tone; every other chord tone is a flowering side shoot.
 *   - FL_SP_CORAL (the Side algorithms): a branching coral. A short foot; the crown branch is the root; every other
 *     chord tone is a branch that forks, with polyps on its tips.
 * In both, a tone's compass direction is its pitch class (seen from above, the organism is the chord drawn on the
 * pitch-class clock), low notes branch low, and the tone's role decides the organ: 3rd = an open flower or a wide
 * fan, 5th = a leafy shoot or a sturdy fork, 7th = a nodding bell or an uneven fork, 9th = a spray of florets or open
 * polyps, 11th = a tendril or a whip, 6th/13th = berries or bulbs, altered tones = thorns or knobs. The chord's
 * quality shapes the whole: major opens, minor closes and rises, diminished stays tight, augmented spreads.
 * Common tones of two chords keep their branches, changed tones wither and regrow.
 * Sound moves it: notes send light up their branch and sparkle out of its flowers or polyps, loudness opens them,
 * and the motes, pollen or bubbles, always rise from the organism itself.
 * Integer maths only (Q8 positions, Q14 trigonometry), no allocation, deterministic for a given seed.
 */
#ifndef FLORA_H
#define FLORA_H

#include <stdint.h>
#include "flora_canvas.h"

enum {
    FL_Q_NONE = 0, /* nothing held: the organism keeps its shape and rests */
    FL_Q_MAJOR,
    FL_Q_MINOR,
    FL_Q_DIM,
    FL_Q_AUG,
    FL_Q_SUS2,
    FL_Q_SUS4,
    FL_Q_SINGLE    /* single notes (chord type Off / none) */
};

enum { FL_SP_PLANT = 0, FL_SP_CORAL = 1 };

/* what is behind the organism:
 *   SCENE     the plant: a dusk sky, twinkling stars, a crescent moon, far hills; the coral: deep water with soft
 *             out-of-focus lights drifting by
 *   RIPPLES   a dusk sky / water; every chord sends a ring of sound out of the organism, one now and then when quiet
 *   NONE      plain dark */
enum { FL_BD_SCENE = 0, FL_BD_RIPPLES, FL_BD_NONE, FL_NBD };

typedef struct {
    uint8_t n;       /* number of chord tones, 0 = nothing held */
    uint8_t note[8]; /* MIDI notes, low to high as voiced */
    uint8_t root;    /* root pitch class 0..11 */
    uint8_t quality; /* FL_Q_* */
    int8_t bass;     /* MIDI note of the bass part, or -1 (kept for the screens; the organism has no roots) */
} fl_chord_t;

#define FL_MAX_SEG 192
#define FL_MAX_PART 48

typedef struct {
    uint16_t key;      /* stable identity: species << 15 | slot << 11 | kind << 7 | index */
    int16_t parent;    /* segment index, -1 = grows from the base */
    uint8_t kind;      /* FL_K_* */
    uint8_t ramp;      /* palette ramp of the organ */
    uint8_t alive;     /* 1 = part of the current body, 0 = withering */
    uint8_t depth;     /* number of ancestors */
    uint8_t slot;      /* pitch class 0..11, or FL_SLOT_* */
    uint8_t grow;      /* 0..255 */
    uint16_t delay;    /* ms before it starts growing */
    int16_t az, el;    /* direction now (1024 per turn; el 0 = level, 256 = up) */
    int16_t az_t, el_t;
    int16_t len, len_t; /* Q8 px */
    uint8_t w, w_t;    /* Q4 px */
    int8_t roll;       /* blade roll of leaves and petals; the style of a polyp */
    uint8_t pz;        /* the note pulse it last sparkled for */
    int32_t p0[3], p1[3]; /* world positions this frame, Q8 */
    int32_t dist;      /* path length from the base to p0, Q8 */
    int16_t sx, sy;    /* last drawn tip, canvas px */
} fl_seg_t;

typedef struct {
    int32_t x, y;   /* canvas Q8 */
    int16_t vx, vy; /* Q8 px per 64 ms */
    uint16_t life;  /* ms left */
    uint8_t col, kind;
} fl_part_t;

typedef struct {
    fl_seg_t seg[FL_MAX_SEG];
    uint8_t order[FL_MAX_SEG];
    uint16_t nseg;
    fl_part_t part[FL_MAX_PART];
    fl_chord_t chord;    /* what is held now (n = 0: nothing) */
    fl_chord_t shape;    /* the chord the body was grown from */
    uint8_t species;     /* FL_SP_* */
    uint8_t backdrop;    /* FL_BD_* (set it directly) */
    uint8_t home;        /* the key's tonic, pitch class: the coral's colour */
    uint32_t t;          /* ms */
    uint32_t rng;
    int32_t yaw;         /* Q8 angle units */
    int32_t level;       /* smoothed loudness 0..65535 */
    int32_t level_in;
    uint32_t pulse_t[12];
    uint8_t pulse_on[12];
    uint8_t pulse_n[12];
    uint32_t ring_t[4];  /* when the last chords sent their rings out (RIPPLES), 0 = none */
    uint8_t ring_i;
    uint8_t key_ramp;    /* the blooms' and the polyps' colour, from the chord's root */
    uint8_t bloom;       /* 0: key_ramp follows the root; else it is FL_R_* + 1 (the intro's pink) */
    uint8_t eye_ramp;    /* the flowers' centres */
    uint8_t body_ramp;   /* the coral's colour, from the key */
    int16_t cx, cy;      /* canvas position of the base, px */
    uint16_t part_acc;
} flora_t;

void flora_init(flora_t *f, uint32_t seed, int cx, int cy);
void flora_set_species(flora_t *f, int species);    /* FL_SP_*: the old body withers, the new one grows */
void flora_set_key(flora_t *f, int tonic_pc);      /* the coral takes its colour from the key */
void flora_set_chord(flora_t *f, const fl_chord_t *c);
void flora_note_on(flora_t *f, uint8_t note, uint8_t vel);
void flora_set_level(flora_t *f, uint16_t level); /* master loudness, 0..65535 */
void flora_tick(flora_t *f, uint32_t dt_ms);
void flora_render(flora_t *f, flc_t *c);

#endif
