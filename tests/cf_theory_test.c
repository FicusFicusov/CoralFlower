/* CoralFlower: tests of cf_theory.c (scales, spelling, numerals, chord names, labels, voicing).
 *   cc -std=c99 -Wall -Wextra -Ifirmware/src -o build/cf_theory_test tests/cf_theory_test.c firmware/src/cf_theory.c
 */
#include <stdio.h>
#include <string.h>
#include "cf_theory.h"

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); \
    printf(__VA_ARGS__); printf("\n"); } } while (0)

static void expect_name(int root, uint32_t mask, int bass, int tonic, int scale, const char *want)
{
    cf_spell_t sp;
    char out[16];
    cf_key_spell(tonic, scale, &sp);
    cf_chord_name(root, mask, bass, &sp, out);
    CHECK(!strcmp(out, want), "name: got '%s' want '%s'", out, want);
}

static uint32_t mk(int type, uint16_t add)
{
    cf_form_t f = {0, 0, 0, 0};
    f.type = (uint8_t)type;
    f.add = add;
    return cf_form_mask(&f);
}

int main(void)
{
    cf_spell_t sp;
    char b[16];
    uint8_t v[12];
    int i;

    /* scales */
    CHECK(cf_scale_size(CF_SC_MAJOR) == 7, "major size");
    CHECK(cf_scale_size(CF_SC_MIN_PENT) == 5, "pent size");
    CHECK(cf_scale_size(CF_SC_CHROMATIC) == 12, "chromatic size");
    CHECK(cf_scale_step(CF_SC_MAJOR, 7) == 12, "octave");
    CHECK(cf_scale_step(CF_SC_MAJOR, -1) == -1, "below the tonic: the 7th an octave down (got %d)",
          cf_scale_step(CF_SC_MAJOR, -1));
    CHECK(cf_scale_step(CF_SC_MAJOR, -4) == -7, "F below C (got %d)", cf_scale_step(CF_SC_MAJOR, -4));
    CHECK(cf_scale_index(CF_SC_MAJOR, 4) == 2, "E is the third degree");
    CHECK(cf_scale_index(CF_SC_MAJOR, 1) == -1, "C# not in C major");
    CHECK(cf_snap(CF_SC_MAJOR, 0, 61, 1) == 62, "C# snaps up to D on a tie");
    CHECK(cf_snap(CF_SC_MAJOR, 0, 61, 0) == 60, "C# snaps down to C on a tie");
    CHECK(cf_snap(CF_SC_MAJOR, 4, 60, 1) == 61, "C in E major goes up to C#");
    CHECK(cf_snap(CF_SC_MAJOR, 4, 70, 0) == 69, "A# in E major goes down to A");
    CHECK(cf_snap(CF_SC_MIN_PENT, 9, 62, 1) == 62, "D is in A minor pentatonic");
    for (i = 0; i < CF_NSCALES; i++)
        CHECK(CF_SCALES[i].mask & 1u, "scale %d holds its tonic", i);

    /* spelling */
    cf_key_spell(4, CF_SC_MAJOR, &sp);
    cf_pc_name(3, &sp, b);
    CHECK(!strcmp(b, "D#"), "E major spells D# (got %s)", b);
    cf_key_spell(10, CF_SC_MAJOR, &sp);
    cf_pc_name(3, &sp, b);
    CHECK(!strcmp(b, "Eb"), "Bb major spells Eb (got %s)", b);
    cf_pc_name(10, &sp, b);
    CHECK(!strcmp(b, "Bb"), "Bb major's tonic (got %s)", b);
    cf_key_spell(6, CF_SC_MAJOR, &sp);
    cf_pc_name(5, &sp, b);
    CHECK(!strcmp(b, "E#"), "F# major spells E# (got %s)", b);
    cf_key_spell(2, CF_SC_MINOR, &sp);
    cf_pc_name(10, &sp, b);
    CHECK(!strcmp(b, "Bb"), "D minor spells Bb (got %s)", b);
    cf_key_spell(0, CF_SC_MAJOR, &sp);
    cf_pc_name(1, &sp, b);
    CHECK(!strcmp(b, "C#"), "C major's chromatic C# (got %s)", b);
    cf_key_spell(5, CF_SC_MAJOR, &sp);
    cf_pc_name(10, &sp, b);
    CHECK(!strcmp(b, "Bb"), "F major spells Bb (got %s)", b);
    cf_key_spell(1, CF_SC_MAJOR, &sp);
    cf_pc_name(1, &sp, b);
    CHECK(!strcmp(b, "Db"), "pc 1 major is Db major (got %s)", b);
    cf_key_spell(8, CF_SC_MINOR, &sp);
    cf_pc_name(8, &sp, b);
    CHECK(!strcmp(b, "G#"), "pc 8 minor is G# minor (got %s)", b);
    cf_key_spell(0, CF_SC_DORIAN, &sp);
    cf_pc_name(3, &sp, b);
    CHECK(!strcmp(b, "Eb"), "C Dorian spells Eb (got %s)", b);

    /* numerals */
    cf_numeral(CF_SC_MAJOR, 0, 2, b);
    CHECK(!strcmp(b, "ii"), "D in C major is ii (got %s)", b);
    cf_numeral(CF_SC_MAJOR, 0, 11, b);
    CHECK(!strcmp(b, "vii\xB0"), "B in C major is vii-dim (got %s)", b);
    cf_numeral(CF_SC_MAJOR, 0, 10, b);
    CHECK(!strcmp(b, "bVII"), "Bb in C major is bVII (got %s)", b);
    cf_numeral(CF_SC_MINOR, 9, 0, b);
    CHECK(!strcmp(b, "III"), "C in A minor is III (got %s)", b);
    cf_numeral(CF_SC_MINOR, 9, 4, b);
    CHECK(!strcmp(b, "v"), "E in A minor is v (got %s)", b);
    cf_numeral(CF_SC_HARM_MINOR, 9, 4, b);
    CHECK(!strcmp(b, "V"), "E in A harmonic minor is V (got %s)", b);

    /* chord names */
    expect_name(0, mk(CF_T_MAJ, 0), -1, 0, 0, "C");
    expect_name(0, mk(CF_T_MIN, CF_A_b7), -1, 0, 0, "Cm7");
    expect_name(0, mk(CF_T_MAJ, CF_A_7 | CF_A_9), -1, 0, 0, "Cmaj9");
    expect_name(9, mk(CF_T_MIN, CF_A_b7 | CF_A_9), -1, 0, 0, "Am9");
    expect_name(0, mk(CF_T_MAJ, CF_A_6 | CF_A_9), -1, 0, 0, "C6/9");
    expect_name(0, mk(CF_T_MAJ, CF_A_9), -1, 0, 0, "Cadd9");
    expect_name(0, mk(CF_T_MIN, CF_A_9), -1, 0, 0, "Cmadd9");
    expect_name(0, mk(CF_T_SUS4, 0), -1, 0, 0, "Csus4");
    expect_name(0, mk(CF_T_SUS4, CF_A_b7), -1, 0, 0, "C7sus4");
    expect_name(11, mk(CF_T_DIM, CF_A_b7), -1, 0, 0, "Bm7b5");
    expect_name(11, mk(CF_T_DIM, 0), -1, 0, 0, "Bdim");
    expect_name(0, mk(CF_T_DIM, CF_A_bb7), -1, 0, 0, "Cdim7");
    expect_name(0, mk(CF_T_MAJ, 0), 4, 0, 0, "C/E");
    expect_name(0, mk(CF_T_MIN, CF_A_7), -1, 0, 0, "Cm(maj7)");
    expect_name(0, mk(CF_T_POWER, 0), -1, 0, 0, "C5");
    expect_name(0, mk(CF_T_AUG, 0), -1, 0, 0, "Caug");
    expect_name(0, mk(CF_T_MAJ, CF_A_b7 | CF_A_b9), -1, 0, 0, "C7b9");
    expect_name(0, mk(CF_T_MAJ, CF_A_b7 | CF_A_9 | CF_A_13), -1, 0, 0, "C13");
    expect_name(5, mk(CF_T_MAJ, CF_A_7 | CF_A_s11), -1, 0, 0, "Fmaj7#11");
    expect_name(10, mk(CF_T_MAJ, CF_A_7), -1, 10, 0, "Bbmaj7");
    expect_name(3, mk(CF_T_MIN, 0), -1, 4, 0, "D#m");
    expect_name(0, mk(CF_T_MAJ, CF_A_4), -1, 0, 0, "Cadd4");
    expect_name(0, 1u, -1, 0, 0, "C");

    /* key-cap labels */
    {
        cf_form_t f = {CF_T_MAJ, 0, 0, 0};
        char l[8];
        cf_form_label(&f, l);
        CHECK(!strcmp(l, "M"), "label M (got %s)", l);
        f.add = CF_A_6;
        cf_form_label(&f, l);
        CHECK(!strcmp(l, "M\x16"), "label M6 small (got %s)", l);
        f.add = CF_A_b7;
        cf_form_label(&f, l);
        CHECK(!strcmp(l, "7"), "label 7 (got %s)", l);
        f.add = CF_A_7 | CF_A_9;
        cf_form_label(&f, l);
        CHECK(!strcmp(l, "M7\x19"), "label maj9 (got %s)", l);
        f.type = CF_T_SUS4;
        f.add = CF_A_b7;
        cf_form_label(&f, l);
        CHECK(!strcmp(l, "7s4"), "label 7sus4 (got %s)", l);
        f.type = CF_T_NONE;
        f.add = CF_A_9;
        cf_form_label(&f, l);
        CHECK(!strcmp(l, "9"), "label 9 alone (got %s)", l);
    }

    /* voicing, auto inversion, bass degree */
    CHECK(cf_voice(60, mk(CF_T_MAJ, 0), 1, v) == 3 && v[0] == 64 && v[1] == 67 && v[2] == 72, "C/E voicing");
    {
        uint8_t prev[3] = {60, 64, 67};
        int a = cf_auto_inversion(65, mk(CF_T_MAJ, 0), prev, 3);
        CHECK(a == 5, "C -> F voice-leads to C F A (got %d)", a);
    }
    CHECK(cf_bass_pc(0, mk(CF_T_MAJ, 0), 1) == 4, "bass degree 2 of C is E");
    CHECK(cf_bass_pc(0, mk(CF_T_MAJ, CF_A_b7), 3) == 10, "bass degree 4 of C7 is Bb");

    /* diatonic and snapped chords */
    CHECK(cf_diatonic_mask(CF_SC_MAJOR, 0, 2, 3) == mk(CF_T_MIN, 0), "ii in C major is minor");
    CHECK(cf_diatonic_mask(CF_SC_MAJOR, 0, 7, 4) == mk(CF_T_MAJ, CF_A_b7), "V7 in C major");
    CHECK(cf_diatonic_mask(CF_SC_MAJOR, 0, 11, 3) == mk(CF_T_DIM, 0), "vii in C major is diminished");
    CHECK(cf_mask_snap(mk(CF_T_MAJ, 0), 2, CF_SC_MAJOR, 0) == mk(CF_T_MIN, 0), "D major snapped to C major: Dm");
    CHECK(cf_mask_snap(mk(CF_T_MAJ, CF_A_7), 7, CF_SC_MAJOR, 0) == mk(CF_T_MAJ, CF_A_b7),
          "Gmaj7 snapped to C major: G7");
    CHECK(cf_mask_type(mk(CF_T_SUS2, 0)) == CF_T_SUS2, "sus2 reads as sus2");
    CHECK(cf_mask_type(mk(CF_T_DIM, CF_A_b7)) == CF_T_DIM, "m7b5 reads as dim");

    /* glide matching: C E G -> A C E pairs low with low; a chord that grows feeds one note twice */
    {
        uint8_t p[3] = {60, 64, 67}, q[3] = {57, 60, 64}, q4[4] = {60, 64, 67, 71}, pi[24], qj[24];
        int k = cf_match(p, 3, q, 3, pi, qj);
        {
            int i, cc = 0, ce = 0;
            for (i = 0; i < k; i++) {
                cc |= p[pi[i]] == 60 && q[qj[i]] == 60;
                ce |= p[pi[i]] == 64 && q[qj[i]] == 64;
            }
            CHECK(cc && ce, "C -> Am: the common tones C and E stay put (k %d)", k);
        }
        k = cf_match(p, 3, q4, 4, pi, qj);
        CHECK(k == 4 && pi[3] == 2 && qj[3] == 3 && pi[2] == 2 && qj[2] == 2, "C -> Cmaj7: G feeds G and B (k %d)", k);
        k = cf_match(q4, 4, p, 3, pi, qj);
        CHECK(k == 4 && pi[3] == 3 && qj[3] == 2, "Cmaj7 -> C: B merges into G (k %d)", k);
    }

    printf("cf_theory: %d checks, %d failures\n", checks, fails);
    return fails != 0;
}
