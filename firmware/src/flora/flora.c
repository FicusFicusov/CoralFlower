#include "flora.h"
#include "flora_lut.h"
#include "flora_pal.h"

enum { FL_K_STEM = 0, FL_K_LEAF, FL_K_PETAL, FL_K_BUD, FL_K_BERRY, FL_K_THORN, FL_K_POLYP };
enum { FL_SLOT_TRUNK = 12, FL_SLOT_CROWN = 13, FL_NSLOTS = 16 };
enum { R_ROOT = 0, R_THIRD, R_FIFTH, R_FLAT5, R_SHARP5, R_SEVENTH, R_NINTH, R_FLAT9, R_ELEVENTH, R_SIXTH };
enum { PO_KNOB = 0, PO_OPEN, PO_BULB };                      /* a polyp's style (its segment's roll) */
enum { P_MOTE = 0, P_BUBBLE, P_PETAL, P_BIG_BUBBLE };        /* particles */

#define PITCH 84      /* camera elevation, angle units (1024 per turn): about 30 degrees */
#define SCALE 400     /* body size, Q8 (256 = sizes as written) */
#define KEY_CORAL 0x8000u
#define IS_CORAL(s) (((s)->key & KEY_CORAL) != 0)

/* ---- integer helpers ---------------------------------------------------------------------------------------- */

static int32_t fl_fsin(int32_t a)
{
    a &= 1023;
    if (a < 256)
        return fl_qsin[a];
    if (a < 512)
        return fl_qsin[512 - a];
    if (a < 768)
        return -fl_qsin[a - 512];
    return -fl_qsin[1024 - a];
}
static int32_t fl_fcos(int32_t a) { return fl_fsin(a + 256); }

static uint32_t fl_hash32(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

/* stable pseudo-random value for a part: the same key always gets the same jitter */
static int32_t fl_jit(uint32_t key, uint32_t salt, int32_t lo, int32_t hi)
{
    return lo + (int32_t)(fl_hash32(key * 2654435761u + salt * 40503u + 17u) % (uint32_t)(hi - lo + 1));
}

static uint32_t fl_rnd(flora_t *f)
{
    uint32_t x = f->rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return f->rng = x;
}

static int32_t fl_clampi(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }

/* move v toward t by a fraction of the gap, at least one unit */
static int32_t fl_approach(int32_t v, int32_t t, uint32_t dt, int32_t tau)
{
    int32_t d = t - v, s;
    if (d == 0)
        return v;
    s = (int32_t)((int64_t)d * (int32_t)(dt < 200 ? dt : 200) / tau);
    if (s == 0)
        s = d > 0 ? 1 : -1;
    if ((d > 0 && s > d) || (d < 0 && s < d))
        s = d;
    return v + s;
}

static int32_t fl_wrap_delta(int32_t from, int32_t to)
{
    int32_t d = (to - from) & 1023;
    return d >= 512 ? d - 1024 : d;
}

/* the pitch class a part answers to (its branch's, the root's for the stem and the crown), -1 none */
static int fl_seg_pc(const flora_t *f, const fl_seg_t *s)
{
    if (s->slot < 12)
        return s->slot;
    return s->slot == FL_SLOT_TRUNK || s->slot == FL_SLOT_CROWN ? f->shape.root % 12 : -1;
}

/* ---- building the body from a chord ------------------------------------------------------------------------- */

typedef struct {
    flora_t *f;
    uint8_t next[FL_NSLOTS][8]; /* per slot and kind: the next index, so keys stay stable */
} bctx_t;

static int fl_add(bctx_t *b, int slot, int parent, int kind, int ramp, int32_t az, int32_t el, int32_t len_px8,
                  int w_q4, int roll, int delay)
{
    flora_t *f = b->f;
    uint16_t key = (uint16_t)((f->species ? KEY_CORAL : 0u) | (unsigned)(slot << 11) | (unsigned)(kind << 7) |
                              (b->next[slot][kind]++ & 127u));
    fl_seg_t *s = 0;
    int i, fresh = 0, pc;
    if (parent < 0 && slot != FL_SLOT_TRUNK)
        return -1;
    for (i = 0; i < f->nseg; i++)
        if (f->seg[i].key == key) {
            s = &f->seg[i];
            break;
        }
    if (!s) {
        if (f->nseg >= FL_MAX_SEG)
            return -1;
        i = f->nseg++;
        s = &f->seg[i];
        fresh = 1;
    }
    s->key = key;
    s->parent = (int16_t)parent;
    s->kind = (uint8_t)kind;
    s->ramp = (uint8_t)ramp;
    s->alive = 1;
    s->slot = (uint8_t)slot;
    s->depth = (uint8_t)(parent < 0 ? 0 : f->seg[parent].depth + 1);
    s->az_t = (int16_t)(az & 1023);
    s->el_t = (int16_t)el;
    s->len_t = (int16_t)(len_px8 * SCALE / 256);
    if (kind == FL_K_LEAF || kind == FL_K_PETAL)
        w_q4 = w_q4 * SCALE / 256;
    else if (kind == FL_K_STEM)
        w_q4 = w_q4 * (SCALE + 256) / 512;
    s->w_t = (uint8_t)(w_q4 > 255 ? 255 : w_q4);
    s->roll = (int8_t)roll;
    if (fresh || s->grow == 0) {
        s->az = s->az_t;
        s->el = s->el_t;
        s->len = s->len_t;
        s->w = s->w_t;
        s->grow = 0;
        s->delay = (uint16_t)delay;
        s->p0[0] = s->p0[1] = s->p0[2] = 0;
        s->p1[0] = s->p1[1] = s->p1[2] = 0;
        s->sx = s->sy = -1;
        pc = fl_seg_pc(f, s);
        s->pz = pc >= 0 ? f->pulse_n[pc] : 0; /* it sparkles for the next note, not for an old one */
    }
    return i;
}

static void fl_spawn(flora_t *f, int32_t x, int32_t y, int vx, int vy, int life, uint8_t col, uint8_t kind)
{
    int i, best = 0;
    for (i = 0; i < FL_MAX_PART; i++) {
        if (f->part[i].life == 0) {
            best = i;
            break;
        }
        if (f->part[i].life < f->part[best].life)
            best = i;
    }
    f->part[best].x = x;
    f->part[best].y = y;
    f->part[best].vx = (int16_t)vx;
    f->part[best].vy = (int16_t)vy;
    f->part[best].life = (uint16_t)life;
    f->part[best].col = col;
    f->part[best].kind = kind;
}

/* drop withered segments, keep parents before children */
static void fl_compact(flora_t *f)
{
    int16_t map[FL_MAX_SEG];
    int i, n = 0;
    for (i = 0; i < f->nseg; i++) {
        if (f->seg[i].alive || f->seg[i].grow) {
            map[i] = (int16_t)n;
            if (n != i)
                f->seg[n] = f->seg[i];
            n++;
        } else {
            map[i] = -1;
        }
    }
    for (i = 0; i < n; i++)
        if (f->seg[i].parent >= 0)
            f->seg[i].parent = map[f->seg[i].parent];
    f->nseg = (uint16_t)n;
}

static void fl_sort_by_depth(flora_t *f)
{
    int d, i, k = 0;
    for (d = 0; d < 32 && k < f->nseg; d++)
        for (i = 0; i < f->nseg; i++)
            if (f->seg[i].depth == d)
                f->order[k++] = (uint8_t)i;
}

static int fl_role_of(int interval, int quality)
{
    switch (interval) {
    case 0: return R_ROOT;
    case 1: return R_FLAT9;
    case 2: return quality == FL_Q_SUS2 ? R_ELEVENTH : R_NINTH;
    case 3:
    case 4: return R_THIRD;
    case 5: return R_ELEVENTH;
    case 6: return R_FLAT5;
    case 7: return R_FIFTH;
    case 8: return R_SHARP5;
    case 9: return R_SIXTH;
    default: return R_SEVENTH;
    }
}

/* ---- the plant ---------------------------------------------------------------------------------------------- */

/* how far the chord's flowers open: the petals' elevation (0 flat .. 256 shut) */
static int32_t fl_open_el(int q)
{
    switch (q) {
    case FL_Q_MINOR: return 130;       /* a cup */
    case FL_Q_DIM: return 200;         /* nearly shut */
    case FL_Q_AUG: return 8;           /* a flat star */
    case FL_Q_SUS2:
    case FL_Q_SUS4: return 95;         /* half open */
    case FL_Q_SINGLE: return 215;      /* a bud */
    default: return 50;                /* major: open */
    }
}

/* a flower at the end of segment at: np petals (rings 2: an inner ring between them), its eye last */
static void fl_bloom(bctx_t *b, int slot, int at, int np, int rings, int32_t open, int32_t plen, int pw, int delay)
{
    flora_t *f = b->f;
    int p;
    int32_t phase = fl_jit((uint32_t)slot, 7, 0, 1023);
    if (at < 0)
        return;
    for (p = 0; p < np; p++)
        fl_add(b, slot, at, FL_K_PETAL, f->key_ramp, phase + p * 1024 / np, open, plen, pw, 10, delay + p * 40);
    if (rings > 1)
        for (p = 0; p < np; p++)
            fl_add(b, slot, at, FL_K_PETAL, f->key_ramp, phase + 512 / np + p * 1024 / np, open + (256 - open) / 3,
                   plen * 2 / 3, pw * 3 / 4, -10, delay + 160 + p * 40);
    fl_add(b, slot, at, FL_K_BUD, f->eye_ramp, 0, 256, 1 << 8, 16, 0, delay + 100);
}

/* The plant's anatomy follows PlantStudio's model (Kurtz-Fernhout, 1998; its source: github.com/pdfernhout/PlantStudio):
 * - phytomers: the stem is a column of nodes; each node carries a leaf and, in the leaf's axil, the bud a side shoot
 *   grows from, so a shoot always sits just above its leaf and points the same way;
 * - phyllotaxis: each node turns 137.5 degrees (391 of 1024) from the one below, so leaves and shoots spiral round
 *   the stem and do not stack on one side (the plant turns slowly: every side comes into view);
 * - apical dominance: no shoot at the top node; the terminal stalk is long, so the crown's flower stands clear above
 *   the side flowers;
 * - one angle rule for the whole plant: every peduncle leaves the stem at 40 degrees and arcs gently upward, every
 *   flower, bud and fruit faces up (a head that hangs reads as broken or wilted);
 * - fruit stands where a flower would, at the same angle; lower shoots are longer and open first (bottom-first);
 * - variation is small and fixed per part (fl_jit), the same chord always grows the same plant.
 * Thorns and tendrils are outside PlantStudio's model: ours, kept uniform and relative to their node. */
#define FL_PED_EL 142   /* a peduncle's elevation: 40 degrees off the upright stem */
#define FL_PED_UP 182   /* .. its second part: the arc toward the light */

/* a side shoot for one chord tone from stem node at, out along the node's leaf (az); reach: its length, Q8 of the
 * full one (the lower, the longer) */
static void fl_shoot(bctx_t *b, int role, int pc, int at, int32_t az, int32_t reach, int32_t open, int delay)
{
    flora_t *f = b->f;
    uint32_t k = (uint32_t)(pc * 31 + role);
    int32_t l1 = (7 << 8) * reach >> 8, l2 = (5 << 8) * reach >> 8;
    int s1, s2, s3, i;
    az += fl_jit(k, 1, -14, 14);
    if (role == R_FLAT5 || role == R_SHARP5 || role == R_FLAT9) { /* tension: prickles round the node, all alike */
        int nt = role == R_SHARP5 ? 4 : 3;
        for (i = 0; i < nt; i++)
            fl_add(b, pc, at, FL_K_THORN, FL_R_LIME, az + i * 1024 / nt, -24, 4 << 8, 12, 0, delay + i * 60);
        return;
    }
    if (role == R_ELEVENTH) { /* a tendril: out at the peduncle's angle, coiling level */
        s1 = at;
        for (i = 0; i < 5; i++)
            s1 = fl_add(b, pc, s1, FL_K_STEM, FL_R_JADE, az + i * 150, FL_PED_EL - i * 30, (6 - i) << 8, 14 - i * 2, 0,
                        delay + i * 90);
        return;
    }
    s1 = fl_add(b, pc, at, FL_K_STEM, FL_R_LIME, az, FL_PED_EL + fl_jit(k, 2, -8, 8), l1, 16, 0, delay);
    s2 = fl_add(b, pc, s1, FL_K_STEM, FL_R_LIME, az, FL_PED_UP + fl_jit(k, 4, -8, 8), l2, 12, 0, delay + 120);
    if (s2 < 0)
        return;
    switch (role) {
    case R_THIRD: /* an open flower */
        fl_bloom(b, pc, s2, 5, 1, open, 6 << 8, 48, delay + 260);
        break;
    case R_FIFTH: /* a leafy shoot with a closed bud: two small leaves on the peduncle, opposite */
        fl_add(b, pc, s1, FL_K_LEAF, FL_R_LIME, az + 256, 70, 7 << 8, 52, 25, delay + 200);
        fl_add(b, pc, s1, FL_K_LEAF, FL_R_LIME, az - 256, 70, 6 << 8, 48, -25, delay + 260);
        fl_bloom(b, pc, s2, 3, 1, 215, 4 << 8, 40, delay + 300);
        break;
    case R_SEVENTH: /* a tall upright cup, a tulip: narrower than the third's flower, on a longer stalk */
        s3 = fl_add(b, pc, s2, FL_K_STEM, FL_R_LIME, az, 228, 3 << 8, 10, 0, delay + 220);
        fl_bloom(b, pc, s3, 4, 1, 176 + (open - 50) / 6, 5 << 8, 40, delay + 320);
        break;
    case R_NINTH:   /* a little raceme: three florets up the axis, each on a pedicel 40 degrees off it, */
    case R_SIXTH: { /* spiralling 137.5 degrees, the lowest first; the sixth's are fruit (berries) in their place */
        int kind = role == R_SIXTH ? FL_K_BERRY : FL_K_BUD;
        s3 = s2;
        for (i = 0; i < 3; i++) {
            int ped;
            if (i)
                s3 = fl_add(b, pc, s3, FL_K_STEM, FL_R_LIME, az, 236, 2 << 8, 8, 0, delay + 200 + i * 80);
            ped = fl_add(b, pc, s3, FL_K_STEM, FL_R_LIME, az + 256 + i * 391, 150, 2 << 8, 6, 0, delay + 240 + i * 80);
            fl_add(b, pc, ped, kind, f->key_ramp, 0, 256, 1 << 8, 16, 0, delay + 300 + i * 80);
        }
        break;
    }
    default:
        break;
    }
}

static void fl_build_plant(bctx_t *b)
{
    flora_t *f = b->f;
    const fl_chord_t *c = &f->shape;
    int n = c->n > 7 ? 7 : c->n, q = c->quality, i, nb = 0, placed = 0, prev = -1, top, nodes;
    int node[9];
    int32_t H = (17 + 3 * n) << 8, rot = fl_jit(c->root, 21, 0, 1023), open = fl_open_el(q), stalk;
    uint16_t seen = 0;

    /* the side shoots: one per chord tone other than the root */
    for (i = 0; i < n; i++) {
        int pc = c->note[i] % 12;
        if (pc != c->root % 12 && !(seen & (1u << pc))) {
            seen |= (uint16_t)(1u << pc);
            nb++;
        }
    }
    /* the stem: a node for the first leaves (low: no bare stem under them), one per side shoot, one at the top without
     * (apical dominance); each internode bends a little, at random but always the same (PlantStudio's curving index),
     * the first nearly upright */
    nodes = nb + 2;
    stalk = H / (nodes + 1);
    for (i = 0; i < nodes; i++) {
        int32_t el = i ? 256 - fl_jit((uint32_t)i, 1, 8, 20) : 252, az = rot + fl_jit((uint32_t)i, 3, -160, 160);
        prev = node[i] = fl_add(b, FL_SLOT_TRUNK, prev, FL_K_STEM, FL_R_LIME, az, el, i ? stalk : stalk * 2 / 3,
                                40 - i * 30 / nodes, 0, i * 120);
        if (prev < 0)
            return;
    }
    /* the leaves: the first pair opposite, then one per node round the golden angle, smaller toward the top, the top
     * node's too (apical dominance stops shoots, not leaves). The pair turned 75 units (26 degrees) off the spiral's
     * start: then the leaves of nodes 1 and 2 (391 and 782) both stand 69 degrees off the pair's plane, so the lowest
     * leaves never lie in one plane (the owner, 2026-10-15); not quite opposite either, as in a real plant */
    fl_add(b, FL_SLOT_TRUNK, node[0], FL_K_LEAF, FL_R_LIME, rot + 75, 48, 9 << 8, 64, 40, 160);
    fl_add(b, FL_SLOT_TRUNK, node[0], FL_K_LEAF, FL_R_LIME, rot + 75 + 512 + fl_jit(c->root, 22, -24, 24), 56, 17 << 7,
           60, -40, 220);
    for (i = 1; i < nodes; i++)
        fl_add(b, FL_SLOT_TRUNK, node[i], FL_K_LEAF, FL_R_JADE, rot + i * 391, 60 + i * 4,
               (i < nodes - 1 ? 10 - i * 5 / (nb ? nb : 1) : 5) << 8, 68 - i * 3, (i & 1) ? 40 : -40, i * 120 + 160);

    /* the crown: on a long terminal stalk, the root's flower, one petal per chord tone in two rings; a bud for single
     * notes */
    top = fl_add(b, FL_SLOT_CROWN, node[nodes - 1], FL_K_STEM, FL_R_LIME, rot, 248, stalk * 7 / 4, 16, 0, nodes * 120);
    if (n <= 1 || q == FL_Q_SINGLE) {
        fl_bloom(b, FL_SLOT_CROWN, top, 3, 1, 215, 5 << 8, 48, nodes * 120 + 80);
    } else {
        int pw = 68;
        int32_t plen = (7 + n / 2) << 8;
        if (q == FL_Q_AUG)
            pw = 40, plen = plen * 5 / 4;
        fl_bloom(b, FL_SLOT_CROWN, top, n < 3 ? 3 : n, 2, open, plen, pw, nodes * 120 + 80);
    }

    /* every other chord tone: a side shoot in the axil of node 1, 2, .. (low notes low), along that node's leaf */
    seen = 0;
    for (i = 0; i < n; i++) {
        int pc = c->note[i] % 12, k;
        if (pc == c->root % 12 || (seen & (1u << pc)))
            continue;
        seen |= (uint16_t)(1u << pc);
        k = 1 + placed;
        fl_shoot(b, fl_role_of((pc - c->root % 12 + 12) % 12, q), pc, node[k], rot + k * 391,
                 256 - placed * 96 / (nb > 1 ? nb - 1 : 1), open, 240 + k * 120 + placed * 60);
        placed++;
    }
}

/* ---- the coral ---------------------------------------------------------------------------------------------- */

/* a branch that forks depth times, polyps of style tip on its ends; uneven: the second arm of the first fork stops */
static void fl_fork(bctx_t *b, int slot, int parent, int32_t az, int32_t el, int32_t len, int w, int depth,
                    int32_t spread, int tip, int uneven, int delay)
{
    flora_t *f = b->f;
    uint32_t k = (uint32_t)(slot * 97 + b->next[slot][FL_K_STEM]);
    int s;
    if (parent < 0)
        return;
    s = fl_add(b, slot, parent, FL_K_STEM, f->body_ramp, az, el, len * fl_jit(k, 3, 86, 114) / 100, w, 0, delay);
    if (s < 0)
        return;
    if (depth <= 0) {
        fl_add(b, slot, s, FL_K_POLYP, f->key_ramp, az, el, 1 << 8, 16, tip, delay + 140);
        return;
    }
    spread += fl_jit(k, 5, -18, 18);
    /* the arms turn upward, but an upright branch opens into a V (else its arms would hide behind each other) */
    fl_fork(b, slot, s, az - spread, el >= 200 ? fl_jit(k, 6, 165, 185) : fl_clampi(el + fl_jit(k, 6, 4, 22), 0, 200),
            len * 4 / 5, w * 7 / 10, depth - 1, spread, tip, 0, delay + 150);
    fl_fork(b, slot, s, az + spread, el >= 200 ? fl_jit(k, 7, 165, 185) : fl_clampi(el + fl_jit(k, 7, 4, 22), 0, 200),
            len * (uneven ? 3 : 4) / 5, w * 7 / 10, uneven ? 0 : depth - 1, spread, tip, 0, delay + 190);
}

/* a branch for one chord tone, from the foot segment at, reaching the tone's way */
static void fl_branch(bctx_t *b, int role, int pc, int at, int32_t el, int q, int delay)
{
    flora_t *f = b->f;
    uint32_t k = (uint32_t)(pc * 31 + role);
    int32_t az = pc * 1024 / 12 + fl_jit(k, 1, -12, 12);
    int32_t len = (q == FL_Q_DIM ? 8 : q == FL_Q_AUG ? 12 : 10) << 8;
    int w = q == FL_Q_DIM ? 58 : q == FL_Q_AUG ? 38 : 48, i, s;
    switch (role) {
    case R_FLAT5:
    case R_SHARP5:
    case R_FLAT9: /* tension: short knobs */
        for (i = 0; i < (role == R_SHARP5 ? 3 : 2); i++)
            fl_fork(b, pc, at, az + (i - 1) * 80, 70 + i * 30, 3 << 8, 30, 0, 0, PO_KNOB, 0, delay + i * 80);
        break;
    case R_ELEVENTH: /* a whip: long, thin, curving */
        s = at;
        for (i = 0; i < 3; i++)
            s = fl_add(b, pc, s, FL_K_STEM, f->body_ramp, az + i * 50, el + 20 + i * 15, 6 << 8, 26 - i * 6, 0,
                       delay + i * 120);
        if (s >= 0)
            fl_add(b, pc, s, FL_K_POLYP, f->key_ramp, az, 256, 1 << 8, 16, PO_OPEN, delay + 420);
        break;
    case R_FIFTH: /* a sturdy fork */
        fl_fork(b, pc, at, az, el, len * 4 / 5, w + 8, 1, 90, PO_KNOB, 0, delay);
        break;
    case R_SEVENTH: /* an uneven fork */
        fl_fork(b, pc, at, az, el, len, w, 2, 100, PO_KNOB, 1, delay);
        break;
    case R_NINTH: /* open polyps */
        fl_fork(b, pc, at, az, el, len, w, 2, 110, PO_OPEN, 0, delay);
        break;
    case R_SIXTH: /* bulbs */
        fl_fork(b, pc, at, az, el, len * 4 / 5, w, 1, 100, PO_BULB, 0, delay);
        break;
    default: /* the third: a wide fan */
        fl_fork(b, pc, at, az, el, len, w, 2, 130, PO_KNOB, 0, delay);
        break;
    }
}

static void fl_build_coral(bctx_t *b)
{
    flora_t *f = b->f;
    const fl_chord_t *c = &f->shape;
    int n = c->n > 7 ? 7 : c->n, q = c->quality, i, nb = 0, placed = 0, prev = -1, foot[2];
    int32_t lean = fl_jit(c->root, 23, 0, 1023);
    /* the quality: major spreads wide, minor rises, diminished stays short and thick, augmented reaches out */
    int32_t el = q == FL_Q_MINOR ? 160 : q == FL_Q_AUG ? 92 : (q == FL_Q_DIM || q == FL_Q_SUS2 || q == FL_Q_SUS4) ? 136 : 110;
    uint16_t seen = 0;

    for (i = 0; i < 2; i++) { /* the foot: short and thick */
        prev = foot[i] = fl_add(b, FL_SLOT_TRUNK, prev, FL_K_STEM, f->body_ramp, lean + i * 300, 244 - i * 8, 5 << 8,
                                84 - i * 14, 0, i * 110);
        if (prev < 0)
            return;
    }
    /* the crown: the root, the tallest branch, forking more for richer chords; a single knob for single notes */
    if (n <= 1 || q == FL_Q_SINGLE)
        fl_fork(b, FL_SLOT_CROWN, foot[1], lean, 236, 9 << 8, 50, 0, 0, PO_OPEN, 0, 220);
    else
        fl_fork(b, FL_SLOT_CROWN, foot[1], lean, 232, (10 + n / 2) << 8, 54, n >= 3 ? 2 : 1, 120, PO_OPEN, 0, 220);

    for (i = 0; i < n; i++) {
        int pc = c->note[i] % 12;
        if (pc != c->root % 12 && !(seen & (1u << pc))) {
            seen |= (uint16_t)(1u << pc);
            nb++;
        }
    }
    seen = 0;
    for (i = 0; i < n; i++) { /* every other tone: a branch, the low ones from low on the foot */
        int pc = c->note[i] % 12;
        if (pc == c->root % 12 || (seen & (1u << pc)))
            continue;
        seen |= (uint16_t)(1u << pc);
        fl_branch(b, fl_role_of((pc - c->root % 12 + 12) % 12, q), pc, foot[placed * 2 < nb ? 0 : 1], el, q,
                  260 + placed * 90);
        placed++;
    }
}

/* ---- the body ----------------------------------------------------------------------------------------------- */

static void fl_build(flora_t *f)
{
    static const uint8_t PLANT_RAMPS[6] = {FL_R_FIRE, FL_R_PEACH, FL_R_ROSE, FL_R_ORCHID, FL_R_SEA, FL_R_SAND};
    static const uint8_t CORAL_RAMPS[6] = {FL_R_ROSE, FL_R_PEACH, FL_R_FIRE, FL_R_COPPER, FL_R_ORCHID, FL_R_SEA};
    static uint8_t was[FL_MAX_SEG];
    bctx_t b;
    /* the colours go round the circle of fifths: near keys look near */
    int i, fifth = (f->shape.root % 12) * 7 % 12, home = (f->home % 12) * 7 % 12;

    for (i = 0; i < FL_NSLOTS * 8; i++)
        b.next[i / 8][i % 8] = 0;
    b.f = f;
    fl_compact(f);
    for (i = 0; i < f->nseg; i++) {
        was[i] = f->seg[i].alive;
        f->seg[i].alive = 0;
    }
    f->key_ramp = f->bloom ? (uint8_t)(f->bloom - 1u) : PLANT_RAMPS[fifth % 6];
    f->eye_ramp = f->key_ramp == FL_R_FIRE ? FL_R_COPPER : FL_R_FIRE;
    f->body_ramp = CORAL_RAMPS[home % 6];
    if (f->species == FL_SP_CORAL)
        fl_build_coral(&b);
    else
        fl_build_plant(&b);

    /* what the change lets go of: petals fall, polyps release a puff of spawn */
    for (i = 0; i < f->nseg; i++) {
        fl_seg_t *s = &f->seg[i];
        if (!was[i] || s->alive || s->grow < 128 || s->sx < 0)
            continue;
        if (s->kind == FL_K_PETAL && (i & 1))
            fl_spawn(f, (int32_t)s->sx << 8, (int32_t)s->sy << 8, (int)(fl_rnd(f) % 64) - 32, 16 + (int)(fl_rnd(f) % 24),
                     1800 + (int)(fl_rnd(f) % 1400), FL_C(s->ramp, 1), P_PETAL);
        else if (s->kind == FL_K_POLYP)
            fl_spawn(f, (int32_t)s->sx << 8, (int32_t)s->sy << 8, (int)(fl_rnd(f) % 40) - 20, -40 - (int)(fl_rnd(f) % 30),
                     1600 + (int)(fl_rnd(f) % 1000), FL_C(s->ramp, 0), P_MOTE);
    }
    fl_sort_by_depth(f);
}

/* ---- public API --------------------------------------------------------------------------------------------- */

void flora_init(flora_t *f, uint32_t seed, int cx, int cy)
{
    uint8_t *p = (uint8_t *)f;
    unsigned i;
    fl_chord_t c;
    for (i = 0; i < sizeof(*f); i++)
        p[i] = 0;
    f->rng = seed ? seed : 0x9E3779B9u;
    f->cx = (int16_t)cx;
    f->cy = (int16_t)cy;
    f->yaw = 160 << 8;
    /* the seedling everyone starts with: C major */
    c.n = 3;
    c.note[0] = 60;
    c.note[1] = 64;
    c.note[2] = 67;
    c.root = 0;
    c.quality = FL_Q_MAJOR;
    c.bass = 48;
    flora_set_chord(f, &c);
}

void flora_set_species(flora_t *f, int species)
{
    species = species ? FL_SP_CORAL : FL_SP_PLANT;
    if (species == f->species)
        return;
    f->species = (uint8_t)species;
    if (f->shape.n)
        fl_build(f);
}

void flora_set_key(flora_t *f, int tonic_pc)
{
    tonic_pc = ((tonic_pc % 12) + 12) % 12;
    if (tonic_pc == f->home)
        return;
    f->home = (uint8_t)tonic_pc;
    if (f->species == FL_SP_CORAL && f->shape.n)
        fl_build(f);
}

void flora_set_chord(flora_t *f, const fl_chord_t *c)
{
    int i, same;
    if (c->n == 0) { /* released: keep the body, let it rest */
        f->chord.n = 0;
        return;
    }
    same = c->n == f->shape.n && c->root == f->shape.root && c->quality == f->shape.quality;
    for (i = 0; same && i < c->n; i++)
        same = c->note[i] == f->shape.note[i];
    f->chord = *c;
    if (same && f->nseg)
        return;
    f->shape = *c;
    fl_build(f);
}

void flora_note_on(flora_t *f, uint8_t note, uint8_t vel)
{
    int pc = note % 12;
    f->pulse_t[pc] = f->t;
    f->pulse_on[pc] = 1;
    f->pulse_n[pc]++;
    if (f->t - f->ring_t[f->ring_i & 3] > 180u || !f->ring_t[f->ring_i & 3]) { /* a chord: one ring, not one a note */
        f->ring_i = (uint8_t)((f->ring_i + 1) & 3);
        f->ring_t[f->ring_i] = f->t ? f->t : 1u;
    }
    if (f->level_in < (int32_t)vel << 9)
        f->level_in = (int32_t)vel << 9;
}

void flora_set_level(flora_t *f, uint16_t level) { f->level_in = level; }

static void fl_place(flora_t *f)
{
    int k;
    int32_t wind = 10 + (f->level >> 11);
    int32_t tw = (int32_t)(f->t * 7 >> 4);   /* the wind: about 2.3 s a sway */
    int32_t tc = (int32_t)(f->t * 5 >> 5);   /* the current: about 6.5 s a surge */
    /* the current carries the coral's upper parts on a slow ellipse: px at 60 px of height, Q8 */
    int32_t cur_x = (fl_fsin(tc) * (640 + (f->level >> 7))) >> 14;
    int32_t cur_z = (fl_fcos(tc * 3 / 4 + 200) * 420) >> 14;
    for (k = 0; k < f->nseg; k++) {
        fl_seg_t *s = &f->seg[f->order[k]];
        int32_t g = s->grow, e, len, az, el, ce, h;
        const fl_seg_t *par = s->parent >= 0 ? &f->seg[s->parent] : 0;
        if (par) {
            s->p0[0] = par->p1[0];
            s->p0[1] = par->p1[1];
            s->p0[2] = par->p1[2];
            s->dist = par->dist + par->len * par->grow / 255;
        } else {
            s->p0[0] = s->p0[1] = s->p0[2] = 0;
            s->dist = 0;
        }
        e = g * g / 255 * (765 - 2 * g) / 255; /* smoothstep 0..255 */
        len = s->len * e / 255;
        h = s->p0[1] >> 8;
        az = s->az;
        el = s->el;
        if (IS_CORAL(s)) {
            /* the tips stir a little on their own */
            el += (fl_fsin(tc * 2 + (s->key & 63) * 37) * (s->depth > 2 ? 10 : 3)) >> 14;
        } else {
            int32_t amp = wind * (h + 4) / 40;
            if (s->kind == FL_K_LEAF || s->kind == FL_K_PETAL)
                amp += wind / 3;
            az += (fl_fsin(tw + h * 9 + (s->key & 63) * 5) * amp) >> 14;
            el += (fl_fsin(tw * 3 / 2 + h * 13 + (s->key & 31) * 11) * amp / 2) >> 14;
            if (s->kind == FL_K_PETAL) { /* loudness opens the flowers, they breathe when quiet */
                int32_t o = (f->level >> 11) + ((fl_fsin((int32_t)(f->t >> 3) + (s->key & 15) * 40) * 6) >> 14);
                if (el < 200)
                    el -= o;
            }
        }
        ce = fl_fcos(el);
        s->p1[0] = s->p0[0] + (((ce * fl_fcos(az)) >> 14) * len >> 14);
        s->p1[1] = s->p0[1] + ((fl_fsin(el) * len) >> 14);
        s->p1[2] = s->p0[2] + (((ce * fl_fsin(az)) >> 14) * len >> 14);
        if (IS_CORAL(s)) { /* the current's shear: what is higher moves more */
            int32_t h0 = s->p0[1] >> 8, h1 = s->p1[1] >> 8, dh = h1 * h1 - h0 * h0;
            s->p1[0] += cur_x * dh / 3600;
            s->p1[2] += cur_z * dh / 3600;
        }
    }
}

static int fl_emitter(const fl_seg_t *s)
{
    return s->alive && s->grow > 180 && s->sx >= 0 && (s->kind == FL_K_BUD || s->kind == FL_K_POLYP);
}

/* a mote out of a flower or a polyp: pollen on land, bubbles and spawn under water; burst: a note's sparkle */
static void fl_emit(flora_t *f, const fl_seg_t *s, int burst)
{
    int32_t x = (int32_t)s->sx << 8, y = (int32_t)(s->sy - 1) << 8;
    uint32_t r = fl_rnd(f);
    if (IS_CORAL(s)) {
        if ((r & 7) == 0 || (burst && (r & 1)))
            fl_spawn(f, x, y, (int)((r >> 4) % 41) - 20, -30 - (int)((r >> 10) % 30) - burst * 20,
                     1800 + (int)((r >> 16) % 1200), FL_C(s->ramp, 0), P_MOTE);
        else
            fl_spawn(f, x, y, (int)((r >> 4) % 13) - 6, -70 - (int)((r >> 10) % 60) - burst * 30,
                     2400 + (int)((r >> 16) % 1600), ((r >> 8) & 3) ? FL_C(FL_R_SAGE, 1) : FL_C(FL_R_SEA, 0),
                     ((r >> 28) & 7) == 0 ? P_BIG_BUBBLE : P_BUBBLE);
    } else {
        fl_spawn(f, x, y, (int)((r >> 4) % (burst ? 121 : 49)) - (burst ? 60 : 24),
                 -30 - (int)((r >> 12) % 30) - burst * 40, 2000 + (int)((r >> 18) % 1500),
                 (r >> 9) & 1 ? FL_C(FL_R_FIRE, 0) : FL_C(f->key_ramp, 0), P_MOTE);
    }
}

void flora_tick(flora_t *f, uint32_t dt)
{
    int i;
    if (dt > 250)
        dt = 250;
    f->t += dt;
    f->yaw += (int32_t)dt * 5; /* one turn in about 50 s */

    /* loudness follower: quick to rise, slow to fall */
    if (f->level_in > f->level)
        f->level = fl_approach(f->level, f->level_in, dt, 40);
    else
        f->level = fl_approach(f->level, f->level_in, dt, 900);
    f->level_in = f->level_in * 15 / 16;

    for (i = 0; i < f->nseg; i++) {
        fl_seg_t *s = &f->seg[i];
        int32_t g = s->grow;
        if (s->alive) {
            if (s->delay > dt)
                s->delay = (uint16_t)(s->delay - dt);
            else {
                s->delay = 0;
                g += (int32_t)dt * 255 / 520;
            }
        } else {
            g -= (int32_t)dt * 255 / 420;
        }
        s->grow = (uint8_t)fl_clampi(g, 0, 255);
        s->az = (int16_t)((s->az + fl_approach(0, fl_wrap_delta(s->az, s->az_t), dt, 260)) & 1023);
        s->el = (int16_t)fl_approach(s->el, s->el_t, dt, 260);
        s->len = (int16_t)fl_approach(s->len, s->len_t, dt, 260);
        s->w = (uint8_t)fl_approach(s->w, s->w_t, dt, 260);
    }
    for (i = 0; i < 12; i++)
        if (f->pulse_on[i] && f->t - f->pulse_t[i] > 1500)
            f->pulse_on[i] = 0;
    fl_place(f);

    /* a note's light reaching a flower or a polyp sparkles out of it */
    for (i = 0; i < f->nseg; i++) {
        fl_seg_t *s = &f->seg[i];
        int pc;
        if (!fl_emitter(s))
            continue;
        pc = fl_seg_pc(f, s);
        if (pc < 0 || !f->pulse_on[pc] || s->pz == f->pulse_n[pc] ||
            (int32_t)(f->t - f->pulse_t[pc]) * 22 < s->dist + s->len)
            continue;
        s->pz = f->pulse_n[pc];
        fl_emit(f, s, 1);
        if (!IS_CORAL(s))
            fl_emit(f, s, 1);
    }

    /* the quiet motes: out of a flower or a polyp now and then, more often when it is loud */
    f->part_acc += dt * (1 + (f->level >> 13));
    while (f->part_acc > 420) {
        int n = 0, pick;
        f->part_acc -= 420;
        for (i = 0; i < f->nseg; i++)
            n += fl_emitter(&f->seg[i]);
        if (!n)
            continue;
        pick = (int)(fl_rnd(f) % (uint32_t)n);
        for (i = 0; i < f->nseg; i++)
            if (fl_emitter(&f->seg[i]) && pick-- == 0) {
                fl_emit(f, &f->seg[i], 0);
                break;
            }
    }
    for (i = 0; i < FL_MAX_PART; i++) {
        fl_part_t *p = &f->part[i];
        if (!p->life)
            continue;
        p->life = p->life > dt ? (uint16_t)(p->life - dt) : 0;
        p->x += p->vx * (int32_t)dt / 64;
        p->y += p->vy * (int32_t)dt / 64;
        if (p->kind == P_BUBBLE || p->kind == P_BIG_BUBBLE)       /* bubbles wobble */
            p->x += fl_fsin((int32_t)(f->t >> 1) + i * 90) * 10 >> 14;
        else                                                       /* motes and petals drift */
            p->x += fl_fsin((int32_t)(f->t >> 3) + i * 70) * 8 >> 14;
        if (p->kind == P_MOTE && p->vx)                            /* and slow down */
            p->vx = (int16_t)(p->vx * 63 / 64);
        if (p->y < -(4 << 8))
            p->life = 0;
    }
    if ((f->t & 1023) < dt)
        fl_compact(f), fl_sort_by_depth(f);
}

/* ---- drawing ------------------------------------------------------------------------------------------------ */

typedef struct {
    int32_t cy, sy, cp, sp, ox, oy;
} cam_t;

static void fl_proj(const cam_t *k, const int32_t p[3], int32_t *sx, int32_t *sy, int32_t *dz)
{
    int32_t xr = (p[0] * k->cy - p[2] * k->sy) >> 14;
    int32_t zr = (p[0] * k->sy + p[2] * k->cy) >> 14;
    *sx = k->ox + xr;
    *sy = k->oy - ((p[1] * k->cp - zr * k->sp) >> 14);
    if (dz)
        *dz = (zr * k->cp + p[1] * k->sp) >> 14;
}

static int32_t fl_pulse_shift(const flora_t *f, const fl_seg_t *s)
{
    int pc = fl_seg_pc(f, s);
    int32_t front;
    if (pc < 0 || !f->pulse_on[pc])
        return 0;
    front = (int32_t)(f->t - f->pulse_t[pc]) * 22; /* Q8 px: ~86 px per second */
    if (s->slot < 12 || s->slot == FL_SLOT_CROWN) {
        /* branches light up once the pulse passes their attachment */
        if (front < s->dist)
            return 0;
        return front - s->dist < (12 << 8) ? 300 : 0;
    }
    return (front >= s->dist && front < s->dist + s->len + (6 << 8)) ? 300 : 0;
}

/* a blade, leaf or petal: a lens along p0 + d with the half-width vector h, profile wf (Q8 of h) at five stations;
 * the two halves take two shades (a fold along the midrib) */
static void fl_blade(flc_t *c, const cam_t *k, const int32_t p0[3], const int32_t d[3], const int32_t h[3],
                     const uint8_t *wf, uint8_t ca, uint8_t cb)
{
    static const uint8_t FR[5] = {0, 64, 128, 192, 255};
    int32_t cx[5], cy[5], lx[5], ly[5], rx[5], ry[5], p[3], q[3];
    int i, j;
    for (i = 0; i < 5; i++) {
        for (j = 0; j < 3; j++)
            p[j] = p0[j] + d[j] * FR[i] / 255;
        fl_proj(k, p, &cx[i], &cy[i], 0);
        for (j = 0; j < 3; j++)
            q[j] = p[j] + h[j] * wf[i] / 255;
        fl_proj(k, q, &lx[i], &ly[i], 0);
        for (j = 0; j < 3; j++)
            q[j] = p[j] - h[j] * wf[i] / 255;
        fl_proj(k, q, &rx[i], &ry[i], 0);
    }
    flc_line(c, cx[0], cy[0], cx[4], cy[4], cb); /* seen edge on, a blade is a line */
    for (i = 0; i < 4; i++) {
        flc_tri(c, cx[i], cy[i], lx[i], ly[i], lx[i + 1], ly[i + 1], ca);
        flc_tri(c, cx[i], cy[i], lx[i + 1], ly[i + 1], cx[i + 1], cy[i + 1], ca);
        flc_tri(c, cx[i], cy[i], cx[i + 1], cy[i + 1], rx[i + 1], ry[i + 1], cb);
        flc_tri(c, cx[i], cy[i], rx[i + 1], ry[i + 1], rx[i], ry[i], cb);
    }
}

static void fl_draw_seg(flora_t *f, flc_t *c, const cam_t *k, fl_seg_t *s)
{
    static const uint8_t WF_LEAF[5] = {0, 200, 255, 170, 0};
    static const uint8_t WF_PETAL[5] = {0, 130, 230, 255, 90};
    int32_t x0, y0, x1, y1;
    int32_t lift = fl_pulse_shift(f, s);
    int px, py;
    fl_proj(k, s->p0, &x0, &y0, 0);
    fl_proj(k, s->p1, &x1, &y1, 0);
    px = (int)(x1 >> 8);
    py = (int)(y1 >> 8);
    s->sx = (int16_t)px;
    s->sy = (int16_t)py;
    switch (s->kind) {
    case FL_K_STEM: {
        /* darker low, lighter up the body: the gradient runs along the whole organism, not each piece */
        int32_t w0 = (int32_t)s->w << 4, w1, t0, t1;
        if (IS_CORAL(s)) {
            w1 = w0 * 4 / 5;
            t0 = fl_clampi(600 - s->depth * 110, 40, 600);
            t1 = fl_clampi(t0 - 110, 0, 600);
        } else {
            w1 = w0 * 3 / 4;
            t0 = fl_clampi(620 - s->depth * 60, 300, 620);
            t1 = t0 - 60;
        }
        flc_stroke(c, x0, y0, x1, y1, w0, w1, s->ramp, t0 - lift, t1 - lift);
        break;
    }
    case FL_K_THORN:
        flc_line(c, x0, y0, x1, y1, FL_C(s->ramp, 2));
        flc_pset(c, px, py, FL_C(s->ramp, lift ? 0 : 1));
        break;
    case FL_K_BUD:
        flc_dot(c, x1, y1, s->grow > 200 ? 1 : 0, FL_C(s->ramp, lift ? 0 : 1));
        flc_pset(c, px, py, FL_C(s->ramp, 0));
        break;
    case FL_K_BERRY:
        flc_dot(c, x1, y1, s->grow > 200 ? 1 : 0, FL_C(s->ramp, 2));
        flc_pset(c, px - (s->grow > 200), py - (s->grow > 200), FL_C(s->ramp, lift ? 0 : 1));
        break;
    case FL_K_POLYP: {
        int r = s->grow > 200 ? 1 : 0, open = s->roll == PO_OPEN || (s->roll == PO_KNOB && f->level > 40000);
        if (s->roll == PO_BULB) { /* a bulb: a round glossy bead */
            flc_dot(c, x1, y1, s->grow > 200 ? 2 : 1, FL_C(s->ramp, 1));
            flc_pset(c, px - 1, py - 1, FL_C(s->ramp, 0));
            break;
        }
        flc_dot(c, x1, y1, r, FL_C(s->ramp, lift ? 0 : 1));
        flc_pset(c, px, py - r, FL_C(s->ramp, 0));
        if (open && s->grow > 220) { /* the tentacles, longer when it is loud, turning slowly */
            int32_t L = (s->roll == PO_OPEN ? 2 << 8 : 1 << 8) + (f->level >> 6), a0 = (int32_t)(f->t >> 5) + (s->key & 63) * 16;
            int t;
            if (L > 4 << 8)
                L = 4 << 8;
            for (t = 0; t < 6; t++) {
                int32_t a = a0 + t * 171, ex = x1 + ((fl_fcos(a) * L) >> 14),
                        ey = y1 - (1 << 8) + ((fl_fsin(a) * L * 5 / 8) >> 14);
                flc_line(c, x1, y1 - (1 << 8), ex, ey, FL_C(s->ramp, 0));
            }
        }
        break;
    }
    case FL_K_LEAF:
    case FL_K_PETAL: {
        int32_t d[3], h[3];
        int32_t hw = (int32_t)s->w << 3; /* half width, Q8 */
        int32_t ca, sa, cr, sr, nx, ny, nz, zr, nzv;
        int front, i;
        for (i = 0; i < 3; i++)
            d[i] = s->p1[i] - s->p0[i];
        ca = fl_fcos(s->az);
        sa = fl_fsin(s->az);
        cr = fl_fcos(s->roll * 4);
        sr = fl_fsin(s->roll * 4);
        hw = hw * s->grow / 255;
        h[0] = (-sa * cr >> 14) * hw >> 14;
        h[1] = sr * hw >> 14;
        h[2] = (ca * cr >> 14) * hw >> 14;
        /* which side faces the camera: the normal is d x h */
        nx = (d[1] * h[2] - d[2] * h[1]) >> 8;
        ny = (d[2] * h[0] - d[0] * h[2]) >> 8;
        nz = (d[0] * h[1] - d[1] * h[0]) >> 8;
        zr = (nx * k->sy + nz * k->cy) >> 14;
        nzv = (zr * k->cp + ny * k->sp) >> 14;
        front = nzv <= 0; /* the upper face, seen from above */
        if (s->kind == FL_K_PETAL) {
            fl_blade(c, k, s->p0, d, h, WF_PETAL, FL_C(s->ramp, front ? 0 : 1), FL_C(s->ramp, front ? 1 : 2));
            if (lift)
                flc_pset(c, px, py, FL_INK);
        } else {
            fl_blade(c, k, s->p0, d, h, WF_LEAF, FL_C(s->ramp, front ? (lift ? 0 : 1) : 2),
                     FL_C(s->ramp, front ? 2 : 3));
        }
        break;
    }
    default:
        break;
    }
}

static void fl_draw_part(flora_t *f, flc_t *c, int i)
{
    fl_part_t *p = &f->part[i];
    int x = (int)(p->x >> 8), y = (int)(p->y >> 8);
    uint8_t col = p->life < 300 ? FL_BG3 : p->col;
    switch (p->kind) {
    case P_PETAL:
        flc_pset(c, x, y, col);
        flc_pset(c, x + 1, y, col);
        break;
    case P_BIG_BUBBLE: /* a ring */
        flc_pset(c, x, y - 1, col);
        flc_pset(c, x - 1, y, col);
        flc_pset(c, x + 1, y, col);
        flc_pset(c, x, y + 1, col);
        break;
    case P_MOTE: /* pollen glints now and then */
        flc_pset(c, x, y, p->life >= 300 && (((f->t >> 7) + (uint32_t)i) & 7) == 0 ? FL_INK : col);
        break;
    default:
        flc_pset(c, x, y, col);
        break;
    }
}

/* ---- the backdrop ------------------------------------------------------------------------------------------- */

static const uint8_t fl_bayer[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
static const uint8_t FL_DUSK[5] = {FL_BG, FL_L0, FL_L1, FL_L2, FL_L3};
static const uint8_t FL_WATER[5] = {FL_BG, FL_S0, FL_S1, FL_S2, FL_S3};

/* a gradient through the stops, ordered-dithered: t in Q8 steps (0 = st[0] .. (n - 1) * 256 = st[n - 1]) */
static uint8_t fl_grad(const uint8_t *st, int n, int32_t t, int x, int y)
{
    int32_t i;
    if (t <= 0)
        return st[0];
    if (t >= (int32_t)(n - 1) << 8)
        return st[n - 1];
    i = t >> 8;
    return (int32_t)((t & 255) >> 4) > fl_bayer[(y & 3) * 4 + (x & 3)] ? st[i + 1] : st[i];
}

static void fl_put(flc_t *c, int x, int y, uint8_t col)
{
    if ((unsigned)x < (unsigned)c->w && (unsigned)y < (unsigned)c->h)
        c->px[y * c->w + x] = col;
}

/* the sky: dark overhead, the glow low down, full at the horizon hz (Q8 steps through FL_DUSK) */
static int32_t fl_sky_t(int y, int hz) { return fl_clampi(y * y * 1024 / (hz * hz), 0, 1024); }
static int fl_hill_far(int x, int h) { return h - 18 + ((fl_fsin(x * 9 + 100) * 4) >> 14) + ((fl_fsin(x * 23 + 40) * 2) >> 14); }
static int fl_hill_near(int x, int h) { return h - 8 + ((fl_fsin(x * 15 + 500) * 2) >> 14); }

/* dusk; the scene adds twinkling stars, a crescent moon with a halo, far hills against the glow, the near ground */
static void fl_bd_dusk(flora_t *f, flc_t *c, int scene)
{
    int W = c->w, H = c->h, x, y, i, hz = scene ? 60 : H + 6; /* (the hills hide the horizon) */
    uint8_t *px = c->px;
    for (y = 0; y < H; y++)
        for (x = 0; x < W; x++)
            px[y * W + x] = fl_grad(FL_DUSK, 5, fl_sky_t(y, hz), x, y);
    if (!scene)
        return;
    for (i = 0; i < 18; i++) { /* (kept off the chord's name, the key and the moon) */
        uint32_t h = fl_hash32((uint32_t)i * 7919u + 13u);
        int sx = (int)(h % (uint32_t)W), sy = (int)((h >> 8) % 44u), tw = (int)(((f->t >> 6) + h) % 48u);
        if ((sx < 38 && sy < 20) || (sx > 84 && sy < 10) || (sx > 84 && sy > 14 && sy < 36))
            continue;
        px[sy * W + sx] = tw < 2 ? FL_C(FL_R_SAND, 0) : (h >> 20) & 1 ? FL_C(FL_R_SAND, 2) : FL_C(FL_R_SAGE, 2);
    }
    {
        int mx = W * 4 / 5, my = 24;
        for (y = my - 10; y <= my + 10; y++)
            for (x = mx - 10; x <= mx + 10; x++) {
                int dx = x - mx, dy = y - my, d2 = dx * dx + dy * dy, e2 = (dx - 3) * (dx - 3) + (dy + 2) * (dy + 2);
                if (d2 <= 36)
                    fl_put(c, x, y, e2 > 25 ? (d2 > 25 || e2 < 34 ? FL_C(FL_R_SAND, 1) : FL_C(FL_R_SAND, 0)) : FL_L1);
                else if (d2 <= 110)
                    fl_put(c, x, y, fl_grad(FL_DUSK, 5, fl_sky_t(y, hz) + (110 - d2) * 3, x, y));
            }
    }
    for (x = 0; x < W; x++) {
        int hf = fl_hill_far(x, H), hn = fl_hill_near(x, H);
        for (y = hf; y < H; y++)
            px[y * W + x] = y >= hn ? FL_BG : y == hf ? FL_L1 : FL_L0;
    }
    for (i = 0; i < 16; i++) { /* grass tufts on the near ground */
        uint32_t h = fl_hash32((uint32_t)i * 104729u + 5u);
        int gx = (int)(h % (uint32_t)W), hn = fl_hill_near(gx, H), k = 1 + (int)((h >> 9) % 3u);
        for (y = hn - k; y < hn; y++)
            px[y * W + gx] = FL_BG;
        if (h & 0x100000u)
            fl_put(c, gx + 1, hn - 1, FL_BG);
    }
}

/* water lit from the surface, dark below */
static void fl_bd_water(flc_t *c)
{
    int W = c->w, H = c->h, x, y;
    for (y = 0; y < H; y++)
        for (x = 0; x < W; x++)
            c->px[y * W + x] = fl_grad(FL_WATER, 5, (H - y) * (H - y) * 1024 / (H * H), x, y);
}

/* the coral's scene: deep water with soft lights out of focus drifting by (half-dithered discs, the nearer ones
 * bigger and fainter) */
static void fl_bd_bokeh(flora_t *f, flc_t *c)
{
    int W = c->w, H = c->h, x, y, i;
    for (y = 0; y < H; y++)
        for (x = 0; x < W; x++)
            c->px[y * W + x] = fl_grad(FL_WATER, 5, (H - y) * 300 / H, x, y);
    for (i = 0; i < 6; i++) {
        uint32_t h = fl_hash32((uint32_t)i * 2654435761u + 99u);
        int r = 4 + (int)(h % 9u), span = W + 2 * r, sp = 1 + (int)((h >> 20) % 3u), lo = r < 8 ? 2 : 1;
        int bx = (int)(((h >> 4) % (uint32_t)W + (f->t >> 7) * (uint32_t)sp / 16u) % (uint32_t)span) - r;
        int by = 8 + (int)((h >> 12) % (uint32_t)(H - 16)) + ((fl_fsin((int32_t)(f->t >> 5) + (int32_t)(h & 1023)) * 2) >> 14);
        int dx, dy;
        for (dy = -r; dy <= r; dy++)
            for (dx = -r; dx <= r; dx++) {
                int d2 = dx * dx + dy * dy, qx = bx + dx, qy = by + dy;
                if (d2 > r * r || (unsigned)qx >= (unsigned)W || (unsigned)qy >= (unsigned)H)
                    continue;
                if (fl_bayer[(qy & 3) * 4 + (qx & 3)] < (d2 * 4 < r * r * 3 ? 8 : 4))
                    c->px[qy * W + qx] = FL_WATER[lo + 1];
            }
    }
}

/* rings of sound: every chord sends a ring out of the organism; when it is quiet one comes now and then */
static void fl_bd_rings(flora_t *f, flc_t *c, int sea)
{
    const uint8_t *st = sea ? FL_WATER : FL_DUSK;
    int i, cx = f->cx, cy = f->cy - 34;
    uint32_t idle = f->t % 4200u;
    for (i = 0; i < 5; i++) {
        uint32_t age = i < 4 ? f->t - f->ring_t[i] : idle;
        int32_t r, a, steps, fade;
        if ((i < 4 && !f->ring_t[i]) || age > 2400u || (i == 4 && f->level > 8000))
            continue;
        r = (int32_t)age * 36 / 1000 + 3;
        fade = 768 - (int32_t)age * 768 / 2400; /* bright when new, gone at 2.4 s */
        if (i == 4)
            fade = fade / 2;
        steps = r * 7 + 16;
        for (a = 0; a < steps; a++) {
            int32_t ang = a * 1024 / steps;
            int x = cx + ((fl_fcos(ang) * r) >> 14), y = cy + ((fl_fsin(ang) * r) >> 14);
            /* it thins out as it fades: fewer of its pixels drawn */
            if ((unsigned)x < (unsigned)c->w && (unsigned)y < (unsigned)c->h && fl_bayer[(y & 3) * 4 + (x & 3)] * 24 < fade)
                c->px[y * c->w + x] = st[fade > 400 ? 4 : 3];
        }
    }
}

static void fl_backdrop(flora_t *f, flc_t *c)
{
    int sea = f->species == FL_SP_CORAL;
    switch (f->backdrop) {
    case FL_BD_SCENE:
        if (sea)
            fl_bd_bokeh(f, c);
        else
            fl_bd_dusk(f, c, 1);
        break;
    case FL_BD_RIPPLES:
        if (sea)
            fl_bd_water(c);
        else
            fl_bd_dusk(f, c, 0);
        fl_bd_rings(f, c, sea);
        break;
    default:
        flc_clear(c, FL_BG);
        break;
    }
}

void flora_render(flora_t *f, flc_t *c)
{
    cam_t k;
    int i, b;
    /* painter's order: 48 depth buckets, far to near */
    static uint8_t bucket_head[48];
    static uint8_t next[FL_MAX_SEG];
    int32_t yaw = f->yaw >> 8;

    k.cy = fl_fcos(yaw);
    k.sy = fl_fsin(yaw);
    k.cp = fl_fcos(PITCH);
    k.sp = fl_fsin(PITCH);
    k.ox = (int32_t)f->cx << 8;
    k.oy = (int32_t)f->cy << 8;

    fl_backdrop(f, c);
    for (i = 0; i < FL_MAX_PART; i++) /* far particles first */
        if (f->part[i].life && (i & 1))
            fl_draw_part(f, c, i);

    for (b = 0; b < 48; b++)
        bucket_head[b] = 0xFF;
    for (i = 0; i < f->nseg; i++) {
        fl_seg_t *s = &f->seg[i];
        int32_t mid[3], sx, sy, dz;
        if (!s->grow)
            continue;
        mid[0] = (s->p0[0] + s->p1[0]) / 2;
        mid[1] = (s->p0[1] + s->p1[1]) / 2;
        mid[2] = (s->p0[2] + s->p1[2]) / 2;
        fl_proj(&k, mid, &sx, &sy, &dz);
        b = fl_clampi((dz >> 8) / 2 + 24, 0, 46);
        if (s->kind == FL_K_BUD || s->kind == FL_K_POLYP)
            b = fl_clampi(b + 1, 0, 47);
        next[i] = bucket_head[b];
        bucket_head[b] = (uint8_t)i;
    }
    for (b = 0; b < 48; b++)
        for (i = bucket_head[b]; i != 0xFF; i = next[i])
            fl_draw_seg(f, c, &k, &f->seg[i]);

    for (i = 0; i < FL_MAX_PART; i++) /* near particles */
        if (f->part[i].life && !(i & 1))
            fl_draw_part(f, c, i);
}
