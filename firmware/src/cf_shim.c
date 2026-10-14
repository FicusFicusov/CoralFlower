/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca); CoralFlower contributors */
/* CoralFlower: the names main.c (kept as Felucca / ChoralRoot left it) and the kept files call of the dropped UI,
 * with CoralFlower's meaning (after ChoralRoot's cr_shim.c). Device unit only (coralflower.c); the emulator calls
 * cf_ui_* itself (tools/emu/emu_fw.c).
 *
 *   felucca_init (main.c's power-on) -> track_defaults / set_engine_of / apply_preset_to: the parts get a sound
 *     (Felucca's TRK_DEF; CoralFlower's own replace them at the first frame)
 *   the main loop: ui_input -> cf_ui_input (the first call: cf_ui_init), ui_leds -> cf_ui_frame, ui_draw -> cf_ui_draw
 *   persist_boot: the flash part (the user sounds; the settings record: cf_settings.c) */

static uint32_t undo_depth;            /* (main.c: no undo copy of the power-on loads) */
static uint32_t pat_sig[NTRK];
static uint8_t pat_last[NTRK];

static void track_defaults(track_t *t)
{
    uint32_t i;
    for (i = 0; i < P_E0; i++)
        t->p[i] = TP[i].def;
}
static void track_defaults_steps(track_t *t) { (void)t; }
static void set_engine_of(track_t *t, uint32_t ei)
{
    const engine_t *e = ENGINES[ei % NENGINES];
    uint32_t i;
    t->eng_req = (uint8_t)(eng_ok(ei % NENGINES) ? ei % NENGINES : ENGI_FM6);
    for (i = 0; i < 8u; i++)
        t->p[P_E0 + i] = e->edit[i].def;
}
static void apply_preset_to(track_t *t, uint32_t pi)
{
    uint32_t part = (uint32_t)(t - trk);
    if (part < 2u)
        cf_pool_load(part, t->eng_req, pool_pos_factory(t->eng_req, pi));
}
static uint32_t steps_sig(const track_t *t) { (void)t; return 0; }

static void persist_boot(void)
{
#if FELUCCA_FLASH && !defined(CF_EMU)
    uint32_t f = irq_save();
    CR_STAGE(BS_FLASH);
    flash_ok = FL_FAR(fl_jedec_ram)() == 0x856014u;       /* the expected 1 MiB part, else stay RAM-only */
    irq_restore(f);
    if (!flash_ok)
        return;
    fl_plain_window_init();
    cr_bank_boot();                                        /* the 32 user sounds (upreset.c) and the FM6 bank */
#endif
}
#ifndef CR_BACKUP_SERVICE
static void ed_service(void) {}
#endif

/* OCT- + OCT+ held at power-on: the hardware calibration (Felucca's, which asks for each printed button and knob
 * in turn). CoralFlower keeps the measured default table (panel.c) until its own calibration screen exists */
static void panel_setup(void) { cf_message("Calibration: default panel"); }

/* ---- the update gesture: OCT- + OCT+ + HOME held 5 s (ChoralRoot's cr_backup.c cr_update_poll, unchanged in what it
 * does). From 2 s a countdown over the whole screen; any of the three let go cancels. At 5 s the audio stops and
 *   a boot stub of version 2 or later ran before this boot: its RESCUE request (USB detached, the boot guard told):
 *     the stub hands over to the staged update loader (update mode, what the web installers talk to) or, with none
 *     staged, to the ROM's USB boot;
 *   no such stub: the ROM's USB boot directly.
 * The buttons are read as main.c reads them (fm1_in.buttons through the panel table) */
#define CRB_STUB_REQ_RESCUE 0x52514552u
#ifndef CRB_STUB_VER
#ifdef FM1_HAVE_STUB
#define CRB_STUB_VER() (fm1_stub_word(1) >> 16 == 0x5342u ? (fm1_stub_word(1) >> 8) & 0xFFu : 0u)
#define CRB_STUB_GO(w) do { bootguard.pending = 0; usb_detach(); fm1_delay_ms(30); fm1_stub_request(w); } while (0)
#else
#define CRB_STUB_VER() 0u
#define CRB_STUB_GO(w) ((void)(w))
#endif
#endif
#ifndef CRB_UPDATE_SCREEN
#ifdef FM1_HAVE_STUB
#define CRB_UPDATE_SCREEN(t) do { fm1_audio_stop(); lcd_fill(0, 0, 240, 240, T_BG); \
                                  draw_text_box(0, 110, 240, &AF_M, (t), T_THEME, 1); } while (0)
#define CRB_UBOOT_GO() do { usb_detach(); fm1_delay_ms(30); bootguard_intentional(&bootguard); fm1_enter_uboot(); } while (0)
#else
#define CRB_UPDATE_SCREEN(t) ((void)(t))
#define CRB_UBOOT_GO() ((void)0)
#endif
#endif
#define CRB_UPD_SHOW_MS 2000u
#define CRB_UPD_GO_MS 5000u
static struct { uint32_t t0; uint8_t left, gone; } crb_upd;
static void cr_update_poll(void)
{
    uint32_t all = (1u << panel.btn[B_OCTDN]) | (1u << panel.btn[B_OCTUP]) | (1u << panel.btn[B_HOME]);
    uint32_t held = fm1_ms - crb_upd.t0, stub = CRB_STUB_VER() >= 2u;
    if ((fm1_in.buttons & all) != all) {
        if (crb_upd.left) {
            crb_upd.left = 0;
            cu.upd_left = 0;
            ui.uboot = 0;
            ui.force = 1;
            cf_message(stub ? "update cancelled" : "USB boot cancelled");
        }
        crb_upd.gone = 0;
        crb_upd.t0 = fm1_ms;
        return;
    }
    if (crb_upd.gone || held <= CRB_UPD_SHOW_MS)
        return;
    if (held < CRB_UPD_GO_MS) {                      /* 3, 2, 1 */
        uint32_t left = (CRB_UPD_GO_MS - held + 999u) / 1000u;
        if (left != crb_upd.left) {
            crb_upd.left = (uint8_t)left;
            ui.uboot = (uint8_t)left;
            ui.force = 1;
        }
        cu.upd_left = (uint8_t)left;
        cu.upd_stub = (uint8_t)stub;
        return;
    }
    crb_upd.gone = 1;
    crb_upd.left = 0;
    cu.upd_left = 0;
    ui.uboot = 0;
    CRB_UPDATE_SCREEN(stub ? "UPDATE MODE" : "USB BOOT");
    if (stub)
        CRB_STUB_GO(CRB_STUB_REQ_RESCUE);            /* (never returns on the unit) */
    else
        CRB_UBOOT_GO();
}

/* the power-on splash: the seed (cf_splash.c) */
static void cf_splash(void);
static void cr_splash(void) { cf_splash(); }
#define CR_A_STRIPES 1                     /* (main.c: splash() calls cr_splash) */

static void ui_input(void)
{
    if (!cu.booted)
        cf_ui_init();
    cf_ui_input();
}
static void ui_leds(void)
{
    if (cu.booted)
        cf_ui_frame();
}
static void ui_draw(void)
{
    if (!cu.booted)
        return;
    if (ui.force) {
        ui.force = 0;
        fb_full = 1;
    }
    cf_ui_draw();
}
