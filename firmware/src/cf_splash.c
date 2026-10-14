/* SPDX-License-Identifier: GPL-3.0-only
 * CoralFlower: the power-on splash. A seed sprouts and opens into a flower (a coral with the Side algorithms), the
 * name grows in under it. Played by main.c while it boots (IRQs still off: a busy-wait clock, the panel by polled DMA). SAFE MODE
 * shows its notice instead. Included after cf_screens.c. */

#define CF_SPLASH_MS 1400u

static void cf_splash_frame(uint32_t t)
{
    static const char *const NAME = "CoralFlower";
    int i, n = (int)str_len(NAME), shown, x;
    flora_render(&flo, &fl_cv);
    fb_fill(FL_BG);
    fb_blit2x(0, 20, fl_px, FLW, FLH);
    /* the name: letter by letter from 500 ms, each one brightening through the ramp (shown: how many letters are in) */
    shown = t < 500u ? 0 : 1 + (int)((t - 500u) / 50u);
    x = 120 - fb_text_w(&CF_FONT_UI, NAME, 2) / 2;
    for (i = 0; i < n; i++) {
        char ch[2];
        int age = shown - 1 - i;                  /* (-1: not in yet) */
        ch[0] = NAME[i];
        ch[1] = 0;
        if (age >= 0)
            fb_text(&CF_FONT_UI, x, 180, ch, age > 3 ? FL_INK : age > 1 ? C_ACC(0) : C_ACC(2), 2);
        x += fb_text_w(&CF_FONT_UI, ch, 2);
    }
    if (t > 900u)
        fb_text_c(&CF_FONT_UI, 120, 212, FELUCCA_VERSION, FL_INK3, 1);
}

/* main.c's splash (IRQs off): the first picture only, the scene before the seed sprouts; the UI's first frames grow it
 * (cf_intro_draw) while the instrument already plays */
static void cf_splash(void)
{
    cs_defaults(&cs);
    cf_settings_load();                          /* (what was set before the power-off, unless SAFE MODE) */
    cf_palette_build();
    fb_full = 1;
    if (cr_safe) {
        fb_fill(FL_BG);
        fb_text_c(&CF_FONT_UI, 120, 90, "SAFE MODE", C_ACC(0), 2);
        fb_text_c(&CF_FONT_UI, 120, 130, "settings and sounds", FL_INK2, 1);
        fb_text_c(&CF_FONT_UI, 120, 144, "are not loaded", FL_INK2, 1);
        cf_present();
        return;
    }
    flora_init(&flo, 0x5EED1u, FLW / 2, FLH + 1);
    flo.bloom = FL_R_ROSE + 1u;                  /* the intro's flower is pink, whatever the key */
    flora_set_key(&flo, cs.chord.tonic);
    flo.backdrop = cs.backdrop;
    flora_set_species(&flo, cs.chord.algo >= CF_ALG_SIDE ? FL_SP_CORAL : FL_SP_PLANT);
    cf_splash_frame(0);                          /* (nothing has grown yet: the scene alone) */
    cf_present();
}

/* the intro: the UI's first CF_SPLASH_MS, the seed grows into a flower and the name comes in. Any key, button or
 * knob ends it at once (the plant stays: it is HOME's) */
static uint8_t cf_intro_on = 1;
static uint32_t cf_intro_t;
static int cf_intro_draw(uint32_t dt)
{
    static const fl_chord_t SEED = {4, {60, 64, 67, 74}, 0, FL_Q_MAJOR, 48};
    if (!cf_intro_on)
        return 0;
    if (!cf_intro_t) {
        flora_set_chord(&flo, &SEED);
        if (cs.boot_sound && !cr_safe)
            cf_boot_go = 1;                      /* (the bells strum a wide chord as the flower opens; cf_fx.c) */
    }
    /* (a step at most 40 ms: the first frame comes long after the boot began, and a jump there showed the name's
     * first letter at once, alone over the bare seed) */
    cf_intro_t += dt ? (dt > 40u ? 40u : dt) : 1u;
    if (cf_intro_t >= CF_SPLASH_MS || cu.idle_ms < cf_intro_t / 2u) {
        cf_intro_on = 0;
        flo.bloom = 0;                           /* from the first chord on, the colours follow the root */
        fb_full = 1;
        return 0;
    }
    flora_set_level(&flo, (uint16_t)(cf_intro_t < 700u ? cf_intro_t * 40u : 28000u));
    cf_splash_frame(cf_intro_t);
    cf_present();
    return 1;
}
