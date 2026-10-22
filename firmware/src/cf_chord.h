/* SPDX-License-Identifier: GPL-3.0-only
 * CoralFlower: the chord input model - which key is a chord button and which a root, how held buttons combine,
 * Lock, Quantize, HOLD, and the chord (or single notes) the hands ask for.
 *
 * Pure logic on a caller-owned cf_chord_t: no firmware state, no timing. The audio side feeds it key events and
 * reads cf_chord_t.out; the screens read the same tables for the key labels. Host-tested by tests/cf_chord_test.c.
 *
 * The 27 keys are numbered 0..26 from F3 (the FM-1's lowest key) chromatically: key k is MIDI 53 + k at octave 0.
 * Upper ("black") keys: F#3 G#3 A#3 | C#4 D#4 | F#4 G#4 A#4 | C#5 D#5 | F#5 (upper index 0..10).
 * Lower ("white") keys: F3 G3 A3 B3 C4 D4 E4 F4 G4 A4 B4 C5 D5 E5 F5 G5 (lower index 0..15).
 */
#ifndef CF_CHORD_H
#define CF_CHORD_H

#include <stdint.h>
#include "cf_theory.h"

#define CF_NKEYS 27
#define CF_NUPPER 11
#define CF_NLOWER 16
#define CF_TOP_NBTN 11          /* algorithm Top: every upper key; button 0 is Off */
#define CF_SIDE_NBTN 7          /* algorithm Side: upper keys 0-2, lower keys 0-3 */
#define CF_MAX_NOTES 12
#define CF_MAX_ROOTS 16
#define CF_KEY_EXT 32           /* a root from outside (0.5, NOTES IN Roots): key CF_KEY_EXT + its MIDI note, a root at
                                 * that pitch (OCT moves it; Quantize snaps it as the Side piano does) */

enum { CF_ALG_TOP = 0, CF_ALG_TOP_LOCK, CF_ALG_SIDE, CF_ALG_SIDE_LOCK, CF_NALGS };
enum { CF_Q_OFF = 0, CF_Q_BASIC, CF_Q_FULL };
enum { CF_ROLE_NONE = 0, CF_ROLE_ROOT, CF_ROLE_BUTTON };
#define CF_TYPE_DEFAULT 0xFFu   /* a merged form with no type of its own: the scale's chord, or major */

typedef struct {
    uint8_t algo;               /* CF_ALG_* */
    uint8_t quantize;           /* CF_Q_* */
    uint8_t tonic;              /* pitch class 0..11 (Key) */
    uint8_t scale;              /* CF_SC_* */
    int8_t octave;              /* OCT- / OCT+: -3..3 */
    cf_form_t top[CF_TOP_NBTN]; /* the button whose form is Off (add CF_OFF_ADD) is HOLD since 0.5 (it was single notes,
                                 * which no button gives as well): never edited; the record keeps the Off form, so
                                 * 0.4.x reads it as before */
    cf_form_t side[CF_SIDE_NBTN];
} cf_chord_cfg_t;

typedef struct {
    uint8_t sounding;           /* something is asked to sound */
    uint8_t single;             /* single notes (Off): note[] are the held roots */
    uint8_t n;
    uint8_t note[CF_MAX_NOTES]; /* the chord part's notes, low to high */
    uint8_t root_pc;            /* the chord's root (single: the last key's pitch class) */
    uint8_t root_note;          /* .. as a MIDI note */
    uint32_t mask;              /* interval mask (single: 1) */
    uint8_t type;               /* CF_T_* the mask reads as */
    int8_t bass_pc;             /* the bass degree's pitch class, -1 */
    uint8_t inv;                /* the inversion used */
    cf_form_t form;             /* the merged form (for SEL and the labels) */
    uint16_t buttons;           /* the effective chord buttons (held or locked) */
    uint8_t hold;               /* HOLD is on (its key lit) */
    uint32_t serial;            /* bumped whenever the chord changes */
} cf_chord_out_t;

typedef struct {
    cf_chord_cfg_t cfg;
    uint16_t held;              /* chord buttons physically down */
    uint16_t gesture;           /* the buttons of the press under way (Lock) */
    uint16_t locked;            /* the latched set (Lock) */
    uint8_t toggle;             /* the press under way began on the only latched button: a release unlatches */
    uint8_t keep;               /* a button pressed or let go while roots are held changes the lights only (and the
                                 * latch in Lock): the chord keeps its buttons until a root goes down (CoralFlower's
                                 * RETRIG Off, every algorithm); set it directly */
    uint16_t play;              /* .. the buttons the chord is built from (the effective ones when nothing is kept) */
    uint8_t bare;               /* .. but a root that went down with no button (no Off, no latch) takes them at once:
                                 * a single note, its chord picked after it (the owner's exception) */
    uint32_t stamp[CF_TOP_NBTN];/* press order of the buttons */
    uint32_t clock;
    uint8_t hold;               /* HOLD (0.5, the owner: "кнопку hold типа чтобы играло и можно было не держать клавиши
                                 * нижние"): the roots stay after the keys go up; a root pressed with none down
                                 * replaces them ("Заменяется новым"), one pressed with others down joins them */
    uint8_t nroots;
    uint8_t root_key[CF_MAX_ROOTS]; /* the sounding root keys, in press order (held, or kept by HOLD) */
    uint8_t root_down[CF_MAX_ROOTS];/* .. still physically down (HOLD keeps the others) */
    uint8_t prev_n;
    uint8_t prev[CF_MAX_NOTES]; /* the last chord that sounded (Auto inversion) */
    cf_chord_out_t out;
} cf_chord_t;

void cf_chord_defaults(cf_chord_cfg_t *cfg);
void cf_chord_init(cf_chord_t *c, const cf_chord_cfg_t *cfg);
/* a configuration change: held keys are kept, the chord re-resolved; a change of algorithm drops the buttons */
void cf_chord_config(cf_chord_t *c, const cf_chord_cfg_t *cfg);
/* a key went down (1) or up (0); returns 1 when the output changed. key CF_KEY_EXT + n: a root at MIDI note n */
int cf_chord_key(cf_chord_t *c, int key, int down);
/* every key up, every latch cleared */
void cf_chord_reset(cf_chord_t *c);

/* what a key is in an algorithm: CF_ROLE_*; *index = the button index, or the root's position */
int cf_key_role(int algo, int key, int *index);
int cf_key_is_upper(int key);
/* the root note key plays under cfg (whatever the buttons), -1 when it is a button */
int cf_key_root_note(const cf_chord_cfg_t *cfg, int key);
/* the form a button holds (Off: type CF_T_NONE with add 0xFFFF) */
const cf_form_t *cf_button_form(const cf_chord_cfg_t *cfg, int btn);
int cf_is_off(const cf_chord_cfg_t *cfg, int btn);   /* .. the HOLD key's (the Off form) */
#define CF_OFF_ADD 0xFFFFu
/* the octave the screen calls 0 sounds an octave under the keys' printed pitch (the owner, 2026-10-15: at the printed
 * pitch a new unit's chords screamed); OCT- / OCT+ still show -3 .. 3 around it */
#define CF_OCT_BASE (-1)

/* the merge of several held forms (pure; exposed for the tests): forms in press order, oldest first */
cf_form_t cf_merge_forms(const cf_form_t *forms, int n);

#endif
