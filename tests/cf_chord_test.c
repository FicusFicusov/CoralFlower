/* CoralFlower: tests of cf_chord.c (the four algorithms, combining, Lock, Quantize, Off and no button: single
 * notes, inversion, bass degree).
 *   cc -std=c99 -Wall -Wextra -Ifirmware/src -o build/cf_chord_test tests/cf_chord_test.c firmware/src/cf_chord.c \
 *      firmware/src/cf_theory.c
 * Keys: key k is MIDI 53 + k (F3 = 0). Upper keys 1 3 5 8 10 13 15 17 20 22 25; lower 0 2 4 6 7 9 11 12 14 ...
 */
#include <stdio.h>
#include <string.h>
#include "cf_chord.h"

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); \
    printf(__VA_ARGS__); printf("\n"); } } while (0)

static cf_chord_t C;
static cf_chord_cfg_t G;

/* the checks below were written for the first builds' Top order (Off M m | s2 s4 | M6 M7 M9 | m6 m7 | m9): they keep
 * it, so they test the engine whatever the default order is (the default's own checks are at the end) */
static const cf_form_t OLD_TOP[CF_TOP_NBTN] = {
    {CF_T_NONE, CF_OFF_ADD, 0, 0}, {CF_T_MAJ, 0, 0, 0}, {CF_T_MIN, 0, 0, 0}, {CF_T_SUS2, 0, 0, 0},
    {CF_T_SUS4, 0, 0, 0}, {CF_T_MAJ, CF_A_6, 0, 0}, {CF_T_MAJ, CF_A_7, 0, 0}, {CF_T_MAJ, CF_A_9, 0, 0},
    {CF_T_MIN, CF_A_6, 0, 0}, {CF_T_MIN, CF_A_b7, 0, 0}, {CF_T_MIN, CF_A_9, 0, 0},
};
static void setup(int algo, int q, int tonic, int scale)
{
    cf_chord_defaults(&G);
    memcpy(G.top, OLD_TOP, sizeof G.top);
    G.octave = (int8_t)-CF_OCT_BASE;             /* (the checks below are at the keys' printed pitch) */
    G.algo = (uint8_t)algo;
    G.quantize = (uint8_t)q;
    G.tonic = (uint8_t)tonic;
    G.scale = (uint8_t)scale;
    cf_chord_init(&C, &G);
}
static void down(int k) { cf_chord_key(&C, k, 1); }
static void up(int k) { cf_chord_key(&C, k, 0); }

static int notes_are(const int *want, int n)
{
    int i;
    if (C.out.n != n)
        return 0;
    for (i = 0; i < n; i++)
        if (C.out.note[i] != want[i])
            return 0;
    return 1;
}
static const char *dump(void)
{
    static char b[96];
    int i, k = 0;
    k += snprintf(b + k, sizeof b - (size_t)k, "[%d:", C.out.n);
    for (i = 0; i < C.out.n; i++)
        k += snprintf(b + k, sizeof b - (size_t)k, " %d", C.out.note[i]);
    snprintf(b + k, sizeof b - (size_t)k, "]");
    return b;
}
#define NOTES(msg, ...) do { static const int w_[] = {__VA_ARGS__}; \
    CHECK(notes_are(w_, (int)(sizeof w_ / sizeof w_[0])), "%s: got %s", msg, dump()); } while (0)

int main(void)
{
    int idx;

    /* key roles */
    CHECK(cf_key_role(CF_ALG_TOP, 1, &idx) == CF_ROLE_BUTTON && idx == 0, "F#3 is Off in Top");
    CHECK(cf_key_role(CF_ALG_TOP, 25, &idx) == CF_ROLE_BUTTON && idx == 10, "F#5 is button 10 in Top");
    CHECK(cf_key_role(CF_ALG_TOP, 7, &idx) == CF_ROLE_ROOT && idx == 4, "C4 is the fifth lower key");
    CHECK(cf_key_role(CF_ALG_SIDE, 5, &idx) == CF_ROLE_BUTTON && idx == 2, "A#3 is d in Side");
    CHECK(cf_key_role(CF_ALG_SIDE, 6, &idx) == CF_ROLE_BUTTON && idx == 6, "B3 is 9 in Side");
    CHECK(cf_key_role(CF_ALG_SIDE, 8, &idx) == CF_ROLE_ROOT, "C#4 is a root in Side");

    /* Top, Quantize On, C major: no button = single notes, a scale step a key (the owner's rule, 2026-10-15) */
    setup(CF_ALG_TOP, CF_Q_ON, 0, CF_SC_MAJOR);
    down(7);
    CHECK(C.out.single, "no button: single notes");
    NOTES("C alone: the note C", 60);
    up(7);
    CHECK(!C.out.sounding, "silent after release");
    down(9);
    NOTES("the next key: D", 62);
    up(9);
    down(0);
    NOTES("the first key: F3", 53);
    up(0);

    /* Quantize Off: chromatic, C on the fifth key */
    setup(CF_ALG_TOP, CF_Q_OFF, 0, CF_SC_MAJOR);
    down(0);
    NOTES("G#3", 56);
    up(0);

    /* a type held: On keeps it, Full snaps it */
    setup(CF_ALG_TOP, CF_Q_ON, 0, CF_SC_MAJOR);
    down(3);
    down(9);
    NOTES("D major with M held (On)", 62, 66, 69);
    up(9);
    up(3);
    setup(CF_ALG_TOP, CF_Q_FULL, 0, CF_SC_MAJOR);
    down(3);
    down(9);
    NOTES("D with M held in Full: Dm", 62, 65, 69);
    up(9);
    up(3);
    down(15);
    down(14);
    NOTES("Gmaj7 in Full: G7", 67, 71, 74, 77);
    up(14);
    up(15);

    /* m7, and the merge of M + M6 + M9 + m7 -> C6/9 */
    setup(CF_ALG_TOP, CF_Q_ON, 0, CF_SC_MAJOR);
    down(22);
    down(7);
    NOTES("Cm7", 60, 63, 67, 70);
    up(7);
    up(22);
    down(3);
    down(13);
    down(17);
    down(22);
    down(7);
    NOTES("M+M6+M9+m7 = C6/9", 60, 64, 67, 69, 74);
    CHECK(C.out.buttons == ((1u << 1) | (1u << 5) | (1u << 7)), "lit: M, M6, M9, not m7 (got %x)", C.out.buttons);
    up(7);
    up(3);
    up(13);
    up(17);
    up(22);
    /* a minor and a major button held: they do not join, the last pressed wins and only it is lit */
    down(5);
    down(13);
    down(7);
    NOTES("m then M6: C6", 60, 64, 67, 69);
    CHECK(C.out.buttons == (1u << 5), "lit: M6 alone (got %x)", C.out.buttons);
    up(7);
    up(13);
    CHECK(C.out.buttons == (1u << 2), "M6 let go: m lit again (got %x)", C.out.buttons);
    up(5);

    /* Off: single notes, polyphonic */
    down(1);
    down(7);
    down(11);
    CHECK(C.out.single, "Off is single notes");
    NOTES("single C and E", 60, 64);
    up(7);
    up(11);
    up(1);

    /* Lock: a tap latches, the same tap unlatches; two at once latch together */
    setup(CF_ALG_TOP_LOCK, CF_Q_ON, 0, CF_SC_MAJOR);
    down(15);
    up(15);
    down(7);
    NOTES("locked M7: Cmaj7", 60, 64, 67, 71);
    up(7);
    down(15);
    up(15);
    down(7);
    NOTES("unlocked: a single note", 60);
    up(7);
    down(3);
    down(13);
    up(3);
    up(13);
    down(7);
    NOTES("M and M6 latched together: C6", 60, 64, 67, 69);
    up(7);
    down(22);
    up(22);
    down(7);
    NOTES("a new tap replaces the latch: Cm7", 60, 63, 67, 70);
    up(7);
    /* keep (RETRIG Off): a tap under a held root latches, lights up, the chord stays; the next root takes it */
    C.keep = 1;
    down(7);
    NOTES("kept: Cm7 as latched", 60, 63, 67, 70);
    down(15);
    NOTES("M7 tapped under the held root: still Cm7", 60, 63, 67, 70);
    CHECK(C.out.buttons == 1u << 6, "the light shows the new latch, M7 (got %03x)", C.out.buttons);
    up(15);
    NOTES("M7 let go: still Cm7", 60, 63, 67, 70);
    down(9);
    NOTES("the next root (D) takes the latch: Dmaj7", 62, 66, 69, 73);
    up(9);
    NOTES("D let go, C still held: the latch taken stays, Cmaj7", 60, 64, 67, 71);
    down(1);
    up(1);
    CHECK(!C.out.single, "Off tapped under the held root: the chord stays");
    up(7);
    down(7);
    CHECK(C.out.single, "the next root: single notes (Off)");
    up(7);
    C.keep = 0;
    down(1);
    up(1);
    /* keep in Top (not Lock): a button under a root that went down with one changes nothing; held when the next
     * root goes down, it plays */
    setup(CF_ALG_TOP, CF_Q_ON, 0, CF_SC_MAJOR);
    C.keep = 1;
    down(3);
    down(7);
    NOTES("Top, C with M: C", 60, 64, 67);
    down(15);
    NOTES("Top, M7 pressed under C (M held): still C", 60, 64, 67);
    CHECK(C.out.buttons == ((1u << 1) | (1u << 6)), "Top: M and M7 lit while held (got %03x)", C.out.buttons);
    down(9);
    NOTES("Top, D down with M and M7 held: Dmaj7", 62, 66, 69, 73);
    up(15);
    NOTES("Top, M7 let go under the held roots: Dmaj7 stays", 62, 66, 69, 73);
    up(9);
    up(7);
    down(7);
    NOTES("Top, the next C (M held): C", 60, 64, 67);
    up(7);
    up(3);
    /* the exception: a root down with no button takes the buttons pressed after it at once (keep or not) */
    setup(CF_ALG_TOP, CF_Q_ON, 0, CF_SC_MAJOR);
    C.keep = 1;
    down(7);
    NOTES("Top, C alone: the note", 60);
    down(15);
    NOTES("Top, M7 under a bare C: at once, Cmaj7", 60, 64, 67, 71);
    up(15);
    NOTES("Top, M7 let go: the note C again", 60);
    up(7);
    down(15);
    down(7);
    NOTES("Top, C with M7: Cmaj7", 60, 64, 67, 71);
    down(22);
    NOTES("Top, m7 under C with M7 (not bare): waits, Cmaj7", 60, 64, 67, 71);
    up(22);
    up(15);
    up(7);
    /* keep in Side: an extension button under a held root waits too */
    setup(CF_ALG_SIDE, CF_Q_ON, 0, CF_SC_MAJOR);
    C.keep = 1;
    down(1);
    down(7 + 5);
    NOTES("Side, F4 with M: F", 65, 69, 72);
    down(4);
    NOTES("Side, 7 pressed under F (M held): still F", 65, 69, 72);
    up(4);
    up(12);
    up(1);

    /* Side: the piano, snapped; quality and extension buttons */
    setup(CF_ALG_SIDE, CF_Q_ON, 0, CF_SC_MAJOR);
    down(8);
    NOTES("C#4 snaps down to C", 60);
    up(8);
    down(4);
    down(7);
    NOTES("7 alone on C in key: Cmaj7", 60, 64, 67, 71);
    up(7);
    up(4);
    down(3);
    down(6);
    down(9);
    NOTES("m + 9 on D: Dm(add9)", 62, 65, 69, 76);
    up(9);
    up(6);
    up(3);
    setup(CF_ALG_SIDE, CF_Q_ON, 4, CF_SC_MAJOR);
    down(7);
    CHECK(C.out.root_note == 61, "C4 in E major goes up to C# (got %d)", C.out.root_note);
    up(7);
    setup(CF_ALG_SIDE, CF_Q_OFF, 0, CF_SC_MAJOR);
    down(1);
    down(5);
    down(8);
    NOTES("M + d: the latest of a tie wins (dim)", 61, 64, 67);
    up(8);
    up(5);
    up(1);

    /* sus with a third: an added 4th; sus2 + sus4 */
    setup(CF_ALG_TOP, CF_Q_OFF, 0, CF_SC_MAJOR);
    down(3);
    down(10);
    down(7);
    NOTES("M + s4: Cadd4", 60, 64, 65, 67);
    up(7);
    up(10);
    up(3);
    down(8);
    down(10);
    down(7);
    NOTES("s2 + s4", 60, 62, 65, 67);
    up(7);
    up(10);
    up(8);

    /* Auto inversion, bass degree, octave */
    setup(CF_ALG_TOP, CF_Q_ON, 0, CF_SC_MAJOR);
    G.top[1].inv = CF_INV_AUTO;
    G.top[1].bass = 1;
    cf_chord_config(&C, &G);
    down(3);
    down(7);
    NOTES("C", 60, 64, 67);
    CHECK(C.out.bass_pc == 4, "bass degree 2 of C is E (got %d)", C.out.bass_pc);
    up(7);
    down(12);
    NOTES("F after C, auto: C F A", 60, 65, 69);
    up(12);
    up(3);
    G.octave = (int8_t)(1 - CF_OCT_BASE);
    G.top[1].inv = 0;
    cf_chord_config(&C, &G);
    down(7);
    NOTES("octave up", 72);
    up(7);

    /* a chord's held root falls back to the one held before (no button: both are single notes) */
    setup(CF_ALG_TOP, CF_Q_ON, 0, CF_SC_MAJOR);
    down(7);
    down(9);
    NOTES("no button, C and D held: both notes", 60, 62);
    up(9);
    up(7);
    down(3);
    down(7);
    down(9);
    NOTES("M held, D on top: D", 62, 66, 69);
    up(9);
    NOTES("M held, back to C", 60, 64, 67);
    up(7);
    up(3);

    /* Off in the first builds' place (button 0): single notes, several at once */
    setup(CF_ALG_TOP, CF_Q_ON, 0, CF_SC_MAJOR);
    down(1);
    down(7);
    down(11);
    CHECK(C.out.single, "old order: Off (button 0) plays single notes");
    NOTES("old order: Off + C + E: both notes, no chord", 60, 64);
    up(11);
    up(7);
    up(1);

    /* the default order: M6 M7 M9 | s2 s4 | m6 m7 m9 | M m | Off, Off on the last upper key */
    cf_chord_defaults(&G);
    G.algo = CF_ALG_TOP;
    cf_chord_init(&C, &G);
    CHECK(cf_is_off(&G, 10) && !cf_is_off(&G, 0), "default: Off is the last button, the first is a chord");
    CHECK(G.top[0].type == CF_T_MAJ && G.top[0].add == CF_A_6 && G.top[8].type == CF_T_MAJ && G.top[8].add == 0 &&
          G.top[9].type == CF_T_MIN && G.top[9].add == 0, "default: M6 first, M and m before Off");
    down(25);
    down(7);
    down(11);
    down(14);
    CHECK(C.out.single, "default: Off (key 25) plays single notes");
    NOTES("default: Off + C + E + G: three notes, an octave under the printed pitch (octave 0)", 48, 52, 55);
    up(14);
    up(11);
    up(7);
    up(25);
    down(7);
    CHECK(C.out.single && C.out.n == 1, "default: a root alone plays its note (%s)", dump());
    up(7);
    down(1);
    down(7);
    CHECK(!C.out.single && C.out.n == 4, "default: M6 (key 1) under C: four notes (%s)", dump());
    up(7);
    up(1);
    cf_chord_defaults(&G);
    G.algo = CF_ALG_TOP_LOCK;
    cf_chord_init(&C, &G);
    down(25);
    up(25);
    down(7);
    down(11);
    CHECK(C.out.single && C.out.n == 2, "default, Lock: Off tapped stays on, two keys: two notes (%s)", dump());
    up(11);
    up(7);

    printf("cf_chord: %d checks, %d failures\n", checks, fails);
    return fails != 0;
}
