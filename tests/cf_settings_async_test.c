/* SPDX-License-Identifier: GPL-3.0-only */
/* A button/encoder arriving after NOR writing starts must reach their actual
 * UI handlers BEFORE the autosave commits. Model time is not device latency.
 * CF_ASYNC_BLOCKING_MUTANT drains the queued transaction synchronously, the
 * original defect, and must fail the before-commit assertions. */
static void controls_during_flash(void);
static void model_lcd_transfer(unsigned w, unsigned h);
#define EMU_LCD_TRANSFER(w,h) model_lcd_transfer(w,h)
#ifdef CF_DRAW_PUMP_MUTANT
#define CF_PUMP() cf_ui_pump()
#endif
#define CF_FLASH_INPUT_HOOK() controls_during_flash()
#define CF_FLASH_AUDIO_HELPERS_ONLY
#include "cf_flash_audio_test.c"
static unsigned watch, injected, served, begin_programs, begin_erases;
static uint32_t injected_ms, served_ms;
static void controls_during_flash(void)
{
    if (!watch || !st_async_busy()) return;
    if (!injected && (fnor.programs > begin_programs || fnor.erases > begin_erases)) {
        web_buttons_tap(1u << EMU_B_ENV); web_enc(EMU_E_ALGO, 1);
        injected = 1; injected_ms = fm1_ms;
    }
    if (injected && !served && cu.scr == SCR_ENV && cs.chord.algo == CF_ALG_TOP) {
        served = 1; served_ms = fm1_ms;
    }
}
static unsigned draw_rows, drawing, drawn_rows, draw_slices;
static void model_lcd_transfer(unsigned w, unsigned h)
{
    if (!sim_on || !drawing) return;
    drawn_rows++;
    /* Actual cf_present/lcd_blit path, RAM display and 12MHz SPI1 transfer
     * duration in the model; no physical LCD timing is claimed. */
    uint64_t until=fnor.ticks+(uint64_t)w*h*16u*2u;
    while(fnor.ticks<until)advance_us(100);
}
static void drive_job(void)
{
    uint32_t start = fm1_ms;
#ifdef CF_ASYNC_BLOCKING_MUTANT
    st_async_drain();
#endif
    while (st_async_busy() && fm1_ms - start < 10000u) {
        advance_us(100); cu.now = fm1_ms; cf_ui_input();
        if (draw_rows) {
            unsigned slices=st_flash.slices;
            cf_ui_frame();fb_full=1;drawing=1;cf_ui_draw();drawing=0;
            draw_slices+=st_flash.slices-slices;
        }
    }
    CHECK(!st_async_busy(), "background save completes under continued main-loop service");
}
static void autosave_controls(int full)
{
    st_async_drain(); sim_on = 0;
    cs.chord.algo = CF_ALG_TOP_LOCK; cs.bpm = full ? 120 : 121;
    CHECK(cf_settings_write(ST_WHOLE) == 1, "baseline settings persisted");
    if (full) {
        st_hdr_t h;
        for (unsigned i = 0; i < 2 * st_ln(CF_SET_OBJ); i++)
            CHECK(cf_settings_write(ST_WHOLE) == 1, "fill valid settings logs");
        st_current(CF_SET_OBJ, &h);
        while (st_cur_e + 1u < st_ln(CF_SET_OBJ)) {
            CHECK(cf_settings_write(ST_WHOLE) == 1, "fill last whole entries"); st_current(CF_SET_OBJ, &h);
        }
    }
    sim_on = 1; cu.scr = SCR_HOME; cs.bpm++;
    cfs_seen = cfs_fnv(&cs, sizeof cs); cfs_changed_ms = fm1_ms - CF_SET_QUICK_MS;
    uint32_t snapshot = cfs_seen, start = fm1_ms;
    begin_programs = fnor.programs; begin_erases = fnor.erases;
    watch = 1; injected = served = 0; cu.now = fm1_ms;
    settings_poll();
    CHECK(st_async_busy() && st_async.obj == CF_SET_OBJ, "normal settings poll queues the eligible record");
    drive_job(); watch = 0;
    if (draw_rows) {
        CHECK(drawn_rows && draw_slices, "real cf_ui_draw/cf_present advances NOR between LCD rows");
        printf("DRAW rows=%u flash_slices=%u physical_timing=false\n",drawn_rows,draw_slices);
    }
    CHECK(injected, "real button and encoder injected after NOR operation starts");
    CHECK(served, "ENV button and ALGORITHM encoder reach UI before autosave commit");
    CHECK(cfs_saved == snapshot, "only the queued snapshot is marked saved");
    printf("CONTROLS full=%d injected=%u served_before_commit=%u response_ms=%u save_ms=%u physical_timing=false\n",
           full, injected, served, served ? served_ms - injected_ms : 0, fm1_ms - start);
    /* In the mutant process the controls remain queued; service them now so
     * later assertions still check their intended state independently. */
    advance_us(1000); cf_ui_input();
    CHECK(cs.chord.algo == CF_ALG_TOP && cfs_saved != cfs_fnv(&cs, sizeof cs), "edit during save remains dirty");
    settings_save();
    cf_rec_t disk;
    CHECK(st_load(CF_SET_OBJ, &disk, sizeof disk) == sizeof disk && disk.s.chord.algo == CF_ALG_TOP,
          "following foreground save retains the new edit");
}
static void sound_and_foreground(void)
{
    st_async_drain(); watch = 0;
    trk[0].p[P_ATK] = 31; cf_edited(0); cfk_build(0, &cfk_rec);
    uint32_t hash = cfk_rec.check;
    CHECK(cfk_background_write(0, &cfk_rec) == 0, "sound tweak queued");
    for (unsigned i=0; !fnor.suspended && st_async_busy() && i<1000; i++) {
        advance_us(100); st_async_poll();
    }
    CHECK(fnor.suspended, "ordinary-read fixture has a suspended NOR operation");
    uint8_t byte;
    CHECK(st_read(0, &byte, 1) == 0 && !fnor.suspended && st_async_busy(),
          "ordinary storage read finishes only the current command safely");
    cfk_rec_t snapshot;
    CHECK(st_load(cfk_obj(0), &snapshot, sizeof snapshot) == sizeof snapshot && snapshot.check == hash,
          "sound loading sees immutable pending tweak");
    trk[0].p[P_ATK] = 62;
    cfk_take(0, 0);
    CHECK(trk[0].p[P_ATK] == 31, "actual sound reload applies pending snapshot");
    cfk_drop(0, 0); drive_job();
    CHECK(cfk_st[0].drop && !cfk_st[0].edited, "completion cannot resurrect a dropped tweak");
    cfk_flush();
    CHECK(st_load(cfk_obj(0), &snapshot, sizeof snapshot) == sizeof snapshot && !snapshot.edited,
          "backup flush persists a drop received during save");

    cs.bpm++; cf_settings_record();
    CHECK(cfs_background_put(CF_SET_OBJ, &cf_rec, sizeof cf_rec) == 0, "settings queued before foreground write");
    /* A caller may already own st_buf when st_put drains the background job. */
    uint8_t expected[96]; memset(expected, 0x5a, sizeof expected); memcpy(st_buf, expected, sizeof expected);
    CHECK(st_put(OBJ_CFEDIT2, st_buf, sizeof expected, ST_ROLL) == 0, "foreground write drains queued save");
    uint8_t actual[96];
    CHECK(st_load(OBJ_CFEDIT2, actual, sizeof actual) == sizeof actual && !memcmp(actual, expected, sizeof actual),
          "draining autosave preserves foreground scratch source");

    cs.bpm++; cf_settings_record();
    CHECK(cfs_background_put(CF_SET_OBJ, &cf_rec, sizeof cf_rec) == 0, "queued save before LIST");
    uint32_t saved = cf_rec.check;
    crb_list();
    CHECK(!st_async_busy() && cfs_saved == saved && crb.listed, "backup LIST drains and includes pending save");
}
static void failed_drop_retry(void)
{
    st_async_drain(); sim_on=0;
    for(unsigned part=0;part<3;part++)cfk_on[part]=0;
    trk[0].p[P_ATK]=42;cf_edited(0);cfk_build(0,&cfk_rec);
    CHECK(cfk_write(0,&cfk_rec,0u)==0,"valid tweak before failed deletion");
    cfk_drop(0,0);sim_on=1;cfk_poll(fm1_ms,0);
    CHECK(st_async_busy()&&st_async.obj==cfk_obj(0),"real poll queues deletion");
    for(unsigned i=0;st_async_busy()&&st_async.phase!=STA_VERIFY&&i<100000;i++) {
        advance_us(100);st_async_poll();
    }
    CHECK(st_async.phase==STA_VERIFY,"deletion reached readback");
    emu_flash[st_async.at]^=1;                    /* corrupt exactly the just-written record */
    st_async_drain();
    CHECK(cfk_st[0].drop&&cfk_retry_ms&&!st_flash.fault,"failed readback retains deletion for retry");
    flash_run_ms(1100,0);cfk_poll(fm1_ms,0);st_async_drain();
    cfk_rec_t disk;
    CHECK(st_load(cfk_obj(0),&disk,sizeof disk)==sizeof disk&&!disk.edited&&!cfk_st[0].drop,
          "retry commits deletion instead of resurrecting old tweak");
}
static void verify_restart(void)
{
    CHECK(!st_async_busy()&&!fnor.suspended&&!st_io_active,"update handoff leaves NOR ready and save complete");
    printf("RESTART busy=%d suspended=%u io_active=%d checks=%u failures=%u\n",
           st_async_busy(),fnor.suspended,st_io_active,checks,failed);
    fflush(stdout);if(failed)_Exit(1);
}
int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    unsigned chip = atoi(argv[1]); rate = 44100;
    web_boot(); web_step(5000); web_enc(EMU_E_PRESETS, -4); web_step(1500);
    web_keys(1u << 7); web_step(500); st_async_drain();
    wave = fopen("/dev/null", "wb"); if (!wave) return 2;
    fnor_reset(emu_flash, chip, 1); fnor.byte_us = 2;
    memset(&st_flash, 0, sizeof st_flash); st_flash.jedec = chip ? 0xB36014u : 0x856014u;
    hw_flash_ok = st_runtime = 1; sim_base = fm1_ms * 1000;
    next_sof = 24000; next_timer = 2400; next_audio = 128u * 544u; next_midi = 500000;
    cf_usb_mock_attach(ep0buf); usb.up = usb.config = 1; usb_cdc_on = 0;
    ua_reset(); ua_cfg_build(); ua_rate_set(rate); ua_set_interface(UA_IF_CAP, 1); ua_service(); sim_on = 1;
    if(!strncmp(argv[1],"restart",7)) {
        cs.bpm++;cf_settings_record();
        CHECK(cfs_background_put(CF_SET_OBJ,&cf_rec,sizeof cf_rec)==0,"save queued before update gesture");
        for(unsigned i=0;!fnor.suspended&&st_async_busy()&&i<1000;i++){advance_us(100);st_async_poll();}
        CHECK(fnor.suspended,"update fixture has a suspended NOR operation");
        setenv("EMU_STUB",!strcmp(argv[1],"restart-rom")?"0":"2",1);atexit(verify_restart);
        fm1_in.buttons=(1u<<panel.btn[B_OCTDN])|(1u<<panel.btn[B_OCTUP])|(1u<<panel.btn[B_HOME]);
        crb_upd.t0=fm1_ms-6000u;crb_upd.gone=0;cr_update_poll();
        CHECK(0,"update gesture reaches the stub");return 1;
    }
    autosave_controls(0); draw_rows=30; autosave_controls(1); draw_rows=0;
    sound_and_foreground();failed_drop_retry();
    CHECK(!st_flash.fault && !fnor.bad_read && !fnor.bad_progress, "NOR remains readable with valid suspend/resume progress");
    CHECK(!missing && !ua.cap_underruns && !ua.cap_overruns && !analog_late, "continuous USB/DAC service during UI autosave");
    printf("SETTINGS ASYNC chip=%u checks=%u failures=%u\n", chip, checks, failed);
    fclose(wave); return failed ? 1 : 0;
}
