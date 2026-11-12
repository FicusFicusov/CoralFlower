/* SPDX-License-Identifier: GPL-3.0-only */
/* Real CoralFlower mixer/audio ISR capture, USB ring/service and MIDI ingress.
 * The USB registers and clock are models; this does not measure physical timing.
 * cc -std=gnu11 -O2 -w -DEMU_WEB -Itools/emu/web/compat -Ibuild/gen -Ifirmware/src \
 *    tests/cf_usb_audio_mix_test.c -o build/host/cf_usb_audio_mix_test -lm
 * Run: <preset 0|25|30> <rate 44100|48000> <USB wav path> [allfx]. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
static unsigned checks, failed;
#define CHECK(c,m) do { checks++; if (!(c)) { failed++; if(failed<16) fprintf(stderr,"FAIL %s:%d %s\n",__FILE__,__LINE__,m); } } while(0)
#define CF_USB_MOCK_CHECK(c,m) CHECK(c,m)
#include "cf_usb_mock.h"
#define FELUCCA_UAC 1
#define FELUCCA_CDC 1
#define FM1_TICKS_PER_US 1u
#include "../tools/emu/web/emu_web.c"

/* The host emulator has no TIMER5/USB bus. The test schedules its bus separately. */
static void ua_service_paced(uint32_t t0, int isr) { (void)t0; (void)isr; ua_service(); }
static void tap(unsigned b) { web_buttons(1u<<b); web_step(40); web_buttons(0); web_step(40); }
static void w32(FILE *f,uint32_t v) { fwrite(&v,4,1,f); }
static void w16(FILE *f,uint16_t v) { fwrite(&v,2,1,f); }
static FILE *wav_open(const char *path,unsigned rate) {
    FILE *f=fopen(path,"wb"); if(!f) return 0;
    fwrite("RIFF",1,4,f);w32(f,0);fwrite("WAVEfmt ",1,8,f);w32(f,16);
    w16(f,1);w16(f,2);w32(f,rate);w32(f,rate*4);w16(f,4);w16(f,16);
    fwrite("data",1,4,f);w32(f,0);return f;
}
static void wav_close(FILE *f,unsigned frames) {
    fseek(f,4,SEEK_SET);w32(f,36+frames*4);fseek(f,40,SEEK_SET);w32(f,frames*4);fclose(f);
}
static void host_take(unsigned frame) {
    cf_usb_mock_reg_global[S_FRAME1]=frame&255;
    cf_usb_mock_reg_global[S_FRAME2]=(frame>>8)&7;
    cf_usb_mock_reg_ep[2][1]&=~1u;
    ua_service();
}
static void levels(void) {
    static const int32_t edges[]={0,1,-1,32767,-32768,65534,-65536,70000,-70000};
    int32_t out[2*CTL];
    ua_reset();ua.cap_alt=1;ua.cap_hold=0;
    for(unsigned i=0;i<2*CTL;i++) out[i]=edges[i%NELEM(edges)];
    cf_usb_audio_capture(out,CTL);
    CHECK(ua.cw==CTL,"stereo final mix was fed");
    for(unsigned i=0;i<2*CTL;i++) {
        int32_t v=out[i]>>1;v=v>32767?32767:v< -32768?-32768:v;
        CHECK(ua.cap[i]==v,"capture normalized to signed24-bit DAC and saturates");
    }
    ua.cap_alt=0; unsigned before=ua.cw;
    cf_usb_audio_capture(out,CTL);CHECK(ua.cw==before,"closed stream does no capture work");
    ua.cap_alt=1;usb.suspended=1;cf_usb_audio_capture(out,CTL);
    CHECK(ua.cw==before,"suspended stream does no capture work");usb.suspended=0;
    ua.cap_hold=1;cf_usb_audio_capture(out,CTL);CHECK(ua.cw==before,"unread stream does no capture work");
}
int main(int argc,char **argv) {
    if(argc<4 || argc>5)return 2;
    unsigned preset=atoi(argv[1]),rate=atoi(argv[2]);
    web_boot();web_step(1800);
    web_enc(EMU_E_PRESETS,preset);web_step(300);
    if(argc==5) { cs.fx.on=(1u<<CFX_N)-1u;cf_fx_apply(&cs.fx); }
    /* A real panel chord, then release. A SEQ pattern supplies MIDI transport. */
    tap(EMU_B_SEQ);tap(EMU_B_SEQ);tap(EMU_B_EDIT);
    web_keys(1u<<7);web_step(20);web_keys(0);web_step(80);
    CHECK(sq_st.pat[0].nev==1,"panel entered a chord");
    cs.clock_in=1;cf_send_config();web_step(20);
    sq_st.pat[0].bars=1;for(unsigned k=0;k<SQ_NTRK;k++)sq_st.pat[0].tb[k]=1;
    CHECK(cr_c1_off==1 && cr_dbg.c1==C1_OFF,"existing single-core runtime is preserved");
    cf_usb_mock_attach(ep0buf);usb.up=usb.config=1;usb_cdc_on=0;
    levels();ua_reset();ua_cfg_build();ua_rate_set(rate);ua_set_interface(UA_IF_CAP,1);host_take(0);
    CHECK(ua.cap_alt && !ua.cap_hold,"host has opened stereo capture");
    FILE *f=wav_open(argv[3],rate);if(!f)return 3;
    uint64_t next_audio=0,next_usb=24000,until=24000000ull*10;
    unsigned frames=0,pulse=0,base=web_ms(),nonzero=0,stereo=0,mute_nonzero=0;
    int32_t out[2*CTL];
    CHECK(web_midi_in(15u|0xfa00u),"MIDI Start accepted during USB capture");
    while(next_usb<=until) {
        if(next_audio<=next_usb) {
            unsigned ms=(unsigned)(next_audio/24000);
            fm1_ms=base+ms;emu_sim_us=fm1_ms*1000u;
            if(ms<4000) {
                while((pulse*500+12)/24<=ms) {
                    CHECK(web_midi_in(15u|0xf800u),"MIDI clock accepted while streaming");pulse++;
                }
            } else if(pulse) { CHECK(web_midi_in(15u|0xfc00u),"MIDI Stop accepted");pulse=0; }
            if(ms>=8500)song.master_q12=0;
            unsigned at=ua.cw;
            audio_block(out,CTL);
            if(rate==44100 && ua.cw-at==CTL)for(unsigned i=0;i<CTL;i++)for(unsigned ch=0;ch<2;ch++) {
                int32_t v=out[2*i+ch]>>8;v=v>32767?32767:v< -32768?-32768:v;
                CHECK(ua.cap[((at+i)&(UA_CAP_RING-1))*2+ch]==v,"USB PCM equals final DAC PCM including MASTER and click");
            }
            next_audio+=CTL*544u; /* actual divider, independent of USB SOF */
        } else {
            unsigned ms=(unsigned)(next_usb/24000);
            fm1_ms=base+ms;emu_sim_us=fm1_ms*1000u;
            CHECK(cf_usb_mock_reg_ep[2][1]&1u,"host finds a packet each millisecond");
            CHECK(!memcmp(cf_usb_mock_dma_tx[2],cf_usb_mock_dma_capture,cf_usb_mock_dma_size[2]),"armed DMA buffer not overwritten");
            unsigned n=cf_usb_mock_dma_size[2]/4;
            const int16_t *p=(const int16_t *)cf_usb_mock_dma_capture;
            for(unsigned i=0;i<n;i++) {
                nonzero+=p[2*i]!=0||p[2*i+1]!=0;
                stereo+=p[2*i]!=p[2*i+1];
                if(ms>=9500)mute_nonzero+=p[2*i]!=0||p[2*i+1]!=0;
            }
            fwrite(p,4,n,f);frames+=n;host_take(ms);
            next_usb+=24000;
        }
    }
    wav_close(f,frames);
    CHECK(nonzero>10000,"USB recording contains the actual synth sound");
    CHECK(!mute_nonzero,"MASTER zero makes USB silent after the existing output ramp/tail");
    CHECK(!ua.cap_underruns&&!ua.cap_overruns,"no ring loss with actual I2S divider and independent USB clock");
    CHECK(!sqp.on&&!sqp.n&&!sqp.nb&&!sqp.ln,"external Stop releases sequence voices during capture");
    CHECK(mi_r==mi_w&&!midi_in_overflow,"MIDI input was consumed without overflow");
    double expected=24000000.0/544.0*10*(rate==48000?160.0/147.0:1);
    CHECK(fabs(frames-expected)<256,"capture duration follows producer clock within startup latency");
    /* The diagnostic must remain safe during a recording, unlike LIST which flushes edits. */
    uint8_t stream_before[sizeof ua];memcpy(stream_before,&ua,sizeof ua);
    unsigned erases_before=st_erases;
    const uint8_t query[]={0x7d,0x46,0x4c,CRB_USB_AUDIO};
    CHECK(crb_handle(query,sizeof query),"USB diagnostic command answered");
    CHECK(crb_n==143 && CRB_OUT[5]==1 && CRB_OUT[6]==27,"USB diagnostic schema and length");
    CHECK(!memcmp(stream_before,&ua,sizeof ua) && st_erases==erases_before,"diagnostic leaves stream and flash untouched");
    char stats_path[1024];snprintf(stats_path,sizeof stats_path,"%s.stats",argv[3]);
    FILE *stats=fopen(stats_path,"wb");CHECK(stats!=NULL,"diagnostic proof file opens");
    if(stats) { fwrite(CRB_OUT+5,1,crb_n-6,stats);fclose(stats); }
    printf("USB MIX preset=%u rate=%u frames=%u signal=%u stereo=%u ring=%u underrun=%u overrun=%u checks=%u failures=%u\n",
        preset,rate,frames,nonzero,stereo,ua.cw-ua.cr,ua.cap_underruns,ua.cap_overruns,checks,failed);
    return failed?1:0;
}
