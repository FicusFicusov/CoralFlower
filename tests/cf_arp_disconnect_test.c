/* SPDX-License-Identifier: GPL-3.0-only */
/* Owner regression: unplug during external playback, with responsive UI.
 * Held and newly pressed live ARP must sound without starting stored transport.
 * Reuse the real renderer/panel/MIDI fixture; loss of F8 models the clock effect
 * of unplugging, not USB controller timing or a physical device test.
 * Usage: <preset 0|25|30> <pattern|rhythm> <output.wav>. */
#define CF_ARP_PAUSE_FIXTURE
#include "cf_arp_pause_test.c"

static const uint32_t chord_keys = (1u<<7)|(1u<<11)|(1u<<14);
static unsigned cases;
static uint32_t pattern_pos(void) { return sqp.nbar*sqp_bar_ticks()+sqp.t; }
static uint32_t rhythm_pos(void) { return dmp.nbar*sqp_bar_ticks()+dmp.t; }

static void autonomous(unsigned ms,const char *label)
{
    unsigned index=cfa_k.idx, notes=ons, f=wav_frames;
    uint32_t pattern=pattern_pos(), rhythm=rhythm_pos(), grid=cf_grid.g24;
    double e=energy;
    pump(ms);
    double rms=sqrt((energy-e)/(2.0*(wav_frames-f)));
    printf("DISCONNECT %s: steps=%u notes=%u rms=%.3f pattern=%u/%u rhythm=%u/%u running=%u wait=%u active=%u lost=%u\n",
           label,cfa_k.idx-index,ons-notes,rms,pattern,pattern_pos(),rhythm,rhythm_pos(),
           cfm.running,cfx_in.wait,cfx_in.active,cfx_in.lost);
    CHECK(cfa_k.idx>=index+3,"live ARP advances after running external clock disappears");
    CHECK(ons>=notes+3,"multiple live notes reach actual MIDI output after clock loss");
    CHECK(rms>10,"live ARP reaches rendered PCM, not only an internal step counter");
    CHECK(cfm.running&&!cfx_in.local,"stored external transport is retained, not silently converted to local PLAY");
    CHECK(pattern_pos()==pattern&&rhythm_pos()==rhythm&&cf_grid.g24==grid,
          "stored pattern, rhythm and shared grid stay frozen while live ARP sounds");
    CHECK(cs.clock_in==1&&cfp_cfg.clock_in,"saved CLOCK In remains selected");
    cases++;
}

static void stop_live(void)
{
    web_keys(0);
    if(cfc.hold) {
        unsigned key=27;
        for(unsigned k=0;k<27;k++) {int b;if(cf_key_role(cfc.cfg.algo,k,&b)==CF_ROLE_BUTTON&&cf_is_off(&cfc.cfg,b))key=k;}
        CHECK(key<27,"HOLD control exists in the actual key map");
        if(key>=27)return; /* reported failure; no successful silent skip */
        web_keys(1u<<key);pump(40);web_keys(0);
    }
    cs.arp.latch=0;cf_send_config();pump(650);
    CHECK(!cfa_k.n&&!cfa_k.cur,"releasing keys/HOLD/LATCH releases live ARP");
}

static void start_and_disconnect(unsigned mode)
{
    stop_live();
    rt(0xfc);pump(4);rt(0xfa);clocks(90);pump(850);
    CHECK(cfm.running&&cfx_in.active&&!cfx_in.wait&&(sqp.on||dmp.on),
          "real external Start and F8 run the selected stored transport before unplug");
    if(mode==2) {cs.arp.latch=1;cf_send_config();pump(30);}
    web_keys(chord_keys);pump(500);
    CHECK(cfa_k.n&&cfa_k.idx>1,"actual C-major keys play live ARP before unplug");
    if(mode==1) {
        unsigned key=27;
        for(unsigned k=0;k<27;k++) {int b;if(cf_key_role(cfc.cfg.algo,k,&b)==CF_ROLE_BUTTON&&cf_is_off(&cfc.cfg,b))key=k;}
        CHECK(key<27,"HOLD key found through current mapping");
        if(key<27){web_keys(chord_keys|(1u<<key));pump(40);web_keys(0);pump(40);}
        CHECK(cfc.hold&&cfa_k.n,"physical HOLD retains the released chord before unplug");
    } else if(mode==2) {
        web_keys(0);pump(40);
        CHECK(cfa_k.latched&&cfa_k.n,"ARP LATCH retains the released chord before unplug");
    }
    /* No MIDI Stop: this is the owner's running DAW -> missing F8 transition. */
    clocks(0);pump(600);
    CHECK(!cfx_in.active&&cfx_in.lost&&cfm.running,"source timed out while external transport stayed armed");
}

static void return_to_source(unsigned status)
{
    unsigned before=cfa_k.idx;
    if(status) {
        rt(status);pump(25);
        CHECK(cfa_k.idx==before,"explicit Start/Continue waits for its first F8 after autonomous live play");
    }
    unsigned notes=ons;
    rt(0xf8);pump(3);
    CHECK(cfa_k.idx==before+1,"first returned F8 joins live ARP once, without a double note");
    CHECK(ons>notes,"resumed live note reaches MIDI output");
    unsigned first=cfa_k.idx, grid=cf_grid.g24, start=web_ms();
    for(unsigned pulse=1;pulse<=24;pulse++) {
        unsigned at=start+(pulse*2000+36)/72;
        if(web_ms()<at)pump(at-web_ms());
        rt(0xf8);pump(3);
        CHECK(cfa_k.idx==first+cf_grid.g24/6-grid/6,"returned ARP follows each external six-pulse boundary");
    }
    rt(0xfc);pump(4);clocks(0);
    CHECK(!sqp.on&&!dmp.on&&!cfm.running,"external Stop still stops stored transport");
}

int main(int argc,char **argv)
{
    if(argc!=4)return 2;
    unsigned preset=(unsigned)atoi(argv[1]);
    int rhythm=!strcmp(argv[2],"rhythm");
    if((preset!=0&&preset!=25&&preset!=30)||(!rhythm&&strcmp(argv[2],"pattern")))return 2;
    web_boot();pump(2000);
    web_enc(EMU_E_PRESETS,(int)preset-(int)cf_snd_pos(0));pump(250);
    CHECK(cf_snd_pos(0)==preset&&cf_snd[0].engine==(preset?ENGI_VA:ENGI_FM6),"actual requested FM/VA preset selected");
    tap(EMU_B_SEQ);tap(EMU_B_SEQ);tap(EMU_B_EDIT);
    web_keys(1u<<7);pump(40);web_keys(0);pump(100);
    CHECK(sq_st.pat[0].nev==1,"panel creates an audible stored pattern before testing frozen transport");
    tap(EMU_B_HOME);
    if(rhythm){tap(EMU_B_SEQ);web_enc(EMU_E_SELECT,1);pump(40);tap(EMU_B_SEQ);tap(EMU_B_HOME);}
    CHECK((cft_target&3u)==(rhythm?2u:1u),"panel selects requested pattern/rhythm PLAY target");
    tap(EMU_B_GLO);web_enc(EMU_E_SELECT,1);pump(40);web_enc(EMU_E_K2,1);pump(40);
    tap(EMU_B_HOME);tap(EMU_B_ARP);web_enc(EMU_E_K1,1);pump(40);tap(EMU_B_HOME);
    cs.bpm=120;cs.arp.rate=6;cs.arp.gate=50;cs.arp.swing=50;cs.bass_on=0;cs.midi_out=1;
    cf_send_config();cf_send_metro();pump(40);
    CHECK(cfa_k.on&&cs.clock_in==1&&cfp_cfg.clock_in,"panel enabled ARP and CLOCK In");
    wav=fopen(argv[3],"wb");if(!wav)return 3;header(0);

    start_and_disconnect(0);autonomous(1100,"held keys after running disconnect");
    web_keys(0);pump(650);web_keys(chord_keys);pump(40);
    autonomous(1100,"new key press after running disconnect");
    return_to_source(0); /* F8 alone resumes the previously running position. */
    start_and_disconnect(1);autonomous(1100,"HOLD after running disconnect");
    return_to_source(0xfa);
    start_and_disconnect(2);autonomous(1100,"LATCH after running disconnect");
    rt(0xfc);pump(4);return_to_source(0xfb);
    stop_live();pump(1600);
    for(unsigned ch=0;ch<16;ch++)for(unsigned n=0;n<128;n++)CHECK(!held[ch][n],"every emitted MIDI note has a release");
    CHECK(sq_st.pat[0].nev==1,"live playing did not edit the stored pattern");
    CHECK(cases==4,"all held/new/HOLD/LATCH disconnect cases ran");
    fseek(wav,0,SEEK_SET);header(wav_frames);fclose(wav);
    printf("ARP DISCONNECT preset=%u target=%s cases=%u checks=%u failures=%u frames=%u\n",preset,argv[2],cases,checks,failed,wav_frames);
    return failed?1:0;
}
