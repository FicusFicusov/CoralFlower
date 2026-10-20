/* SPDX-License-Identifier: GPL-3.0-only
 * CoralFlower: the SEQUENCER screens' pictures (the controls: cf_sequi.c). Included by cf_screens.c after cf_views.c.
 *
 * A garden: a chord step is a bloom in the colour its root gives the plant's blooms, a bass step a tulip, a layer
 * step a star flower, a step that lasts is a runner with leaves to where it ends; a drum hit is a daisy in its drum's colour (the owner, 2026-10-18:
 * the drums with flowers too). A bar is a row; the bars that play are held between two stalks (the owner's green
 * side bars), the ones that do not stay dark. Every picture is drawn from the same few sprites. */

/* ------------------------------------------------------------ sprites --- */
/* a sprite's letters: a b c d the colour's ramp light to dark, e f the eye's, g h i the leaf's (lime), '.' nothing */
typedef struct { uint8_t ramp, eye; } sq_ink_t;
static uint8_t sq_dim;                           /* a bar outside the window: its flowers two steps darker */
static uint8_t sq_shade(uint32_t ramp, int s) { s += sq_dim ? 2 : 0; return FL_C(ramp, s > 3 ? 3 : s); }
static void sq_sprite(int x, int y, const char *const *row, int w, int h, sq_ink_t k)
{
    int i, j;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++) {
            char ch = row[j][i];
            uint8_t c;
            switch (ch) {
            case 'a': case 'b': case 'c': case 'd': c = sq_shade(k.ramp, ch - 'a'); break;
            case 'e': case 'f': c = sq_shade(k.eye, ch - 'e'); break;
            case 'g': case 'h': case 'i': c = sq_shade(FL_R_LIME, ch - 'g'); break;
            default: continue;
            }
            fb_pset(x + i, y + j, c);
        }
}
static const char *const SQ_BLOOM7[7] = {".ab.ba.", "abbabba", "bbbebbb", ".aefea.", "bbbebbb", "abbabba", ".ab.ba."};
static const char *const SQ_BLOOM5[5] = {".a.a.", "abeba", ".efe.", "abeba", ".a.a."};
static const char *const SQ_TULIP7[7] = {".a.a.a.", ".ababa.", ".bbbbb.", ".bbbcb.", "..bcc..", "...h...", "...h..."};
static const char *const SQ_TULIP5[5] = {"a.a.a", "abbba", ".bcb.", "..h..", "..h.."};
static const char *const SQ_STAR7[7] = {"...a...", "..aba..", ".abeba.", "abefeba", ".abeba.", "..aba..", "...a..."};
static const char *const SQ_STAR5[5] = {"..a..", ".aba.", "abeba", ".aba.", "..a.."};
static const char *const SQ_DAISY7[7] = {".a...a.", "..aba..", ".abeba.", "aaefeaa", ".abeba.", "..aba..", ".a...a."};
static const char *const SQ_DAISY5[5] = {"a.a.a", ".beb.", "aefea", ".beb.", "a.a.a"};
static const char *const SQ_LOW3[3] = {".b.", "bcb", ".b."};   /* (a low daisy: its colour's darker steps) */

/* the blooms' colours go round the circle of fifths, as the plant's (flora.c fl_build) */
static sq_ink_t sq_root_ink(uint32_t pc)
{
    static const uint8_t R[6] = {FL_R_FIRE, FL_R_PEACH, FL_R_ROSE, FL_R_ORCHID, FL_R_SEA, FL_R_SAND};
    sq_ink_t k;
    k.ramp = R[(pc % 12u) * 7u % 12u % 6u];
    k.eye = k.ramp == FL_R_FIRE ? FL_R_COPPER : FL_R_FIRE;
    return k;
}
static sq_ink_t sq_lane_ink(uint32_t l)
{
    static const uint8_t R[SQ_NLANE] = {FL_R_ROSE, FL_R_PEACH, FL_R_FIRE, FL_R_SEA, FL_R_JADE, FL_R_COPPER,
                                        FL_R_COPPER, FL_R_COPPER, FL_R_ORCHID, FL_R_SAND, FL_R_SAGE};
    /* (the three toms: one family, one colour) */
    sq_ink_t k;
    k.ramp = R[l % SQ_NLANE];
    k.eye = k.ramp == FL_R_FIRE ? FL_R_COPPER : FL_R_FIRE;
    return k;
}

/* the grid's rows are garden beds: the ground at the row's foot, a blade of grass on every step (a taller one on
 * the beats), and what is written there grows out of it on a stem */
#define SQG_X0 12                                /* the cells: x SQG_X0 .. SQG_X1, the bars' rows SQG_RH apart */
#define SQG_X1 228
#define SQG_Y0 (Y_VIEW + 19)
#define SQG_RH 15
#define SQG_GND 13                               /* the ground: this far under a row's top */

/* a flower on its stem, for a step w px wide whose middle is cx, in the row whose top is y: what 0 a bloom (a
 * chord), 1 a tulip (a bass note), 2 a daisy (a drum), 3 a star flower (the layer); low: a short one (another drum
 * than the one picked) */
static void sq_flower(int cx, int y, int w, int what, sq_ink_t k, int low)
{
    int gy = y + SQG_GND, big = w >= 11 && !low, top = low ? y + 6 : big ? y + 1 : y + 3, n = big ? 7 : 5, sy;
    const char *const *spr = what == 3 ? (big ? SQ_STAR7 : SQ_STAR5) : what == 2 ? (big ? SQ_DAISY7 : SQ_DAISY5) :
                             what ? (big ? SQ_TULIP7 : SQ_TULIP5) :
                             (big ? SQ_BLOOM7 : SQ_BLOOM5);
    sy = top + n - (what == 1 ? 2 : 0);          /* (the tulip's own stem is in its sprite) */
    if (gy > sy)
        fb_rect(cx, sy, 1, gy - sy, sq_shade(FL_R_LIME, 1));
    if (!low && gy - sy >= 3) {                  /* a leaf on the stem */
        fb_pset(cx + 1, gy - 2, sq_shade(FL_R_LIME, 1));
        fb_pset(cx + 2, gy - 3, sq_shade(FL_R_LIME, 0));
    }
    if (low)
        sq_sprite(cx - 1, top + 1, SQ_LOW3, 3, 3, k);
    else
        sq_sprite(cx - n / 2, top, spr, n, n, k);
}

/* a step that lasts: a runner along the ground from its flower to its last step, a sprout on every step on the way,
 * a bud of the flower's colour on the last */
static void sq_runner(int x0, int x1, int gy, const int *cx, int n, sq_ink_t k, int ends)
{
    int i;
    fb_hline(x0, gy - 1, x1 - x0 + 1, sq_shade(FL_R_LIME, 1));
    for (i = 0; i < n; i++) {
        fb_pset(cx[i], gy - 2, sq_shade(FL_R_LIME, 1));
        fb_pset(cx[i] - 1, gy - 3, sq_shade(FL_R_LIME, 0));
        fb_pset(cx[i] + 1, gy - 3, sq_shade(FL_R_LIME, 0));
    }
    if (!ends)                                   /* (it runs on into the next bar's row) */
        return;
    fb_pset(x1, gy - 4, sq_shade(k.ramp, 1));
    fb_pset(x1, gy - 5, sq_shade(k.ramp, 0));
}

/* what a step shows: as written, or (a pattern that follows the key) what its keys play under HOME's key now. The
 * resolved roots, names and first notes are kept for the pattern shown and redone when the key or the steps change */
static struct {
    uint32_t sig;
    uint8_t pat, root[SQ_NEV], note0[SQ_NEV];
    uint16_t mask[SQ_NEV];
} sqv;
static void sqv_update(const sq_pat_t *p)
{
    uint32_t sig = cfs_fnv(&cs.chord, sizeof cs.chord) ^ cfs_fnv(p->ev, p->nev * sizeof p->ev[0]) * 31u ^ p->nev ^
                   (uint32_t)p->follow << 8, i;
    if (sig == sqv.sig && sqv.pat == squ.pat)
        return;
    sqv.sig = sig;
    sqv.pat = squ.pat;
    for (i = 0; i < p->nev; i++) {
        sq_ev_t now;
        const sq_ev_t *e = p->follow && sq_resolve(&squ_cm, &p->ev[i], &cs.chord, &now) ? &now : &p->ev[i];
        sqv.root[i] = e->root;
        sqv.mask[i] = e->mask;
        sqv.note0[i] = e->note[0];
    }
}
static uint32_t sqv_root(uint32_t i) { return sqv.root[i]; }

/* ------------------------------------------------------------ the grid --- */
static int sq_cell_x(int i, int st) { return SQG_X0 + i * (SQG_X1 - SQG_X0) / st; }
/* where a step's flower stands: at its start, the same few pixels into its cell whatever the bar's count of steps,
 * so a beat stands over the same beat in a bar of 8ths and a bar of 16ths (they stood at their cells' middles, and a
 * bar of 8ths looked shifted against one of 16ths: the owner, 2026-10-19) */
static int sq_step_x(int i, int st) { return sq_cell_x(i, st) + 4; }

/* the stalks either side of the bars that play (the window: bars start .. start + bars - 1): two green stems, a leaf
 * at every bar */
static void sq_stalks(int start, int bars)
{
    int y0 = SQG_Y0 + start * SQG_RH, y1 = SQG_Y0 + (start + bars) * SQG_RH - 2, b, side;
    uint8_t c0 = FL_C(FL_R_LIME, 1), c1 = FL_C(FL_R_LIME, 2), ch = FL_C(FL_R_LIME, 0);
    for (side = 0; side < 2; side++) {
        int x = side ? FB_W - 6 : 4, d = side ? -1 : 1, xi = side ? x - 1 : x + 2;
        fb_rect(x, y0, 1, y1 - y0, c0);
        fb_rect(x + 1, y0, 1, y1 - y0, c1);
        fb_rect(x, y0 - 2, 2, 2, ch);            /* (a bud on the top) */
        for (b = start; b < start + bars; b++) {
            int y = SQG_Y0 + b * SQG_RH + 6;
            fb_pset(xi, y, c0);
            fb_pset(xi + d, y - 1, ch);
            fb_pset(xi + 2 * d, y - 1, ch);
        }
    }
}

/* a row's ground and grass (a bar that plays), or its faint dots (one that does not) */
static void sq_bed(int b, int st, int on, uint32_t busy)   /* busy: bit i, step i has something growing */
{
    int y = SQG_Y0 + b * SQG_RH, gy = y + SQG_GND, i;
    if (on)
        fb_hline(SQG_X0 - 2, gy, SQG_X1 - SQG_X0 + 3, FL_C(FL_R_LIME, 3));
    for (i = 0; i < st; i++) {
        int cx = sq_step_x(i, st), beat = (i * cs.beats) % st == 0;
        if ((busy >> i) & 1u)
            continue;
        if (!on) {
            if (beat)
                fb_pset(cx, gy - 1, FL_BG3);
        } else {
            fb_rect(cx, gy - (beat ? 3 : 1), 1, beat ? 3 : 1, FL_C(FL_R_LIME, 2));
            if (beat) {                          /* (a beat: a tuft, its tips lit) */
                fb_pset(cx, gy - 4, FL_C(FL_R_LIME, 1));
                fb_pset(cx + 1, gy - 2, FL_C(FL_R_LIME, 2));
                fb_pset(cx - 1, gy - 2, FL_C(FL_R_LIME, 2));
                fb_pset(cx + 2, gy - 3, FL_C(FL_R_LIME, 1));
                fb_pset(cx - 2, gy - 3, FL_C(FL_R_LIME, 1));
            }
        }
    }
}

/* one bar of CHORDS / BASS / LAYER: the bed, the runners, then the flowers (a bar outside the window: its flowers
 * dark, no bed: it keeps what it has, it does not play). A step stands where it is in time: off the grid too (written
 * on a finer one) */
static int sq_u_x(uint32_t u) { return SQG_X0 + (int)(u * (uint32_t)(SQG_X1 - SQG_X0) / SQ_U); }   /* (u: in a bar) */
static int sq_pos_x(uint32_t u) { return sq_u_x(u) + 4; }   /* (a flower at u: as sq_step_x) */
static void sq_bar_notes(const sq_pat_t *p, uint32_t trk, int rb, int on)
{
    int b = rb, st = (int)sq_steps(p, trk, (uint32_t)b), y = SQG_Y0 + rb * SQG_RH, i;   /* (rb: the row, its bar) */
    uint32_t e, busy = 0, b0 = (uint32_t)b * SQ_U, b1 = b0 + SQ_U;
    for (e = 0; e < p->nev; e++) {               /* (the steps something grows on or runs over: no grass there) */
        const sq_ev_t *v = &p->ev[e];
        uint32_t s0, s1;
        if (v->trk != trk || !sq_ev_on(v))
            continue;
        s0 = sq_ev_start(p, v);
        s1 = sq_ev_end(p, v);
        if (s1 <= b0 || s0 >= b1)
            continue;
        for (i = 0; i < st && i < 32; i++) {
            uint32_t u = b0 + (uint32_t)i * SQ_U / (uint32_t)st;
            if (u >= s0 && u < s1)
                busy |= 1u << i;
        }
    }
    sq_bed(rb, st, on, busy);
    sq_dim = (uint8_t)!on;
    for (e = 0; e < p->nev; e++) {
        const sq_ev_t *v = &p->ev[e];
        int x0, w0, cx[SQ_MAXST], n = 0;
        uint32_t s0, s1;
        sq_ink_t k;
        if (v->trk != trk || !sq_ev_on(v))       /* (a step past this metre's bar: asleep, unseen) */
            continue;
        s0 = sq_ev_start(p, v);
        s1 = sq_ev_end(p, v);
        k = sq_root_ink(sqv_root(e));
        if (v->bar < (uint32_t)b && s1 > b0) {   /* a step from a bar above runs on into this one */
            uint32_t end = s1 < b1 ? s1 : b1;
            for (i = 0; i < st; i++) {
                uint32_t u = b0 + (uint32_t)i * SQ_U / (uint32_t)st;
                if (u < end && n < SQ_MAXST)
                    cx[n++] = sq_step_x(i, st);
            }
            sq_runner(SQG_X0 - 2, sq_u_x(end - b0) - 1, y + SQG_GND, cx, n, k, s1 <= b1);
            continue;
        }
        if (v->bar != (uint32_t)b)
            continue;
        x0 = sq_pos_x(v->pos);
        w0 = sq_cell_x(1, st) - sq_cell_x(0, st);   /* (the flower's room: a step, or less when it is shorter) */
        if (sq_u_x(v->len) - sq_u_x(0) < w0)
            w0 = sq_u_x(v->len) - sq_u_x(0);
        if (v->len > SQ_U / (uint32_t)st) {      /* (the runner to where the step ends: the next one's start, or on
                                                  * over the bar line; a sprout on every step of the grid on the way) */
            uint32_t end = s1 < b1 ? s1 : b1;
            for (i = 0; i < st && n < SQ_MAXST; i++) {
                uint32_t u = b0 + (uint32_t)i * SQ_U / (uint32_t)st;
                if (u > s0 && u < end)
                    cx[n++] = sq_step_x(i, st);
            }
            sq_runner(x0, end >= b1 ? SQG_X1 + 2 : sq_u_x(end - b0) - 1, y + SQG_GND, cx, n, k, s1 <= b1);
        }
        sq_flower(x0, y, w0, trk == SQ_LAYER ? 3 : trk == SQ_BASS, k, 0);
    }
    sq_dim = 0;
}

/* one bar of DRUMS: the drum picked as tall daisies, the other drums' hits as low ones */
static void sq_bar_drums(const sq_pat_t *p, int rb, int on)
{
    int b = rb, st = (int)sq_dsteps(), y = SQG_Y0 + rb * SQG_RH, i;
    int w = sq_cell_x(1, st) - sq_cell_x(0, st);
    uint32_t l, busy = 0;
    for (l = 0; l < SQ_NLANE; l++)               /* (a hit: no grass there) */
        busy |= p->hit[l][b];
    sq_bed(rb, st, on, busy);
    sq_dim = (uint8_t)!on;
    for (i = 0; i < st; i++) {                   /* (every second step as late as SWING plays it) */
        int x = sq_step_x(i, st);
        if (i & 1)
            x += p->swing * w / 200;
        if ((p->hit[squ.lane][b] >> i) & 1u) {
            sq_flower(x, y, w, 2, sq_lane_ink(squ.lane), 0);
        } else {
            for (l = 0; l < SQ_NLANE; l++)
                if ((p->hit[l][b] >> i) & 1u) {
                    sq_flower(x, y, w, 2, sq_lane_ink(l), 1);
                    break;
                }
        }
    }
    sq_dim = 0;
}

/* playing: a firefly over the bar that plays, where the time is in it, and pollen rising from the flower that
 * sounded last (from the organism itself: never over the canvas at random) */
static int sq_playing_here(void) { return cf_sn.sq_on && cf_sn.sq_pat == squ.pat && cf_sn.sq_bt; }
static void sq_firefly(int x, int y)
{
    fb_rect(x, y, 2, 2, FL_C(FL_R_SAND, 0));
    fb_pset(x - 1, y, FL_C(FL_R_LIME, 0));
    fb_pset(x + 2, y + 1, FL_C(FL_R_LIME, 0));
    fb_pset(x, y - 1, FL_C(FL_R_LIME, 1));
    fb_pset(x + 1, y + 2, FL_C(FL_R_LIME, 1));
    fb_pset(x - 3, y + 1, FL_C(FL_R_LIME, 1));   /* (its trail) */
    fb_pset(x - 5, y + 1, FL_C(FL_R_LIME, 2));
}
static void sq_play_marks(const sq_pat_t *p, uint32_t trk)
{
    int b = (int)sq_trk_bar(p, trk, cf_sn.sq_n), y = SQG_Y0 + b * SQG_RH, k, cx, age;   /* (the track's bar) */
    if (!sq_playing_here())
        return;
    sq_firefly(SQG_X0 + (int)(cf_sn.sq_t * (uint32_t)(SQG_X1 - SQG_X0) / cf_sn.sq_bt), y + 1);
    k = cf_sn.sq_lit[trk];                       /* (where it sounded: 1/96 of the bar) */
    age = (int)(cf_sn.now_ms - cf_sn.sq_lit_t[trk]);
    if (k == 0xFF || k >= (int)SQ_U || age < 0 || age > 450)
        return;
    cx = sq_pos_x((uint32_t)k);
    {
        uint8_t c = FL_C(trk == SQ_DRUMS ? sq_lane_ink(squ.lane).ramp : FL_R_SAND, age < 220 ? 0 : 1);
        fb_pset(cx - 2, y - age / 90, c);
        fb_pset(cx + 2, y - 1 - age / 120, c);
        fb_pset(cx, y - 2 - age / 70, c);
    }
}

/* step i's name ("Cmaj7"), a bass step's note ("C2"), as it plays (sqv) */
static void sq_ev_name(const sq_pat_t *p, uint32_t i, char *b)
{
    if (p->ev[i].trk == SQ_BASS || sqv.mask[i] == 1u) {
        char a[4];
        cf_pc_name(sqv.note0[i] % 12u, &cu_spell, a);
        str_cpy(b, a, 4);
        kint(b + str_len(b), sqv.note0[i] / 12 - 1, 0);
        return;
    }
    cf_chord_name(sqv.root[i], sqv.mask[i], -1, &cu_spell, b);
}

static void draw_sq_track(void)
{
    sq_pat_t *p = sq_cur();
    sqv_update(p);
    char b[24], c[24];
    int bar, drums = squ.trk == SQ_DRUMS;
    fb_rect(0, Y_VIEW, FB_W, H_VIEW, FL_BG);
    /* the line over the grid: the pattern (the owner, 2026-10-19: "Pattern A", not the frame's bar and step), or a bar
     * being copied, then what is in the frame */
    if (squ.mv) {
        str_cpy(b, "Moving", sizeof b);          /* (EDIT held, SELECT turned: the chord rides with the frame) */
    } else if (squ.cpy) {
        str_cpy(b, "Erasing", sizeof b);         /* (SEL held, SELECT turned) */
    } else if (squ.ocpy) {                       /* (OCT+ held, SELECT turned) */
        str_cpy(b, "Copy bar ", sizeof b);
        kint(b + str_len(b), squ.ofrom + 1, 0);
        str_cpy(b + str_len(b), " to ", 5);
        kint(b + str_len(b), squ.bar + 1, 0);
    } else {
        str_cpy(b, "Pattern ", sizeof b);
        b[8] = (char)('A' + squ.pat);
        b[9] = 0;
    }
    fb_text(&CF_FONT_UI, 8, Y_VIEW + 4, b, squ.cpy || squ.ocpy || squ.mv ? C_ACC(0) : FL_INK2, 1);
    if (!squ.cpy && !squ.ocpy && !drums && (p->arp >> squ.trk) & 1u) {   /* (the track's ARP on: its button named,
                                                  * then its state: a chip alone read as a button, not as "on") */
        int ax = 8 + fb_text_w(&CF_FONT_UI, b, 1) + 6;
        ax += draw_chip(ax, Y_VIEW + 4, "ARP") + 3;
        fb_text(&CF_FONT_UI, ax, Y_VIEW + 4, "on", C_ACC(1), 1);
    }
    c[0] = 0;
    if (drums) {
        str_cpy(c, sq_lane_name(squ.lane), sizeof c);
    } else {
        int i = sq_at(p, squ.trk, squ.bar, squ.step);
        if (squ.nofr)
            c[0] = 0;                            /* (no frame: nothing to name) */
        else if (i >= 0)
            sq_ev_name(p, (uint32_t)i, c);
        else
            str_cpy(c, squ.trk == SQ_BASS ? "play a note" : "play a chord", sizeof c);
    }
    if (cf_sn.sq_on && cf_sn.sq_rec == squ.trk && cf_sn.sq_pat == squ.pat) {   /* a take into this track: where
                                                  * it is, not "play a chord" (that is the frame's step writing) */
        if (cf_sn.sq_cin) {
            str_cpy(c, "count-in", sizeof c);
        } else {
            str_cpy(c, "REC bar ", sizeof c);
            kint(c + 8, (int)sq_trk_bar(p, squ.trk, cf_sn.sq_n) + 1, sqk.on ? " of 8" : 0);
        }
        fb_text_r(&CF_FONT_UI, 232, Y_VIEW + 4, c, FL_C(FL_R_FIRE, 1), 1);
    } else {
        char d[24];
        fb_signs(c, d, sizeof d);
        fb_text_r(&CF_FONT_UI, 232, Y_VIEW + 4, d, drums ? FL_C(sq_lane_ink(squ.lane).ramp, 0) : C_ACC(0), 1);
    }
    sq_stalks((int)sq_ts(p, squ.trk), (int)sq_tbars(p, squ.trk));   /* (the track's own bars: the ones it plays, round) */
    for (bar = 0; bar < (int)SQ_BARS; bar++) {
        int on = sq_trk_plays(p, squ.trk, (uint32_t)bar);
        if (drums)
            sq_bar_drums(p, bar, on);
        else
            sq_bar_notes(p, squ.trk, bar, on);
    }
    sq_play_marks(p, squ.trk);
    if (squ.ocpy) {                              /* a bar being copied: a dashed frame round the bar it comes from */
        int y = SQG_Y0 + squ.ofrom * SQG_RH - 1, x;
        for (x = SQG_X0 - 3; x < SQG_X1 + 2; x += 4) {
            fb_hline(x, y, 2, C_ACC(1));
            fb_hline(x, y + SQG_RH, 2, C_ACC(1));
        }
    }
    if (cf_sn.sq_rec != 0xFFu && cf_sn.sq_cin) {   /* the count-in: its beats left, big, over the grid */
        char d[2];
        d[0] = (char)('0' + (cf_sn.sq_cin + 95u) / 96u % 10u);
        d[1] = 0;
        fb_text_halo(&CF_FONT_UI, 120 - fb_text_w(&CF_FONT_UI, d, 4) / 2, SQG_Y0 + 20, d, FL_C(FL_R_ROSE, 1), FL_BG, 4);
    }
    /* the cursor: a step's frame (DRUMS: the bar's) */
    {
        int y = SQG_Y0 + squ.bar * SQG_RH - 1, st = (int)sq_steps(p, squ.trk, squ.bar);
        if (drums) {
            fb_rframe(SQG_X0 - 3, y, SQG_X1 - SQG_X0 + 5, SQG_RH + 1, C_ACC(0));
        } else if (!squ.nofr) {                  /* (hidden after a take: the keys only sound) */
            int x = sq_cell_x(squ.step, st), w = sq_cell_x(squ.step + 1, st) - x;
            fb_rframe(x - (w < 8), y, w + (w < 8), SQG_RH + 1, C_ACC(0));
        }
    }
    {
        static const char *const H[2] = {"GLO", "help"};   /* (the top bar names the track's sound: room for one) */
        static const char *const R[2] = {"REC", "stop"};
        if (cf_sn.sq_rec != 0xFFu)               /* (a take: what ends it) */
            draw_hints(R, 1);
        else
            draw_hints(H, 1);
    }
}

/* ------------------------------------------------------- the tracks --- */
/* a track's sign, 11 x 11: a flower (CHORDS), a bud (BASS), star flowers (LAYER), daisies (DRUMS) */
static void sq_track_icon(int x, int y, uint32_t trk, sq_ink_t k)
{
    static const char *const FLOWER[11] = {"...aba.....", "..abeba....", "...aba.....", "....h......", "....h..gg..",
                                           "....hhgg...", "..gghh.....", "...gh......", "....h......", "....h......",
                                           "....i......"};
    static const char *const BUD[11] = {"....a......", "...aab.....", "..aabbc....", "..abbcc....", "...bcc.....",
                                        "....h......", "....h.gg...", "....hgg....", "..ggh......", "...gh......",
                                        "....i......"};
    static const char *const DAISIES[11] = {".a.........", "aea....a...", ".a....aea..", ".h.....a...", ".h.....h...",
                                            "..h...h....", "..h.a.h....", "..haeah....", "...hah.....", "...hh......",
                                            "...i......."};
    static const char *const STAR[11] = {"...a.......", "..aba......", ".abeba.....", "..aba......", "...a.......",
                                         "...h..a....", "...h.aba...", "...hh.a....", "....hgh....", "....h......",
                                         "....i......"};
    sq_sprite(x, y, trk == SQ_DRUMS ? DAISIES : trk == SQ_BASS ? BUD : trk == SQ_LAYER ? STAR : FLOWER, 11, 11, k);
}

/* a track's bars in small: a dash for a step (its root's colour), a dot for a drum hit. Its own bars only, as wide as
 * the pattern's: a shorter track's strip is shorter (it plays round under the others) */
static void sq_track_strip(const sq_pat_t *p, uint32_t trk, int x0, int ym, int w)   /* ym: the row's middle */
{
    int nb = (int)sq_len(p), b0, b, i;
    for (b0 = 0; b0 < (int)sq_tbars(p, trk) && b0 < nb; b0++) {   /* each bar a dark slot 4 px tall, centred on ym
                                                  * (the owner: in the middle of the row), what plays in it over it */
        int bx = x0 + b0 * w / nb, bw = w / nb - 2;
        int st;
        b = (int)sq_ts(p, trk) + b0;
        st = (int)sq_steps(p, trk, (uint32_t)b);
        fb_rect(bx, ym - 2, bw, 4, FL_BG2);
        if (trk == SQ_DRUMS) {
            for (i = 0; i < st; i++) {
                uint32_t l;
                for (l = 0; l < SQ_NLANE; l++)
                    if ((p->hit[l][b] >> i) & 1u) {   /* (kick, snare, clap the slot's lower half; the rest its upper) */
                        fb_rect(bx + i * bw / st, l < 3u ? ym : ym - 2, 1 + (bw >= 2 * st), 2,
                                FL_C(sq_lane_ink(l).ramp, 1));
                    }
            }
        } else {
            uint32_t e;
            for (e = 0; e < p->nev; e++) {
                const sq_ev_t *v = &p->ev[e];
                if (v->trk == trk && v->bar == b && sq_ev_on(v)) {
                    int x = bx + v->pos * bw / (int)SQ_U, xw = v->len * bw / (int)SQ_U - 1;
                    if (x + xw > x0 + w)         /* (a step lasting past the track's end: to the strip's end) */
                        xw = x0 + w - x;
                    fb_rect(x, ym - 2, xw > 1 ? xw : 1, 4, FL_C(sq_root_ink(sqv_root(e)).ramp, 1));
                }
            }
        }
    }
}

static void sq_pat_row(int y0)                   /* the patterns' letters and SONG, a row of tabs, the knob that turns
                                                  * them first as a chip (the top bar has room for one hint) */
{
    /* tabs, as MUI's (the owner, 2026-10-19: "у MUI есть компонент табов, надо взять у них как сделано"): every choice
     * plain text, the shown one in the accent with a bar under it as wide as its tab; no box round a choice (a filled
     * box, then a frame, read as a button: "song как кнопка выглядит", "и A как кнопка выглядит"). A pattern with steps
     * in its colour, as its blocks in the song; an empty one dim. Playing: the firefly over it, as over a playing block
     * and a track's playhead (an underline would read as the tab's bar); coming in next: a dot there, blinking */
    int i, x = 8 + draw_chip(8, y0, "ALGO") + 5;
    for (i = 0; i <= (int)SQ_NPAT; i++) {
        char b[6];
        int w, cur = i == squ.pat, used = i < (int)SQ_NPAT && sq_has((uint32_t)i);   /* (steps in it: a pattern
                                                  * cleared reads empty, not "opened once", the owner, 2026-10-20) */
        int play = cf_sn.sq_on && (i < (int)SQ_NPAT ? !cf_sn.sq_song && i == cf_sn.sq_pat : cf_sn.sq_song);
        int next = cf_sn.sq_on && cf_sn.sq_next != 0xFFu &&
                   (i < (int)SQ_NPAT ? cf_sn.sq_next == i : cf_sn.sq_next >= SQ_NPAT);
        if (i < (int)SQ_NPAT) {
            b[0] = (char)('A' + i);
            b[1] = 0;
        } else {
            str_cpy(b, "SONG", sizeof b);
        }
        w = fb_text_w(&CF_FONT_UI, b, 1) + 6;
        fb_rect(x, y0 + 12, w + 2, 2, FL_BG);    /* (the bars' line kept clear: the garden's dots there read as one) */
        fb_text_halo(&CF_FONT_UI, x + 3, y0, b, cur ? C_ACC(0) : used ? FL_C(sq_root_ink((uint32_t)i * 7u).ramp, 0) :
                     i == (int)SQ_NPAT ? FL_INK2 : FL_INK3, FL_BG, 1);
        if (cur)
            fb_rect(x, y0 + 12, w, 2, C_ACC(0));   /* (the tab's bar) */
        if (play)
            sq_firefly(x + w / 2 - 1, y0 - 4);
        else if (next && ((cu.now / 250u) & 1u))
            fb_rect(x + w / 2 - 1, y0 - 4, 2, 2, FL_C(FL_R_SAND, 1));
        x += w + 2;
    }
}

#define SQT_W 104                                /* a track's strip; the picked track's row ends in its EDIT chip */
static void draw_sq_tracks(void)
{
    const sq_pat_t *p = sq_cur();
    sqv_update(p);
    char t[12];
    uint32_t r;
    if (squ.ocpy) {                              /* (OCT+ held, ALGORITHM turned: the copy's way) */
        str_cpy(t, "Copy A to A", sizeof t);
        t[5] = (char)('A' + squ.ofrom % SQ_NPAT);
        t[10] = (char)('A' + squ.pat % SQ_NPAT);
    } else {
        str_cpy(t, "Pattern ", sizeof t);
        t[8] = (char)('A' + squ.pat);
        t[9] = 0;
    }
    draw_head(t);
    sq_pat_row(HEAD_LINE);
    for (r = 0; r < SQ_NTRK; r++) {             /* four rows, 20 px, 2 apart; a muted track's dark */
        int y = VIEW_Y - 4 + (int)r * 22, sel = r == squ.trk, mute = (p->mute >> r) & 1u, ym = y + 10;
        int has = !sq_empty(p) && (r == SQ_DRUMS ? sq_drums(p) : sq_count(p, r) > 0);
        sq_ink_t k = r == SQ_DRUMS ? sq_lane_ink(0) : sq_root_ink(r == SQ_BASS ? 7u : r == SQ_LAYER ? 4u : 0u);
        fb_rbox(6, y, 228, 20, 3, sel ? cf_panel(2) : FL_BG1);
        if (sel && squ.sel_down && !squ.cpy && sq_track_has(p, r)) {   /* SEL held: the row fills as the seconds go
                                                  * (then it clears) */
            uint32_t t = cu.now - squ.sel_down;
            fb_rect(7, y + 1, (int)(t >= SQ_CLEAR_MS ? 226u : t * 226u / SQ_CLEAR_MS), 18, FL_C(FL_R_FIRE, 3));
        }
        if (sel)
            fb_rframe(6, y, 228, 20, squ.sel_down && !squ.cpy && sq_track_has(p, r) ? FL_C(FL_R_FIRE, 1) : C_ACC(1));
        sq_track_icon(13, ym - 5, r, k);
        fb_text(&CF_FONT_UI, 30, ym - 6, SQ_TRK_NAME[r], mute ? FL_INK3 : sel ? FL_INK : FL_INK2, 1);
        if (sel)                                 /* (what opens it, on the row itself: the top bar has no room) */
            draw_chip(230 - fb_text_w(&CF_FONT_UI, "EDIT", 1) - 6, ym - 6, "EDIT");
        if (mute) {                              /* (muted: said so, where the strip was) */
            fb_text(&CF_FONT_UI, 92, ym - 6, "muted", C_ACC(1), 1);
            continue;
        }
        if (!has) {
            fb_text(&CF_FONT_UI, 92, ym - 6, r == SQ_BASS && !sq_empty(p) && sq_count(p, SQ_CHORDS) ?
                    "follows the chords" : "empty", FL_INK3, 1);
            continue;
        }
        sq_track_strip(p, r, 92, ym, SQT_W);
        if (sq_playing_here()) {                 /* (where the time is: in the track's own bars) */
            int bw = SQT_W / (int)sq_len(p), tb = (int)sq_trk_bar(p, (uint32_t)r, cf_sn.sq_n);
            fb_rect(92 + (tb - (int)sq_ts(p, (uint32_t)r)) * bw + (int)(cf_sn.sq_t * (uint32_t)(bw - 2) / cf_sn.sq_bt), ym - 5, 1, 11,
                    FL_C(FL_R_SAND, 0));
        }
    }
    if (squ.sel_down && !squ.cpy && squ.pat < SQ_NPAT && sq_track_has(p, squ.trk)) {
        /* SEL held on a track: what will happen, the seconds left to it, how to keep it (the owner, 2026-10-19: three
         * seconds, a countdown, "чтобы пользователь понимал что произойдёт") */
        uint32_t tm = cu.now - squ.sel_down, left = tm >= SQ_CLEAR_MS ? 0u : (SQ_CLEAR_MS - tm + 999u) / 1000u;
        uint8_t warn = FL_C(FL_R_FIRE, 1);
        char a[20], d[2];
        str_cpy(a, "Clear ", sizeof a);
        str_cpy(a + 6, SQ_TRK_NAME[squ.trk % SQ_NTRK], sizeof a - 6u);
        draw_panel(20, Y_VIEW + 38, 200, 100);
        fb_text_c(&CF_FONT_UI, 120, Y_VIEW + 44, a, warn, 1);
        d[0] = (char)('0' + left);
        d[1] = 0;
        fb_text_c(&CF_FONT_UI, 120, Y_VIEW + 60, d, warn, 3);
        draw_chip_text_c(Y_VIEW + 116, "let go of [SEL]: keep it", FL_INK2);
    }
    {
        static const char *const H[2] = {"GLO", "help"};
        draw_hints(H, 1);
    }
}

/* ------------------------------------------------------------ the song --- */
/* the song as one row of blocks, read as a timeline: a block a pattern, as long as its bars, in the pattern's own
 * colour (its letter over the tracks has it too), a tick a bar along its foot. The frame is on one block; after the
 * last one a dashed "+" place: where the next pattern key puts its pattern. A long song scrolls by whole blocks to keep
 * the frame in view, arrows say more is out of view, and a map under the row shows the whole song (the owner,
 * 2026-10-19, "Один ряд и карта": rows that wrapped stretched over the garden) */
#define SQS_Y0 (VIEW_Y - 1)                      /* the row; the hints SQS_RH x 3 under it */
#define SQS_RH 24
static sq_ink_t sq_pat_ink(uint32_t pi) { return sq_root_ink(pi * 7u); }   /* (each pattern its colour: six of
                                                                              * them, G and H as A and B) */
static int sq_block_w(uint32_t pi)
{
    int bars = sq_empty(&sq_st.pat[pi % SQ_NPAT]) ? 1 : (int)sq_len(&sq_st.pat[pi % SQ_NPAT]), w = bars * 6 + 6;
    return w < 18 ? 18 : w;
}
static void sq_block(int x, int y, uint32_t pi, int cur)
{
    const sq_pat_t *p = &sq_st.pat[pi % SQ_NPAT];
    sq_ink_t k = sq_pat_ink(pi);
    int w = sq_block_w(pi), b;
    char l[2];
    fb_rbox(x, y, w, 20, 2, FL_C(k.ramp, sq_has(pi % SQ_NPAT) ? 2 : 3));   /* (cleared: as empty, as long as its
                                                  * bars, which it plays) */
    fb_hline(x + 2, y + 1, w - 4, FL_C(k.ramp, 1));
    for (b = 0; sq_has(pi % SQ_NPAT) && b < (int)sq_len(p); b++)   /* a tick a bar */
        fb_rect(x + 4 + b * 6, y + 15, 4, 2, FL_C(k.ramp, 0));
    l[0] = (char)('A' + pi % SQ_NPAT);
    l[1] = 0;
    fb_text(&CF_FONT_UI, x + 4, y + 3, l, FL_INK, 1);
    if (cur)
        fb_rframe(x - 2, y - 2, w + 4, 24, C_ACC(0));
}
static void sq_plus(int x, int y)                /* the place the next pattern key fills */
{
    int i;
    for (i = 0; i < 14; i += 3) {
        fb_hline(x + i, y, 2, FL_INK3);
        fb_hline(x + i, y + 19, 2, FL_INK3);
    }
    for (i = 0; i < 20; i += 3) {
        fb_vline(x, y + i, 2, FL_INK3);
        fb_vline(x + 15, y + i, 2, FL_INK3);
    }
    fb_hline(x + 5, y + 10, 6, FL_INK2);
    fb_vline(x + 8, y + 7, 6, FL_INK2);
}
static void sq_song_firefly(int x, int y, int i, uint32_t pi)
{
    if (cf_sn.sq_on && cf_sn.sq_song && i == cf_sn.sq_slot) {   /* (playing: a firefly over it) */
        const sq_pat_t *pp = &sq_st.pat[pi % SQ_NPAT];
        int w = sq_block_w(pi) - 4;
        int f = pp->bars && cf_sn.sq_bt ? (cf_sn.sq_bar * w + (int)(cf_sn.sq_t * (uint32_t)w / cf_sn.sq_bt)) /
                                          (int)sq_len(pp) : 0;
        sq_firefly(x + 2 + f, y - 3);
    }
}
static void draw_sq_song(void)
{
    const sq_song_t *g = &sq_st.song;
    int i, n = g->n, bars = 0, secs;
    char b[24], t[8];
    draw_head("Song");
    sq_pat_row(HEAD_LINE);
    for (i = 0; i < n; i++)
        if (!sq_empty(&sq_st.pat[g->pat[i] % SQ_NPAT]))
            bars += sq_len(&sq_st.pat[g->pat[i] % SQ_NPAT]);
    {                                            /* one row from the block fk on, whole blocks, the frame kept in view
                                                  * with one block before it; arrows where blocks are out of view; under
                                                  * it the whole song small (a map), the part on the screen in brackets */
        static int fk;
        int pos[SQ_SONG + 1], total = 0, vw = 216, y = SQS_Y0 + 2, cur = n ? squ.slot : 0, x, last = -1, room;
        for (i = 0; i < n; i++) {
            pos[i] = total;
            total += sq_block_w(g->pat[i]) + 4;
        }
        pos[n] = total;
        total += 16;
        if (fk > cur - 1)
            fk = cur > 0 ? cur - 1 : 0;
        for (;;) {                               /* (the frame, and the "+" with the last block, must fit) */
            room = (cur >= n - 1 ? pos[n] + 16 : pos[cur] + sq_block_w(g->pat[cur])) - pos[fk];
            if (room <= vw || fk >= cur)
                break;
            fk++;
        }
        if (total <= vw)
            fk = 0;
        for (i = fk, x = 12; i < n; i++) {
            int w = sq_block_w(g->pat[i]);
            if (x + w > 12 + vw)
                break;
            sq_block(x, y, g->pat[i], i == squ.slot);
            sq_song_firefly(x, y, i, g->pat[i]);
            last = i;
            x += w + 4;
        }
        if (last == n - 1 && x + 16 <= 12 + vw)
            sq_plus(x, y);
        else if (n)
            last = last < 0 ? fk : last;
        if (fk > 0)                              /* (more before: an arrow at the left end) */
            for (i = 0; i < 4; i++)
                fb_vline(4 + i, y + 10 - i, 1 + 2 * i, FL_INK2);
        if (last < n - 1 || (last == n - 1 && x + 16 > 12 + vw))   /* (more after) */
            for (i = 0; i < 4; i++)
                fb_vline(235 - i, y + 10 - i, 1 + 2 * i, FL_INK2);
        if (total > vw) {
            int my = y + 31, vx0 = pos[fk], vx1 = last >= 0 ? pos[last] + sq_block_w(g->pat[last]) : pos[n] + 16;
            int wx0 = 12 + vx0 * vw / total, wx1 = 12 + vx1 * vw / total;
            if (last == n - 1)
                wx1 = 12 + vw;
            for (i = 0; i < n; i++) {
                int a0 = 12 + pos[i] * vw / total, z = 12 + (pos[i] + sq_block_w(g->pat[i])) * vw / total;
                sq_ink_t k = sq_pat_ink(g->pat[i]);
                fb_rect(a0, i == squ.slot ? my - 1 : my, z - a0 > 1 ? z - a0 : 1, i == squ.slot ? 5 : 3,
                        i == squ.slot ? C_ACC(0) : FL_C(k.ramp, 2));
                if (cf_sn.sq_on && cf_sn.sq_song && i == cf_sn.sq_slot)   /* (playing: a dot over it, the block may be
                                                                           * out of view) */
                    fb_rect(a0, my - 4, 2, 2, FL_INK);
            }
            fb_vline(wx0 - 2, my - 3, 9, FL_INK2);
            fb_hline(wx0 - 2, my - 3, 3, FL_INK2);
            fb_hline(wx0 - 2, my + 5, 3, FL_INK2);
            fb_vline(wx1 + 1, my - 3, 9, FL_INK2);
            fb_hline(wx1 - 1, my - 3, 3, FL_INK2);
            fb_hline(wx1 - 1, my + 5, 3, FL_INK2);
        }
    }
    if (!n)                                      /* (an empty song: how to start one) */
        fb_text_halo(&CF_FONT_UI, 32, SQS_Y0 + 7, "press A..H on the upper keys", FL_INK2, FL_BG, 1);
    /* how long it is, on the title's line; what the buttons do with the frame, under the lane */
    secs = cs.bpm ? bars * cs.beats * 60 / cs.bpm : 0;
    kint(b, bars, bars == 1 ? " bar  " : " bars  ");
    kint(t, secs / 60, ":");
    str_cpy(b + str_len(b), t, 8);
    t[0] = (char)('0' + secs % 60 / 10);
    t[1] = (char)('0' + secs % 10);
    t[2] = 0;
    str_cpy(b + str_len(b), t, 4);
    if (n)
        fb_text_r(&CF_FONT_UI, 232, HEAD_Y + 8, b, FL_INK2, 1);
    draw_chip_text_c(SQS_Y0 + 3 * SQS_RH + 2, n ? "[SEL] remove  [EDIT] open it" : "", FL_INK2);
    {
        static const char *const H[2] = {"GLO", "help"};
        draw_hints(H, 1);
    }
}

/* ------------------------------------------------------- SAVE > Pattern --- */

/* ------------------------------------------------------------ the help --- */
static void draw_sq_help(void)
{
    /* (what the knobs do their cells say: the owner, 2026-10-19, "K1-4 are not needed here") */
    /* (PLAY and REC need no line: the owner, 2026-10-19; REC held undoes any last change, not only a take) */
    static const char *const TR[] = {"[SELECT] track", "[ALGO] pattern A..H, song", "[EDIT] open the track",
                                     "hold [OCT+], turn [ALGO]:", "   copy the pattern there",
                                     "hold [SEL]: clear the track", "hold [REC]: undo last change",
                                     "[SEQ] home  [GLO] close", 0};
    static const char *const SO[] = {"upper keys A..H: add at the end", "[SELECT] move the frame",
                                     "[SEL] remove it  [EDIT] open it", "[PLAY] play from the frame",
                                     "hold [REC]: undo last change", "[SEQ] home  [GLO] close", 0};
    /* (a gesture says "turn", and the copy where it lands: "copy the bar there" explained it, the owner, 2026-10-19) */
    /* (no PRESETS line, no SEL-held eraser: "тут лишнее SEL + hold, и лишнее PRESETS", the owner, 2026-10-20; the
     * gestures stay) */
    static const char *const NO[] = {"[SELECT] move the frame", "keys: write in the frame", "[SEL] clear the step",
                                     "[ARP] arpeggio", "hold [EDIT], turn [SELECT]: move",
                                     "hold [OCT+], turn [SELECT]:", "   copy the bar there",
                                     "hold [REC]: undo last change", "[SAVE] back  [GLO] close", 0};
    static const char *const BA[] = {"[SELECT] move the frame", "keys: write in the frame", "[SEL] clear the step",
                                     "hold [EDIT], turn [SELECT]: move",
                                     "hold [OCT+], turn [SELECT]:", "   copy the bar there",
                                     "hold [REC]: undo last change", "[SAVE] back  [GLO] close", 0};   /* (no ARP) */
    static const char *const DR[] = {"upper keys: pick a drum", "lower keys: steps on / off",
                                     "[SELECT] bar  [PRESETS] the kit",   /* (no eraser line: "Стереть убрать, набор
                                                  * оставить", the owner, 2026-10-20) */
                                     "hold [OCT+], turn [SELECT]:", "   copy the bar there",
                                     "REC on: upper keys are pads", "hold [REC]: undo last change",
                                     "[SAVE] back  [GLO] close", 0};
    const char *const *l = cu.scr == SCR_SQ ? (squ.pat == SQ_SONGPG ? SO : TR) : squ.trk == SQ_DRUMS ? DR :
                           squ.trk == SQ_BASS ? BA : NO;
    int n = 0, i;
    while (l[n])
        n++;
    draw_panel(10, Y_VIEW + 4, 220, 10 + n * 14);   /* (up to 9 lines, 14 px apart) */
    for (i = 0; i < n; i++)
        draw_chip_text(18, Y_VIEW + 9 + i * 14, l[i], FL_INK);
}

static void draw_seq(void)
{
    if (cu.scr == SCR_SQED) {
        draw_sq_track();
    } else {
        flora_render(&flo, &fl_cv);
        draw_plant(Y_VIEW);
        fb_veil(0, Y_VIEW, FB_W, H_VIEW, FL_BG);   /* (the plant faint: the picture leads) */
        if (squ.pat == SQ_SONGPG)
            draw_sq_song();
        else
            draw_sq_tracks();
    }
    if (squ.help || squ.help_hold)
        draw_sq_help();
}
