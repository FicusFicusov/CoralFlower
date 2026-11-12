/* SPDX-License-Identifier: GPL-3.0-only */
/* Real firmware, simulated audio/time, real USB-MIDI ingress. Tests set the
 * clock setting through its normal config handoff; patterns use panel inputs. */
#include "../tools/emu/web/emu_web.c"

static unsigned checks, failures;
#define CHECK(x, msg) do { checks++; if (!(x)) { failures++; \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, msg); } } while (0)
static void tap(unsigned b) {
    web_buttons(1u << b); web_step(40); web_buttons(0); web_step(40);
}
static void rt(unsigned s) { CHECK(web_midi_in(15u | s << 8), "MIDI ingress accepts event"); }
static void drain(void) {
    for (unsigned i=0; mi_r!=mi_w && i<8; i++) web_step(1);
    CHECK(mi_r==mi_w, "audio ISR consumed the input before observing it");
}
static void clock_at(unsigned ms) {
    if (web_ms() < ms) web_step(ms - web_ms());
    rt(0xf8); drain();
}
static void setup(void) {
    web_boot(); web_step(1800);
    tap(EMU_B_SEQ); tap(EMU_B_SEQ); tap(EMU_B_EDIT);
    web_keys(1u << 7); web_step(20); web_keys(0); web_step(80);
    CHECK(sq_st.pat[0].nev == 1, "panel entered a chord in pattern A");
    cs.clock_in = 1; cf_send_config(); web_step(20);
}
static void transport(void) {
    setup();
    unsigned base = web_ms();
    for (unsigned p=0;p<25;p++) clock_at(base + (p*500+12)/24);
    CHECK(!sqp.on && !cfm.running, "idle clocks do not start the pattern");
    rt(0xfa); web_step(5);
    CHECK(!sqp.n, "Start waits for the first clock before notes");
    rt(0xf8); web_step(2);
    CHECK(sqp.on && sqp.n, "Start plus clock plays selected pattern");
    unsigned began=web_ms();
    for (unsigned p=1;p<13;p++) clock_at(began + (p*500+12)/24);
    unsigned pos = sqp.t;
    rt(0xfa); rt(0xfb); web_step(2);
    CHECK(sqp.t >= pos, "redundant Start/Continue does not restart");
    rt(0xfc); web_step(2); pos=sqp.t;
    CHECK(!sqp.on && !sqp.n && !cfm.running, "Stop ends transport and held notes");
    for(unsigned p=1;p<=8;p++) clock_at(web_ms()+21);
    CHECK(sqp.t == pos, "idle clocks preserve stopped position");
    rt(0xfb); web_step(3);
    CHECK(sqp.t == pos, "Continue waits for clock");
    rt(0xf8); web_step(2);
    CHECK(sqp.on && sqp.t >= pos && sqp.t <= pos+4,
          "Continue resumes stopped sequence at the next MIDI clock");
}
static void acquisition(void) {
    setup();
    clock_at(web_ms());                         /* a live source, unlike autonomous PLAY with CLOCK In saved */
    tap(EMU_B_PLAY);
    CHECK(sqp.t==0 && !sqp.n, "local PLAY with a live clock waits without free-running");
    unsigned base=web_ms();
    for (unsigned p=0;p<=240;p++) clock_at(base+(p*500+12)/24);
    unsigned actual=sqp.nbar*sqp_bar_ticks()+sqp.t;
    printf("ACQUISITION expected=960 actual=%u error_ticks=%d bpm=%u\n",actual,(int)actual-960,cfm.bpm);
    CHECK(actual>=960 && actual<=961, "no acquisition phase jump or drift after ten beats");
    unsigned pos=actual;
    web_step(700);
    actual=sqp.nbar*sqp_bar_ticks()+sqp.t;
    CHECK(actual <= pos+3, "clock loss never switches to internal free-run");
}
static void timing(unsigned bpm, int jitter) {
    setup();
    sq_st.pat[0].bars=1;
    for(unsigned k=0;k<SQ_NTRK;k++) sq_st.pat[0].tb[k]=1;
    rt(0xfa); drain();
    unsigned start=web_ms()+10, first=0, max_error=0, max_pair=0;
    int end_error=0;
    static const int noise[] = {0,3,-2,1,-3,2,-1};
    for (unsigned p=0; p<=24*120; p++) {
        int offset=jitter ? noise[p%7] : 0;
        unsigned ideal=(p*60000u+bpm*12u)/(bpm*24u);
        clock_at(start+ideal+offset);
        unsigned actual=sqp.nbar*sqp_bar_ticks()+sqp.t;
        /* drain returns after a whole 128-sample DMA buffer: at 300 BPM one
         * interpolated tick can already follow the pulse within that buffer. */
        CHECK(actual>=p*4 && actual<=p*4+EMU_BLOCK*bpm*96u/(FS*60u),
              "pulse count plus at most the observed audio buffer's interpolation");
        if (!p) first=cfm.beat_ms;
        if (p%24==0) {
            int error=(int)(cfm.beat_ms-first)-(int)ideal;
            unsigned mag=abs(error);
            if (mag>max_error) max_error=mag;
            end_error=error;
            CHECK(cfm.beats_done==p/24+1, "metronome counts the same beats as sequence");
            if (p%96==0) {
                unsigned pair=abs((int)sqp.lit_t[SQ_CHORDS]-(int)cfm.beat_ms);
                if (pair>max_pair) max_pair=pair;
                CHECK(pair==0, "pattern downbeat and metronome fire in the same audio block");
            }
        }
    }
    printf("TIMING bpm=%u jitter=%d beats=120 phase_max_ms=%u end_error_ms=%d seq_metro_max_ms=%u\n",
           bpm,jitter,max_error,end_error,max_pair);
    CHECK(max_error <= (jitter ? 6u : 3u), "phase error stays within input jitter plus one audio block");
    CHECK(abs((int)cfx_in.bpm-(int)bpm)<=1, "measured tempo follows source");
}
static void locate(void) {
    setup();
    /* A two-slot stored song, with a silent unused slot between A and B. */
    sq_st.pat[1]=sq_st.pat[0];
    for (unsigned i=0;i<2;i++) { sq_st.pat[i].bars=1; for(unsigned k=0;k<SQ_NTRK;k++) sq_st.pat[i].tb[k]=1; }
    sq_st.song.n=3; sq_st.song.pat[0]=0; sq_st.song.pat[1]=2; sq_st.song.pat[2]=1;
    cu.scr=SCR_SQ; squ.pat=SQ_SONGPG; squ.slot=2; web_step(20);
    rt(0xfa); drain(); clock_at(web_ms()+10);
    CHECK(sqp.song && sqp.slot==0 && sqp.pat==0, "MIDI Start begins song at its first playable slot");
    unsigned start=web_ms();
    for(unsigned p=1;p<=96;p++) clock_at(start+(p*500+12)/24);
    CHECK(sqp.slot==2 && sqp.pat==1 && sqp.t==0, "song advances over empty slot at bar boundary");
    rt(0xfc); drain();
    CHECK(web_midi_in(3u | 0xf2u<<8 | 20u<<16), "SPP queued"); drain();
    CHECK(!sqp.on && sqp.slot==2 && sqp.pat==1 && sqp.t==96,
          "SPP 20 locates one bar plus one beat into song without playback");
    rt(0xfb); drain(); clock_at(web_ms()+10);
    CHECK(sqp.on && sqp.t==96 && cfm.beats_done==6, "SPP Continue starts at the requested position");
    CHECK(web_midi_in(3u | 0xf2u<<8), "SPP while running queued"); drain();
    CHECK(sqp.pat==1 && sqp.t>=96, "SPP while playing is ignored");
    rt(0xfc); drain();
    CHECK(web_midi_in(3u | 0xf2u<<8 | 127u<<16 | 127u<<24), "maximum SPP queued"); drain();
    CHECK(sqp.t < sqp_bar_ticks(), "maximum 14-bit SPP locates in bounded time");
    rt(0xfa); drain(); clock_at(web_ms()+10);
    CHECK(sqp.slot==0 && sqp.t==0, "Start overrides previous SPP and resets to beginning");
}
static void rhythm(void) {
    setup(); tap(EMU_B_HOME); tap(EMU_B_SEQ); web_enc(EMU_E_SELECT,1); web_step(40); tap(EMU_B_SEQ);
    CHECK(cu.scr==SCR_DRUM, "rhythm page selected through panel");
    rt(0xfa); drain();
    CHECK(dmp.on && dmp.t==0, "Start arms selected rhythm");
    clock_at(web_ms()+10);
    CHECK(sqp.lane_t[0]==cfm.beat_ms, "rhythm downbeat matches metronome");
    unsigned start=web_ms();
    for(unsigned p=1;p<=48;p++) clock_at(start+(p*500+12)/24);
    CHECK(dmp.t==192, "rhythm uses shared 96-PPQN clock");
    rt(0xfc); drain(); unsigned pos=dmp.t;
    CHECK(!dmp.on && !sqp.doff, "Stop ends rhythm and sends drum note-offs");
    clock_at(web_ms()+100); CHECK(dmp.t==pos, "stopped rhythm does not advance");
    rt(0xfb); drain(); clock_at(web_ms()+10);
    CHECK(dmp.on && dmp.t>=pos && dmp.t<=pos+4, "Continue resumes rhythm");
}
static void recovery(void) {
    setup(); rt(0xfa); drain();
    unsigned start=web_ms();
    for(unsigned p=0;p<25;p++) {
        clock_at(start+(p*500+12)/24);
        CHECK(midi_enqueue(15u | 0xf8u<<8,2u), "other source clock queued"); drain();
    }
    CHECK(sqp.t==96, "USB and TRS clocks are never added together");
    web_step(500); unsigned pos=sqp.t;
    CHECK(!cfx_in.active && !sqp.n, "clock loss unlocks and releases sequencer notes");
    web_step(500); CHECK(sqp.t==pos, "lost clock holds position");
    CHECK(midi_enqueue(15u | 0xf8u<<8,2u), "replacement source pulse queued"); drain();
    CHECK(cfx_in.active && sqp.t>=pos && sqp.t<=pos+4, "a new source resumes after lease expiry");
    CHECK(midi_enqueue(15u | 0xfcu<<8,2u), "replacement source Stop queued"); drain();
    CHECK(!sqp.on, "new source owns transport too");
    cs.clock_in=0; cf_send_config(); web_step(20); rt(0xfa); drain();
    CHECK(!sqp.on, "Clock Off ignores external transport");
    tap(EMU_B_PLAY); CHECK(sqp.on, "Clock Off preserves local PLAY");
    cs.clock_in=1; cf_send_config(); web_step(20); pos=sqp.t;
    web_step(500); CHECK(sqp.t==pos, "switching to In waits for external clock");
    cs.clock_in=2; cf_send_config(); web_step(100);
    CHECK(sqp.t!=pos && !cfp_cfg.clock_in, "switching to Out resumes internal clock");
}
static void arpeggio(void) {
    setup();
    sq_st.pat[0].ev[0].len=96;
    sq_st.pat[0].ev[1]=sq_st.pat[0].ev[0]; sq_st.pat[0].ev[1].trk=SQ_LAYER;
    sq_st.pat[0].nev=2; sq_st.pat[0].arp=(1u<<SQ_CHORDS)|(1u<<SQ_LAYER);
    cs.arp.rate=6; cs.arp.gate=50; cf_send_config(); web_step(20);
    unsigned warm=web_ms();
    for(unsigned p=0;p<25;p++) clock_at(warm+(p*2000+36)/72); /* 90 BPM */
    rt(0xfa); drain(); unsigned start=warm+(25*2000+36)/72;
    clock_at(start);
    CHECK(cfa_sq[0].idx==1 && cfa_sq[1].idx==1, "both track arps play exactly once on the first downbeat");
    CHECK(cfa_bpm()==90, "external tempo determines arp gate length");
    for(unsigned p=1;p<=48;p++) clock_at(start+(p*2000+36)/72);
    CHECK(cfa_sq[0].idx==9 && cfa_sq[1].idx==9, "CHORDS/LAYER arps share quarter-beat boundaries");
    rt(0xfc); drain();
    CHECK(!cfa_sq[0].cur && !cfa_sq[1].cur, "Stop releases both track arps");
    tap(EMU_B_HOME); cu.arp_on=1; cf_send_config(); web_step(20);
    web_keys(1u<<7); web_step(40);
    rt(0xfa); drain(); start=web_ms()+10;
    for(unsigned p=0;p<=48;p++) clock_at(start+(p*2000+36)/72);
    CHECK(cfa_k.idx>=8 && cfa_k.idx<=10, "live arp also follows external quarter-beats");
    rt(0xfc); drain(); unsigned index=cfa_k.idx;
    for(unsigned p=0;p<24;p++) clock_at(web_ms()+28);
    CHECK(cfa_k.idx>=index+3 && cfa_k.idx<=index+5, "live arp remains playable on external Stop");
    CHECK(!sqp.on&&!dmp.on&&!cfm.running, "live arp does not restart stored transport");
    web_keys(0); web_step(20);
}
static void output_clock(void) {
    setup(); cs.bpm=120; cf_send_metro(); cs.clock_in=2; cs.midi_out=1; cf_send_config(); web_step(20);
    while(web_midi_out_take()) {}
    cf_post(CFE_SEQ,1,0,0);
    unsigned fa=0, first=0, reference=0, clocks=0, stops=0, maxerr=0;
    unsigned start=web_ms();
    for(unsigned ms=0;ms<2100;ms++) {
        web_step(1);
        for(uint32_t pkt;(pkt=web_midi_out_take())!=0;) {
            unsigned st=(pkt>>8)&255u;
            if(st==0xfa) { CHECK(!fa,"only one Start is emitted"); fa=web_ms(); }
            if(st==0xf8) {
                CHECK(fa!=0,"Start precedes all output clocks");
                if(!first) first=web_ms();
                /* The first pulse is deliberately delayed after FA. Measure
                 * steady output against the following beat, not that delay. */
                if(clocks==24) reference=web_ms();
                if(clocks>=24) {
                    int error=(int)(web_ms()-reference)-(int)(((clocks-24)*500+12)/24);
                    if((unsigned)abs(error)>maxerr) maxerr=(unsigned)abs(error);
                }
                clocks++;
            }
        }
    }
    CHECK(first>=fa+1,"output Start precedes first F8 by at least one millisecond");
    CHECK(clocks>=100 && clocks<=102 && maxerr<=3,"Clock Out emits 24 PPQN without phase drift");
    tap(EMU_B_PLAY);
    for(uint32_t pkt;(pkt=web_midi_out_take())!=0;) if(((pkt>>8)&255u)==0xfc) stops++;
    CHECK(stops==1,"local Stop emits exactly one MIDI Stop");
    printf("OUTPUT clocks=%u first_gap_ms=%u max_phase_ms=%u duration_ms=%u\n",clocks,first-fa,maxerr,web_ms()-start);
}
static void count_in(void) {
    setup();
    cf_post(CFE_SEQ,4,0,SQ_CHORDS); web_step(20);
    CHECK(sqp.cin==384 && sqp.rec==SQ_CHORDS, "recording arms a full count-in in Clock In");
    unsigned start=web_ms()+10;
    for(unsigned p=0;p<=96;p++) clock_at(start+(p*500+12)/24);
    CHECK(sqp.cin==0 && sqp.t==0 && sqp.n, "count-in ends exactly on the next bar downbeat");
    CHECK(cfm.acc==0 && cfm.beats_done==1 && cfm.beat_ms==sqp.lit_t[SQ_CHORDS],
          "count-in resets click and pattern to the same downbeat");
    rt(0xfc); drain();
    CHECK(sqp.rec==0xff && !sqp.ropen, "external Stop ends the take");
    sq_pat_t saved=sq_st.pat[0];
    rt(0xfb); drain(); clock_at(web_ms()+20);
    CHECK(sqp.rec==0xff && !memcmp(&saved,&sq_st.pat[0],sizeof saved),
          "Continue resumes playback without silently resuming recording");
}
static void switch_without_source(void) {
    setup();
    cs.clock_in=0; cf_send_config(); web_step(20);
    cf_post(CFE_SEQ,1,0,0); web_step(3);
    printf("SWITCH before note_count=%u t=%u\n", sqp.n, sqp.t);
    cs.clock_in=1; cf_send_config(); web_step(20);
    unsigned pos=sqp.t;
    web_step(600);
    printf("SWITCH after600ms note_count=%u t=%u wait=%u seen=%u lost=%u\n",
           sqp.n,sqp.t,cfx_in.wait,cfx_in.seen,cfx_in.lost);
    CHECK(sqp.t==pos,"In without clocks holds position");
    CHECK(!sqp.n,"In without clocks releases previously playing notes after 300ms");
}

static void rhythm_to_recording(void) {
    setup(); tap(EMU_B_HOME); tap(EMU_B_SEQ); web_enc(EMU_E_SELECT,1); web_step(40); tap(EMU_B_SEQ);
    rt(0xfa); drain();
    unsigned start=web_ms()+10;
    clock_at(start);
    cf_post(CFE_DRUM,3,0,SQ_CHORDS); web_step(3);
    CHECK(dmp.rin,"recording transfer armed");
    for(unsigned p=1;p<=96;p++) clock_at(start+(p*500+12)/24);
    printf("TRANSFER atbar sq_on=%u drum_on=%u note_count=%u t=%u wait=%u beat_ms=%u now=%u\n",
           sqp.on,dmp.on,sqp.n,sqp.t,cfx_in.wait,cfm.beat_ms,cf_now_ms);
    CHECK(sqp.on && !dmp.on,"sequence takes over at bar boundary");
    CHECK(sqp.n && !cfx_in.wait,"sequence downbeat plays at rhythm-to-sequence bar boundary");
    clock_at(start+(97*500+12)/24);
    printf("TRANSFER nextclock note_count=%u t=%u wait=%u beat_ms=%u now=%u\n",
           sqp.n,sqp.t,cfx_in.wait,cfm.beat_ms,cf_now_ms);
}

static void lost_rhythm_start(void) {
    setup(); tap(EMU_B_HOME); tap(EMU_B_SEQ); web_enc(EMU_E_SELECT,1); web_step(40); tap(EMU_B_SEQ);
    rt(0xfa); drain(); unsigned start=web_ms()+10;
    for(unsigned p=0;p<=10;p++) clock_at(start+(p*500+12)/24);
    web_step(500);
    rt(0xfa); drain();
    printf("RHYTHM RESTART beforeclock t=%u sub=%u wait=%u lost=%u\n",dmp.t,cfx_in.sub,cfx_in.wait,cfx_in.lost);
    clock_at(web_ms()+10);
    printf("RHYTHM RESTART firstclock t=%u sub=%u wait=%u\n",dmp.t,cfx_in.sub,cfx_in.wait);
    CHECK(dmp.t==0,"Start after clock loss restarts rhythm on the next F8");
}
int main(int argc, char **argv) {
    if (argc<2) return 2;
    if (!strcmp(argv[1],"transport")) transport();
    else if (!strcmp(argv[1],"acquisition")) acquisition();
    else if (!strcmp(argv[1],"timing") && argc==4) timing((unsigned)atoi(argv[2]),atoi(argv[3]));
    else if (!strcmp(argv[1],"locate")) locate();
    else if (!strcmp(argv[1],"rhythm")) rhythm();
    else if (!strcmp(argv[1],"recovery")) recovery();
    else if (!strcmp(argv[1],"arp")) arpeggio();
    else if (!strcmp(argv[1],"out")) output_clock();
    else if (!strcmp(argv[1],"count-in")) count_in();
    else if (!strcmp(argv[1],"switch")) switch_without_source();
    else if (!strcmp(argv[1],"transfer")) rhythm_to_recording();
    else if (!strcmp(argv[1],"restart")) lost_rhythm_start();
    else return 2;
    printf("clock %s: %u checks, %u failures\n",argv[1],checks,failures);
    return failures ? 1 : 0;
}
