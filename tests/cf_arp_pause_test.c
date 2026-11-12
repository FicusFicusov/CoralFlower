/* SPDX-License-Identifier: GPL-3.0-only */
/* Owner regression: Clock In must permit live ARP while the DAW is stopped,
 * with or without F8, without advancing stored transport. Real firmware and
 * audio renderer; USB MIDI ingress and panel keys, simulated time/hardware.
 * cc -std=gnu11 -O1 -g -w -fsanitize=address,undefined -DEMU_WEB \
 *   -Itools/emu/web/compat -Ibuild/gen -Ifirmware/src \
 *   tests/cf_arp_pause_test.c -o build/host/cf_arp_pause_test -lm
 * Run: <preset 0|25|30> <output.wav> */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "../tools/emu/web/emu_web.c"
static unsigned checks,failed,clock_bpm,clock_origin,clock_pulse;
static FILE *wav;
static unsigned wav_frames,peak,ons,offs;
static double energy;
static int held[16][128];
#define CHECK(c,m) do {checks++;if(!(c)){failed++;if(failed<25)fprintf(stderr,"FAIL %s:%d %s\n",__FILE__,__LINE__,m);}}while(0)
static void rt(unsigned s) {CHECK(web_midi_in(15u|s<<8),"USB realtime accepted");}
static void w16(uint16_t n){fwrite(&n,2,1,wav);}
static void w32(uint32_t n){fwrite(&n,4,1,wav);}
static void header(unsigned n) {
    fwrite("RIFF",1,4,wav);w32(36+n*4);fwrite("WAVEfmt ",1,8,wav);w32(16);
    w16(1);w16(2);w32(44100);w32(176400);w16(4);w16(16);fwrite("data",1,4,wav);w32(n*4);
}
static void pump(unsigned ms) {
    while(ms--) {
        if(clock_bpm && (uint64_t)(web_ms()-clock_origin)*clock_bpm*24u >= (uint64_t)clock_pulse*60000u) {
            rt(0xf8);clock_pulse++;
        }
        web_step(1);
        while(web_avail()>=128) {
            web_render(128);
            for(unsigned i=0;i<128;i++)for(unsigned ch=0;ch<2;ch++) {
                float f=ch?web_out_r()[i]:web_out_l()[i];
                int16_t v=(int16_t)(f>=1?32767:f<=-1?-32768:f*32767);
                if(wav){fwrite(&v,2,1,wav);energy+=(double)v*v;if((unsigned)abs(v)>peak)peak=abs(v);}
            }
            if(wav)wav_frames+=128;
        }
        for(uint32_t p;(p=web_midi_out_take())!=0;) {
            unsigned st=p>>8&255,n=p>>16&127,v=p>>24&127,ch=st&15;
            if((st&0xf0)==0x90 && v){ons++;held[ch][n]=1;}
            if((st&0xf0)==0x80 || ((st&0xf0)==0x90&&!v)){offs++;held[ch][n]=0;}
        }
    }
}
static void tap(unsigned b){web_buttons(1u<<b);pump(40);web_buttons(0);pump(40);}
static void clocks(unsigned bpm){clock_bpm=bpm;clock_origin=web_ms();clock_pulse=0;}
static void stopped(unsigned t,unsigned d,unsigned g) {
    CHECK(!sqp.on&&!dmp.on&&!cfm.running,"manual ARP does not start transport");
    CHECK(sqp.t==t&&dmp.t==d&&cf_grid.g24==g,"manual ARP does not move stored transport or global grid");
    CHECK(!cfa_sq[0].cur&&!cfa_sq[1].cur,"stored track arps stay stopped");
}
static void progress(unsigned ms,unsigned lo,unsigned hi,const char *label) {
    unsigned before=cfa_k.idx;double e=energy;unsigned frames=wav_frames;pump(ms);
    unsigned steps=cfa_k.idx-before;
    CHECK(steps>=lo&&steps<=hi,label);
    double rms=sqrt((energy-e)/(2.0*(wav_frames-frames)));
    CHECK(rms>10,"ARP interval contains rendered sound, not only a state counter");
    printf("ARP %s: steps=%u bpm=%u rms=%.2f\n",label,steps,cfa_bpm(),rms);
}
static void resume(unsigned status) {
    clock_bpm=0;unsigned before=cfa_k.idx;
    rt(status);pump(20);
    CHECK(cfa_k.idx==before,"Start/Continue stops local stepping while waiting for F8");
    rt(0xf8);pump(4);
    CHECK(cfa_k.idx==before+1,"first resumed F8 plays the live ARP once");
    CHECK(sqp.on&&cfm.running,"external transport resumes selected pattern");
    unsigned first=cfa_k.idx,start=web_ms(),first_grid=cf_grid.g24;
    for(unsigned p=1;p<=24;p++) {
        unsigned at=start+(p*2000+36)/72;
        if(web_ms()<at)pump(at-web_ms());rt(0xf8);pump(3);
        CHECK(cfa_k.idx==first+cf_grid.g24/6-first_grid/6,"live ARP follows the external six-pulse grid, including Continue's existing position");
    }
    rt(0xfc);pump(4);
}
static void cancel_before_clock(unsigned status,int mode_change) {
    if(mode_change){cs.arp.mode=CF_ARP_STRUM_UP;cf_send_config();pump(20);}
    rt(status);pump(4);web_keys(1u<<7);pump(40);
    if(mode_change){cs.arp.mode=CF_ARP_UP;cf_send_config();pump(20);}
    CHECK(cfa_k.n&&!cfa_k.idx&&!cfa_k.cur,"new held chord waits for the armed transport's first clock");
    rt(0xfc);pump(4);
    progress(700,5,6,"cancelled Start/Continue permits held live chord without F8");
    web_keys(0);pump(500);CHECK(!cfa_k.n&&!cfa_k.cur,"release after cancelled transport clears live chord");
}
int main(int argc,char **argv) {
    if(argc!=3)return 2;
    unsigned preset=atoi(argv[1]);web_boot();pump(1800);
    unsigned base_sound=cf_snd_pos(0);
    web_enc(EMU_E_PRESETS,preset);pump(300);
    CHECK(cf_snd_pos(0)==base_sound+preset,"requested preset detents selected");
    CHECK(cf_snd[0].engine==(preset?ENGI_VA:ENGI_FM6),"requested FM/VA engine selected");
    tap(EMU_B_SEQ);tap(EMU_B_SEQ);tap(EMU_B_EDIT);
    web_keys(1u<<7);pump(40);web_keys(0);pump(100);
    CHECK(sq_st.pat[0].nev==1,"panel creates a stored pattern to guard against free-run");
    tap(EMU_B_HOME);tap(EMU_B_GLO);web_enc(EMU_E_SELECT,1);pump(40);web_enc(EMU_E_K2,1);pump(40);
    CHECK(cs.clock_in==1&&cfp_cfg.clock_in,"GLO MIDI CLOCK In selected by the actual panel controls");
    tap(EMU_B_HOME);tap(EMU_B_ARP);web_enc(EMU_E_K1,1);pump(40);
    cs.bpm=120;cs.arp.rate=6;cs.arp.gate=50;cs.arp.swing=50;cs.bass_on=0;cs.midi_out=1;
    cf_send_config();cf_send_metro();pump(40);tap(EMU_B_HOME);
    CHECK(cfa_k.on&&!cfm.running,"live ARP on while transport stopped");
    unsigned t=sqp.t,d=dmp.t,g=cf_grid.g24;
    wav=fopen(argv[2],"wb");if(!wav)return 3;header(0);
    web_keys(1u<<7);pump(40);CHECK(cfa_k.n>0,"manual chord reaches live ARP");
    progress(1100,8,10,"initial no clock uses local120");stopped(t,d,g);
    web_keys(0);pump(500);CHECK(!cfa_k.n&&!cfa_k.cur,"release stops unlatched ARP");
    cancel_before_clock(0xfa,0);cancel_before_clock(0xfb,0);cancel_before_clock(0xfa,1);
    clocks(90);pump(1200);rt(0xfc);pump(4);
    CHECK(cfa_bpm()==90,"idle F8 establishes DAW tempo");
    web_keys(1u<<7);pump(40);progress(1400,8,9,"Stop with F8 uses90");stopped(t,d,g);
    clock_bpm=0;progress(1400,8,9,"Stop without F8 retains90");stopped(t,d,g);
    CHECK(!cfx_in.active,"clock timeout was exercised");
    /* The physical HOLD button, separately from ARP LATCH. */
    unsigned hold_key=27;
    for(unsigned k=0;k<27;k++){int b;if(cf_key_role(cfc.cfg.algo,k,&b)==CF_ROLE_BUTTON&&cf_is_off(&cfc.cfg,b))hold_key=k;}
    CHECK(hold_key<27,"panel HOLD key was found from current mapping");
    web_keys((1u<<7)|(1u<<hold_key));pump(40);web_keys(0);pump(40);
    CHECK(cfc.hold&&cfa_k.n,"HOLD retains released chord");
    progress(700,4,5,"HOLD while paused");
    web_keys(1u<<hold_key);pump(40);web_keys(0);pump(500);
    CHECK(!cfc.hold&&!cfa_k.n&&!cfa_k.cur,"HOLD off releases retained chord");
    cs.arp.latch=1;cf_send_config();pump(20);web_keys(1u<<7);pump(40);web_keys(0);pump(40);
    CHECK(cfa_k.latched&&cfa_k.n,"ARP LATCH retains released chord");progress(700,4,5,"LATCH while paused");
    cs.arp.latch=0;cf_send_config();pump(500);CHECK(!cfa_k.n&&!cfa_k.cur,"LATCH off releases notes");
    web_keys(1u<<7);pump(40);resume(0xfa);progress(700,4,5,"manual chord continues after Stop");
    resume(0xfb);web_keys(0);pump(500);
    CHECK(!cfa_k.n&&!cfa_k.cur&&!sqp.on&&!sqp.n&&!dmp.on,"Stop plus key release clears live and stored voices");
    /* Roots input is a manual player too, not direct synth-bypass input. */
    cs.notes_in=CF_NI_ROOTS;cf_send_config();pump(40);
    CHECK(web_midi_in(9u|0x90u<<8|60u<<16|100u<<24),"manual USB MIDI root accepted");pump(40);
    progress(700,4,5,"manual MIDI root while paused");
    CHECK(web_midi_in(8u|0x80u<<8|60u<<16),"manual USB MIDI release accepted");pump(2500);
    CHECK(!cfa_k.n&&!cfa_k.cur,"manual MIDI release ends ARP");
    for(unsigned ch=0;ch<16;ch++)for(unsigned n=0;n<128;n++)CHECK(!held[ch][n],"all emitted MIDI notes received release");
    CHECK(sq_st.pat[0].nev==1,"live playing did not edit stored pattern");
    fseek(wav,0,SEEK_SET);header(wav_frames);fclose(wav);
    printf("ARP PAUSE preset=%u frames=%u peak=%u rms=%.2f note_ons=%u note_offs=%u checks=%u failures=%u\n",
        preset,wav_frames,peak,sqrt(energy/(2.0*wav_frames)),ons,offs,checks,failed);
    return failed?1:0;
}
