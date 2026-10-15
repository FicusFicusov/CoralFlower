/* SPDX-License-Identifier: GPL-3.0-only
 * CoralFlower: drawing the screens into the framebuffer (cf_fb.c). Pure drawing: what to show comes from cf_ui.c
 * through the small structs below. Included after cf_state.h, before cf_ui.c.
 *
 *   y   0..15   the top bar: the sound or the screen's name, the battery
 *   y  16..155  the view: the plant (HOME) or the screen's own picture, popups over it
 *   y 158..197  the keys: the upper row (11 keys, 15 px) over the lower row (16 keys, 24 px: the note over the
 *               scale degree), as on the unit
 *   y 203..239  the four knobs: name, value, a bar (a gap above them and between them)
 */

#define Y_TOP 0
#define Y_VIEW 16
#define H_VIEW 140
#define Y_KEYS 158
#define Y_KNOBS 203

typedef struct { char name[12]; char val[14]; int16_t bar; uint8_t dis, sel; } cf_knob_t;   /* bar 0..255, -1 none */
typedef struct { char top[8]; char bot[8]; uint8_t style, btn; } cf_keycap_t;   /* btn: a chord button's cap */
enum { KC_ROOT, KC_BTN, KC_ON, KC_DOWN, KC_OFF, KC_SOUNDING, KC_SCALE };

static void ui_knob_info(int k, cf_knob_t *o);           /* cf_ui.c */
static void ui_keycap(int key, cf_keycap_t *o);           /* cf_ui.c */

/* ---------------------------------------------------------------- top bar --- */
static uint32_t cf_batt(void)                    /* 4 charging (USB), else 0..3 (main.c batt_raw) */
{
    if (!song.batt_raw)
        return 4u;
    return song.batt_raw >= 591 ? 3u : song.batt_raw >= 561 ? 2u : song.batt_raw >= 531 ? 1u : 0u;
}

/* 18 x 10 and a 2 x 4 cap: three 4 x 6 cells with a 1 px gap all round (symmetric on both sides) */
static void draw_battery(int x, int y)
{
    uint32_t b = cf_batt(), i, n;
    uint8_t c = b == 0u ? FL_C(FL_R_ROSE, 1) : b == 4u ? FL_C(FL_R_LIME, 1) : FL_INK2;
    fb_rframe(x, y, 18, 10, c);
    fb_rect(x + 18, y + 3, 2, 4, c);
    n = b == 4u ? (cu.now / 450u) % 4u : b;      /* charging: the cells fill one by one */
    if (b == 0u)
        n = (cu.now / 500u) & 1u;                /* empty: one cell blinks */
    for (i = 0; i < n; i++)
        fb_rect(x + 2 + (int)i * 5, y + 2, 4, 6, c);
}

/* a padlock, 8 x 10, as tall as the battery: the Lock algorithms, after the layout's name in the top bar (the owner,
 * 2026-10-15: "Top" and a lock, not "Top Lock") */
static void draw_lock(int x, int y, uint8_t c)
{
    fb_hline(x + 2, y, 4, c);                     /* the shackle */
    fb_rect(x + 1, y + 1, 1, 3, c);
    fb_rect(x + 6, y + 1, 1, 3, c);
    fb_rect(x, y + 4, 8, 6, c);                   /* the body, a keyhole in it */
    fb_rect(x + 3, y + 6, 2, 2, FL_BG);
}

static void draw_topbar(const char *left, const char *mid)
{
    fb_rect(0, Y_TOP, FB_W, 16, FL_BG);
    fb_text(&CF_FONT_UI, 4, Y_TOP + 1, left, FL_INK, 1);
    if (mid)
        fb_text_r(&CF_FONT_UI, 206, Y_TOP + 1, mid, FL_INK3, 1);
    draw_battery(216, Y_TOP + 3);
    fb_hline(0, Y_TOP + 15, FB_W, FL_BG1);
}

/* ---------------------------------------------------------------- panels --- */
/* the lower half takes the scene's tint (dusk violet by the plant, deep water by the coral): what = 0 the floor under
 * the keys and knobs, 1 a keycap or a knob cell, 2 a raised one (a chord button, a knob just turned, the keys of the
 * sounding root) */
static uint8_t cf_panel(int what)
{
    return (uint8_t)((flo.species == FL_SP_CORAL ? FL_S0 : FL_L0) + what);
}

/* ------------------------------------------------------------------ keys --- */
/* where key k's cap sits: lower keys on a 15 px grid, upper keys over the gaps between them */
static void key_box(int key, int *x, int *y, int *w, int *h)
{
    int k, lower = 0;
    for (k = 0; k < key; k++)
        lower += !cf_key_is_upper(k);
    if (cf_key_is_upper(key)) {
        *x = lower * 15 - 7;
        *y = Y_KEYS;
        *w = 14;
        *h = 15;
    } else {
        *x = lower * 15;
        *y = Y_KEYS + 16;
        *w = 14;
        *h = 24;
    }
}

/* a chord button's name: its letters in the UI face, its added degree as a small digit at their top right (M7 as M
 * with a small 7), so a two-part name fits the 14 px cap */
static void draw_button_name(int x, int y, int w, int h, const char *name, uint8_t ink)
{
    char l[8], dg[4], b[8];
    int i, nl = 0, nd = 0, wl, cx;
    for (i = 0; name[i] && nl < 7; i++) {
        uint8_t ch = (uint8_t)name[i];
        if (ch >= 0x10u && ch <= 0x19u)          /* (the micro face's raised digits) */
            ch = (uint8_t)('0' + ch - 0x10u);
        if (ch >= '0' && ch <= '9') {
            if (nd < 3)
                dg[nd++] = (char)ch;
        } else {
            l[nl++] = (char)ch;
        }
    }
    if (!nl) {                                   /* a degree alone ("7", "9": an added tone): big */
        for (i = 0; i < nd; i++)
            l[nl++] = dg[i];
        nd = 0;
    }
    l[nl] = 0;
    dg[nd] = 0;
    fb_signs(l, b, sizeof b);
    wl = fb_text_w(&CF_FONT_UI, b, 1);
    cx = x + (w - wl - (nd ? fb_text_w(&CF_FONT_MICRO, dg, 1) : 0)) / 2;
    fb_text(&CF_FONT_UI, cx, y + (h - 8) / 2 - 3, b, ink, 1);
    if (nd)
        fb_text(&CF_FONT_MICRO, cx + wl, y + (h - 8) / 2, dg, ink, 1);
}

/* a keycap: a chord button (upper row) shows its name; a root key (lower row) its note over the scale degree, both
 * in the UI face (the UI face's caps are 8 rows, 3 rows under the top of its cell). A name the UI face cannot fit
 * (OFF) is drawn in the micro face. */
static void draw_keycap(int key)
{
    cf_keycap_t c;
    int x, y, w, h;
    uint8_t fill, ink, ink2;
    char b[8];
    key_box(key, &x, &y, &w, &h);
    c.top[0] = c.bot[0] = 0;
    c.style = KC_ROOT;
    c.btn = 0;
    ui_keycap(key, &c);
    switch (c.style) {
    case KC_ON: fill = C_ACC(1); ink = FL_BG; ink2 = FL_BG; break;
    case KC_DOWN: fill = FL_INK; ink = FL_BG; ink2 = FL_BG; break;
    case KC_SOUNDING: fill = cf_panel(2); ink = C_ACC(0); ink2 = C_ACC(1); break;
    case KC_BTN: fill = cf_panel(2); ink = FL_INK; ink2 = FL_INK2; break;
    case KC_OFF: fill = cf_panel(1); ink = FL_INK3; ink2 = FL_INK3; break;
    case KC_SCALE: fill = cf_panel(1); ink = C_SEC(1); ink2 = FL_INK; break;   /* the tonic: its letter coloured */
    default: fill = cf_panel(1); ink = FL_INK2; ink2 = FL_INK3; break;
    }
    fb_rbox(x, y, w, h, 1, fill);
    if (!c.top[0])
        return;
    fb_signs(c.top, b, sizeof b);
    if (fb_text_w(&CF_FONT_UI, b, 1) > w && (c.top[1] < 0x10 || c.top[1] > 0x19) && (c.top[1] < '0' || c.top[1] > '9')) {
        fb_text_c(&CF_FONT_MICRO, x + w / 2, y + (h - 5) / 2, c.top, ink, 1);   /* (OFF) */
    } else if (c.btn || cf_key_is_upper(key)) {  /* (the Side algorithms have chord buttons on the lower row too) */
        draw_button_name(x, y, w, h, c.top, ink);
    } else if (c.bot[0]) {
        char d[8];
        fb_signs(c.bot, d, sizeof d);
        fb_text_c(&CF_FONT_UI, x + w / 2, y, b, ink, 1);
        fb_text_c(&CF_FONT_UI, x + w / 2, y + 10, d, ink2, 1);
    } else {
        fb_text_c(&CF_FONT_UI, x + w / 2, y + (h - 8) / 2 - 3, b, ink, 1);
    }
}

static void draw_keys(void)
{
    int k;
    fb_rect(0, Y_KEYS, FB_W, Y_KNOBS - Y_KEYS, cf_panel(0));
    for (k = 0; k < CF_NKEYS; k++)
        draw_keycap(k);
}

/* ----------------------------------------------------------------- knobs --- */
static void draw_knobs(void)
{
    int k;
    fb_rect(0, Y_KNOBS, FB_W, FB_H - Y_KNOBS, cf_panel(0));
    for (k = 0; k < 4; k++) {
        cf_knob_t o;
        int x = k * 60, lit;
        o.name[0] = o.val[0] = 0;
        o.bar = -1;
        o.dis = 0;
        o.sel = 0;
        ui_knob_info(k, &o);
        lit = (int32_t)(cu.now - cu.knob_ms[k]) < 900 && cu.knob_ms[k];
        fb_rbox(x + 2, Y_KNOBS, 56, 37, 2, cf_panel(lit ? 2 : 1));
        if (!o.name[0])
            continue;
        fb_text_c(&CF_FONT_UI, x + 30, Y_KNOBS + 2, o.name, o.dis ? FL_INK3 : (lit ? C_ACC(0) : FL_INK2), 1);
        fb_text_c(&CF_FONT_UI, x + 30, Y_KNOBS + 15, o.val, o.dis ? FL_INK3 : FL_INK, 1);
        if (o.bar >= 0) {
            int bw = 42 * o.bar / 255;
            fb_rect(x + 9, Y_KNOBS + 31, 42, 2, FL_BG3);
            fb_rect(x + 9, Y_KNOBS + 31, bw, 2, o.dis ? FL_INK3 : C_ACC(1));
        }
    }
}

/* ------------------------------------------------------------------ view --- */
static void draw_plant(int y0)
{
    fb_blit2x(0, y0, fl_px, FLW, FLH);
}

/* the chord's name, big, over the plant (sharp and flat as signs) */
/* HOME's chord name, its capitals' tops on the line `cap` (the key's in the corner): the root big, what follows it
 * (quality, extensions, bass) small at the top, as a chord symbol is written, then the scale degree; single notes
 * (Off) all big. (The UI face's capitals begin 3 rows under its cell, 6 at 2x.) */
static void draw_chord_name(int x, int cap, const char *name, const char *small, int single)
{
    char b[20], r[4];
    int n = single ? (int)str_len(name) : 1 + (name[0] && (name[1] == '#' || name[1] == 'b'));
    str_cpy(r, name, (uint32_t)(n < 3 ? n : 3) + 1u);
    fb_signs(single ? name : r, b, sizeof b);
    x += fb_text_halo(&CF_FONT_UI, x, cap - 6, b, FL_INK, FL_BG, 2);
    if (!single && name[n]) {
        fb_signs(name + n, b, sizeof b);
        x += 1 + fb_text_halo(&CF_FONT_UI, x + 1, cap - 3, b, FL_INK, FL_BG, 1);
    }
    if (small && small[0]) {
        fb_signs(small, b, sizeof b);
        fb_text_halo(&CF_FONT_UI, x + 6, cap - 3, b, FL_INK2, FL_BG, 1);
    }
}

/* the hints: the controls a screen gives another role, as JIANT shows them, each control's name on a light chip and
 * what it does after it. One place on every screen: the top bar, right of the screen's name, before the battery (the
 * owner's choice). A screen names them while it draws (draw_hints: h is name, action, name, action, .. n pairs), the
 * frame puts them up after (draw_top_hints); pairs that would not fit are left out from the last. */
static const char *const *cu_hints;
static int cu_nhints;
static void draw_hints(const char *const *h, int n)
{
    cu_hints = h;
    cu_nhints = n;
}
static int hint_w(const char *const *h, int n)
{
    int i, w = 0;
    for (i = 0; i < n; i++)
        w += (i ? 6 : 0) + fb_text_w(&CF_FONT_UI, h[2 * i], 1) + 6 + 3 + fb_text_w(&CF_FONT_UI, h[2 * i + 1], 1);
    return w;
}
/* a control's name as a chip, the one look a button or an encoder has wherever the screen names it (the top bar's
 * hints, the pictures' captions); y: the text's line. Returns its width */
static int draw_chip(int x, int y, const char *name)
{
    int w = fb_text_w(&CF_FONT_UI, name, 1) + 6;
    fb_rbox(x, y + 1, w, 11, 2, FL_INK2);
    fb_text(&CF_FONT_UI, x + 3, y, name, FL_BG, 1);
    return w;
}
/* a caption whose [NAMES] are controls, drawn as chips ("hold [SAVE] to keep it": a space next to a chip becomes a
 * 4-pixel gap); returns the width */
static int draw_chip_text(int x, int y, const char *s, uint8_t c)
{
    char b[32];
    int x0 = x;
    while (*s) {
        uint32_t n = 0;
        if (*s == '[') {
            for (s++; *s && *s != ']' && n < sizeof b - 1u; s++)
                b[n++] = *s;
            b[n] = 0;
            if (*s == ']')
                s++;
            x += draw_chip(x, y, b);
            if (*s == ' ' || *s == ':' || *s == ',')
                x += *s == ' ' ? (s++, 4) : 1;
            continue;
        }
        while (*s && *s != '[' && n < sizeof b - 1u)
            b[n++] = *s++;
        if (*s == '[' && n && b[n - 1u] == ' ')
            n--;                                 /* (the space before a chip: a gap instead) */
        b[n] = 0;
        x += fb_text_halo(&CF_FONT_UI, x, y, b, c, FL_BG, 1);
        if (*s == '[')
            x += 4;
    }
    return x - x0;
}
static void draw_top_hints(int left)            /* left: where the screen's name ends */
{
    int n = cu_nhints, x, i;
    if (!cu_hints)
        return;
    while (n > 0 && hint_w(cu_hints, n) > 212 - left - 8)
        n--;
    x = 212 - hint_w(cu_hints, n);
    for (i = 0; i < n; i++) {
        x += draw_chip(x, Y_TOP + 1, cu_hints[2 * i]) + 3;
        x += fb_text(&CF_FONT_UI, x, Y_TOP + 1, cu_hints[2 * i + 1], FL_INK2, 1) + 6;
    }
}

/* a box for popups: a dark panel with an accent edge */
static void draw_panel(int x, int y, int w, int h)
{
    fb_rbox(x, y, w, h, 2, FL_BG1);
    fb_rframe(x, y, w, h, C_ACC(2));
}
