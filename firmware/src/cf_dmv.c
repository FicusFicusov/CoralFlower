/* SPDX-License-Identifier: GPL-3.0-only
 * CoralFlower: the drum machine drawn (0.6): its page, the METRONOME's carousel of where SEQ goes, the plaque (what
 * plays) on the top lines. Included by cf_screens.c after cf_seqv.c (its flowers draw the hits). */

static void draw_view(void (*view)(int));

/* the plaque at xr (its right edge) on the top line, as HOME's "SEQ A" always looked; returns its left edge */
static int dm_plaque_chip(int xr, const char *b, uint32_t kind)
{
    int w = fb_text_w(&CF_FONT_UI, b, 1) + 12, x = xr - w;
    uint8_t box = kind == DMP_REC ? FL_C(FL_R_FIRE, 2) : kind == DMP_RECIN || kind == DMP_PALE ? FL_BG2 : cf_panel(2);
    uint8_t dot = kind == DMP_REC ? FL_INK : kind == DMP_RECIN ? FL_C(FL_R_FIRE, 1) : kind == DMP_PALE ? FL_BG3 :
                  C_ACC(0);
    fb_rbox(x, Y_TOP + 2, w, 11, 2, box);
    fb_rect(x + 3, Y_TOP + 5, 4, 4, dot);
    fb_text(&CF_FONT_UI, x + 9, Y_TOP + 1, b, kind == DMP_PALE ? FL_INK3 : FL_INK, 1);
    return x;
}
/* the other pages' top line (cf_ui_draw): the plaque where the PLAY hint stands (it says what PLAY stops); 1 drawn */
static int dm_plaque_top(void)
{
    char b[16];
    uint32_t kind = dm_plaque(b, sizeof b);
    if (!dm_plaque_here(kind) || (kind == DMP_PALE && cu.scr != SCR_SEQ))
        return 0;
    dm_plaque_chip(212, b, kind);
    return 1;
}

/* the METRONOME's carousel: where SEQ goes from here (the hint "Press [SEQ] again" was its place) */
static void dm_draw_carousel(void)
{
    const char *n = dm_seq_to ? "Drum machine" : "Sequencer";
    int w = fb_text_w(&CF_FONT_UI, n, 2), y = Y_VIEW + 64, lit = cu.now - cv_turn_ms < 220u && cv_turn_ms;
    fb_text_halo(&CF_FONT_UI, FB_W / 2 - fb_text_w(&CF_FONT_UI, "SEQ opens", 1) / 2, Y_VIEW + 50, "SEQ opens", FL_INK2,
                 FL_BG, 1);
    fb_text_halo(&CF_FONT_UI, FB_W / 2 - w / 2, y, n, C_ACC(0), FL_BG, 2);
    draw_chevron(FB_W / 2 - w / 2 - 14, y + 7, 0, dm_seq_to ? (lit ? C_ACC(0) : FL_INK2) : FL_BG3);
    draw_chevron(FB_W / 2 + w / 2 + 9, y + 7, 1, dm_seq_to ? FL_BG3 : lit ? C_ACC(0) : FL_INK2);
    fb_rect(FB_W / 2 - 6, Y_VIEW + 92, 4, 4, dm_seq_to ? FL_INK3 : C_ACC(1));
    fb_rect(FB_W / 2 + 2, Y_VIEW + 92, 4, 4, dm_seq_to ? C_ACC(1) : FL_INK3);
    draw_chip_text_c(Y_VIEW + 101, "[SELECT] Sequencer / Drum machine", FL_INK3);
}

/* a drum's name in the rhythm's kit (the hand drums' kit: congas and a clave for the toms and the rim), as the
 * DRUMS editor names it (cf_sequi.c sq_lane_label) */
static const char *dm_lane_label(uint32_t l, uint32_t kit)
{
    static const char *const N[SQ_NLANE] = {"KICK", "SNARE", "CLAP", "HAT", "OPEN HAT", "LOW TOM", "MID TOM",
                                            "HIGH TOM", "RIM", "BELL", "CRASH"};
    static const char *const H[4] = {"CONGA L", "CONGA M", "CONGA H", "CLAVE"};
    l %= SQ_NLANE;
    return (SQ_KITS[kit % SQ_NKIT].e[0] & 1u) && l >= 5u && l <= 8u ? H[l - 5u] : N[l];
}

/* the drum machine: the rhythm's name in a carousel, under it its place, TIME SIG and bars; its drums' rows of hits
 * (both bars of a 2-bar one, a dashed line between), a cursor running over them while it plays. Its name blinks while
 * it waits for the bar line; a dot after it: the user's KIT or SWING on it (SEL takes them back) */
static void view_drum(int x0)
{
    uint32_t r = dm_cur(), sd = sq_dsteps(), nb, steps, l, b, i, nl = 0;
    const dm_rhythm_t *y = &DM_RHY[r % DM_N];
    int gx = x0 + 34, gw = 216 - 34, row, yt = VIEW_Y - 2, k = 0, play = dm_plays(r);
    nb = y->bars ? y->bars : 1u;
    steps = nb * sd;
    for (l = 0; l < SQ_NLANE; l++)
        nl += y->lane[l] != 0;
    row = nl <= 4u ? 16 : 13;
    for (l = 0; l < SQ_NLANE; l++) {
        int yr = yt + k * row;
        if (!y->lane[l])
            continue;
        k++;
        fb_text(&CF_FONT_MICRO, x0 - 4, yr + 7, dm_lane_label(l, dm_kit(r)), FL_INK3, 1);
        fb_hline(gx, yr + SQG_GND + 1, gw, FL_BG3);
        for (b = 0; b < nb; b++) {
            uint32_t m = dm_mask(r, l, b);
            for (i = 0; i < sd; i++) {
                int s = (int)(b * sd + i), cw = gw / (int)steps, cx = gx + s * gw / (int)steps + cw / 2;
                if (i & 1u)
                    cx += (int)dm_swing(r) * cw / 200;
                if ((m >> i) & 1u)
                    sq_flower(cx, yr, cw, 2, sq_lane_ink(l), 0);
                else
                    fb_rect(cx, yr + SQG_GND, 1, 1, FL_BG3);
            }
        }
    }
    if (nb > 1u) {                               /* (the second bar begins: a dashed line) */
        int xb = gx + (int)sd * gw / (int)steps - 1;
        int yy;
        for (yy = yt; yy < yt + k * row; yy += 4)
            fb_rect(xb, yy, 1, 2, FL_C(FL_R_ORCHID, 2));
    }
    if (play && cf_sn.sq_bt) {                   /* (playing: where it is) */
        uint32_t pos = cf_sn.dm_bar * sd * 256u + cf_sn.dm_t * sd * 256u / cf_sn.sq_bt;
        int xc = gx + (int)(pos * (uint32_t)gw / (steps * 256u));
        fb_rect(xc, yt - 2, 1, k * row + 2, FL_C(FL_R_FIRE, 1));
    }
}
static void draw_drum(void)
{
    uint32_t r = dm_cur(), beats = dm_beats(), n = dm_count(beats), p = dm_place_now();
    const dm_rhythm_t *y = &DM_RHY[r % DM_N];
    int wait = cf_sn.dm_on && cf_sn.dm_next == r && cf_sn.dm_r != r, w, lit = cu.now - cv_turn_ms < 220u && cv_turn_ms;
    char t[40], a[8];
    flora_render(&flo, &fl_cv);
    draw_plant(Y_VIEW);
    fb_veil(0, Y_VIEW, FB_W, H_VIEW, FL_BG);    /* (the plant faint: the picture leads) */
    w = fb_text_w(&CF_FONT_UI, y->name, 2);
    if (!wait || (cu.now / 250u) & 1u)           /* (waiting for the bar line: it blinks) */
        fb_text_halo(&CF_FONT_UI, FB_W / 2 - w / 2, HEAD_Y, y->name, FL_INK, FL_BG, 2);
    if (dm_tweaked(r))                           /* (the user's KIT or SWING on it) */
        fb_rect(FB_W / 2 + w / 2 + 4, HEAD_Y + 14, 4, 4, C_ACC(0));
    draw_chevron(FB_W / 2 - w / 2 - 16, HEAD_Y + 7, 0, p ? (lit && cv_turn_dir < 0 ? C_ACC(0) : FL_INK2) : FL_BG3);
    draw_chevron(FB_W / 2 + w / 2 + 12, HEAD_Y + 7, 1,
                 p + 1u < n ? (lit && cv_turn_dir > 0 ? C_ACC(0) : FL_INK2) : FL_BG3);
    str_cpy(t, "rhythm ", sizeof t);
    kint(t + str_len(t), (int)p + 1, 0);
    str_cpy(t + str_len(t), " of ", sizeof t - str_len(t));
    kint(a, (int)n, 0);
    str_cpy(t + str_len(t), a, sizeof t - str_len(t));
    str_cpy(t + str_len(t), "   ", sizeof t - str_len(t));
    kint(a, (int)beats, 0);
    str_cpy(t + str_len(t), a, sizeof t - str_len(t));
    str_cpy(t + str_len(t), beats == 6u ? "/8   " : "/4   ", sizeof t - str_len(t));
    kint(a, y->bars, 0);
    str_cpy(t + str_len(t), a, sizeof t - str_len(t));
    str_cpy(t + str_len(t), y->bars > 1u ? " bars" : " bar", sizeof t - str_len(t));
    fb_text_halo(&CF_FONT_UI, FB_W / 2 - fb_text_w(&CF_FONT_UI, t, 1) / 2, HEAD_LINE, t, C_ACC(1), FL_BG, 1);
    draw_view(view_drum);
    draw_chip_text_c(Y_VIEW + H_VIEW - 14, "[SELECT] rhythm", FL_INK3);
    {
        static const char *const H[2] = {"PLAY", "start"};
        static const char *const H2[2] = {"PLAY", "stop"};
        draw_hints(cf_sn.dm_on || cf_sn.sq_on || cf_sn.running ? H2 : H, 1);
    }
}
