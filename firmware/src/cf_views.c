/* SPDX-License-Identifier: GPL-3.0-only
 * CoralFlower: the screens' heads and their central pictures.
 *
 * Every screen but HOME has one head. The top bar names the screen; under it, big, is what you have in front of you
 * there (a page, a sound, an effect: never the top bar's name again), the screen's icon at the end of that line. The
 * line under the title belongs to it: on a screen whose pages SELECT turns, the pager (arrows round a row of marks,
 * the page you are on a long one, then the page's name when the title is not it); elsewhere a short green note. The
 * centre under the head is the screen's picture: what its knobs do, drawn live. Included by cf_screens.c. */

#define HEAD_Y (Y_VIEW + 5)                      /* the title (2x): its capitals at + 6 .. + 21 */
#define HEAD_LINE (Y_VIEW + 35)                  /* the line under it */
#define VIEW_Y (Y_VIEW + 54)                     /* the picture: VIEW_Y .. VIEW_Y + VIEW_H */
#define VIEW_H 82

/* the beat runs (the metronome's clock: PLAY, the sequencer, SEQ open, MIDI clock in): the arpeggiator is locked to it */
static int cf_grid_shown(void) { return cf_sn.running || cf_sn.metro_on || cf_sn.ext_clock; }

/* ------------------------------------------------------------- icons --- */
/* each screen's icon, drawn 2x (a logical pixel is 2 x 2, as the title's) */
typedef struct { uint8_t w, h; const char *row[9]; } cf_icon_t;
static const cf_icon_t CF_ICON[SCR_N] = {
    [SCR_SEL] = {7, 7, {"..###..", ".#...#.", "#.....#", "#..#..#", "#.....#", ".#...#.", "..###.."}},
    [SCR_ENV] = {12, 6, {"...#........", "..#.#.......", "..#..#......", ".#....#####.", ".#.........#",
                         "#..........#"}},
    [SCR_BASS] = {7, 7, {".###...", "#...#.#", "##..#..", "....#.#", "...#...", "..#....", "##....."}},
    [SCR_EDIT] = {8, 7, {".##.....", "#..#....", "#..#....", "#..#...#", "....#..#", "....#..#", ".....##."}},
    [SCR_GLO] = {7, 9, {"###....", "#.#....", "###.###", ".####.#", ".#..###", ".#.....", ".#..###", ".####.#",
                        "....###"}},
    [SCR_FX] = {10, 7, {"...#....#.", "...#...###", "..###...#.", "#######...", "..###.....", "...#......",
                        "...#......"}},
    [SCR_ARP] = {8, 7, {".......#", ".......#", ".....###", ".....#..", "...###..", "...#....", "####...."}},
    [SCR_SEQ] = {7, 7, {"...#...", "...#.#.", "..#.#..", "..##...", ".#...#.", ".#...#.", "#######"}},
    [SCR_SAVE] = {7, 7, {"######.", "#.##.##", "#.##..#", "#.....#", "#.###.#", "#.###.#", "#######"}},
};
static void draw_icon(int xr, int ymid, uint32_t scr, uint8_t c)   /* xr: its right edge */
{
    const cf_icon_t *ic = &CF_ICON[scr % SCR_N];
    int i, j, x = xr - ic->w * 2, y = ymid - ic->h;
    for (j = 0; j < ic->h; j++)
        for (i = 0; i < ic->w; i++)
            if (ic->row[j][i] == '#')
                fb_rect(x + i * 2, y + j * 2, 2, 2, c);
}

/* ------------------------------------------------------------- heads --- */
static void draw_head(const char *title)
{
    char b[24];
    fb_signs(title, b, sizeof b);
    fb_text_halo(&CF_FONT_UI, 6, HEAD_Y, b, FL_INK, FL_BG, 2);
    draw_icon(234, HEAD_Y + 14, cu.scr, C_ACC(1));
}
static void draw_head_note(const char *note)    /* the green line under a title with no pages */
{
    char b[24];
    if (!note || !note[0])
        return;
    fb_signs(note, b, sizeof b);
    fb_text_halo(&CF_FONT_UI, 8, HEAD_LINE, b, C_ACC(1), FL_BG, 1);
}

static int cv_slide(void)                         /* the picture's x offset now */
{
    uint32_t t = cu.now - cv_turn_ms;
    int k;
    if (!cv_turn_ms || t >= 180u)
        return 0;
    k = 180 - (int)t;
    return cv_turn_dir * 28 * k * k / (180 * 180);
}

static void draw_chevron(int x, int y, int right, uint8_t c)   /* 5 x 7, its strokes 2 px wide */
{
    int r;
    for (r = 0; r < 7; r++) {
        int d = r < 3 ? 3 - r : r - 3;
        fb_rect(x + (right ? 3 - d : d), y + r, 2, 1, c);
    }
}
/* the pager on the line under the title; name: the page's name after it (0: the title is the page). wrap: SELECT goes
 * round (else the arrow at an end is dark) */
static void draw_pager(int cur, int n, int wrap, const char *name)
{
    int i, x = 8, y = HEAD_LINE + 3, lit = cu.now - cv_turn_ms < 220u && cv_turn_ms;
    uint8_t cl = !wrap && cur == 0 ? FL_BG3 : lit && cv_turn_dir < 0 ? C_ACC(0) : FL_INK2;
    uint8_t cr = !wrap && cur == n - 1 ? FL_BG3 : lit && cv_turn_dir > 0 ? C_ACC(0) : FL_INK2;
    draw_chevron(x, y - 1, 0, cl);
    x += 8;
    for (i = 0; i < n; i++) {
        int w = i == cur ? 12 : 4;
        fb_rect(x, y + 1, w, 4, i == cur ? C_ACC(1) : FL_INK3);
        x += w + 3;
    }
    draw_chevron(x + 2, y - 1, 1, cr);
    x += 12;
    if (name && name[0]) {
        char b[24];
        fb_signs(name, b, sizeof b);
        fb_text_halo(&CF_FONT_UI, x + 2, HEAD_LINE, b, C_ACC(1), FL_BG, 1);
    }
}

/* ---------------------------------------------------- drawing helpers --- */
/* a wave across w px: shape 0 sine, 1 triangle, 2 saw, 3 square; cycles in 1/256; amp px; phase Q32 */
static int32_t cv_wave(int shape, uint32_t ph)
{
    switch (shape) {
    case 1: return ph < 0x80000000u ? (int32_t)(ph >> 15) - 32768 : 32767 - (int32_t)((ph - 0x80000000u) >> 15);
    case 2: return (int32_t)(ph >> 16) - 32768;
    case 3: return ph < 0x80000000u ? 26000 : -26000;
    default: return osc_sine(ph);
    }
}
/* (cyc << 24) / w, the phase step of a picture's wave, in 32 bits: the device has no 64-bit division (__divdi3); the
 * low 8 bits of the step go (a 1/16M of a cycle: nothing a 240-pixel picture shows) */
static uint32_t cv_step(int32_t cyc, int w)
{
    uint32_t c = (uint32_t)(cyc < 0 ? 0 : cyc > 32767 ? 32767 : cyc);
    return (c << 16) / (uint32_t)(w > 0 ? w : 1) << 8;
}

static void draw_wave(int x0, int ym, int w, int amp, int32_t cyc256, uint32_t ph, int shape, uint8_t c, int dots)
{
    int x, py = 0;
    uint32_t step = cv_step(cyc256, w);
    for (x = 0; x < w; x++) {
        int y = ym - (int)(cv_wave(shape, ph + step * (uint32_t)x) * amp / 32768);
        if (dots && (x & 1))
            continue;
        if (x && !dots)
            fb_line(x0 + x - 1, py, x0 + x, y, c);
        else
            fb_pset(x0 + x, y, c);
        py = y;
    }
}

/* a piano: octs octaves from the C lo, white keys wk px wide and h tall, the black ones over them; mark(n) gives a
 * key's colour (0: its own). piano_cx: the middle of note n's key, for the labels over and under it */
static const int8_t CV_WI[12] = {0, -1, 1, -1, 2, 3, -1, 4, -1, 5, -1, 6};   /* a white key's place, -1: black */
static int piano_cx(int x, int lo, int wk, int n)
{
    int o = (n - lo) / 12, pc = (n - lo) % 12;
    return CV_WI[pc] >= 0 ? x + (o * 7 + CV_WI[pc]) * wk + (wk - 1) / 2 : x + (o * 7 + CV_WI[pc - 1] + 1) * wk - 1;
}
static void draw_piano(int x, int y, int lo, int octs, int wk, int h, uint8_t (*mark)(int))
{
    int n, bw = wk * 6 / 10 < 2 ? 2 : wk * 6 / 10, bh = h * 6 / 10;
    for (n = lo; n < lo + octs * 12; n++)       /* the white keys */
        if (CV_WI[(n - lo) % 12] >= 0) {
            uint8_t c = mark ? mark(n) : 0;
            fb_rect(x + ((n - lo) / 12 * 7 + CV_WI[(n - lo) % 12]) * wk, y, wk - 1, h, c ? c : FL_INK3);
        }
    for (n = lo; n < lo + octs * 12; n++)       /* the black ones, over them */
        if (CV_WI[(n - lo) % 12] < 0) {
            uint8_t c = mark ? mark(n) : 0;
            int cx = piano_cx(x, lo, wk, n);
            fb_rect(cx - bw / 2, y, bw, bh, c ? c : FL_BG);
        }
}

/* ------------------------------------------------- GLOBAL > Display --- */
/* COLOR: every theme a swatch, this one raised and framed; BACKDROP: the three behind the plant, small, this one
 * framed. Each knob's choices in a row, the one taken marked */
static void cv_backdrop_thumb(int x, int y, int w, int h, uint32_t bd, int on)
{
    int coral = flo.species == FL_SP_CORAL, i;
    uint8_t base = coral ? FL_S0 : FL_L0;
    fb_rect(x, y, w, h, FL_BG);
    if (bd != FL_BD_NONE)
        for (i = 0; i < 4; i++)                  /* the sky / the water, darker up */
            fb_rect(x, y + h * i / 4, w, h / 4 + 1, (uint8_t)(base + (coral ? 3 - i : i)));
    if (bd == FL_BD_SCENE) {
        if (!coral) {
            fb_rect(x + w - 11, y + 4, 4, 4, FL_INK2);         /* the moon */
            fb_rect(x + w - 10, y + 4, 3, 3, (uint8_t)(base + 0));
            fb_pset(x + 6, y + 5, (cu.now / 600u) & 1u ? FL_INK : FL_INK3);
            fb_pset(x + 15, y + 3, FL_INK3);
            for (i = 0; i < w; i++)                            /* far hills */
                fb_rect(x + i, y + h - 5 - ((i * 7 / w) & 1) - (i % 9 < 4), 1, 6, FL_BG1);
        } else {
            for (i = 0; i < 3; i++) {                          /* soft lights drifting */
                int bx = x + 6 + (int)((i * 13 + cu.now / 90u) % (uint32_t)(w - 10)), by = y + 5 + i * 5;
                fb_rect(bx, by, 3, 3, (uint8_t)(base + 3));
            }
        }
    } else if (bd == FL_BD_RIPPLES) {                          /* a ring going out */
        int r = 3 + (int)(cu.now / 70u % 14u), cx = x + w / 2, cy = y + h - 3, a;
        for (a = 0; a < 512; a += 24) {
            int px = cx + (int)((r * fcos_ui(a)) >> 14) * 3 / 2, py = cy - (int)((r * fsin_ui(a)) >> 14);
            if (px > x && px < x + w - 1 && py > y)
                fb_pset(px, py, FL_INK3);
        }
    }
    if (on) {
        fb_frame(x - 2, y - 2, w + 4, h + 4, C_ACC(1));
    } else {
        fb_frame(x - 1, y - 1, w + 2, h + 2, FL_BG2);
    }
}
static void view_display(int x0)
{
    static const char *const BD[FL_NBD] = {"Scene", "Ripples", "None"};
    int i, n = CF_NTHEMES, sw = 14, gap = 6, tw = n * sw + (n - 1) * gap, x = x0 + (216 - tw) / 2, y = VIEW_Y + 4;
    for (i = 0; i < n; i++) {
        int on = i == cs.accent % CF_NTHEMES, xx = x + i * (sw + gap);
        uint8_t c = FL_C(CF_THEMES[i].ramp, 1);
        if (on) {
            fb_rect(xx - 2, y - 2, sw + 4, sw + 4, C_ACC(1));
            fb_rect(xx - 1, y - 1, sw + 2, sw + 2, FL_BG);
            fb_rect(xx, y, sw, sw, C_ACC(1));
            fb_rect(xx, y, sw, 3, C_ACC(0));
        } else {
            fb_rect(xx, y + 2, sw, sw - 4, c);
        }
    }
    y += sw + 18;
    for (i = 0; i < FL_NBD; i++) {
        int w = 52, xx = x0 + 108 - (3 * w + 2 * 16) / 2 + i * (w + 16), on = (uint32_t)i == cs.backdrop % FL_NBD;
        cv_backdrop_thumb(xx, y, w, 28, (uint32_t)i, on);
        fb_text_c(&CF_FONT_UI, xx + w / 2, y + 32, BD[i], on ? FL_INK : FL_INK3, 1);
    }
}

/* ----------------------------------------------------- GLOBAL > MIDI --- */
/* the unit, its USB cable, two lanes out of it: the chord part on its channel, the bass on its own; a note sent runs
 * along its lane. MIDI OUT Off: the lanes dark, cut */
static uint32_t cv_mpulse[4][4];
static uint8_t cv_mp_i[4], cv_m_onset, cv_m_nb;
static uint32_t cv_m_sq[2];                      /* (the sequencer's LAYER and DRUMS: their last step seen) */
/* GLO > MIDI and > Channels: the unit, four lanes out of it to the four channels (the chord, the bass, the
 * sequencer's LAYER and DRUMS), a pulse down a lane for each note that goes out; cut when MIDI OUT is off */
static void view_midi(int x0)
{
    static const char *const NM[4] = {"CHORD", "BASS", "LAYER", "DRUMS"};
    int ym = VIEW_Y + 32, xa = x0 + 54, xb = x0 + 160, p, i;
    int on = cs.midi_out, chs = cu.page[SCR_GLO] == 2u;
    uint8_t ch[4];
    ch[0] = cs.ch_chord;
    ch[1] = cs.ch_bass;
    ch[2] = cs.ch_layer;
    ch[3] = cs.ch_drums;
    if (cf_sn.onset != cv_m_onset) {            /* a chord began: one pulse down its lane, one down the bass's */
        cv_m_onset = cf_sn.onset;
        cv_mpulse[0][cv_mp_i[0]++ & 3u] = cu.now ? cu.now : 1u;
        if (cf_sn.nb)
            cv_mpulse[1][cv_mp_i[1]++ & 3u] = cu.now ? cu.now : 1u;
    } else if (cf_sn.nb && cf_sn.bass[0] != cv_m_nb) {
        cv_mpulse[1][cv_mp_i[1]++ & 3u] = cu.now ? cu.now : 1u;
    }
    cv_m_nb = cf_sn.nb ? cf_sn.bass[0] : 0u;
    for (p = 0; p < 2; p++)                      /* the sequencer's LAYER, DRUMS: a step that sounded */
        if (cf_sn.sq_lit_t[SQ_LAYER + p] != cv_m_sq[p]) {
            cv_m_sq[p] = cf_sn.sq_lit_t[SQ_LAYER + p];
            cv_mpulse[2 + p][cv_mp_i[2 + p]++ & 3u] = cu.now ? cu.now : 1u;
        }
    /* the unit */
    fb_rbox(x0 + 12, ym - 15, 36, 30, 3, cf_panel(2));
    for (i = 0; i < 5; i++)
        fb_rect(x0 + 16 + i * 6, ym + 4, 4, 7, cf_panel(1));
    fb_rect(x0 + 16, ym - 10, 28, 3, on ? C_ACC(1) : FL_INK3);
    fb_text_c(&CF_FONT_MICRO, x0 + 30, ym + 19, "USB", FL_INK3, 1);
    if (cs.clock_in == 1u) {                     /* MIDI clock in: what comes into the unit, a beat light when locked */
        int lit = cf_sn.ext_clock && (int32_t)(cf_sn.now_ms - cf_sn.beat_ms) < 120;
        fb_text_c(&CF_FONT_MICRO, x0 + 30, ym - 26, cf_sn.ext_clock ? "CLOCK IN" : "NO CLOCK", cf_sn.ext_clock ?
                  C_ACC(1) : FL_INK3, 1);
        fb_rect(x0 + 28, ym - 19, 4, 4, lit ? C_ACC(0) : FL_BG3);
    } else if (cs.clock_in == 2u) {              /* .. out: the unit's beat going out, its light while it runs */
        int lit = cf_sn.running && (int32_t)(cf_sn.now_ms - cf_sn.beat_ms) < 120;
        fb_text_c(&CF_FONT_MICRO, x0 + 30, ym - 26, "CLOCK OUT", on ? C_ACC(1) : FL_INK3, 1);
        fb_rect(x0 + 28, ym - 19, 4, 4, lit && on ? C_ACC(0) : FL_BG3);
    }
    for (p = 0; p < 4; p++) {                    /* four lanes 18 px apart, their names on them */
        int lane = ym - 27 + p * 18, mid = (xa + xb) / 2;
        uint8_t c = on ? (p ? FL_INK2 : C_ACC(1)) : FL_BG3;
        char b[8];
        fb_line(x0 + 48, ym, xa, lane, c);
        if (on) {
            fb_hline(xa, lane, xb - xa, c);
        } else {                                 /* cut */
            fb_hline(xa, lane, mid - 6 - xa, c);
            fb_hline(mid + 6, lane, xb - mid - 6, c);
        }
        if (on)
            for (i = 0; i < 4; i++) {           /* the notes going out */
                uint32_t t = cu.now - cv_mpulse[p][i];
                if (cv_mpulse[p][i] && t < 420u) {
                    int px = xa + (int)t * (xb - xa) / 420;
                    fb_rect(px - 2, lane - 2, 5, 5, p ? FL_INK : C_ACC(0));
                }
            }
        fb_text_halo(&CF_FONT_UI, xa + 6, lane - 6, NM[p], on ? FL_INK2 : FL_INK3, FL_BG, 1);
        fb_rbox(xb, lane - 7, 46, 14, 3, on ? cf_panel(2) : cf_panel(1));
        if (chs)                                 /* (Channels: the knob over each, K1..K4) */
            fb_rframe(xb, lane - 7, 46, 14, C_ACC(1));
        str_cpy(b, "CH ", sizeof b);
        kint(b + 3, ch[p] + 1, 0);
        fb_text_c(&CF_FONT_UI, xb + 23, lane - 6, b, on ? (p ? FL_INK : C_ACC(0)) : FL_INK3, 1);
    }
}

/* ----------------------------------------------------- GLOBAL > Play --- */
/* two chords on a little piano roll, the way the settings make them sound: GLIDE (Legato: the second pressed while the
 * first is held, the voices slide; Always: they slide even after a release), the loudness under them (VELOCITY its
 * height; RETRIG: a held change strikes again or not; a tick: a note that starts again, so Notes leaves the shared C
 * alone and Chord strikes it too), and the keys they light (LIGHTS: the scale too, or only the
 * keys played) */
static uint8_t cv_play_mark(int n)               /* (draw_keyline's marks: the second chord; the scale, if lit) */
{
    static const uint8_t B[3] = {60, 65, 69};
    int i, pc = (n - (int)cs.chord.tonic + 120) % 12, sc = 0, sz = cf_scale_size(cs.chord.scale);
    for (i = 0; i < 3; i++)
        if (n == B[i])
            return C_ACC(1);
    if (!cs.lights)
        return 0;
    for (i = 0; i < sz && !sc; i++)
        sc = cf_scale_step(cs.chord.scale, i) % 12 == pc;
    return sc ? FL_INK3 : 0;
}
/* GLOBAL > Reset: what goes (the sounds, the patterns, the settings), three pictures; under them the step
 * the reset is at (cf_ui.c cfr): how to begin, the hold's bar, the last question with its seconds, the erase */
static void view_reset(int x0)
{
    static const char *const IC[3][7] = {
        {"...##..", "...#.#.", "...#...", "...#...", ".###...", "####...", ".##...."},   /* a note: the sounds */
        {"..#.#..", ".#####.", "..###..", "...#...", ".#.#...", "..##...", "...#..."},   /* a flower: the patterns */
        {"..#.#..", ".#####.", "##...##", ".#...#.", "##...##", ".#####.", "..#.#.."},   /* a gear: the settings */
    };
    static const char *const NM[3] = {"sounds", "patterns", "settings"};
    uint8_t warn = FL_C(FL_R_FIRE, 1), ink = cfr.arm || cfr.fin || cfr.go ? warn : FL_INK2;
    int i, j, k;
    for (k = 0; k < 3; k++) {
        int cx = x0 + 44 + k * 64, y = VIEW_Y - 4;   /* (64 apart, centred in the view) */
        for (j = 0; j < 7; j++)
            for (i = 0; i < 7; i++)
                if (IC[k][j][i] == '#')
                    fb_rect(cx - 7 + i * 2, y + j * 2, 2, 2, ink);
        fb_text_c(&CF_FONT_UI, cx, y + 18, NM[k], FL_INK3, 1);
    }
    if (cfr.go) {
        fb_text_c(&CF_FONT_UI, x0 + 108, VIEW_Y + 42, "Resetting...", warn, 2);
    } else if (cfr.fin) {                         /* the last question, and the seconds left to answer it */
        char b[4];
        uint32_t left = cu.now - cfr.fin_ms >= CF_RESET_ASK_MS ? 0u : (CF_RESET_ASK_MS - (cu.now - cfr.fin_ms) + 999u) / 1000u;
        draw_chip_text_c(VIEW_Y + 40, "Sure? Press [SAVE] again", warn);
        kint(b, (int)left, 0);
        fb_text_c(&CF_FONT_UI, x0 + 108, VIEW_Y + 58, b, warn, 1);
    } else if (cfr.hold) {                        /* held: the seconds counted down (as a track's clearing), the bar
                                                  * fills in 3 s */
        uint32_t t = cu.now - cfr.hold, left = t >= CF_RESET_HOLD_MS ? 0u : (CF_RESET_HOLD_MS - t + 999u) / 1000u;
        char b[2];
        draw_chip_text_c(VIEW_Y + 32, "keep holding [SAVE]", warn);
        b[0] = (char)('0' + left);
        b[1] = 0;
        fb_text_halo(&CF_FONT_UI, x0 + 108 - fb_text_w(&CF_FONT_UI, b, 2) / 2, VIEW_Y + 44, b, warn, FL_BG, 2);
        fb_rect(x0 + 8, VIEW_Y + 74, 200, 6, FL_BG1);
        fb_rect(x0 + 8, VIEW_Y + 74, (int)(t >= CF_RESET_HOLD_MS ? 200u : t * 200u / CF_RESET_HOLD_MS), 6, warn);
    } else if (cfr.arm) {
        draw_chip_text_c(VIEW_Y + 40, "hold [SAVE] 3 seconds", warn);
        draw_chip_text_c(VIEW_Y + 56, "erases all you made", FL_INK3);
    } else {
        draw_chip_text_c(VIEW_Y + 40, "turn ACTION to Reset", FL_INK2);
        draw_chip_text_c(VIEW_Y + 56, "back up first: the site", FL_INK3);
    }
}

static void view_play(int x0)
{
    static const int8_t A[3] = {0, 4, 7}, B[3] = {0, 5, 9};   /* C E G -> C F A: the C is shared */
    int xl = x0 + 20, xr = x0 + 196, mid = x0 + 104, gap = cs.glide_always ? 14 : 0, gl, i, x;
    int yb = VIEW_Y + 34, ye = VIEW_Y + 60, eh = 20, pk = eh * cs.vel / 127, sus = pk * 5 / 8;
    int a1 = mid - gap / 2, b0 = mid + gap / 2;
    gl = cs.glide_ms ? 6 + (int)(cs.glide_ms > 1000u ? 1000u : cs.glide_ms) * 24 / 1000 : 0;
    /* the roll: 2 px a semitone */
    for (i = 0; i < 3; i++) {
        int ya = yb - A[i] * 2, ybb = yb - B[i] * 2;
        fb_rect(xl, ya, a1 - xl, 2, FL_INK2);
        if (gl) {                                 /* the slide from the first chord's note to the second's */
            if (gap)
                for (x = a1; x < b0; x += 3)
                    fb_pset(x, ya, C_ACC(2));
            fb_line(b0, ya, b0 + gl, ybb, C_ACC(0));
            fb_rect(b0 + gl, ybb, xr - b0 - gl, 2, C_ACC(1));
        } else {
            fb_rect(b0, ybb, xr - b0, 2, C_ACC(1));
        }
        if (gap || cs.retrig == CF_RT_CHORD || (cs.retrig == CF_RT_STRIKE && A[i] != B[i]))
            fb_rect(b0 - 2, gl ? ya : ybb, 2, 2, FL_BG),   /* this note starts again (gliding: from the old */
            fb_rect(b0, (gl ? ya : ybb) - 2, 2, 6, C_ACC(0));   /* pitch): a break, then a tick */
    }
    fb_text_halo(&CF_FONT_UI, xl, VIEW_Y - 4, gl ? (cs.glide_always ? "glides always" : "glides when held") :
                 "glide time: off", FL_INK3, FL_BG, 1);
    /* the loudness */
    fb_hline(xl, ye, xr - xl, FL_BG3);
    fb_line(xl, ye, xl + 4, ye - pk, C_ACC(1));
    fb_line(xl + 4, ye - pk, xl + 16, ye - sus, C_ACC(1));
    if (gap) {                                    /* let go, then struck afresh */
        fb_hline(xl + 16, ye - sus, a1 - xl - 16, C_ACC(1));
        fb_line(a1, ye - sus, b0, ye, C_ACC(1));
        fb_line(b0, ye, b0 + 4, ye - pk, C_ACC(1));
        fb_line(b0 + 4, ye - pk, b0 + 16, ye - sus, C_ACC(1));
        fb_hline(b0 + 16, ye - sus, xr - b0 - 16, C_ACC(1));
    } else if (cs.retrig == CF_RT_STRIKE || cs.retrig == CF_RT_CHORD) {   /* held: struck again */
        fb_hline(xl + 16, ye - sus, mid - xl - 16, C_ACC(1));
        fb_line(mid, ye - sus, mid + 3, ye - pk, C_ACC(0));
        fb_line(mid + 3, ye - pk, mid + 14, ye - sus, C_ACC(1));
        fb_hline(mid + 14, ye - sus, xr - mid - 14, C_ACC(1));
    } else {                                      /* held: it goes on, no new attack */
        fb_hline(xl + 16, ye - sus, xr - xl - 16, C_ACC(1));
    }
    draw_piano(x0 + 108 - 14 * 8 / 2, VIEW_Y + 66, 60, 2, 8, 12, cv_play_mark);
}

/* ------------------------------------------------------------- BASS --- */
/* the bass's wave, as wide as its octave makes it (an octave up: twice the cycles), as tall as its LEVEL, the
 * octaver's an octave over it; moving and bright while the bass sounds, still while it does not, a flat line when it
 * is Off. Under it, where it sits: the keys, the chord's notes and the bass's */
static uint8_t cv_bass_mark(int n)
{
    int i;
    for (i = 0; i < cf_sn.nb; i++)
        if (n == cf_sn.bass[i])
            return C_ACC(0);
    for (i = 0; i < cf_sn.n; i++)
        if (n == cf_sn.note[i])
            return FL_INK2;
    return 0;
}
static void view_bass(int x0)
{
    int ym = VIEW_Y + 24, w = 192, xa = x0 + 12, amp = 22 * cs.bass_level / 127, live = cf_sn.nb > 0;
    int32_t cyc = 512 << (cs.bass_oct + 2) >> 2;           /* -2: half a cycle .. +2: eight */
    uint32_t ph = live ? cu.now * 2600000u * (uint32_t)(cs.bass_oct + 3) : 0u;
    fb_hline(xa, ym, w, FL_BG3);
    if (!cs.bass_on) {
        fb_text_c(&CF_FONT_UI, xa + w / 2, ym - 16, "the bass is off", FL_INK3, 1);
    } else {
        if (cs.octaver)
            draw_wave(xa, ym, w, amp / 2 + 1, cyc * 2, ph * 2u, 0, live ? FL_INK2 : FL_INK3, 1);
        draw_wave(xa, ym, w, amp ? amp : 1, cyc, ph, 0, live ? C_ACC(0) : C_ACC(2), 0);
    }
    draw_piano(x0 + 108 - 35 * 5 / 2, VIEW_Y + 50, 24, 5, 5, 12, cv_bass_mark);
    {                                          /* the octaves under the keys */
        int o;
        for (o = 0; o < 5; o++) {
            char b[3] = {'C', (char)('1' + o), 0};
            fb_text_c(&CF_FONT_MICRO, piano_cx(x0 + 108 - 35 * 5 / 2, 24, 5, 24 + 12 * o), VIEW_Y + 64, b, FL_INK3, 1);
        }
    }
}

/* EDIT > Glide (every sound's last page): one note sliding to the next, the slide as wide as the time it takes, and
 * what the VOICE does */
static void view_glide(int x0)
{
    static const char *const CAP[4] = {"glides into each new note", "one note: it always glides",
                                       "glides when notes overlap", "every voice on one note"};
    const int16_t *p = trk[cu.edit_part].p;
    int g = clamp(p[P_GLIDE], 0, 127), w = g ? 4 + (int)(cfe_glide_ms(g) / 12u) : 0, yl = VIEW_Y + 46, yh = VIEW_Y + 14;
    int xa, xb;
    if (w > 130)
        w = 130;
    xa = x0 + 40;
    xb = xa + (w ? w : 1);
    fb_rect(x0 + 10, yl, xa - x0 - 10, 3, FL_INK2);   /* the note before */
    if (w)
        fb_line(xa, yl + 1, xb, yh + 1, C_ACC(0));
    fb_rect(xb, yh, x0 + 206 - xb, 3, C_ACC(1));      /* the note it goes to */
    if (!w)
        fb_rect(xa, yh, 2, yl - yh + 3, C_ACC(2));    /* (no glide: a jump) */
    fb_text_halo(&CF_FONT_UI, x0 + 10, VIEW_Y + 58,
                 g || p[P_VOICE] == V_UNISON ? CAP[clamp(p[P_VOICE], 0, 3)] : "GLIDE off: notes jump", FL_INK3,
                 FL_BG, 1);
}

#if FELUCCA_VA
/* ------------------------------------------------------- SOUND, the VA --- */
/* a value of the VA sound edited: deep page `page`, column col (cf_edit.c's) */
static int32_t cv_va(uint32_t page, uint32_t col)
{
    const eng_deep_t *d = cfe_deep(cu.edit_part);
    return d && page < d->npages ? d->get(&trk[cu.edit_part], page, col) : 0;
}
/* where a modulation source goes: the slots that use it, their destinations ("CUT  PIT1"), into b */
static void cv_va_routes(uint32_t src, char *b, uint32_t n)
{
    uint32_t k;
    b[0] = 0;
    for (k = 0; k < 8u; k++)
        if ((uint32_t)cv_va(24u + k, 0) == src && cv_va(24u + k, 1) && cv_va(24u + k, 2) &&
            str_len(b) + 7u < n) {
            if (b[0])
                str_cpy(b + str_len(b), "  ", 3);
            str_cpy(b + str_len(b), N_VA_DST[cv_va(24u + k, 1) % VD_N], 7);
        }
}

/* an oscillator's wave at phase ph, as it plays: MODE BASIC (WAVE, SQR's width = SHAPE), MORPH (SHAPE runs sine ..
 * pulse), NOISE (WAVE is the colour) */
static int32_t cv_osc(int32_t mode, int32_t wave, int32_t shape, uint32_t ph, int x)
{
    const uint32_t inc = 1u << 22;
    if (mode == VOM_MORPH)
        return va_morph(ph, inc, shape * 258);
    if (mode == VOM_NOISE || wave == VW_NOIS) {   /* (a fixed noise: the same each frame, jittering a little) */
        uint32_t h = ((uint32_t)x * 2654435761u) ^ (cu.now / 80u * 40503u);
        int32_t wn = (int32_t)((h >> 8) & 0xFFFFu) - 32768;
        if (mode == VOM_NOISE && wave == VN_BROWN)
            return (sine_i(ph) >> 1) + (wn >> 2);
        if (mode == VOM_NOISE && wave == VN_VINYL)
            return (h & 0x1F0000u) ? wn >> 4 : wn;
        return wn;
    }
    switch (wave) {
    case VW_SAW: return osc_saw(ph, inc);
    case VW_SQR: return osc_pulse(ph, inc, VA_PULSE_PW(shape * 258));
    case VW_TRI: return osc_tri(ph);
    case VW_PWM: return osc_pulse(ph, inc, VA_PULSE_PW((int32_t)(16000 + (osc_sine(cu.now * 900000u) >> 1))));
    default: return sine_i(ph);
    }
}
/* the semitones of COARSE as cycles across the picture (1/256): two at 0, twice that an octave up */
static int32_t cv_cycles(int32_t semis)
{
    static const uint16_t R[12] = {256, 271, 287, 304, 323, 342, 362, 384, 406, 431, 456, 483};
    int32_t s = semis + 48, c = 512 * R[s % 12] / 256;
    s /= 12;
    return s >= 4 ? c << (s - 4) : c >> (4 - s);
}
/* OSC k: its wave, as tall as its LEVEL, as many cycles as its COARSE makes; the four oscillators' levels at the right
 * (the mix), this one lit. The second page: the wave as MODE and SHAPE make it, KTRK / SYNC / RING named */
static void view_va_osc(int x0, uint32_t k, uint32_t pg)
{
    int32_t wave = cv_va(2u * k, 0), lvl = cv_va(2u * k, 1), coarse = cv_va(2u * k, 2), fine = cv_va(2u * k, 3);
    int32_t mode = cv_va(2u * k + 1u, 0), shape = cv_va(2u * k + 1u, 1), ktrk = cv_va(2u * k + 1u, 2);
    int32_t cyc = cv_cycles(coarse), amp = 4 + 22 * lvl / 127, x, py = 0, ym = VIEW_Y + 30, w = 150, i;
    uint32_t step = cv_step(cyc, w), ph = (uint32_t)fine * 600000u * (cu.now / 40u);
    uint8_t c = lvl ? C_ACC(0) : FL_INK3;
    fb_hline(x0 + 4, ym, w, FL_BG3);
    for (x = 0; x < w; x++) {
        int y = ym - (int)(cv_osc(mode, wave, shape, ph + step * (uint32_t)x, x) * amp / 32768);
        if (x)
            fb_line(x0 + 4 + x - 1, py, x0 + 4 + x, y, c);
        py = y;
    }
    for (i = 0; i < 4; i++) {                   /* the mix */
        int32_t l = cv_va(2u * (uint32_t)i, 1);
        int h = 2 + (int)(l * 46 / 127), bx = x0 + 168 + i * 11;
        char b[2] = {(char)('1' + i), 0};
        fb_rect(bx, VIEW_Y + 54 - 46, 7, 46, FL_BG1);
        fb_rect(bx, VIEW_Y + 54 - h, 7, h, (uint32_t)i == k ? C_ACC(1) : FL_INK3);
        fb_text_c(&CF_FONT_MICRO, bx + 4, VIEW_Y + 58, b, (uint32_t)i == k ? FL_INK : FL_INK3, 1);
    }
    {                                           /* what the knobs set, in words under the wave */
        char b[40];
        str_cpy(b, mode == VOM_MORPH ? "morph" : mode == VOM_NOISE ? "noise" : N_VA_WAVE[wave % VW_N], sizeof b);
        if (pg) {
            if (!ktrk)
                str_cpy(b + str_len(b), "  fixed pitch", 14);
            if (k == 1u && cv_va(3u, 3))
                str_cpy(b + str_len(b), "  sync", 7);
            if (k == 3u && cv_va(7u, 3))
                str_cpy(b + str_len(b), "  ring", 7);
        } else if (coarse) {
            char t[16];                          /* ("24 st up" and its end: 9; t[8] ran over at 10 and up) */
            kint(t, coarse, coarse > 0 ? " st up" : " st");
            str_cpy(b + str_len(b), "  ", 3);
            str_cpy(b + str_len(b), t[0] == '-' ? t + 1 : t, 9);
            if (coarse < 0)
                str_cpy(b + str_len(b), " down", 6);
        }
        for (i = 0; b[i]; i++)
            if (b[i] >= 'A' && b[i] <= 'Z')
                b[i] = (char)(b[i] + 32);
        fb_text_halo(&CF_FONT_UI, x0 + 6, VIEW_Y + 62, b, FL_INK3, FL_BG, 1);
    }
}

/* FILTER: its response over the frequencies (low at the left), the type morphing LP > BP > HP > NOTCH, the resonance a
 * peak at the cutoff; FENV an arrow from the cutoff, as far as the envelope moves it. The second page: SPREAD two
 * curves apart (left, right), DRIVE a little curve of the saturation at the right */
static int cv_filter_y(int type, int d, int res, int h)   /* one type's gain at d px from the cutoff, px up */
{
    int a = d < 0 ? -d : d, bump = a < 8 ? res * (8 - a) / 8 : 0, y;
    switch (type & 3) {
    case VF_LP: y = d < 0 ? h * 2 / 3 : h * 2 / 3 - d * h / 30; break;
    case VF_HP: y = d > 0 ? h * 2 / 3 : h * 2 / 3 + d * h / 30; break;
    case VF_BP: y = h * 2 / 3 - a * h / 30; break;
    default: y = a < 10 ? h * 2 / 3 - (10 - a) * h / 16 : h * 2 / 3; bump = 0; break;
    }
    return y + bump;
}
static void view_va_filter(int x0, uint32_t pg)
{
    int32_t cut = cv_va(8u, 0), res = cv_va(8u, 1), ft = cv_va(8u, 2), fenv = cv_va(8u, 3);
    int32_t spread = cv_va(9u, 2), drive = cv_va(9u, 3);
    int w = 176, h = 44, yb = VIEW_Y + 56, xa = x0 + 6, cx = (int)cut * (w - 1) / 127, x, s, py = 0;
    int t0 = (int)(ft / 32) & 3, t1 = (t0 + 1) & 3, f = (int)(ft % 32), r = (int)res * h / 150;
    fb_hline(xa, yb, w, FL_BG3);
    for (s = (pg && spread ? -1 : 0); s <= (pg && spread ? 1 : 0); s++) {
        int off = s * (int)spread * w / 600;
        for (x = 0; x < w; x++) {
            int d = x - cx - off, y0 = cv_filter_y(t0, d, r, h), y1 = cv_filter_y(t1, d, r, h);
            int y = (y0 * (32 - f) + y1 * f) / 32;
            y = y < 0 ? 0 : y > h ? h : y;
            if (x)
                fb_line(xa + x - 1, yb - py, xa + x, yb - y, s ? FL_INK2 : C_ACC(0));
            py = y;
        }
    }
    fb_vline(xa + cx, yb - h, h, FL_BG3);
    if (fenv) {                                  /* the envelope's sweep */
        int ex = xa + cx + (int)fenv * w / 160, ya = yb - h - 4;
        fb_hline(fenv > 0 ? xa + cx : ex, ya, (fenv > 0 ? (int)fenv : -(int)fenv) * w / 160, C_ACC(1));
        fb_rect(ex - 1, ya - 2, 3, 5, C_ACC(1));
        fb_text_halo(&CF_FONT_MICRO, xa + cx + (fenv > 0 ? -16 : 4), ya - 2, "ENV", C_ACC(1), FL_BG, 1);
    }
    if (pg) {                                    /* DRIVE: the saturation's curve */
        int bx = x0 + 188, by = VIEW_Y + 34, g = 256 + (int)drive * 24, px = 0, pyy = 0;
        fb_frame(bx - 1, by - 15, 24, 31, FL_BG2);
        for (x = 0; x < 22; x++) {
            int32_t in = (x * 2 - 22) * 32768 / 22, y = softclip((int32_t)(((int64_t)in * g) >> 8));
            int yy = by - (int)(y * 13 / 32768);
            if (x)
                fb_line(bx + px, pyy, bx + x, yy, drive ? C_ACC(1) : FL_INK3);
            px = x;
            pyy = yy;
        }
        fb_text_c(&CF_FONT_MICRO, bx + 11, by + 18, "DRIVE", FL_INK3, 1);
    }
    fb_text_halo(&CF_FONT_UI, xa, VIEW_Y + 62, N_VA_FTYPE128[ft & 127], FL_INK3, FL_BG, 1);
}

/* ENV k: its shape, attack, hold, decay to the sustain, the release after the key (a mark where it is let go); where it
 * goes (ENV 1: the loudness, and the mod slots that take it) under it. ENV 1's second page: VEL, the peak a soft
 * press reaches, dotted */
static void view_va_env(int x0, uint32_t k, uint32_t pg)
{
    uint32_t pa = 10u + 2u * k;
    int32_t a = cv_va(pa, 0), d = cv_va(pa, 1), s = cv_va(pa, 2), r = cv_va(pa, 3), hd = cv_va(pa + 1u, 0);
    int yb = VIEW_Y + 52, h = 46, x = x0 + 8, xa, xh, xd, xs, xr, ys = yb - h * (int)s / 127;
    char b[40];
    xa = x + 4 + (int)a * 36 / 127;
    xh = xa + (int)hd * 20 / 127;
    xd = xh + 4 + (int)d * 40 / 127;
    xs = xd + 34;
    xr = xs + 4 + (int)r * 44 / 127;
    fb_hline(x, yb, xr - x + 6, FL_BG3);
    fb_line(x, yb, xa, yb - h, C_ACC(0));
    fb_hline(xa, yb - h, xh - xa + 1, C_ACC(0));
    fb_line(xh, yb - h, xd, ys, C_ACC(0));
    fb_hline(xd, ys, xs - xd, C_ACC(0));
    fb_line(xs, ys, xr, yb, C_ACC(0));
    fb_vline(xs, yb - h - 2, h + 4, FL_BG3);     /* the key let go */
    fb_text_c(&CF_FONT_MICRO, xs, yb + 3, "KEY UP", FL_INK3, 1);
    if (k == 0u && pg) {                         /* VEL: how low a soft press's peak stays */
        int vl = h - h * (int)cv_va(11u, 1) / 254;
        for (x = xa; x < xs; x += 3)
            fb_pset(x, yb - vl, FL_INK2);
        fb_text_halo(&CF_FONT_MICRO, xs + 4, yb - vl - 2, "SOFT", FL_INK2, FL_BG, 1);
    }
    str_cpy(b, k == 0u ? "to: AMP  " : "to: ", sizeof b);
    cv_va_routes(VS_ENV1 + k, b + str_len(b), sizeof b - str_len(b));
    fb_text_halo(&CF_FONT_UI, x0 + 8, VIEW_Y + 62, str_len(b) > 4u ? b : "to: nothing yet (MOD)", FL_INK3, FL_BG, 1);
}

/* LFO k: its wave, as many cycles as its RATE, as tall as its DEPTH, growing in over its FADE; a dot running along it
 * at its speed; where it goes under it. LFO SYN: the four, each named by its sync. VOICE: the unison's voices spread
 * from the middle out to left and right */
static void view_va_lfo(int x0, uint32_t pg)
{
    int w = 176, xa = x0 + 6, x, py = 0, i;
    if (pg < 4u) {
        int32_t rate = cv_va(18u + pg, 0), wv = cv_va(18u + pg, 1), dep = cv_va(18u + pg, 2), fade = cv_va(18u + pg, 3);
        int ym = VIEW_Y + 28, amp = 3 + 22 * (int)dep / 127, fpx = (int)fade * w / 160;
        int32_t cyc = 128 + rate * 6 * 256 / 127;
        uint32_t step = cv_step(cyc, w);
        int dot = (int)((cu.now * (uint32_t)(2 + rate)) / 64u % (uint32_t)w);
        char b[40];
        static const uint8_t SH[5] = {0, 1, 2, 3, 3};
        fb_hline(xa, ym, w, FL_BG3);
        for (x = 0; x < w; x++) {
            uint32_t ph = step * (uint32_t)x;
            int32_t v = wv == 4 ? (int32_t)((((ph >> 30) + (uint32_t)x / 22u) * 2654435761u) >> 16) - 32768
                                : cv_wave(SH[wv % 5], ph);
            int g = fpx && x < fpx ? x * 256 / fpx : 256, y = ym - (int)(v * amp / 32768) * g / 256;
            if (x)
                fb_line(xa + x - 1, py, xa + x, y, C_ACC(0));
            if (x == dot)
                fb_rect(xa + x - 1, y - 1, 3, 3, FL_INK);
            py = y;
        }
        str_cpy(b, "to: ", sizeof b);
        cv_va_routes(VS_LFO1 + pg, b + 4, sizeof b - 4u);
        fb_text_halo(&CF_FONT_UI, xa, VIEW_Y + 62, b[4] ? b : "to: nothing yet (MOD)", FL_INK3, FL_BG, 1);
    } else if (pg == 4u) {
        for (i = 0; i < 4; i++) {
            int ym = VIEW_Y + 6 + i * 16, sync = (int)cv_va(22u, (uint32_t)i);
            char b[8] = {'L', 'F', 'O', ' ', (char)('1' + i), 0, 0, 0};
            fb_text_halo(&CF_FONT_UI, xa, ym - 6, b, FL_INK2, FL_BG, 1);
            draw_wave(xa + 46, ym, 100, 5, 256 * 3, 0, 0, sync ? C_ACC(0) : FL_INK3, 0);
            fb_text_halo(&CF_FONT_UI, xa + 154, ym - 6, sync ? "tempo" : "free", sync ? C_ACC(1) : FL_INK3, FL_BG, 1);
        }
    } else {                                     /* the stereo field, left to right, the unison's voices on it */
        int32_t sp = cv_va(23u, 0);
        int cx = x0 + 108, ym = VIEW_Y + 26;
        fb_hline(x0 + 22, ym, 172, FL_BG3);
        fb_text_halo(&CF_FONT_UI, x0 + 8, ym - 6, "L", FL_INK3, FL_BG, 1);
        fb_text_halo(&CF_FONT_UI, x0 + 200, ym - 6, "R", FL_INK3, FL_BG, 1);
        for (i = -2; i <= 2; i++) {
            int ex = cx + i * (1 + (int)sp * 40 / 127);
            draw_wave(ex - 6, ym - 12 + (i & 1) * 2, 12, 3, 512, (uint32_t)(i + 2) * 0x30000000u, 0,
                      i ? FL_INK2 : C_ACC(0), 0);
            fb_rect(ex - 2, ym - 2, 5, 5, i ? FL_INK2 : C_ACC(0));
        }
        fb_text_halo(&CF_FONT_UI, x0 + 6, VIEW_Y + 62, sp ? "unison voices spread out" : "unison voices in the middle",
                     FL_INK3, FL_BG, 1);
    }
}

/* MOD k: the slot as a sentence: its source, an arrow as strong as its amount (the sign over it), its destination;
 * under it what it does, or what it still needs */
static void view_va_mod(int x0, uint32_t k)
{
    int32_t src = cv_va(24u + k, 0), dst = cv_va(24u + k, 1), amt = cv_va(24u + k, 2);
    int on = src && dst && amt, ym = VIEW_Y + 24, xa = x0 + 54, xb = x0 + 160, i;
    int th = on ? 1 + (int)(amt < 0 ? -amt : amt) * 3 / 64 : 1;
    uint8_t c = on ? C_ACC(0) : FL_INK3;
    fb_rbox(x0 + 6, ym - 11, 46, 22, 3, on ? cf_panel(2) : cf_panel(1));
    fb_text_c(&CF_FONT_UI, x0 + 29, ym - 6, N_VA_SRC[src % VS_N], on ? FL_INK : FL_INK3, 1);
    fb_rbox(xb, ym - 11, 50, 22, 3, on ? cf_panel(2) : cf_panel(1));
    fb_text_c(&CF_FONT_UI, xb + 25, ym - 6, N_VA_DST[dst % VD_N], on ? FL_INK : FL_INK3, 1);
    for (i = 0; i < th; i++)                     /* as thick as the amount */
        fb_hline(xa, ym - th / 2 + i, xb - xa - 6, c);
    for (i = 0; i < 4; i++)                      /* the arrowhead */
        fb_vline(xb - 6 + i, ym - 3 + i, 7 - 2 * i, c);
    {
        char b[8];
        kint(b, (int)amt, 0);
        if (amt > 0)
            b[0] = '+', kint(b + 1, (int)amt, 0);
        fb_text_c(&CF_FONT_UI, (xa + xb) / 2, ym - 16, amt ? b : "0", c, 1);
    }
    fb_text_halo(&CF_FONT_UI, x0 + 6, VIEW_Y + 62, on ? (amt > 0 ? "raises it" : "lowers it") :
                 !src ? "SRC: pick what moves it" : !dst ? "DST: pick what it moves" : "AMT: how much", FL_INK3, FL_BG, 1);
}

/* the VA's page picture, by section */
static void view_va(int x0)
{
    uint32_t sec = cu.edit_sec, pg = cu.page[SCR_EDIT];
    if (cfe_glide_pg(cu.edit_part, sec, pg)) {
        view_glide(x0);
        return;
    }
    if (sec < 4u)
        view_va_osc(x0, sec, pg);
    else if (sec == 4u)
        view_va_filter(x0, pg);
    else if (sec == 5u)
        view_va_env(x0, pg / 2u, pg & 1u);
    else if (sec == 6u)
        view_va_lfo(x0, pg);
    else
        view_va_mod(x0, pg);
}
#endif

/* ------------------------------------------------------- SOUND, the FM --- */
/* a value of the FM sound edited: an operator's (op 0..5 = OP1..OP6), or the voice's */
static int cv_fo(uint32_t op, uint32_t field) { return fm6_patch[cu.edit_part][(5u - op) * FP_OP + field]; }
static int cv_fg(uint32_t field) { return fm6_patch[cu.edit_part][field]; }

/* the algorithm as a graph: who modulates whom (FM6_ALG's buses, run the sixth operator first: an operator that
 * reads a bus takes what was written to it since it was last overwritten), each operator's row (0: a carrier, up
 * the stack) and column (in half cells; a modulator over the middle of what it modulates) */
typedef struct {
    uint8_t mods[6];                             /* bit j: OP j+1 modulates OP i+1 */
    int8_t row[6], col2[6];
    uint8_t ncol, nrow;
    int8_t fbop, fbto;                            /* the operator with feedback; the end of a loop back to OP6 (-1) */
} cv_alg_t;
static int cv_alg_place(cv_alg_t *g, const uint8_t *owner, int i, int left)   /* returns the width used */
{
    int j, w = 0, first = -1, last = -1;
    for (j = 5; j >= 0; j--)                      /* (the higher operator to the left: OP6 over OP5 ..) */
        if (owner[j] == i) {
            w += cv_alg_place(g, owner, j, left + w);
            if (first < 0)
                first = j;
            last = j;
        }
    if (first < 0) {
        g->col2[i] = (int8_t)(left * 2 + 1);
        return 1;
    }
    g->col2[i] = (int8_t)((g->col2[first] + g->col2[last]) / 2);
    return w;
}
static void cv_alg_graph(uint32_t alg, cv_alg_t *g)
{
    uint32_t bus[3] = {0, 0, 0}, k;
    uint8_t owner[6];
    int i, j, it, x = 0;
    alg &= 31u;
    g->fbop = g->fbto = -1;
    for (k = 0; k < 6u; k++) {
        int op = 5 - (int)k, f = FM6_ALG[alg][k], in = (f >> 4) & 3, out = f & 3;
        g->mods[op] = (uint8_t)(in ? bus[in] : 0u);
        if (out)
            bus[out] = (f & FM6_OADD) ? bus[out] | (1u << op) : 1u << op;
        if ((f & (FM6_FBIN | FM6_FBOUT)) == (FM6_FBIN | FM6_FBOUT))
            g->fbop = (int8_t)op;
        else if (f & FM6_FBOUT)
            g->fbto = (int8_t)op;
    }
    for (i = 0; i < 6; i++)
        g->row[i] = 0;
    for (it = 0; it < 6; it++)                    /* a modulator: one row over the highest it modulates */
        for (i = 0; i < 6; i++)
            for (j = 0; j < 6; j++)
                if (((g->mods[i] >> j) & 1u) && g->row[j] < g->row[i] + 1)
                    g->row[j] = (int8_t)(g->row[i] + 1);
    for (j = 0; j < 6; j++) {                     /* each modulator drawn over its first (lowest) target */
        owner[j] = 0xFFu;
        for (i = 0; i < 6 && owner[j] == 0xFFu; i++)
            if ((g->mods[i] >> j) & 1u)
                owner[j] = (uint8_t)i;
    }
    g->nrow = 1;
    for (i = 0; i < 6; i++) {
        if (g->row[i] + 1 > g->nrow)
            g->nrow = (uint8_t)(g->row[i] + 1);
        if (!fm6_carrier(alg, (uint32_t)i + 1u))
            continue;
        x += cv_alg_place(g, owner, i, x);
    }
    g->ncol = (uint8_t)x;
}
/* the graph drawn: cells cw x rh, its foot at yb, centred on cx. big: boxes with their numbers and level bars; else
 * small squares. sel: the operator lit (-1 none) */
static void draw_alg(uint32_t alg, int cx, int yb, int cw, int rh, int bw, int sel, int big)
{
    cv_alg_t g;
    int i, j, x[6], y[6];
    cv_alg_graph(alg, &g);
    for (i = 0; i < 6; i++) {
        x[i] = cx + (g.col2[i] - g.ncol) * cw / 2;
        y[i] = yb - g.row[i] * rh - bw;
    }
    for (i = 0; i < 6; i++)                       /* the wires: a modulator's foot to its target's head */
        for (j = 0; j < 6; j++)
            if ((g.mods[i] >> j) & 1u)
                fb_line(x[j], y[j] + bw, x[i], y[i] - 1, FL_INK3);
    for (i = 0; i < 6; i++) {
        int car = fm6_carrier(alg & 31u, (uint32_t)i + 1u);
        uint8_t fill = i == sel ? C_ACC(1) : car ? cf_panel(2) : cf_panel(1);
        if (big) {
            char b[2] = {(char)('1' + i), 0};
            int lv = cv_fo((uint32_t)i, FP_OL);
            fb_rbox(x[i] - bw / 2, y[i], bw, bw, 2, fill);
            fb_text_c(&CF_FONT_UI, x[i], y[i] + (bw - 11) / 2, b, i == sel ? FL_BG : car ? FL_INK : FL_INK2, 1);
            fb_rect(x[i] - bw / 2, y[i] + bw + 2, bw * lv / 99, 2, car ? C_ACC(1) : FL_INK3);
        } else {
            fb_rect(x[i] - bw / 2, y[i], bw, bw, i == sel ? C_ACC(0) : car ? FL_INK2 : FL_INK3);
        }
    }
    if (g.fbop >= 0) {                            /* the feedback: a loop over the operator */
        int lx = x[g.fbop] + bw / 2 + 1, fy = y[g.fbop], l = big ? 4 : 2;
        uint8_t c = big && cv_fg(FP_FB) ? C_ACC(0) : FL_INK3;
        fb_hline(x[g.fbop], fy - l, bw / 2 + l, c);
        fb_vline(lx + l - 1, fy - l, bw / 2 + l, c);
        fb_hline(lx, fy + bw / 2 - 1, l, c);
    }
    if (g.fbto >= 0) {                            /* a loop back to OP6, round the side */
        int xr = x[5] + bw / 2 + (big ? 6 : 3);
        fb_vline(xr, y[5] + bw / 2, y[g.fbto] + bw / 2 - y[5] - bw / 2, FL_INK3);
        fb_hline(x[g.fbto] + bw / 2, y[g.fbto] + bw / 2, xr - x[g.fbto] - bw / 2, FL_INK3);
        fb_hline(x[5] + bw / 2, y[5] + bw / 2, xr - x[5] - bw / 2, FL_INK3);
    }
}

/* a DX7 envelope over w px from x, foot yb, h tall: from L4 by R1 to L1, R2 to L2, R3 to L3 (held while the key is),
 * the key let go, R4 back to L4. rates: the segments lit; levels: the corners. pitch: levels round the middle (50) */
static void draw_dx_env(int x, int yb, int w, int h, const int *r, const int *l, int rates, int levels, int pitch)
{
    int px[5], py[5], i, seg[4], tot = 0, hold = w / 6;
    for (i = 0; i < 4; i++)
        tot += seg[i] = 3 + (99 - r[i]) * (99 - r[i]) / 140;
    for (i = 0; i < 4; i++)
        seg[i] = seg[i] * (w - hold) / (tot ? tot : 1);
#define CV_LY(v) (pitch ? yb - h / 2 - ((v) - 50) * h / 100 : yb - (v) * h / 99)
    px[0] = x;
    py[0] = CV_LY(l[3]);
    for (i = 0; i < 3; i++) {
        px[i + 1] = px[i] + seg[i];
        py[i + 1] = CV_LY(l[i]);
    }
    px[4] = px[3] + hold + seg[3];
    py[4] = CV_LY(l[3]);
    fb_hline(x, pitch ? yb - h / 2 : yb, w, FL_BG3);
    for (i = 0; i < 3; i++)
        fb_line(px[i], py[i], px[i + 1], py[i + 1], rates ? C_ACC(0) : C_ACC(1));
    fb_hline(px[3], py[3], hold, C_ACC(1));
    fb_vline(px[3] + hold, yb - h, h + 2, FL_BG3);
    fb_text_c(&CF_FONT_MICRO, px[3] + hold, yb + 4, "KEY UP", FL_INK3, 1);
    fb_line(px[3] + hold, py[3], px[4], py[4], rates ? C_ACC(0) : C_ACC(1));
    for (i = 0; i < 4; i++) {
        char b[2] = {(char)('1' + i), 0};
        if (rates) {                              /* the segment's number at its middle */
            int sx0 = i < 3 ? px[i] : px[3] + hold, sy0 = i < 3 ? py[i] : py[3];
            int sx1 = i < 3 ? px[i + 1] : px[4], sy1 = i < 3 ? py[i + 1] : py[4];
            fb_text_halo(&CF_FONT_MICRO, (sx0 + sx1) / 2 + 2, (sy0 + sy1) / 2 - 7, b, FL_INK, FL_BG, 1);
        }
        if (levels) {                             /* the corner it reaches */
            int cxp = i < 3 ? px[i + 1] : px[4], cyp = i < 3 ? py[i + 1] : py[4];
            fb_rect(cxp - 1, cyp - 1, 3, 3, FL_INK);
            fb_text_halo(&CF_FONT_MICRO, cxp - 1, cyp - 9, b, FL_INK, FL_BG, 1);
        }
    }
#undef CV_LY
}

/* the keys and what an operator's level does along them (Scaling, Curves): the break point, the depth and curve on
 * each side; Curves also shows RATE SCL: the envelopes quicker up the keys */
static void view_fm_scaling(int x0, uint32_t op, int curves)
{
    int bp = cv_fo(op, FP_BP), ld = cv_fo(op, FP_LD), rd = cv_fo(op, FP_RD), lc = cv_fo(op, FP_LC) & 3,
        rcv = cv_fo(op, FP_RC) & 3, rs = cv_fo(op, FP_RS);
    int xk = x0 + 24, w = 8 * 7 * 3, ym = VIEW_Y + 24, bpn = bp + 21, xb = piano_cx(xk, 12, 3, bpn), x, py = 0;
    static const char *const CV[4] = {"-LIN", "-EXP", "+EXP", "+LIN"};
    char b[24];
    fb_hline(xk, ym, w, FL_BG3);
    for (x = 0; x < w; x++) {                     /* the level's offset along the keys */
        int nn = 12 + x * 96 / w, left = nn < bpn, d = left ? bpn - nn : nn - bpn, c = left ? lc : rcv;
        int dep = left ? ld : rd, span = left ? bpn - 12 : 108 - bpn, t = span > 0 ? d * 256 / span : 0;
        int sh = c == 1 || c == 2 ? t * t / 256 : t, y = dep * sh * 20 / (99 * 256);
        y = c < 2 ? y : -y;                       /* (- lowers it, + raises it) */
        y = ym + y;
        if (x)
            fb_line(xk + x - 1, py, xk + x, y, C_ACC(0));
        py = y;
    }
    fb_vline(xb, ym - 22, 44, FL_INK3);
    {
        static const char *const NN[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
        int n = bp + 21;
        str_cpy(b, NN[n % 12], sizeof b);
        kint(b + str_len(b), n / 12 - 1, 0);
        fb_text_halo(&CF_FONT_UI, xb + 3, ym - 22, b, FL_INK2, FL_BG, 1);
    }
    draw_piano(xk, VIEW_Y + 46, 12, 8, 3, 10, 0);
    if (curves) {
        str_cpy(b, CV[lc], sizeof b);
        str_cpy(b + str_len(b), "  ", 3);
        str_cpy(b + str_len(b), CV[rcv], 6);
        fb_text_halo(&CF_FONT_UI, xk, VIEW_Y + 62, b, FL_INK3, FL_BG, 1);
        for (x = 0; x < 8; x++)                   /* RATE SCL: the envelopes' speed along the keys */
            fb_rect(xk + 112 + x * 8, VIEW_Y + 70 - 2 - x * rs / 2, 5, 2 + x * rs / 2, rs ? C_ACC(1) : FL_INK3);
    } else {
        fb_text_halo(&CF_FONT_UI, xk, VIEW_Y + 62, "the level along the keys", FL_INK3, FL_BG, 1);
    }
}

/* Tuning: the operator's wave against the note's (dim): as many cycles more as its ratio, or a fixed frequency */
static void view_fm_tuning(int x0, uint32_t op)
{
    int det = cv_fo(op, FP_DET) - 7, fc = cv_fo(op, FP_FC), ff = cv_fo(op, FP_FF), fixed = cv_fo(op, FP_MODE);
    int ym = VIEW_Y + 22, w = 160, xa = x0 + 8;
    char b[24], t[8];
    int32_t r100, cyc;
    if (fixed) {                                  /* 10 ^ (COARSE % 4 + FINE / 100) Hz */
        static const int32_t D[4] = {1, 10, 100, 1000};
        int32_t m = 65536, i;
        for (i = 0; i < ff; i++)
            m = (int32_t)(((int64_t)m * 67062) >> 16);   /* x 10^0.01 */
        r100 = (int32_t)(((int64_t)D[fc & 3] * 100 * m) >> 16);
        cyc = 256 * 3;
        kint(b, (int)(r100 / 100), " Hz");
        str_cpy(t, "fixed", sizeof t);
    } else {
        r100 = (fc ? fc * 100 : 50) * (100 + ff) / 100;
        cyc = 512 * r100 / 100;
        if (cyc > 256 * 24)
            cyc = 256 * 24;
        {                                         /* "1.00" */
            uint32_t n;
            kint(b, (int)(r100 / 100), 0);
            n = str_len(b);
            b[n] = '.';
            b[n + 1] = (char)('0' + r100 / 10 % 10);
            b[n + 2] = (char)('0' + r100 % 10);
            b[n + 3] = 0;
        }
        str_cpy(t, "ratio", sizeof t);
    }
    if (!fixed)
        draw_wave(xa, ym, w, 18, 512, 0, 0, FL_INK3, 1);   /* the note */
    draw_wave(xa, ym, w, 18, cyc, (uint32_t)det * (cu.now * 40000u), 0, C_ACC(0), 0);
    fb_text_halo(&CF_FONT_UI, x0 + 176, VIEW_Y + 2, t, FL_INK3, FL_BG, 1);
    fb_text_halo(&CF_FONT_UI, x0 + 176, VIEW_Y + 16, b, FL_INK, FL_BG, 1);
    if (det) {
        kint(t, det, 0);
        if (det > 0)
            t[0] = '+', kint(t + 1, det, 0);
        str_cpy(b, "detune ", sizeof b);
        str_cpy(b + str_len(b), t, 5);
        fb_text_halo(&CF_FONT_UI, x0 + 176, VIEW_Y + 30, b, FL_INK3, FL_BG, 1);
    }
    fb_text_halo(&CF_FONT_UI, xa, VIEW_Y + 62, fixed ? "the same pitch on every key" :
                 "times the note's pitch (dim)", FL_INK3, FL_BG, 1);
}

/* Sens & Level: the operator's level as a long bar, the part a soft press leaves of it (VEL SENS), its end trembling
 * with the LFO as much as AM SENS lets it; what that level means for this operator (loudness or brightness) */
static void view_fm_level(int x0, uint32_t op)
{
    int lv = cv_fo(op, FP_OL), vs = cv_fo(op, FP_KVS) & 7, am = cv_fo(op, FP_AMS) & 3;
    int car = fm6_carrier((uint32_t)cv_fg(FP_ALG) & 31u, op + 1u), xa = x0 + 8, w = 150, y = VIEW_Y + 10;
    int full = w * lv / 99, soft = full * (14 - vs) / 14, wob = am ? (osc_sine(cu.now * 3000000u) * am * 2) / 32768 : 0;
    w = 116;
    full = w * lv / 99;
    soft = full * (14 - vs) / 14;
    fb_text_halo(&CF_FONT_UI, xa, y, "hard", FL_INK2, FL_BG, 1);
    fb_rect(xa + 40, y + 1, w, 10, FL_BG1);
    fb_rect(xa + 40, y + 1, full + wob > 0 ? full + wob : 0, 10, C_ACC(1));
    fb_text_halo(&CF_FONT_UI, xa, y + 18, "soft", FL_INK2, FL_BG, 1);
    fb_rect(xa + 40, y + 19, w, 10, FL_BG1);
    fb_rect(xa + 40, y + 19, soft, 10, C_ACC(2));
    fb_text_halo(&CF_FONT_UI, xa, VIEW_Y + 62, car ? "carrier: level = loudness" : "modulator: level = brightness",
                 FL_INK3, FL_BG, 1);
}

/* the FM voice's LFO: its wave at its SPEED; under it what it moves, the pitch (PMD x PMS) and the loudness (AMD);
 * the second page: the DELAY before it grows in, SYNC (it restarts at every key) */
static void view_fm_lfo(int x0, int pg)
{
    int wv = cv_fg(FP_LFW) % 6, sp = cv_fg(FP_LFS), pmd = cv_fg(FP_LPMD), amd = cv_fg(FP_LAMD);
    int dly = cv_fg(FP_LFD), pms = cv_fg(FP_LPMS) & 7, sync = cv_fg(FP_LKS), xa = x0 + 60, w = 140, x, i;
    static const uint8_t SH[6] = {1, 2, 2, 3, 0, 3};
    int32_t cyc = 256 + sp * 7 * 256 / 99;
    uint32_t step = cv_step(cyc, w);
    static const char *const LN[3] = {"LFO", "pitch", "loud"};
    for (i = 0; i < 3; i++) {                     /* the LFO, then the pitch and the loudness it moves */
        int ym = VIEW_Y + 8 + i * 22, amp = i == 0 ? 8 : i == 1 ? pmd * (pg ? pms : 7) * 8 / (99 * 7) : amd * 8 / 99, py = 0;
        fb_text_halo(&CF_FONT_UI, x0 + 6, ym - 6, LN[i], i ? FL_INK3 : FL_INK2, FL_BG, 1);
        fb_hline(xa, ym, w, FL_BG3);
        for (x = 0; x < w; x++) {
            uint32_t ph = step * (uint32_t)x;
            int32_t v;
            int g = 256, y;
            if (pg && sync)                      /* SYNC: the wave starts over at each key (every 47 px) */
                ph = step * (uint32_t)(x % 47);
            if (pg && dly) {                     /* DELAY: silent, then growing in */
                int d0 = dly * w / 200, xx = pg && sync ? x % 47 : x;
                g = xx < d0 ? 0 : xx < d0 * 2 ? (xx - d0) * 256 / (d0 ? d0 : 1) : 256;
            }
            v = wv == 5 ? (int32_t)((((ph >> 30) + (uint32_t)x / 18u) * 2654435761u) >> 16) - 32768
                         : wv == 1 ? -cv_wave(2, ph) : cv_wave(SH[wv], ph);
            y = ym - (int)(v * amp / 32768) * g / 256;
            if (x)
                fb_line(xa + x - 1, py, xa + x, y, i ? C_ACC(1) : C_ACC(0));
            py = y;
        }
        if (pg && sync && !i)
            for (x = 0; x < w; x += 47)
                fb_vline(xa + x, ym - 10, 20, FL_INK3);
    }
}

/* the FM page's picture: the operator's (with the algorithm small at the right, this operator lit), the pitch
 * envelope's, or the voice's */
static void view_fm(int x0)
{
    uint32_t sec = cu.edit_sec, pg = cu.page[SCR_EDIT], alg = (uint32_t)cv_fg(FP_ALG) & 31u;
    int r[4], l[4], i;
    if (cfe_glide_pg(cu.edit_part, sec, pg)) {
        view_glide(x0);
        return;
    }
    if (sec < 6u) {
        for (i = 0; i < 4; i++)
            r[i] = cv_fo(sec, FP_R1 + (uint32_t)i), l[i] = cv_fo(sec, FP_L1 + (uint32_t)i);
        if (pg < 2u)
            draw_dx_env(x0 + 8, VIEW_Y + 52, 150, 46, r, l, pg == 0u, pg == 1u, 0);
        else if (pg == 2u)
            view_fm_tuning(x0, sec);
        else if (pg == 3u)
            view_fm_level(x0, sec);
        else
            view_fm_scaling(x0, sec, pg == 5u);
        if (pg < 4u && pg != 2u)                  /* the algorithm, small, this operator lit */
            draw_alg(alg, x0 + 190, VIEW_Y + 52, 9, 10, 6, (int)sec, 0);
        if (pg < 2u)
            fb_text_halo(&CF_FONT_UI, x0 + 8, VIEW_Y + 62, pg ? "how loud at each corner" : "how fast to each corner",
                         FL_INK3, FL_BG, 1);
    } else if (sec == 6u) {
        for (i = 0; i < 4; i++)
            r[i] = cv_fg(FP_PR1 + (uint32_t)i), l[i] = cv_fg(FP_PL1 + (uint32_t)i);
        draw_dx_env(x0 + 8, VIEW_Y + 52, 190, 46, r, l, pg == 0u, pg == 1u, 1);
        fb_text_halo(&CF_FONT_UI, x0 + 8, VIEW_Y + 62, "pitch up or down from the note", FL_INK3, FL_BG, 1);
    } else if (pg == 0u) {                        /* Voice: the algorithm, big */
        cv_alg_t g;
        cv_alg_graph(alg, &g);
        draw_alg(alg, x0 + 108, VIEW_Y + 52, g.ncol > 4 ? 32 : 40, g.nrow > 3 ? 14 : g.nrow > 2 ? 18 : 22,
                 g.nrow > 3 ? 12 : 16, -1, 1);
        fb_text_halo(&CF_FONT_UI, x0 + 8, VIEW_Y + 62, "bottom sounds, top shapes it", FL_INK3, FL_BG, 1);
    } else {
        view_fm_lfo(x0, (int)pg - 1);
    }
}

/* ------------------------------------------------------------- CHORD --- */
/* the chord button being edited, played on the key's tonic: its notes lit on three octaves of keys as they are
 * voiced (INV moves the lowest up), each named by its degree over it and its note under it, the bass tone (BASS)
 * marked in the low octave. Under it, how to pick another button; an arrow over that button in the key row */
static const char *cv_degree(int iv, uint32_t mask)          /* an interval's name in the chord */
{
    static const char *const D[12] = {"1", "b9", "9", "b3", "3", "11", "b5", "5", "#5", "6", "b7", "7"};
    int third = (mask >> 3) & 1u || (mask >> 4) & 1u;
    iv %= 12;
    if (!third && iv == 2)
        return "2";
    if (!third && iv == 5)
        return "4";
    return D[iv];
}
static uint8_t cv_sel_n, cv_sel_note[12], cv_sel_bass;   /* (draw_piano's marks) */
static uint8_t cv_sel_mark(int n)
{
    int i;
    for (i = 0; i < cv_sel_n; i++)
        if (n == cv_sel_note[i])
            return C_ACC(0);
    return n == cv_sel_bass ? C_SEC(1) : 0;
}
static void view_sel(int x0)
{
    const cf_form_t *f = sel_form();
    int off = cf_is_off(&cs.chord, cu.sel_btn), tonic = cs.chord.quantize != CF_Q_OFF ? cs.chord.tonic : 0;
    int base = 60 + tonic, lo = 48, wk = 10, xk = x0 + 108 - 21 * wk / 2, i, n, ky = VIEW_Y + 14, bass_pc, stag;
    uint32_t mask = off ? 1u : cf_form_mask(f);
    uint8_t note[12];
    char a[4];
    n = off ? 1 : cf_voice(base, mask, f->inv < 0 ? 0 : f->inv, note);
    if (off)
        note[0] = (uint8_t)base;
    bass_pc = off ? -1 : cf_bass_pc(tonic, mask, f->bass);
    cv_sel_n = (uint8_t)n;
    for (i = 0; i < n; i++)
        cv_sel_note[i] = note[i];
    cv_sel_bass = bass_pc < 0 ? 0xFFu : (uint8_t)(48 + bass_pc);
    draw_piano(xk, ky, lo, 3, wk, 26, cv_sel_mark);
    if (bass_pc >= 0)                            /* the bass, in the low octave */
        fb_text_c(&CF_FONT_MICRO, piano_cx(xk, lo, wk, 48 + bass_pc), ky + 30, "BASS", C_SEC(1), 1);
    for (i = 0, stag = 0; i < n; i++) {          /* the chord's notes: the degree over, the name under; a note close
                                                  * to the one before steps its labels out of the way */
        int nx = piano_cx(xk, lo, wk, note[i]);
        stag = i > 0 && note[i] - note[i - 1] < 3 ? !stag : 0;
        if (note[i] < lo || note[i] >= lo + 36)
            continue;
        fb_text_c(&CF_FONT_UI, nx + 1, ky - 13 - stag * 10, cv_degree(note[i] - base + 24, mask), FL_INK, 1);
        cf_pc_name(note[i] % 12, &cu_spell, a);
        fb_text_c(&CF_FONT_MICRO, nx + 1, ky + 30 + stag * 7, a, FL_INK2, 1);
    }
    fb_text_halo(&CF_FONT_UI, x0 + 6, VIEW_Y + 62, "press a chord button below", FL_INK3, FL_BG, 1);
}
/* the arrow over the button being edited, in the key row (drawn after the view's clip is let go) */
static void draw_sel_pointer(void)
{
    int k, idx, x, y, w, h, bob = (int)((cu.now / 250u) & 1u);
    for (k = 0; k < CF_NKEYS; k++)
        if (cf_key_role(cs.chord.algo, k, &idx) == CF_ROLE_BUTTON && idx == (int)cu.sel_btn) {
            key_box(k, &x, &y, &w, &h);
            for (idx = 0; idx < 4; idx++)
                fb_hline(x + w / 2 - 3 + idx, y - 6 + idx + bob, 7 - 2 * idx, C_ACC(0));
            return;
        }
}

/* --------------------------------------------------------------- ARP --- */
/* the pattern on a little roll: sixteen steps, each note a bar as long as GATE, the second of each pair late by
 * SWING, high notes high; the step playing lit (the chord held, or the key's triad as a preview); Strum: the
 * chord's notes coming in one after another, RATE ms apart, held. LATCH: a chip while it plays on alone */
static int cv_arp_k(int mode, int len, int i)   /* the arpeggiator's walk (cf_arp.c cfa_pick), random as a hash */
{
    int k;
    switch (mode) {
    case CF_ARP_DOWN: return len - 1 - i % len;
    case CF_ARP_INC: k = i % (2 * len); return k < len ? k : 2 * len - 1 - k;
    case CF_ARP_EXC:
        if (len < 3)
            return i % len;
        k = i % (2 * len - 2);
        return k < len ? k : 2 * len - 2 - k;
    case CF_ARP_RAND: return (int)(((uint32_t)i * 2654435761u >> 13) % (uint32_t)len);
    case CF_ARP_REPEAT: return (i / 2) % len;
    default: return i % len;
    }
}
static void view_arp(int x0)
{
    const int mode = cs.arp.mode % CF_ARP_NMODES, run = cf_sn.arp_on && cf_sn.arp_n;
    int n, i, oc = cs.arp.oct >= 1 && cs.arp.oct <= 4 ? cs.arp.oct : 1, len, lo = 127, hi = 0, sw = 12;
    int xa = x0 + 12, yt = VIEW_Y + 2, h = 40, cur = run ? (int)((cf_sn.arp_idx + 15u) % 16u) : -1;
    uint8_t note[CF_MAX_NOTES], seq[16];
    static const char *const WHAT[CF_ARP_NMODES] = {"low to high, over and over", "high to low, over and over",
        "up and down, the ends twice", "up and down, the ends once", "a random note each step",
        "root, 3rd, 5th.. as built", "each note twice, upward", "every chord strummed up",
        "every chord strummed down"};
    if (run) {
        n = cf_sn.arp_n;
        for (i = 0; i < n; i++)
            note[i] = cf_sn.arp_note_list[i];
    } else {                                     /* (nothing held: the key's triad, to show the pattern) */
        int t = 60 + (cs.chord.quantize != CF_Q_OFF ? cs.chord.tonic : 0);
        n = 3;
        note[0] = (uint8_t)t;
        note[1] = (uint8_t)(t + 4);
        note[2] = (uint8_t)(t + 7);
    }
    if (mode == CF_ARP_ORDER && run) {           /* (Order: from the root up) */
        int j, r = cf_sn.out.root_pc;
        for (i = 1; i < n; i++) {
            uint8_t v = note[i];
            for (j = i - 1; j >= 0 && (note[j] - r + 120) % 12 > (v - r + 120) % 12; j--)
                note[j + 1] = note[j];
            note[j + 1] = v;
        }
    }
    len = n * oc;
    if (CF_ARP_STRUMMING(mode)) {                /* the strum: all of them, one after another */
        int k = len > 16 ? 16 : len, gp = 2 + cs.arp.strum_ms / 6;
        for (i = 0; i < k; i++) {
            int j = mode == CF_ARP_STRUM_DOWN ? k - 1 - i : i;
            seq[i] = (uint8_t)(note[j % n] + 12 * (j / n));
        }
        for (i = 0; i < k; i++) {
            lo = seq[i] < lo ? seq[i] : lo;
            hi = seq[i] > hi ? seq[i] : hi;
        }
        for (i = 0; i < k; i++) {
            int y = yt + h - (hi > lo ? (seq[i] - lo) * h / (hi - lo) : h / 2), x = xa + i * gp;
            if (x > xa + 190)
                break;
            fb_rect(x, y - 1, 3, 3, run ? C_ACC(0) : FL_INK2);
            fb_hline(x + 3, y, xa + 192 - x - 3, run ? C_ACC(1) : FL_INK3);
        }
    } else {
        for (i = 0; i < 16; i++) {
            int k = cv_arp_k(mode, len, run ? (int)(cf_sn.arp_idx - 1u) - cur + i : i);
            seq[i] = (uint8_t)(note[k % n] + 12 * (k / n));
            lo = seq[i] < lo ? seq[i] : lo;
            hi = seq[i] > hi ? seq[i] : hi;
        }
        for (i = 0; i < 16; i++) {
            int y = yt + h - (hi > lo ? (seq[i] - lo) * h / (hi - lo) : h / 2);
            int x = xa + i * sw + ((i & 1) ? sw * (cs.arp.swing - 50) / 50 : 0);
            int w = sw * cs.arp.gate / 100 - 1;
            fb_rect(x, y - 1, w < 2 ? 2 : w, 3, i == cur ? C_ACC(0) : run ? C_ACC(2) : FL_INK3);
        }
        for (i = 0; i <= 16; i += 4)              /* the beats: every four steps */
            fb_vline(xa + i * sw - 1, yt + h + 4, 3, FL_BG3);
        if (cur >= 0)
            fb_rect(xa + cur * sw, yt + h + 8, sw - 2, 2, C_ACC(0));
    }
    if (cf_sn.arp_latched) {                     /* playing on, the keys let go */
        fb_rbox(x0 + 164, VIEW_Y + 50, 44, 11, 2, C_ACC(1));
        fb_text_c(&CF_FONT_UI, x0 + 186, VIEW_Y + 49, "LATCH", FL_BG, 1);
    }
    fb_text_halo(&CF_FONT_UI, x0 + 6, VIEW_Y + 62, !cu.arp_on ? "off: turn ON/OFF to play it" : cu.page[SCR_ARP] ?
                 (cs.arp.latch ? "plays on when you let go" : "stops when you let go") : run ? WHAT[mode] :
                 "hold a chord to play it", FL_INK3, FL_BG, 1);
}

/* -------------------------------------------------------------- SAVE --- */
/* where the sound will be in the row of sounds: the one before, it (its number, its new name, lit), the one after;
 * what the save does; how many user slots are free; and the hold: a bar that fills while SAVE is held */
/* a row: the sound at idx of the part's list, shown with number shown (they move when one is added); name and engine e
 * given: the one being saved */
static void cv_save_row(int x0, int y, uint32_t part, uint32_t idx, uint32_t shown, const char *name, uint32_t e_own,
                        int lit)
{
    uint32_t e, pos, n = cf_snd_count(part);
    char num[4], nm[13];
    if (!n)
        return;
    idx %= n;
    cf_snd_at(part, idx, &e, &pos);
    if (name)
        e = e_own;
    snd_num(shown % (n + 1u), num);
    if (!name) {
        pool_name(e, pos, nm);
        name = nm;
    }
    if (lit)
        fb_rbox(x0 + 6, y - 2, 204, 13, 2, C_ACC(1));
    fb_text(&CF_FONT_UI, x0 + 10, y - 1, num, lit ? FL_BG : FL_INK3, 1);
    fb_text(&CF_FONT_UI, x0 + 40, y - 1, cf_engine_name(e), lit ? FL_BG : C_SEC(1), 1);
    fb_text(&CF_FONT_UI, x0 + 66, y - 1, name, lit ? FL_BG : FL_INK2, 1);
}
static void view_save(int x0)
{
    uint32_t part = cf_save_part(), idx = cf_snd_save_index(part, cu.save_new), n = cf_snd_count(part), k, nfree = 0;
    int replace = !cu.save_new && cf_snd[part].kind != PK_INIT, del = cu.save_del;
    uint8_t warn = FL_C(FL_R_FIRE, 1);
    char b[32];
    if (del) {                                    /* ACTION Delete: what goes, what comes back */
        uint32_t i0 = cf_snd_pos(part);
        cv_save_row(x0, VIEW_Y + 2, part, i0 + n - 1u, (i0 + n - 1u) % (n ? n : 1u), 0, 0, 0);
        cv_save_row(x0, VIEW_Y + 16, part, i0, i0, cf_snd[part].name, trk[part].eng_req % NENGINES, 1);
        cv_save_row(x0, VIEW_Y + 30, part, i0 + 1u, i0 + 1u, 0, 0, 0);
        fb_text_halo(&CF_FONT_UI, x0 + 8, VIEW_Y + 44, cf_snd_delete_restores(part) ?
                     "the factory sound comes back" : "deleted from the list", warn, FL_BG, 1);
        if (cu.save_hold) {
            uint32_t t = cu.now - cu.save_hold;
            fb_rect(x0 + 8, VIEW_Y + 64, 200, 6, FL_BG1);
            fb_rect(x0 + 8, VIEW_Y + 64, (int)(t >= 1000u ? 200u : t * 200u / 1000u), 6, warn);
        } else {
            draw_chip_text(x0 + 8, VIEW_Y + 62, "hold [SAVE] to delete it", warn);
        }
        return;
    }
    for (k = 0; k < UP_SLOTS; k++)
        nfree += !up_used(k);
    cv_save_row(x0, VIEW_Y + 2, part, idx + n - 1u, (idx + n - 1u) % (n ? n : 1u), 0, 0, 0);
    cv_save_row(x0, VIEW_Y + 16, part, idx, idx, cu.save_name, trk[part].eng_req % NENGINES, 1);
    cv_save_row(x0, VIEW_Y + 30, part, replace ? idx + 1u : idx, idx + 1u, 0, 0, 0);   /* (as new: it moves down one) */
    if (replace)
        str_cpy(b, cf_snd[part].kind == PK_USER ? "saved over itself" : "in place of the factory one", sizeof b);
    else
        str_cpy(b, "added as a new sound", sizeof b);
    fb_text_halo(&CF_FONT_UI, x0 + 8, VIEW_Y + 44, b, FL_INK3, FL_BG, 1);
    if (cu.save_hold) {                           /* held: the bar fills in a second */
        uint32_t t = cu.now - cu.save_hold;
        int w = (int)(t >= 1000u ? 200u : t * 200u / 1000u);
        fb_rect(x0 + 8, VIEW_Y + 64, 200, 6, FL_BG1);
        fb_rect(x0 + 8, VIEW_Y + 64, w, 6, C_ACC(0));
    } else {                                      /* how; and the user slots left (32 in all) */
        draw_chip_text(x0 + 8, VIEW_Y + 62, nfree || replace ? "hold [SAVE] to keep it" : "no free slot",
                       nfree || replace ? FL_INK2 : C_ACC(0));
        kint(b, (int)nfree, " free");
        fb_text_r(&CF_FONT_UI, x0 + 208, VIEW_Y + 62, b, FL_INK3, 1);
    }
}


/* ------------------------------------------------- GLOBAL > GRAIN, PITCH, HOLD --- */
/* the ring (the last ~0.7 s of the music, the newest on the right) as a bar, a grain's place and width on it (POS, SIZE),
 * and as many specks above it as DENS says, scattered by RAND; the layer dark while MIX is 0 */
static void view_grain(int x0)
{
    int xl = x0 + 14, xr = x0 + 194, w = xr - xl, y = VIEW_Y + 26, i, n;
    int on = cgr.mix != 0;
    int gw = 4 + (int)cgr.size * (w / 2) / 100;
    int gx = xr - gw - (int)cgr.pos * (w - gw) / 100;
    uint32_t seed = (cu.now >> 7) * 40503u;
    fb_rect(xl, y, w, 10, FL_BG3);
    fb_rect(gx, y, gw, 10, on ? C_ACC(1) : FL_INK3);
    if (cgr.freeze)
        fb_rect(xr - 2, y - 3, 2, 16, C_ACC(0));              /* the head, stopped */
    n = 2 + (int)cgr.dens * 12 / 100;
    for (i = 0; i < n; i++) {
        uint32_t h = (uint32_t)(i + 1) * 2654435761u + seed;
        int jx = (((int)((h >> 8) & 255u) - 128) * (int)cgr.rnd * w) / (100 * 256 * 2);
        int px = gx + gw / 2 + jx, py = y - 6 - (int)((h >> 20) & 15u);
        px = px < xl ? xl : px > xr - 2 ? xr - 2 : px;
        fb_rect(px, py, 2, 2, on ? C_ACC(0) : FL_INK3);
    }
    fb_text_halo(&CF_FONT_UI, xl, y + 16, "older", FL_INK3, FL_BG, 1);
    fb_text_r(&CF_FONT_UI, xr, y + 16, cgr.freeze ? "frozen" : "now", cgr.freeze ? C_ACC(0) : FL_INK3, 1);
    fb_text_halo(&CF_FONT_UI, xl, y + 34, on ? "grains cut from the music" : "MIX is 0: grain is off", FL_INK2, FL_BG, 1);
}
