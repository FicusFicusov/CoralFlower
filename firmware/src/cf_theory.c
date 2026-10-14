/* SPDX-License-Identifier: GPL-3.0-only
 * CoralFlower: music theory (cf_theory.h). */
#include "cf_theory.h"

/* ------------------------------------------------------------------ scales --- */
#define M(a, b, c, d, e, f, g) ((uint16_t)((1u << (a)) | (1u << (b)) | (1u << (c)) | (1u << (d)) | (1u << (e)) | \
                                           (1u << (f)) | (1u << (g))))
#define M5(a, b, c, d, e) ((uint16_t)((1u << (a)) | (1u << (b)) | (1u << (c)) | (1u << (d)) | (1u << (e))))
#define M6(a, b, c, d, e, f) ((uint16_t)(M5(a, b, c, d, e) | (1u << (f))))
#define M8(a, b, c, d, e, f, g, h) ((uint16_t)(M(a, b, c, d, e, f, g) | (1u << (h))))

const cf_scale_t CF_SCALES[CF_NSCALES] = {
    {"Major", "MAJ", M(0, 2, 4, 5, 7, 9, 11)},
    {"Natural Minor", "MIN", M(0, 2, 3, 5, 7, 8, 10)},
    {"Major Pentatonic", "PENT", M5(0, 2, 4, 7, 9)},
    {"Minor Pentatonic", "mPENT", M5(0, 3, 5, 7, 10)},
    {"Dorian", "DOR", M(0, 2, 3, 5, 7, 9, 10)},
    {"Mixolydian", "MIX", M(0, 2, 4, 5, 7, 9, 10)},
    {"Lydian", "LYD", M(0, 2, 4, 6, 7, 9, 11)},
    {"Phrygian", "PHR", M(0, 1, 3, 5, 7, 8, 10)},
    {"Locrian", "LOC", M(0, 1, 3, 5, 6, 8, 10)},
    {"Harmonic Minor", "HMIN", M(0, 2, 3, 5, 7, 8, 11)},
    {"Melodic Minor", "MMIN", M(0, 2, 3, 5, 7, 9, 11)},
    {"Blues", "BLUES", M6(0, 3, 5, 6, 7, 10)},
    {"Major Blues", "MBLUE", M6(0, 2, 3, 4, 7, 9)},
    {"Harmonic Major", "HMAJ", M(0, 2, 4, 5, 7, 8, 11)},
    {"Phrygian Dominant", "PHRD", M(0, 1, 4, 5, 7, 8, 10)},
    {"Lydian Dominant", "LYDD", M(0, 2, 4, 6, 7, 9, 10)},
    {"Altered", "ALT", M(0, 1, 3, 4, 6, 8, 10)},
    {"Whole Tone", "WHOLE", M6(0, 2, 4, 6, 8, 10)},
    {"Diminished W-H", "DIMWH", M8(0, 2, 3, 5, 6, 8, 9, 11)},
    {"Diminished H-W", "DIMHW", M8(0, 1, 3, 4, 6, 7, 9, 10)},
    {"Hungarian Minor", "HUNG", M(0, 2, 3, 6, 7, 8, 11)},
    {"Double Harmonic", "DHARM", M(0, 1, 4, 5, 7, 8, 11)},
    {"Hirajoshi", "HIRA", M5(0, 2, 3, 7, 8)},
    {"In Sen", "INSEN", M5(0, 1, 5, 7, 10)},
    {"Iwato", "IWATO", M5(0, 1, 5, 6, 10)},
    {"Kumoi", "KUMOI", M5(0, 2, 3, 7, 9)},
    {"Chromatic", "CHROM", 0x0FFFu},
};

/* for spelling: the mode each 7-tone scale is read as, as semitones from its tonic up to its parent major tonic
 * (C Dorian's parent is Bb major: +10; A minor's is C major: +3); -1 = spell by the key's preference only */
static const int8_t SC_PARENT[CF_NSCALES] = {
    0, 3, -1, -1, 10, 5, 7, 8, 1, 3, 3, -1, -1, 0, 8, 5, 1, -1, -1, -1, 3, 8, -1, -1, -1, -1, -1,
};

static int pc12(int v) { return ((v % 12) + 12) % 12; }

static uint16_t sc_mask(int scale)
{
    return (scale >= 0 && scale < CF_NSCALES) ? CF_SCALES[scale].mask : CF_SCALES[0].mask;
}

int cf_scale_size(int scale)
{
    uint16_t m = sc_mask(scale);
    int n = 0;
    while (m) {
        n += m & 1u;
        m >>= 1;
    }
    return n;
}

int cf_scale_step(int scale, int n)
{
    uint16_t m = sc_mask(scale);
    int size = cf_scale_size(scale), oct, idx, i, k = 0;
    oct = n >= 0 ? n / size : -((-n + size - 1) / size);
    idx = n - oct * size;
    for (i = 0; i < 12; i++)
        if ((m >> i) & 1u) {
            if (k == idx)
                return oct * 12 + i;
            k++;
        }
    return oct * 12;
}

int cf_scale_index(int scale, int s)
{
    uint16_t m = sc_mask(scale);
    int i, k = 0, t = pc12(s);
    if (!((m >> t) & 1u))
        return -1;
    for (i = 0; i < t; i++)
        k += (m >> i) & 1u;
    return k;
}

int cf_snap(int scale, int tonic, int note, int up_on_tie)
{
    uint16_t m = sc_mask(scale);
    int rel = pc12(note - tonic), d;
    if ((m >> rel) & 1u)
        return note;
    for (d = 1; d < 12; d++) {
        int up = (m >> pc12(rel + d)) & 1u, dn = (m >> pc12(rel - d)) & 1u;
        if (up && dn)
            return up_on_tie ? note + d : note - d;
        if (up)
            return note + d;
        if (dn)
            return note - d;
    }
    return note;
}

/* --------------------------------------------------------------- spelling --- */
static const int8_t NAT_PC[7] = {0, 2, 4, 5, 7, 9, 11};      /* C D E F G A B */
static const int8_t PC_LETTER[12] = {0, 0, 1, 1, 2, 3, 3, 4, 4, 5, 5, 6};   /* the letter below a black key */

int cf_key_prefers_flats(int tonic, int scale)
{
    int p = SC_PARENT[scale >= 0 && scale < CF_NSCALES ? scale : 0];
    int parent = pc12(tonic + (p >= 0 ? p : (scale == CF_SC_MIN_PENT || scale == CF_SC_BLUES ? 3 : 0)));
    /* parent major keys spelled with flats: F Bb Eb Ab Db; Gb/F# is spelled F# */
    return parent == 5 || parent == 10 || parent == 3 || parent == 8 || parent == 1;
}

static void spell_one(int pc, int flats, int8_t *letter, int8_t *acc)
{
    int l = PC_LETTER[pc];
    if (NAT_PC[l] == pc) {
        *letter = (int8_t)l;
        *acc = 0;
    } else if (flats) {
        *letter = (int8_t)((l + 1) % 7);
        *acc = -1;
    } else {
        *letter = (int8_t)l;
        *acc = 1;
    }
}

void cf_key_spell(int tonic, int scale, cf_spell_t *sp)
{
    int flats = cf_key_prefers_flats(tonic, scale), pc, i;
    uint16_t m = sc_mask(scale);
    int8_t tl, ta;
    tonic = pc12(tonic);
    for (pc = 0; pc < 12; pc++)
        spell_one(pc, flats, &sp->letter[pc], &sp->acc[pc]);
    if (cf_scale_size(scale) != 7 || SC_PARENT[scale] < 0)
        return;
    /* a seven-tone scale takes one letter per degree, from the tonic's */
    spell_one(tonic, flats, &tl, &ta);
    for (i = 0; i < 7; i++) {
        int s = cf_scale_step(scale, i), p = pc12(tonic + s), l = (tl + i) % 7, a = p - NAT_PC[l];
        if (a > 6)
            a -= 12;
        if (a < -6)
            a += 12;
        if (a >= -2 && a <= 2) {
            sp->letter[p] = (int8_t)l;
            sp->acc[p] = (int8_t)a;
        }
        (void)m;
    }
}

void cf_pc_name(int pc, const cf_spell_t *sp, char out[4])
{
    int k = 0, a;
    pc = pc12(pc);
    out[k++] = "CDEFGAB"[sp->letter[pc]];
    for (a = sp->acc[pc]; a > 0 && k < 3; a--)
        out[k++] = '#';
    for (a = sp->acc[pc]; a < 0 && k < 3; a++)
        out[k++] = 'b';
    out[k] = 0;
}

static const char *const ROMAN[7] = {"I", "II", "III", "IV", "V", "VI", "VII"};

void cf_numeral(int scale, int tonic, int root_pc, char out[8])
{
    int s = pc12(root_pc - tonic), k = 0, idx, lower = 0, i;
    const char *r;
    char mark = 0;
    if (cf_scale_size(scale) == 7 && (idx = cf_scale_index(scale, s)) >= 0) {
        /* the triad the scale builds on this degree decides the case */
        int t3 = pc12(cf_scale_step(scale, idx + 2) - cf_scale_step(scale, idx));
        int t5 = pc12(cf_scale_step(scale, idx + 4) - cf_scale_step(scale, idx));
        lower = t3 == 3;
        if (t3 == 3 && t5 == 6)
            mark = '\xB0';
        if (t3 == 4 && t5 == 8)
            mark = '+';
        r = ROMAN[idx];
    } else {
        /* chromatic: counted from the major scale, with b or # */
        static const int8_t DEG[12] = {0, 1, 1, 2, 2, 3, 4, 4, 5, 5, 6, 6};
        static const int8_t ACC[12] = {0, -1, 0, -1, 0, 0, 1, 0, -1, 0, -1, 0};
        if (ACC[s] < 0)
            out[k++] = 'b';
        if (ACC[s] > 0)
            out[k++] = '#';
        r = ROMAN[DEG[s]];
    }
    for (i = 0; r[i] && k < 6; i++)
        out[k++] = lower ? (char)(r[i] - 'A' + 'a') : r[i];
    if (mark && k < 7)
        out[k++] = mark;
    out[k] = 0;
}

/* ------------------------------------------------------------ chord forms --- */
static const uint32_t TYPE_MASK[CF_NTYPES] = {
    1u << 0,                                   /* NONE */
    (1u << 0) | (1u << 4) | (1u << 7),         /* MAJ */
    (1u << 0) | (1u << 3) | (1u << 7),         /* MIN */
    (1u << 0) | (1u << 3) | (1u << 6),         /* DIM */
    (1u << 0) | (1u << 4) | (1u << 8),         /* AUG */
    (1u << 0) | (1u << 2) | (1u << 7),         /* SUS2 */
    (1u << 0) | (1u << 5) | (1u << 7),         /* SUS4 */
    (1u << 0) | (1u << 7),                     /* POWER */
};
static const int8_t ADD_SEMI[12] = {9, 10, 11, 14, 13, 15, 17, 18, 21, 2, 5, 9};

const uint16_t CF_ADDS[CF_NADDS] = {
    0, CF_A_b7, CF_A_7, CF_A_6, CF_A_9, CF_A_b7 | CF_A_9, CF_A_7 | CF_A_9, CF_A_6 | CF_A_9, CF_A_2, CF_A_4,
    CF_A_b7 | CF_A_9 | CF_A_11, CF_A_b7 | CF_A_9 | CF_A_13, CF_A_b7 | CF_A_11, CF_A_7 | CF_A_s11, CF_A_b7 | CF_A_b9,
    CF_A_b7 | CF_A_s9, CF_A_11, CF_A_bb7, CF_A_7 | CF_A_9 | CF_A_13,
};
static const char *const ADD_NAMES[CF_NADDS] = {
    "None", "7", "Maj7", "6", "Add9", "9", "Maj9", "6/9", "Add2", "Add4",
    "11", "13", "7 Add11", "Maj7#11", "7b9", "7#9", "Add11", "Dim7", "Maj13",
};
static const char *const TYPE_NAMES[CF_NTYPES] = {
    "None", "Major", "Minor", "Diminished", "Augmented", "Sus2", "Sus4", "Power",
};

int cf_adds_index(uint16_t add)
{
    int i;
    for (i = 0; i < CF_NADDS; i++)
        if (CF_ADDS[i] == add)
            return i;
    return -1;
}
const char *cf_adds_name(int i) { return i >= 0 && i < CF_NADDS ? ADD_NAMES[i] : "Custom"; }
const char *cf_type_name(int type) { return type >= 0 && type < CF_NTYPES ? TYPE_NAMES[type] : "?"; }

uint32_t cf_form_mask(const cf_form_t *f)
{
    uint32_t m = TYPE_MASK[f->type < CF_NTYPES ? f->type : CF_T_MAJ];
    int i;
    for (i = 0; i < 12; i++)
        if ((f->add >> i) & 1u)
            m |= 1u << ADD_SEMI[i];
    return m & 0xFFFFFFu;
}

int cf_mask_tones(uint32_t mask, int8_t out[12])
{
    int i, n = 0;
    for (i = 0; i < 24 && n < 12; i++)
        if ((mask >> i) & 1u)
            out[n++] = (int8_t)i;
    return n;
}

uint32_t cf_mask_snap(uint32_t mask, int root_pc, int scale, int tonic)
{
    /* the degree class of an interval: a snapped third stays a third, a seventh a seventh */
    static const int8_t CLS[12] = {1, 2, 2, 3, 3, 4, 5, 5, 5, 6, 7, 7};
    uint32_t out = 1u;
    int i;
    for (i = 1; i < 24; i++)
        if ((mask >> i) & 1u) {
            int note = root_pc + i, up = cf_snap(scale, tonic, note, 1), dn = cf_snap(scale, tonic, note, 0);
            int cu = CLS[pc12(up - root_pc)], cd = CLS[pc12(dn - root_pc)], c = CLS[i % 12], pick, s;
            if (up == dn || (cu == c && cd != c))
                pick = up;
            else if (cd == c && cu != c)
                pick = dn;
            else if (up - note != note - dn)
                pick = up - note < note - dn ? up : dn;
            else
                pick = ((out >> (up - root_pc)) & 1u) ? dn : up;
            s = pick - root_pc;
            if (s > 0 && s < 24)
                out |= 1u << s;
        }
    return out;
}

uint32_t cf_diatonic_mask(int scale, int tonic, int root_pc, int n)
{
    int idx = cf_scale_index(scale, root_pc - tonic), base, k;
    uint32_t m = 1u;
    if (idx < 0) /* a root outside the scale: a major triad, as a plain chord */
        return TYPE_MASK[CF_T_MAJ];
    base = cf_scale_step(scale, idx);
    for (k = 1; k < n; k++) {
        int s = cf_scale_step(scale, idx + 2 * k) - base;
        if (s > 0 && s < 24)
            m |= 1u << s;
    }
    return m;
}

int cf_mask_type(uint32_t mask)
{
    uint32_t f = (mask | mask >> 12) & 0xFFFu;
    int m3 = (f >> 4) & 1u, n3 = (f >> 3) & 1u, p5 = (f >> 7) & 1u, d5 = (f >> 6) & 1u, a5 = (f >> 8) & 1u;
    if (m3)
        return (a5 && !p5) ? CF_T_AUG : CF_T_MAJ;
    if (n3)
        return (d5 && !p5) ? CF_T_DIM : CF_T_MIN;
    if ((mask >> 5) & 1u)
        return CF_T_SUS4;
    if ((mask >> 2) & 1u)
        return CF_T_SUS2;
    if (p5)
        return CF_T_POWER;
    return CF_T_NONE;
}

static int put(char *o, int k, int cap, const char *s)
{
    while (*s && k < cap - 1)
        o[k++] = *s++;
    o[k] = 0;
    return k;
}

void cf_chord_name(int root_pc, uint32_t mask, int bass_pc, const cf_spell_t *sp, char out[16])
{
    char nm[4];
    int k = 0, has = 0;
    int b3 = (mask >> 3) & 1u, M3 = (mask >> 4) & 1u, P5 = (mask >> 7) & 1u, d5 = (mask >> 6) & 1u;
    int a5 = (mask >> 8) & 1u, b7 = (mask >> 10) & 1u, M7 = (mask >> 11) & 1u, six = (mask >> 9) & 1u;
    int n9 = (mask >> 14) & 1u, f9 = (mask >> 13) & 1u, s9 = (mask >> 15) & 1u;
    int n11 = (mask >> 17) & 1u, s11 = (mask >> 18) & 1u, n13 = (mask >> 21) & 1u;
    int a2 = (mask >> 2) & 1u, a4 = (mask >> 5) & 1u, seventh = b7 || M7;
    cf_pc_name(root_pc, sp, nm);
    k = put(out, k, 16, nm);
    if (mask == 1u)
        goto bass;
    if (!M3 && !b3) { /* no third: sus, power, or a bare set of degrees */
        if (a4 || a2) {
            if (seventh)
                k = put(out, k, 16, M7 ? "maj7" : (n9 ? "9" : "7"));
            k = put(out, k, 16, a4 && a2 ? "sus24" : a4 ? "sus4" : "sus2");
            goto bass;
        }
        if (P5 && !seventh && !six && !n9) {
            k = put(out, k, 16, "5");
            goto bass;
        }
    }
    if (b3 && !M3 && d5 && !P5) { /* diminished family */
        if (six && !b7 && !M7)
            k = put(out, k, 16, "dim7");
        else if (b7)
            k = put(out, k, 16, "m7b5");
        else
            k = put(out, k, 16, "dim");
        has = 1;
    } else if (M3 && a5 && !P5) {
        k = put(out, k, 16, seventh ? (M7 ? "maj7#5" : "7#5") : "aug");
        has = 1;
    } else {
        if (b3 && !M3)
            k = put(out, k, 16, "m");
        if (seventh) {
            const char *ext = n13 ? "13" : n11 ? "11" : n9 ? "9" : "7";
            if (M7)
                k = put(out, k, 16, (b3 && !M3) ? "(maj" : "maj");
            k = put(out, k, 16, ext);
            if (M7 && b3 && !M3)
                k = put(out, k, 16, ")");
            if (d5 && !P5)
                k = put(out, k, 16, "b5");
        } else if (six) {
            k = put(out, k, 16, n9 ? "6/9" : "6");
        }
        has = 1;
    }
    /* what the name does not say yet */
    if (has) {
        if (f9)
            k = put(out, k, 16, "b9");
        if (s9)
            k = put(out, k, 16, "#9");
        if (s11)
            k = put(out, k, 16, "#11");
        if (!seventh) {
            if (n9 && !six)
                k = put(out, k, 16, "add9");
            if (n11)
                k = put(out, k, 16, "add11");
            if (n13 && !six)
                k = put(out, k, 16, "add13");
        }
        if (a2 && (M3 || b3))
            k = put(out, k, 16, "add2");
        if (a4 && (M3 || b3))
            k = put(out, k, 16, "add4");
    }
bass:
    if (bass_pc >= 0 && pc12(bass_pc) != pc12(root_pc)) {
        cf_pc_name(bass_pc, sp, nm);
        k = put(out, k, 16, "/");
        k = put(out, k, 16, nm);
    }
    (void)k;
}

/* small digits: '0'..'9' -> 0x10..0x19 */
static int put_small(char *o, int k, int cap, const char *s)
{
    while (*s && k < cap - 1) {
        char c = *s++;
        o[k++] = (c >= '0' && c <= '9') ? (char)(c - '0' + 0x10) : c;
    }
    o[k] = 0;
    return k;
}

void cf_form_label(const cf_form_t *f, char out[8])
{
    int k = 0, t = f->type;
    uint16_t a = f->add;
    out[0] = 0;
    switch (t) {
    case CF_T_MAJ:
        if (a & CF_A_7)
            k = put(out, k, 8, "M7");
        else if (a & CF_A_b7)
            k = put(out, k, 8, "7");
        else
            k = put(out, k, 8, "M");
        break;
    case CF_T_MIN:
        k = put(out, k, 8, (a & CF_A_7) ? "mM7" : (a & CF_A_b7) ? "m7" : "m");
        break;
    case CF_T_DIM:
        k = put(out, k, 8, (a & CF_A_bb7) ? "\xB0" "7" : (a & CF_A_b7) ? "\xF8" : "\xB0");
        break;
    case CF_T_AUG:
        k = put(out, k, 8, (a & (CF_A_b7 | CF_A_7)) ? "+7" : "+");
        break;
    case CF_T_SUS2:
    case CF_T_SUS4:
        if (a & (CF_A_b7 | CF_A_7))
            k = put(out, k, 8, (a & CF_A_7) ? "M7" : "7");
        k = put(out, k, 8, t == CF_T_SUS2 ? "s2" : "s4");
        break;
    case CF_T_POWER:
        k = put(out, k, 8, "5");
        break;
    default: /* NONE: the added degrees are the label, full size */
        if (a & CF_A_7)
            k = put(out, k, 8, "M7");
        else if (a & CF_A_b7)
            k = put(out, k, 8, "7");
        if (a & CF_A_6)
            k = put(out, k, 8, "6");
        if (a & CF_A_9)
            k = put(out, k, 8, "9");
        if (a & CF_A_11)
            k = put(out, k, 8, "11");
        if (a & CF_A_13)
            k = put(out, k, 8, "13");
        if (a & CF_A_2)
            k = put(out, k, 8, "2");
        if (a & CF_A_4)
            k = put(out, k, 8, "4");
        if (!k)
            k = put(out, k, 8, "1");
        return;
    }
    /* added degrees as small digits */
    if (a & CF_A_6)
        k = put_small(out, k, 8, "6");
    if (a & CF_A_b9)
        k = put_small(out, k, 8, "b9");
    if (a & CF_A_9)
        k = put_small(out, k, 8, "9");
    if (a & CF_A_s9)
        k = put_small(out, k, 8, "#9");
    if (a & CF_A_11)
        k = put_small(out, k, 8, "11");
    if (a & CF_A_s11)
        k = put_small(out, k, 8, "#11");
    if (a & CF_A_13)
        k = put_small(out, k, 8, "13");
    if (a & CF_A_2)
        k = put_small(out, k, 8, "2");
    if (a & CF_A_4)
        k = put_small(out, k, 8, "4");
    (void)k;
}

/* --------------------------------------------------------------- voicing --- */
static void sort_u8(uint8_t *a, int n)
{
    int i, j;
    for (i = 1; i < n; i++) {
        uint8_t v = a[i];
        for (j = i - 1; j >= 0 && a[j] > v; j--)
            a[j + 1] = a[j];
        a[j + 1] = v;
    }
}

int cf_voice(int root_note, uint32_t mask, int inv, uint8_t out[12])
{
    int8_t t[12];
    int n = cf_mask_tones(mask, t), i, k;
    for (i = 0; i < n; i++) {
        int v = root_note + t[i];
        out[i] = (uint8_t)(v < 0 ? 0 : v > 127 ? 127 : v);
    }
    for (k = 0; k < inv && n > 1; k++) {
        if (out[0] + 12 > 127)
            break;
        out[0] = (uint8_t)(out[0] + 12);
        sort_u8(out, n);
    }
    return n;
}

static int iabs(int v) { return v < 0 ? -v : v; }

/* total motion between two sorted note sets: a lattice walk that pairs lowest with lowest and lets one note feed
 * two (the glide's matcher uses the same walk) */
static int match_cost(const uint8_t *p, int n, const uint8_t *q, int m)
{
    int D[12][12], i, j;
    if (!n || !m)
        return 0;
    for (i = 0; i < n; i++)
        for (j = 0; j < m; j++) {
            int c = iabs((int)p[i] - (int)q[j]) * 16, best = 0x7FFFFFF;
            if (!i && !j) {
                D[0][0] = c;
                continue;
            }
            if (i && j && D[i - 1][j - 1] < best)
                best = D[i - 1][j - 1];
            if (i && D[i - 1][j] + 1 < best)
                best = D[i - 1][j] + 1;
            if (j && D[i][j - 1] + 1 < best)
                best = D[i][j - 1] + 1;
            D[i][j] = c + best;
        }
    return D[n - 1][m - 1];
}

int cf_match(const uint8_t *p, int n, const uint8_t *q, int m, uint8_t pi[24], uint8_t qj[24])
{
    int D[12][12], i, j, k = 0, a, b;
    uint8_t from[12][12];
    if (n <= 0 || m <= 0)
        return 0;
    if (n > 12)
        n = 12;
    if (m > 12)
        m = 12;
    for (i = 0; i < n; i++)
        for (j = 0; j < m; j++) {
            int c = iabs((int)p[i] - (int)q[j]) * 16, best = 0x7FFFFFF;
            uint8_t f = 0;
            if (!i && !j) {
                D[0][0] = c;
                from[0][0] = 0;
                continue;
            }
            if (i && j && D[i - 1][j - 1] < best) {
                best = D[i - 1][j - 1];
                f = 0;
            }
            if (i && D[i - 1][j] + 1 < best) {
                best = D[i - 1][j] + 1;
                f = 1;
            }
            if (j && D[i][j - 1] + 1 < best) {
                best = D[i][j - 1] + 1;
                f = 2;
            }
            D[i][j] = c + best;
            from[i][j] = f;
        }
    /* walk back from the top pair, then reverse */
    a = n - 1;
    b = m - 1;
    for (;;) {
        pi[k] = (uint8_t)a;
        qj[k] = (uint8_t)b;
        k++;
        if (!a && !b)
            break;
        if (from[a][b] == 0) {
            a--;
            b--;
        } else if (from[a][b] == 1) {
            a--;
        } else {
            b--;
        }
    }
    for (i = 0; i < k / 2; i++) {
        uint8_t x = pi[i], y = qj[i];
        pi[i] = pi[k - 1 - i];
        qj[i] = qj[k - 1 - i];
        pi[k - 1 - i] = x;
        qj[k - 1 - i] = y;
    }
    return k;
}

int cf_auto_inversion(int root_note, uint32_t mask, const uint8_t *prev, int n_prev)
{
    uint8_t v[12], ps[12];
    int8_t t[12];
    int n = cf_mask_tones(mask, t), best = 0, best_cost = 0x7FFFFFF, inv, i;
    if (n_prev <= 0 || n <= 1)
        return 0;
    for (i = 0; i < n_prev && i < 12; i++)
        ps[i] = prev[i];
    sort_u8(ps, n_prev < 12 ? n_prev : 12);
    /* every inversion, from the root an octave down too: the encoding is root_note - 12 + inv - n for those */
    for (inv = 0; inv < 2 * n; inv++) {
        int base = inv < n ? root_note : root_note - 12, k = inv % n, c;
        int m = cf_voice(base, mask, k, v);
        if (v[0] < 12 || v[m - 1] > 120)
            continue;
        c = match_cost(ps, n_prev < 12 ? n_prev : 12, v, m);
        if (c < best_cost) {
            best_cost = c;
            best = inv;
        }
    }
    return best; /* 0..n-1: that inversion; n..2n-1: inversion best-n with the root an octave lower */
}

int cf_bass_pc(int root_pc, uint32_t mask, int deg)
{
    int8_t t[12];
    int n = cf_mask_tones(mask, t);
    if (!n)
        return -1;
    return pc12(root_pc + t[((deg % n) + n) % n]);
}
