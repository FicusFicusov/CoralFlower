/* SPDX-License-Identifier: GPL-3.0-only
 * CoralFlower: the screens' pictures and the popups, then the frame (cf_ui_draw). Included after cf_ui.c. */

#define fcos_ui(a) fl_fcos(a)                       /* (flora.c's integer sine) */
#define fsin_ui(a) fl_fsin(a)
static char cu_name[16], cu_name_sub[12];        /* the chord shown (it stays after the hands let go) */
static uint8_t cu_name_single;                   /* .. it is single notes (Off) */
static uint8_t cu_name_live;

static void name_update(void)
{
    const cf_chord_out_t *o = &cf_sn.out;
    if (!o->sounding || !o->n) {
        cu_name_live = 0;
        return;
    }
    cu_name_live = 1;
    cu_name_single = o->single;
    if (o->single) {
        char a[4];
        int i, k = 0;
        cu_name[0] = 0;
        for (i = 0; i < o->n && i < 3; i++) {
            cf_pc_name(o->note[i] % 12, &cu_spell, a);
            str_cpy(cu_name + k, a, sizeof cu_name - (uint32_t)k);
            k = (int)str_len(cu_name);
            if (i + 1 < o->n && i < 2 && k < 14)
                cu_name[k++] = ' ', cu_name[k] = 0;
        }
        cu_name_sub[0] = 0;
        return;
    }
    cf_chord_name(o->root_pc, o->mask, cs.bass_on && o->bass_pc != o->root_pc ? o->bass_pc : -1, &cu_spell,
                  cu_name);
    cu_name_sub[0] = 0;
    if (cs.chord.quantize != CF_Q_OFF)
        cf_numeral(cs.chord.scale, cs.chord.tonic, o->root_pc, cu_name_sub);
}

/* a sound's number in its row, "007" */
static void snd_num(uint32_t idx, char *b)
{
    b[0] = (char)('0' + (idx + 1u) / 100u % 10u);
    b[1] = (char)('0' + (idx + 1u) / 10u % 10u);
    b[2] = (char)('0' + (idx + 1u) % 10u);
    b[3] = 0;
}
/* the top bar's sound: number, engine, name */
static void draw_top_sound(uint32_t part)
{
    char num[4];
    int x = 4;
    snd_num(cf_snd_pos(part), num);
    x += fb_text(&CF_FONT_UI, x, Y_TOP + 1, num, FL_INK3, 1) + 5;
    x += fb_text(&CF_FONT_UI, x, Y_TOP + 1, cf_engine_name(cf_snd[part].engine), C_SEC(1), 1) + 5;
    fb_text(&CF_FONT_UI, x, Y_TOP + 1, cf_snd[part].name, FL_INK, 1);
}

#include "cf_views.c"                           /* (the heads, the pager, the central pictures) */
#include "cf_seqv.c"                            /* (the SEQUENCER screens' pictures) */
static void draw_title(const char *t, const char *note);
/* ------------------------------------------------------------------- FX --- */
/* the selected effect's own little picture, x0..x0+w along, its foot on yb, at most h tall */
static void draw_fx_graph(uint32_t e, int x0, int yb, int w, int h)
{
    const uint8_t *v = cs.fx.v[e];
    uint8_t c = cfx_is_on(e) ? C_ACC(1) : FL_INK3, c2 = cfx_is_on(e) ? C_ACC(2) : FL_BG3;
    int x, px = -1, py = 0;
    fb_hline(x0, yb, w, FL_BG3);
    for (x = 0; x < w; x++) {
        int32_t y = 0;
        switch (e) {
        case CFX_FILTER: {                       /* the response: flat, the corner (a bump for the resonance), a slope */
            int cx = (int)v[1] * (w - 1) / 99, d = x - cx, bump = (int)v[2] * h / 160;
            int lp = d < 0 ? h * 2 / 3 : h * 2 / 3 - d * h / 28, a = d < 0 ? -d : d;
            if (a < 8)
                lp += bump * (8 - a) / 8;
            if (v[0] == 1)                       /* band pass: both sides fall */
                lp = h * 2 / 3 - a * h / 28 + (a < 8 ? bump * (8 - a) / 8 : 0);
            else if (v[0] == 2)                  /* high pass: the mirror */
                lp = d > 0 ? h * 2 / 3 : h * 2 / 3 + d * h / 28 + (a < 8 ? bump * (8 - a) / 8 : 0);
            y = lp < 0 ? 0 : lp > h ? h : lp;
            break;
        }
        default:
            break;
        }
        if (e == CFX_FILTER) {
            if (px >= 0)
                fb_line(x0 + px, yb - py, x0 + x, yb - (int)y, c);
            px = x;
            py = (int)y;
        }
    }
    if (e == CFX_REVERB || e == CFX_DELAY) {     /* bars: a tail decaying, or the echoes */
        int i, n = e == CFX_REVERB ? 56 : 12, step = e == CFX_REVERB ? 4 : 6 + (int)v[0] * 40 / 100;
        int32_t a = h * 256 * (50 + (int)v[2]) / 150, k = e == CFX_REVERB ? 205 + (int)v[1] * 45 / 100 : 80 + (int)v[1] * 17 / 10;
        for (i = 0; i < n && i * step < w; i++) {
            int bh = (int)(a >> 8);
            if (e == CFX_REVERB)                 /* (a grain of noise in the tail) */
                bh = bh * (12 + (int)((uint32_t)(i * 2654435761u) >> 29)) / 16;
            if (bh > 0)
                fb_rect(x0 + i * step, yb - bh, e == CFX_REVERB ? 2 : 3, bh, i ? c : C_ACC(0));
            a = a * k / 256;
        }
    } else if (e == CFX_DIST) {                  /* the transfer curve: tanh of the driven input */
        int32_t g = CFX_DRIVE[v[0] > 100u ? 100u : v[0]];
        for (x = 0; x < w; x++) {
            int32_t in = (x * 2 - w) * 32768 / w, y = softclip((int32_t)(((int64_t)in * g) >> 8));
            int yy = yb - h / 2 - (int)(y * (h / 2) / 32768);
            if (px >= 0)
                fb_line(x0 + px, py, x0 + x, yy, c);
            px = x;
            py = yy;
        }
    } else if (e == CFX_CHORUS || e == CFX_PHASER) {   /* the sweep: a sine moving with the rate */
        uint32_t ph = (uint32_t)cu.now * (uint32_t)(20 + v[0] * 6) * 1000u;
        int amp = (h / 2 - 1) * (10 + (int)v[1]) / 110;
        for (x = 0; x < w; x++) {
            int yy = yb - h / 2 - (osc_sine(ph + (uint32_t)x * (e == CFX_PHASER ? 60000000u : 28000000u)) * amp) / 32768;
            if (px >= 0)
                fb_line(x0 + px, py, x0 + x, yy, x & 1 ? c : c2);
            px = x;
            py = yy;
        }
    }
}

/* FX: the chain as a row of tiles in its order (on: raised, off: dim), the sound running along it, the selected
 * effect named above and pictured below */
static void draw_fx(void)
{
    uint32_t sel = cu.fx_pos % CFX_N, e = cs.fx.order[sel] % CFX_N, i;
    int x0 = 8, y0 = Y_VIEW + 58, tw = 34, gap = 4, run = 6 * tw + 5 * gap;
    char sub[24];
    flora_render(&flo, &fl_cv);
    draw_plant(Y_VIEW);
    fb_veil(0, Y_VIEW, FB_W, H_VIEW, FL_BG);   /* (the plant faint: the picture leads) */
    str_cpy(sub, e == CFX_FILTER ? CFX_FTYPE[cs.fx.v[e][0] % 3u] : e == CFX_REVERB ? CFX_RTYPE[cs.fx.v[e][0] % 3u] : "",
            sizeof sub);
    if (sub[0])
        str_cpy(sub + str_len(sub), "  ", 3);
    str_cpy(sub + str_len(sub), cfx_is_on(e) ? "on" : "off", 4);
    draw_title(CFX_NAME[e], sub);
    fb_hline(x0, y0 + 13, run, FL_BG3);          /* the signal's way, a dot running along it */
    {
        int d = (int)((cu.now / 10u) % (uint32_t)run);
        fb_rect(x0 + d, y0 + 12, 2, 3, C_ACC(1));
    }
    for (i = 0; i < CFX_N; i++) {
        uint32_t id = cs.fx.order[i] % CFX_N;
        int x = x0 + (int)i * (tw + gap), on = cfx_is_on(id);
        fb_rbox(x, y0, tw, 26, 2, on ? cf_panel(2) : cf_panel(1));
        if (i == sel)
            fb_rframe(x, y0, tw, 26, C_ACC(1));
        fb_text_c(&CF_FONT_UI, x + tw / 2, y0 + 3, CFX_SHORT[id], on ? FL_INK : FL_INK3, 1);
        fb_rect(x + tw / 2 - 3, y0 + 19, 6, 2, on ? C_ACC(1) : FL_BG3);
    }
    fb_rect(x0 + (int)sel * (tw + gap) + tw / 2 - 1, y0 + 29, 3, 2, C_ACC(1));   /* (under the selected one) */
    draw_fx_graph(e, x0, Y_VIEW + 122, run, 24);
    {
        static const char *const H[4] = {"SELECT", "pick", "ALGO", "move"};
        draw_hints(H, 2);
    }
}

/* ------------------------------------------------------------------ HOME --- */
static void draw_home(void)
{
    char key[16], k2[8];
    flora_render(&flo, &fl_cv);
    draw_plant(Y_VIEW);
    name_update();
    if (cu_name[0])
        draw_chord_name(6, Y_VIEW + 7, cu_name, cu_name_sub, cu_name_single);   /* (C MAJ's caps are at + 7) */
    /* the key in the corner */
    if (cs.chord.quantize != CF_Q_OFF) {
        kpc(k2, cs.chord.tonic);
        str_cpy(key, k2, sizeof key);
        str_cpy(key + str_len(key), " ", 2);
        str_cpy(key + str_len(key), CF_SCALES[cs.chord.scale].shrt, 8);
    } else {
        str_cpy(key, "CHROMATIC", sizeof key);
    }
    fb_text_halo(&CF_FONT_UI, 234 - fb_text_w(&CF_FONT_UI, key, 1), Y_VIEW + 4, key, FL_INK2, FL_BG, 1);
    if (cs.chord.octave) {
        char b[8];
        b[0] = 'O';
        b[1] = 'C';
        b[2] = 'T';
        b[3] = ' ';
        b[4] = cs.chord.octave > 0 ? '+' : '-';
        b[5] = (char)('0' + (cs.chord.octave > 0 ? cs.chord.octave : -cs.chord.octave));
        b[6] = 0;
        fb_text_halo(&CF_FONT_UI, 234 - fb_text_w(&CF_FONT_UI, b, 1), Y_VIEW + 18, b, C_ACC(1), FL_BG, 1);
    }
    if (cf_sn.running) {                       /* the beat */
        int i;
        for (i = 0; i < cs.beats; i++) {
            int on = i == (cf_sn.beat + cs.beats - 1) % cs.beats;
            fb_rect(234 - (cs.beats - i) * 6, Y_VIEW + H_VIEW - 8, 4, 4, on ? C_ACC(0) : FL_BG3);
        }
    }
}

/* ------------------------------------------------------- the other screens --- */
static void draw_title(const char *t, const char *note)   /* a head with no pages (cf_views.c) */
{
    draw_head(t);
    draw_head_note(note);
}
/* the picture of a paged screen, sliding in after a turn (cv_slide), clipped under the head */
static void draw_view(void (*view)(int))
{
    fb_clip(0, VIEW_Y - 8, FB_W, Y_VIEW + H_VIEW);
    view(12 + cv_slide());
    fb_clip(0, Y_VIEW, FB_W, Y_VIEW + H_VIEW);
}

/* SEL: the chord button's chord on the pitch-class clock, the bass degree marked */
static void draw_sel(void)
{
    const cf_form_t *f = sel_form();
    char lab[8], b[24];
    flora_render(&flo, &fl_cv);
    draw_plant(Y_VIEW);
    fb_veil(0, Y_VIEW, FB_W, H_VIEW, FL_BG);   /* (the plant faint: the picture leads) */
    if (cf_is_off(&cs.chord, cu.sel_btn)) {
        draw_title("OFF", "single notes, no chord");
    } else {                                     /* the button as its key shows it; its chord in words */
        cf_form_label(f, lab);
        str_cpy(b, cf_type_name(f->type), sizeof b);
        if (f->add) {
            str_cpy(b + str_len(b), " + ", 4);
            str_cpy(b + str_len(b), cf_adds_name(cf_adds_index(f->add)), 10);
        }
        draw_title(lab, b);
    }
    draw_view(view_sel);
    draw_sel_pointer();
    {
        static const char *const H[2] = {"SELECT", "button"};
        draw_hints(H, 1);
    }
}

/* ENV: the envelope's shape, drawn over the plant */
static void draw_env(void)
{
    static const char *const TGT[4] = {"Chord + bass", "Chord", "Bass", "Layer"};
    int a = cf_env_get(env_part(), 0), d = cf_env_get(env_part(), 1), sl = cf_env_get(env_part(), 2);
    int r = cf_env_get(env_part(), 3), x0 = 16, yb = Y_VIEW + H_VIEW - 14, yt = Y_VIEW + 50;
    int xa = x0 + 8 + a * 44 / 99, xd = xa + 8 + d * 44 / 99, xs = xd + 40, xr = xs + 8 + r * 50 / 99;
    int ys = yb - (yb - yt) * sl / 99;
    flora_render(&flo, &fl_cv);
    draw_plant(Y_VIEW);
    fb_veil(0, Y_VIEW, FB_W, H_VIEW, FL_BG);   /* (the plant faint: the picture leads) */
    draw_head(TGT[cu.env_tgt % 4u]);
    draw_pager((int)(cu.env_tgt % 4u), 4, 1, 0);
    fb_hline(x0, yb, 210, FL_BG3);
    fb_line(x0, yb, xa, yt, C_ACC(1));
    fb_line(xa, yt, xd, ys, C_ACC(1));
    fb_line(xd, ys, xs, ys, C_ACC(1));
    fb_line(xs, ys, xr, yb, C_ACC(1));
    fb_rect(xa - 1, yt - 1, 3, 3, C_ACC(0));
    fb_rect(xd - 1, ys - 1, 3, 3, C_ACC(0));
    fb_rect(xs - 1, ys - 1, 3, 3, C_ACC(0));
    {
        static const char *const H[2] = {"SELECT", "sound"};   /* (chord + bass, chord, bass, the LAYER) */
        draw_hints(H, 1);
    }
}

/* EDIT: the sound, the section and its page; FM: the six operators, the carriers below */
static void draw_edit(void)
{
    uint32_t part = cu.edit_part, np = cfe_npages(part, cu.edit_sec);
    char t[24];
    flora_render(&flo, &fl_cv);
    draw_plant(Y_VIEW);
    fb_veil(0, Y_VIEW, FB_W, H_VIEW, FL_BG);   /* (the plant faint: the picture leads) */
    /* the title: the section (FM: "OP 1", its page's name by the pager), or the VA's page ("OSC 1", its second page
     * the same name). The sound edited: in the top bar, after SOUND (cf_ui_draw) */
    if (cfe_fm6(part)) {
        const char *k = cfe_key_name(part, cu.edit_sec);
        static const char *const FMSEC[2] = {"Pitch", "Voice"};
        if (cu.edit_sec < 6u) {
            str_cpy(t, "OP ", sizeof t);
            t[3] = k[2];
            t[4] = 0;
        } else {
            str_cpy(t, FMSEC[(cu.edit_sec - 6u) & 1u], sizeof t);
        }
        draw_head(t);
        {                                        /* the page's name, without the title's word ("Pitch Rate": Rate) */
            const char *pn = cfe_page_title(part, cu.edit_sec, cu.page[SCR_EDIT]);
            uint32_t n = str_len(t), i;
            for (i = 0; i < n && pn[i] == t[i]; i++)
                ;
            if (i == n && (!pn[n] || pn[n] == ' '))
                pn += pn[n] ? n + 1u : n;
            draw_pager(cu.page[SCR_EDIT], (int)np, 0, pn);
        }
    } else {
        uint32_t n;
        str_cpy(t, cfe_page_title(part, cu.edit_sec, cu.page[SCR_EDIT]), sizeof t);
        n = str_len(t);
        if (n && t[n - 1] == '+')
            t[n - 1] = 0;
        draw_head(t);
        draw_pager(cu.page[SCR_EDIT], (int)np, 0, 0);
#if FELUCCA_VA
        if (cfe_va(part))
            draw_view(view_va);
#endif
    }
    if (cfe_fm6(part))
        draw_view(view_fm);
    {
        static const char *const H[4] = {"SELECT", "page", "KEYS", "section"};
        draw_hints(H, 2);
    }
}

static void draw_generic(void)
{
    static const char *const PG_GLO[8] = {"Display", "MIDI", "Channels", "Play", "Reset", "Grain", "Pitch", "Hold"};
    flora_render(&flo, &fl_cv);
    draw_plant(Y_VIEW);
    fb_veil(0, Y_VIEW, FB_W, H_VIEW, FL_BG);   /* (the plant faint: the picture leads) */
    switch (cu.scr) {
    case SCR_GLO: {
        static const char *const H[2] = {"SELECT", "page"};
        static void (*const V[8])(int) = {view_display, view_midi, view_midi, view_play, view_reset, view_grain,
                                          view_grain, view_grain};   /* (MIDI and Channels: one picture; the three
                                                                      * GRAIN pages another) */
        uint32_t p = cu.page[SCR_GLO] % 8u;
        draw_head(PG_GLO[p]);
        draw_pager((int)p, 8, 1, 0);
        draw_view(V[p]);
        draw_hints(H, 1);
        break;
    }
    case SCR_BASS: {
        char b[16], a[4];
        int i;
        b[0] = 0;
        for (i = 0; i < cf_sn.nb && i < 2; i++) {   /* the bass's notes now, "C2  C3" */
            cf_pc_name(cf_sn.bass[i] % 12u, &cu_spell, a);
            str_cpy(b + str_len(b), a, 4);
            kint(b + str_len(b), cf_sn.bass[i] / 12 - 1, 0);
            if (i + 1 < cf_sn.nb)
                str_cpy(b + str_len(b), "  ", 3);
        }
        draw_title(cf_snd[CF_PART_BASS].name, b);
        draw_view(view_bass);
        break;
    }
    case SCR_SEQ: {
        char b[12], s2[8];
        kint(b, cs.bpm, " BPM");
        kint(s2, cs.beats, 0);
        str_cpy(s2 + str_len(s2), cs.beats == 6 ? "/8" : "/4", 4);
        draw_title(b, s2);
        /* (the owner's words, in the middle of the picture: on one line it ran edge to edge, at the foot it sat on
         * the keys; "по центру экрана", 2026-10-19) */
        draw_chip_text_c(Y_VIEW + 70, "Press [SEQ] again", FL_INK2);
        draw_chip_text_c(Y_VIEW + 84, "to open sequencer", FL_INK2);
        {
            static const char *const H[2] = {"PLAY", "start"};   /* (the click sounds only while the beat runs) */
            static const char *const H2[2] = {"PLAY", "stop"};
            draw_hints(cf_sn.running ? H2 : H, 1);
        }
        break;
    }
    case SCR_ARP: {                              /* the mode; Pattern and Feel; the pattern pictured */
        static const char *const PG[2] = {"Pattern", "Feel"};
        static const char *const H[2] = {"SELECT", "page"};
        draw_head(CF_ARP_NAME[cs.arp.mode % CF_ARP_NMODES]);
        draw_pager(cu.page[SCR_ARP] & 1, 2, 1, PG[cu.page[SCR_ARP] & 1]);
        fb_text_r(&CF_FONT_UI, 234, HEAD_LINE, cu.arp_on ? (cf_grid_shown() ? "on the beat" : "on") : "off",
                  cu.arp_on ? C_ACC(1) : FL_INK3, 1);
        draw_view(view_arp);
        draw_hints(H, 1);
        break;
    }
    case SCR_SAVE: {                             /* the name being given, its letter under edit; Chord / Bass / Layer */
        static const char *const PG[3] = {"Chord", "Bass", "Layer"};
        static const char *const H[4] = {"SELECT", "part", "SAVE", "hold"};
        uint32_t part = cf_save_part();
        char pre[13];
        draw_head(cu.save_name[0] ? cu.save_name : " ");
        str_cpy(pre, cu.save_name, (uint32_t)cu.save_cur + 1u);
        fb_rect(6 + fb_text_w(&CF_FONT_UI, pre, 2), HEAD_Y + 25, 13, 2, C_ACC(0));   /* (under the letter K3 sets) */
        draw_pager((int)part, 3, 1, PG[part]);
        if (cu.edited[part])
            fb_text_r(&CF_FONT_UI, 234, HEAD_LINE, "edited", C_ACC(1), 1);
        draw_view(view_save);
        draw_hints(H, 2);
        break;
    }
    default: break;
    }
}

/* ---------------------------------------------------------------- popups --- */
static void draw_pop_sound(uint32_t part)
{
    int x = 16, y = Y_VIEW + 26, w = 208, h = 94, i;
    uint32_t n = cf_snd_count(part), pos = cf_snd_pos(part);
    char a[4], b[4], t[12];
    draw_panel(x, y, w, h);
    fb_text(&CF_FONT_UI, x + 8, y + 5, part == 2u ? "LAYER SOUND" : part ? "BASS SOUND" : "CHORD SOUND", C_ACC(0), 1);
    snd_num(pos, a);
    snd_num(n ? n - 1u : 0u, b);
    str_cpy(t, a, sizeof t);
    str_cpy(t + 3, " / ", 4);
    str_cpy(t + 6, b, 4);
    fb_text_r(&CF_FONT_UI, x + w - 8, y + 5, t, FL_INK2, 1);
    for (i = -2; i <= 2; i++) {
        uint32_t idx = n ? (pos + n * 2u + (uint32_t)(i + 2) - 2u) % n : 0u, e, pp;
        char nm[13], num[4];
        int yy = y + 22 + (i + 2) * 14;
        cf_snd_at(part, idx, &e, &pp);
        pool_name(e, pp, nm);
        snd_num(idx, num);
        if (!i)
            fb_rect(x + 4, yy - 1, w - 8, 14, FL_BG2);
        if (i && idx == 0u)                      /* the row starts again here */
            fb_hline(x + 8, yy - 2, w - 16, C_ACC(3));
        fb_text(&CF_FONT_UI, x + 8, yy, num, i ? FL_INK3 : FL_INK2, 1);
        fb_text(&CF_FONT_UI, x + 38, yy, cf_engine_name(e), i ? FL_INK3 : C_SEC(0), 1);
        fb_text(&CF_FONT_UI, x + 64, yy, nm, i ? FL_INK2 : FL_INK, 1);
    }
}

static void draw_pop_algo(void)
{
    int x = 14, y = Y_VIEW + 36, w = 212, h = 80, k;
    draw_panel(x, y, w, h);
    fb_text(&CF_FONT_UI, x + 8, y + 5, "CHORD ALGORITHM", C_ACC(0), 1);
    fb_text_r(&CF_FONT_UI, x + w - 8, y + 5, ALG_NAME[cs.chord.algo], FL_INK, 1);
    /* the keyboard in small: chord buttons in the accent, roots in grey */
    for (k = 0; k < CF_NKEYS; k++) {
        int kx, ky, kw, kh, role = cf_key_role(cs.chord.algo, k, 0);
        key_box(k, &kx, &ky, &kw, &kh);
        kx = x + 15 + kx * 3 / 4;
        ky = y + 30 + (ky - Y_KEYS) * 3 / 4;
        fb_rbox(kx, ky, kw * 3 / 4, kh * 3 / 4, 1, role == CF_ROLE_BUTTON ? C_ACC(1) : FL_BG3);
    }
    fb_text(&CF_FONT_UI, x + 8, y + h - 18, cs.chord.algo & 1 ? "buttons latch" : "hold the buttons", FL_INK2, 1);
}

static void draw_popup(void)
{
    switch (cu.pop) {
    case POP_SOUND_CHORD: draw_pop_sound(CF_PART_CHORD); break;
    case POP_SOUND_BASS: draw_pop_sound(CF_PART_BASS); break;
    case POP_SOUND_LAYER: draw_pop_sound(CF_PART_LAYER); break;
    case POP_ALGO: draw_pop_algo(); break;
    case POP_OCTAVE: {
        char b[12];
        draw_panel(60, Y_VIEW + 52, 120, 44);
        str_cpy(b, "OCT ", 12);
        b[4] = cs.chord.octave > 0 ? '+' : cs.chord.octave < 0 ? '-' : ' ';
        b[5] = (char)('0' + (cs.chord.octave < 0 ? -cs.chord.octave : cs.chord.octave));
        b[6] = 0;
        fb_text_c(&CF_FONT_UI, 120, Y_VIEW + 61, b, FL_INK, 2);
        break;
    }
    case POP_MSG: {                              /* one line, or two (a '\n'); a button's name as a chip */
        char a[64];
        int i;
        str_cpy(a, cu.msg, sizeof a);
        for (i = 0; a[i] && a[i] != '\n'; i++)
            ;
        if (a[i]) {
            a[i] = 0;
            draw_panel(14, Y_VIEW + 50, 212, 48);
            draw_chip_text_c(Y_VIEW + 59, a, FL_INK);
            draw_chip_text_c(Y_VIEW + 75, a + i + 1, FL_INK);
        } else {
            draw_panel(20, Y_VIEW + 56, 200, 36);
            draw_chip_text_c(Y_VIEW + 67, a, FL_INK);
        }
        break;
    }
    default: break;
    }
}

/* -------------------------------------------------------------- the frame --- */
static int cf_intro_draw(uint32_t dt);
static void cf_ui_draw(void)
{
    char top[24];
    fb_unclip();
    if (cf_intro_draw(cu.frame_dt))
        return;
    if (cu.scr == SCR_HOME) {
        /* the layout's name, and a padlock for the Lock ones */
        const char *an = cs.chord.algo >= CF_ALG_SIDE ? "Side" : "Top";
        int lk = cs.chord.algo == CF_ALG_TOP_LOCK || cs.chord.algo == CF_ALG_SIDE_LOCK, xr = lk ? 195 : 206;
        draw_topbar("", 0);
        fb_text_r(&CF_FONT_UI, xr, Y_TOP + 1, an, FL_INK3, 1);
        if (lk)
            draw_lock(xr + 3, Y_TOP + 3, FL_INK3);
        draw_top_sound(CF_PART_CHORD);
        {
            int x = xr - fb_text_w(&CF_FONT_UI, an, 1) - 6;
            if (cu.arp_on) {                     /* the arpeggiator plays: a chip before the algorithm */
                int w = fb_text_w(&CF_FONT_UI, "ARP", 1) + 6;
                x -= w;
                fb_rbox(x, Y_TOP + 2, w, 11, 2, C_ACC(1));
                fb_text(&CF_FONT_UI, x + 3, Y_TOP + 1, "ARP", FL_BG, 1);
                x -= 4;
            }
            if (cf_sn.sq_on && !sqk.on) {        /* the sequencer plays: its pattern (the song's) and bar */
                char b[12];
                int w;
                str_cpy(b, cf_sn.sq_song ? "SONG " : "SEQ ", sizeof b);
                b[str_len(b) + 1] = 0;
                b[str_len(b)] = (char)('A' + cf_sn.sq_pat % SQ_NPAT);   /* (the letter alone: "A3" read as a note) */
                w = fb_text_w(&CF_FONT_UI, b, 1) + 12;
                x -= w;
                fb_rbox(x, Y_TOP + 2, w, 11, 2, cf_panel(2));
                fb_rect(x + 3, Y_TOP + 5, 4, 4, C_ACC(0));
                fb_text(&CF_FONT_UI, x + 9, Y_TOP + 1, b, FL_INK, 1);
                x -= 4;
            }
            if (sqk.on) {                        /* a take from HOME: its count-in, then its bar of 8 */
                char b[12];
                int w, rec = sqk.rolled;
                if (rec) {
                    str_cpy(b, "REC ", sizeof b);
                    kint(b + 4, (int)(sqk.nbar % SQ_BARS) + 1, "/8");
                } else {
                    str_cpy(b, "REC in ", sizeof b);
                    b[7] = (char)('0' + (cf_sn.sq_cin + SQ_PPQN - 1u) / SQ_PPQN % 10u);
                    b[8] = 0;
                }
                w = fb_text_w(&CF_FONT_UI, b, 1) + 12;
                x -= w;
                fb_rbox(x, Y_TOP + 2, w, 11, 2, rec ? FL_C(FL_R_FIRE, 2) : FL_BG2);
                fb_rect(x + 3, Y_TOP + 5, 4, 4, rec ? FL_INK : FL_C(FL_R_FIRE, 1));
                fb_text(&CF_FONT_UI, x + 9, Y_TOP + 1, b, FL_INK, 1);
            }
        }
        (void)top;
    } else {
        draw_topbar(cu.scr == SCR_SQED ? (squ.trk == SQ_DRUMS ? "DRUMS" : squ.trk == SQ_BASS ? "BASS" :
                                          squ.trk == SQ_LAYER ? "LAYER" : "CHORDS") :
                    SCR_NAME[cu.scr], 0);
        if (cu.scr == SCR_EDIT)                  /* the sound edited, after SOUND */
            fb_text(&CF_FONT_UI, 4 + fb_text_w(&CF_FONT_UI, SCR_NAME[cu.scr], 1) + 7, Y_TOP + 1,
                    cf_snd[cu.edit_part].name, FL_INK3, 1);
    }
    cu_hints = 0;                                /* (the screen names its hints while it draws: draw_hints) */
    fb_clip(0, Y_VIEW, FB_W, Y_VIEW + H_VIEW);
    if (cu.scr == SCR_HOME)
        draw_home();
    else if (cu.scr == SCR_SEL)
        draw_sel();
    else if (cu.scr == SCR_ENV)
        draw_env();
    else if (cu.scr == SCR_EDIT)
        draw_edit();
    else if (cu.scr == SCR_FX)
        draw_fx();
    else if (sq_screen(cu.scr))
        draw_seq();
    else
        draw_generic();
    draw_popup();
    fb_unclip();
    if (cu.scr == SCR_SQED) {                    /* the open track and its sound (PRESETS turns it) */
        const char *tn = squ.trk == SQ_DRUMS ? "DRUMS" : squ.trk == SQ_BASS ? "BASS" : squ.trk == SQ_LAYER ? "LAYER" :
                         "CHORDS";
        const char *sn = squ.trk == SQ_DRUMS ? sq_kit_name(sq_cur()) : cf_snd[squ.trk == SQ_LAYER ? CF_PART_LAYER :
                         squ.trk == SQ_BASS ? CF_PART_BASS : CF_PART_CHORD].name;
        int x = 4 + fb_text_w(&CF_FONT_UI, tn, 1) + 7;
        fb_text(&CF_FONT_UI, x, Y_TOP + 1, sn, FL_INK3, 1);
        draw_top_hints(x + fb_text_w(&CF_FONT_UI, sn, 1));
    } else if (cu.scr != SCR_HOME)
        draw_top_hints(4 + fb_text_w(&CF_FONT_UI, SCR_NAME[cu.scr], 1) +
                       (cu.scr == SCR_EDIT ? 7 + fb_text_w(&CF_FONT_UI, cf_snd[cu.edit_part].name, 1) : 0));
    if (cu.upd_left) {                           /* the update gesture: a countdown over everything */
        char b[2];
        b[0] = (char)('0' + cu.upd_left % 10u);
        b[1] = 0;
        fb_fill(FL_BG);
        fb_text_c(&CF_FONT_UI, 120, 70, b, C_ACC(0), 4);
        fb_text_c(&CF_FONT_UI, 120, 140, cu.upd_stub ? "update mode" : "USB boot", FL_INK, 1);
        fb_text_c(&CF_FONT_UI, 120, 156, "let go to cancel", FL_INK2, 1);
        cf_present();
        return;
    }
    fb_rect(0, Y_VIEW + H_VIEW, FB_W, Y_KEYS - Y_VIEW - H_VIEW, FL_BG);   /* the gap over the keys: the intro's plant
                                                  * (cf_splash.c, 4 px lower) left its stem's foot there, and no
                                                  * screen draws it (the owner saw it over the m6 key, 2026-10-19) */
    draw_keys();
    draw_knobs();
    cf_present();
}
