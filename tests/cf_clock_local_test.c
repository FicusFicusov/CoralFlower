/* SPDX-License-Identifier: GPL-3.0-only */
/* Persist CLOCK In through the panel, then boot a new process from that flash.
 * Owner regression: explicit PLAY without an external source must sound drums;
 * clock loss alone must not turn a paused/external transport into a local one.
 * The oracle observes rendered PCM, emitted MIDI notes and elapsed beat time.
 * Usage: seed <flash>; repro|run <flash> <absolute preset 0|25|30> <wav>. */
#include <math.h>
#include "../tools/emu/web/emu_web.c"

static unsigned checks, failures, clock_bpm, clock_origin, clock_pulse;
static unsigned frames, drum_ons, melodic_ons;
static double energy;
static FILE *wav;
#define CHECK(c,m) do { checks++; if (!(c)) { failures++; \
    fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,m); } } while (0)
static void rt(unsigned status) { CHECK(web_midi_in(15u|status<<8),"USB MIDI ingress accepted"); }
static void word(uint16_t v) { fwrite(&v,2,1,wav); }
static void dword(uint32_t v) { fwrite(&v,4,1,wav); }
static void header(void) {
    fwrite("RIFF",1,4,wav); dword(36+frames*4); fwrite("WAVEfmt ",1,8,wav); dword(16);
    word(1); word(2); dword(EMU_FS); dword(EMU_FS*4); word(4); word(16);
    fwrite("data",1,4,wav); dword(frames*4);
}
static void pump(unsigned ms) {
    while (ms--) {
        if (clock_bpm && (uint64_t)(web_ms()-clock_origin)*clock_bpm*24 >= (uint64_t)clock_pulse*60000) {
            rt(0xf8); clock_pulse++;
        }
        web_step(1);
        while (web_avail()>=128) {
            web_render(128);
            for (unsigned i=0;i<128;i++) for (unsigned ch=0;ch<2;ch++) {
                float f=ch?web_out_r()[i]:web_out_l()[i];
                int16_t v=f>=1?32767:f<=-1?-32768:(int16_t)(f*32767);
                energy+=(double)v*v;
                if (wav) fwrite(&v,2,1,wav);
            }
            frames+=128;
        }
        for (uint32_t p;(p=web_midi_out_take())!=0;) {
            unsigned st=p>>8&255, vel=p>>24&127;
            if ((st&0xf0)==0x90 && vel) {
                if ((st&15)==cfp_ch(CF_PART_DRUMS)) drum_ons++;
                else melodic_ons++;
            }
        }
    }
}
static void tap(unsigned b) { web_buttons(1u<<b); pump(40); web_buttons(0); pump(40); }
static void turn(unsigned e,int delta) { web_enc(e,delta); pump(40); }
static void clocks(unsigned bpm) { clock_bpm=bpm; clock_origin=web_ms(); clock_pulse=0; }
static unsigned position(void) { return dmp.nbar*sqp_bar_ticks()+dmp.t; }
static void playing(unsigned ms,unsigned bpm,const char *label) {
    unsigned t=position(), on=drum_ons, f=frames; double e=energy;
    pump(ms);
    unsigned ticks=position()-t, expected=ms*bpm*96/60000;
    double rms=sqrt((energy-e)/(2.0*(frames-f)));
    printf("LOCAL CLOCK %s: ticks=%u expected=%u drum_ons=%u rms=%.3f bpm=%u active=%u wait=%u\n",
           label,ticks,expected,drum_ons-on,rms,cfm.bpm,cfx_in.active,cfx_in.wait);
    CHECK(dmp.on&&cfm.running,"rhythm transport is running");
    CHECK(abs((int)ticks-(int)expected)<=2,"drum position advances at the requested tempo");
    CHECK(drum_ons>on+2,"multiple drum notes actually reach MIDI output");
    CHECK(rms>10,"drums actually reach rendered PCM without a held chord or metronome");
}
static void stopped(unsigned ms,const char *label) {
    unsigned t=position(), on=drum_ons; pump(ms);
    printf("LOCAL CLOCK %s: position=%u drum_ons=%u\n",label,position(),drum_ons-on);
    CHECK(!dmp.on&&!sqp.on&&!cfm.running,"external/local Stop leaves stored transport stopped");
    CHECK(position()==t&&drum_ons==on,"stopped transport emits no new drum notes or ticks");
}
static void live_arp(unsigned ms,const char *label) {
    unsigned index=cfa_k.idx, notes=melodic_ons, f=frames; double e=energy;
    stopped(ms,label);
    double rms=sqrt((energy-e)/(2.0*(frames-f)));
    CHECK(cfa_k.idx>index+2&&melodic_ons>notes+2,"live ARP emits notes while drums stay paused");
    CHECK(rms>10,"live ARP actually reaches rendered PCM");
    printf("LOCAL CLOCK ARP %s: steps=%u rms=%.3f engine=%u bpm=%u\n",label,cfa_k.idx-index,rms,cf_snd[0].engine,cfa_bpm());
}
static void tempo(unsigned bpm) {
    tap(EMU_B_ARP); turn(EMU_E_SELECT,1-(int)cu.page[SCR_ARP]);
    turn(EMU_E_K1,(int)bpm-(int)cs.bpm); tap(EMU_B_HOME);
    CHECK(cs.bpm==bpm&&cfm.bpm==bpm&&cfa_bpm()==bpm,"unlocked TEMPO knob controls transport and live ARP");
}
static void manual_continue(int idle_clock) {
    /* Manual Stop must preserve the beat phase too, including when the first
     * returned F8 arrives while stopped. Observe a real drum downbeat and the
     * metronome's independently generated beat timestamp after Continue. */
    pump(400); tempo(120); tap(EMU_B_PLAY); pump(173); tap(EMU_B_PLAY);
    unsigned t=position(), bar=dmp.nbar;
    CHECK(t%96>8&&t%96<88,"manual Stop happened inside a beat, not at a passing-by-chance downbeat");
    if (idle_clock) { rt(0xf8); pump(3); }
    rt(0xfb); pump(30);
    CHECK(position()==t,"Continue preserves local stopped position until F8");
    unsigned start=web_ms();
    for (unsigned p=0;p<120&&dmp.nbar==bar;p++) {
        unsigned at=start+(p*500+12)/24;
        if (web_ms()<at) pump(at-web_ms());
        rt(0xf8); pump(3);
    }
    CHECK(dmp.nbar==bar+1,"continued rhythm reached the next bar within the bounded pulse count");
    printf("LOCAL CONTINUE idle_f8=%d stopped_tick=%u drum_downbeat_ms=%u metro_beat_ms=%u\n",
           idle_clock,t,sqp.lane_t[0],cfm.beat_ms);
    CHECK(sqp.lane_t[0]==cfm.beat_ms,"drum downbeat and metronome stay in phase after manual Stop/Continue");
    rt(0xfc); pump(4);
}
static int finish(const char *mode,unsigned preset) {
    if (wav) { fseek(wav,0,SEEK_SET); header(); fclose(wav); }
    printf("CLOCK LOCAL %s preset=%u: %u checks, %u failures\n",mode,preset,checks,failures);
    return failures?1:0;
}
int main(int argc,char **argv) {
    if (argc<3) return 2;
    if (!strcmp(argv[1],"seed")) {
        web_boot(); pump(2000);
        tap(EMU_B_GLO); turn(EMU_E_SELECT,1); turn(EMU_E_K1,1); turn(EMU_E_K2,1);
        CHECK(cs.clock_in==1&&cfp_cfg.clock_in&&cs.midi_out,"panel selects CLOCK In and MIDI Out");
        tap(EMU_B_HOME); tempo(120); tap(EMU_B_SEQ); turn(EMU_E_SELECT,1); tap(EMU_B_SEQ);
        CHECK(cu.scr==SCR_DRUM,"panel selects rhythm page");
        tap(EMU_B_HOME); pump(1800);
        CHECK(cfs_saved==cfs_fnv(&cs,sizeof cs),"normal autosave completed");
        FILE *f=fopen(argv[2],"wb"); if (!f) return 3;
        CHECK(fwrite(web_flash(),1,WEB_FLASH_SIZE,f)==WEB_FLASH_SIZE,"full saved flash written"); fclose(f);
        return finish("seed",0);
    }
    if (argc!=5 || (strcmp(argv[1],"run")&&strcmp(argv[1],"repro"))) return 2;
    FILE *f=fopen(argv[2],"rb"); if (!f) return 3;
    CHECK(fread(web_flash_stage(),1,WEB_FLASH_SIZE,f)==WEB_FLASH_SIZE,"saved flash staged before cold boot"); fclose(f);
    web_flash_stage_len(WEB_FLASH_SIZE); web_boot(); pump(2200);
    CHECK(cs.clock_in==1&&cfp_cfg.clock_in&&!cfx_in.seen&&!cfx_in.active,"cold boot restores In without receiving any MIDI");
    CHECK(!cfm.running&&!cfm.on,"boot does not start transport or metronome");
    CHECK((cft_target&3)==2,"HOME PLAY restores the last rhythm target");
    unsigned preset=(unsigned)atoi(argv[3]);
    turn(EMU_E_PRESETS,(int)preset-(int)cf_snd_pos(0));
    CHECK(cf_snd_pos(0)==preset,"actual requested preset selected, independent of boot default");
    CHECK(cf_snd[0].engine==(preset?ENGI_VA:ENGI_FM6),"actual FM/VA engine selected");
    pump(2000);
    wav=fopen(argv[4],"wb"); if (!wav) return 3; frames=0; energy=0; header();
    tap(EMU_B_PLAY); playing(2100,120,"cold boot In, no MIDI, explicit PLAY");
    if (!strcmp(argv[1],"repro")) return finish("repro",preset);
    tap(EMU_B_PLAY); stopped(500,"local Stop");
    tempo(60); tap(EMU_B_PLAY); playing(3100,60,"local restart and TEMPO 60");
    /* F8 alone hands a running local rhythm to the external grid; it neither
     * restarts at bar zero nor requires a preceding transport command. */
    unsigned before=position(); clocks(90); pump(1200);
    CHECK(cfx_in.active&&position()>before,"returning F8 takes over the current running position");
    playing(2100,90,"returned F8 at 90");
    clocks(0); before=position(); pump(600);
    CHECK(!cfx_in.active&&position()<=before+3,"loss permits only the final pulse's three subdivisions");
    before=position(); unsigned on=drum_ons; pump(900);
    CHECK(position()==before&&drum_ons==on,"clock loss alone never starts local drums");
    tap(EMU_B_PLAY); stopped(400,"Stop after disconnect");
    tap(EMU_B_PLAY); playing(2100,90,"explicit PLAY after disconnect");
    tempo(60); playing(3100,60,"manual TEMPO overrides stale external 90");
    rt(0xfa); pump(4); before=position(); on=drum_ons; pump(100);
    CHECK(dmp.on&&before==0&&position()==before&&drum_ons==on,"external Start re-arms local playback at zero until F8");
    rt(0xf8); pump(3);
    CHECK(dmp.t<=1&&drum_ons>on,"first F8 sounds the Start downbeat");
    clocks(90); pump(1300); rt(0xfc); pump(4);
    stopped(600,"DAW Stop with continuing F8");
    tap(EMU_B_ARP); turn(EMU_E_SELECT,-(int)cu.page[SCR_ARP]); turn(EMU_E_K1,1); tap(EMU_B_HOME);
    web_keys((1u<<7)|(1u<<11)|(1u<<14)); pump(40);
    CHECK(cfa_k.n>0,"actual C-major keys reach live ARP");
    live_arp(1200,"live ARP under DAW pause");
    clocks(0); stopped(600,"DAW pause survives clock loss");
    tempo(75); live_arp(1200,"manual tempo during autonomous ARP");
    web_keys(0); pump(600);
    CHECK(!cfa_k.n&&!cfa_k.cur,"key release stops live ARP");
    before=position(); rt(0xfb); pump(100);
    CHECK(dmp.on&&position()==before,"Continue waits at the stopped position");
    rt(0xf8); pump(3);
    CHECK(position()>=before&&position()<=before+4,"Continue's first F8 resumes, never restarts the rhythm");
    clocks(90); pump(1000); rt(0xfc); pump(4); stopped(400,"returned external Stop");
    clocks(0); pump(400); rt(0xfa); pump(100);
    CHECK(position()==0&&cfx_in.wait,"Start with no F8 stays armed, despite the local fallback feature");
    pump(600); CHECK(position()==0,"armed DAW Start does not fall back automatically");
    rt(0xf8); pump(3); CHECK(dmp.t<=1,"returned F8 starts the armed downbeat");
    rt(0xfc); pump(4); stopped(500,"final Stop");
    manual_continue(0); manual_continue(1);
    CHECK(cs.clock_in==1&&cfp_cfg.clock_in,"CLOCK In remains selected throughout");
    pump(1500); CHECK(cf_settings_load()&&cs.clock_in==1,"autosaved setting is still CLOCK In");
    return finish("run",preset);
}
