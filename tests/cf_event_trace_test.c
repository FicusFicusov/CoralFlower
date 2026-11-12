/* SPDX-License-Identifier: GPL-3.0-only */
/* Real MODE gesture, autosave and stereo PCM with diagnostic readback.
 * Hardware/USB scheduling is modeled; this is instrumentation validation. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
static unsigned checks, failed;
#define CHECK(c,m) do { checks++; if (!(c)) { failed++; fprintf(stderr,"FAIL %s:%d %s\n",__FILE__,__LINE__,m); } } while (0)
#define CF_USB_MOCK_CHECK(c,m) CHECK(c,m)
#include "cf_usb_mock.h"
#define FELUCCA_UAC 1
#define FELUCCA_CDC 1
#define FM1_TICKS_PER_US 1u
#include "../tools/emu/web/emu_web.c"
static void ua_service_paced(uint32_t t, int isr) { (void)t; (void)isr; ua_service(); }
static FILE *wav;
static unsigned frames, usb_on;
static void w32(FILE *f, uint32_t n) { fwrite(&n,4,1,f); }
static void w16(FILE *f, uint16_t n) { fwrite(&n,2,1,f); }
static void header(unsigned n) {
    fwrite("RIFF",1,4,wav); w32(wav,36+n*4); fwrite("WAVEfmt ",1,8,wav); w32(wav,16);
    w16(wav,1);w16(wav,2);w32(wav,48000);w32(wav,192000);w16(wav,4);w16(wav,16);
    fwrite("data",1,4,wav);w32(wav,n*4);
}
static void pump(unsigned ms) {
    while (ms--) {
        web_step(1);
        while (web_avail() >= 128) web_render(128);
        while (web_midi_out_take()) ;
        if (usb_on) {
            CHECK(cf_usb_mock_reg_ep[2][1]&1u,"modeled host has an armed packet");
            if (wav) {
                fwrite(cf_usb_mock_dma_capture,1,cf_usb_mock_dma_size[2],wav);
                frames += cf_usb_mock_dma_size[2]/4;
            }
            unsigned frame = web_ms() & 2047u;
            cf_usb_mock_reg_global[S_FRAME1] = frame & 255u;
            cf_usb_mock_reg_global[S_FRAME2] = frame >> 8;
            cf_usb_mock_reg_ep[2][1] &= ~1u;
            ua_service();
        }
    }
}
static void tap(unsigned b) { web_buttons(1u<<b);pump(40);web_buttons(0);pump(40); }
#if CF_EVENT_DIAG
static unsigned events(unsigned lane, unsigned kind) {
    unsigned n=0;
    for(unsigned i=0;i<cf_diag.n[lane];i++) n += (cf_diag.events[cf_diag_base(lane)+i].tag&65535u)==kind;
    return n;
}
static void pages(const char *path) {
    FILE *f=fopen(path,"wb"); CHECK(f!=NULL,"page evidence file opens"); if(!f)return;
    uint8_t stream[sizeof ua], saved[sizeof cs], capture[sizeof cf_diag];
    memcpy(stream,&ua,sizeof ua);memcpy(saved,&cs,sizeof cs);memcpy(capture,&cf_diag,sizeof cf_diag);
    unsigned writes=emu_flash_writes;
    for(unsigned lane=0;lane<3;lane++) for(unsigned off=0;off<cf_diag.n[lane];off+=8) {
        uint8_t query[]={0x7d,0x46,0x4c,CRB_EVENT_TRACE,lane,off&127u,off>>7,0,0,0};
        CHECK(crb_handle(query,sizeof query),"trace command responds");
        CHECK(CRB_OUT[5]==1&&CRB_OUT[6]==lane&&CRB_OUT[7]==19,"trace schema/lane/metadata count");
        CHECK(crb_n<=400,"bounded SysEx response");
        w32(f,crb_n-6);fwrite(CRB_OUT+5,1,crb_n-6,f);
    }
    CHECK(!memcmp(stream,&ua,sizeof ua)&&!memcmp(saved,&cs,sizeof cs),"trace read leaves stream/settings untouched");
    CHECK(!memcmp(capture,&cf_diag,sizeof cf_diag)&&emu_flash_writes==writes,"trace read neither clears capture nor writes flash");
    uint8_t bad[]={0x7d,0x46,0x4c,CRB_EVENT_TRACE,3,0,0,0,0,0};
    CHECK(!crb_handle(bad,sizeof bad),"invalid lane rejected");
    bad[4]=0;bad[9]=16;CHECK(!crb_handle(bad,sizeof bad),"overflowing cursor rejected");
    fclose(f);
}
#endif
int main(int argc,char **argv) {
    if(argc!=4)return 2;
    unsigned preset=atoi(argv[1]);
    web_boot();pump(2200);web_enc(EMU_E_PRESETS,preset);pump(250);
    tap(EMU_B_ARP);web_enc(EMU_E_K1,1);pump(40);
    CHECK(cu.scr==SCR_ARP&&cu.page[SCR_ARP]==0&&cu.arp_on,"actual ARP Pattern screen and on state");
    web_keys((1u<<7)|(1u<<11)|(1u<<14));pump(200);
    CHECK(cfa_k.n>=3,"actual held triad reaches live ARP");
    cf_usb_mock_attach(ep0buf);usb.up=usb.config=1;usb_cdc_on=0;
    ua_reset();ua_cfg_build();ua_rate_set(48000);ua_set_interface(UA_IF_CAP,1);usb_on=1;pump(30);
    wav=fopen(argv[2],"wb");if(!wav)return 3;header(0);
    unsigned writes=emu_flash_writes;
    web_enc(EMU_E_K2,1);pump(10200);
    CHECK(cs.arp.mode==1&&cfp_cfg.arp.mode==1,"MODE change is selected and applied");
    CHECK(emu_flash_writes>writes&&cfs_saved==cfs_fnv(&cs,sizeof cs),"actual settings autosave completed");
    CHECK(cfa_k.n>=3&&cfa_k.steps>30,"held ARP continues after autosave (default 100 BPM eighth notes)");
    fseek(wav,0,SEEK_SET);header(frames);fclose(wav);wav=NULL;
#if CF_EVENT_DIAG
    CHECK(cf_diag.generation==1&&!cf_diag.active,"one actual MODE gesture arms and freezes ten-second capture");
    CHECK(events(CFD_MAIN,CFD_MODE)==1&&events(CFD_AUDIO,CFD_CONFIG)>=1,"UI/audio handoff is observable");
    CHECK(events(CFD_MAIN,CFD_SAVE_BEGIN)==1&&events(CFD_MAIN,CFD_SAVE_END)==1,"save has dated begin and end");
    CHECK(events(CFD_USB,CFD_PCM)>=35,"PCM anchors cover the window");
    CHECK(!cf_diag.dropped[0]&&!cf_diag.dropped[1]&&!cf_diag.dropped[2],"normal repro fits all diagnostic lanes");
    pages(argv[3]);
    /* Wrap, saturation and separate producers: no interruption of the caller's
     * existing IRQ state by an emitter. The host lock is recursive. */
    cf_diag_start(1,2,fm1_ms);cf_diag.t0=UINT32_MAX-99u;emu_sim_us=150;
    fm1_irq_off();unsigned depth=emu_cpu_depth;
    CFD_EVENT(CFD_AUDIO,CFD_CONFIG,0,1,2);
    CHECK(emu_cpu_depth==depth,"emitter preserves an already-disabled IRQ context");fm1_irq_on();
    CHECK(cf_diag.events[64].ticks==250u,"event clock handles TIMER4 wrap");
    for(unsigned i=0;i<129;i++)CFD_EVENT(CFD_AUDIO,CFD_ARP_NOTE,60,i,0);
    CHECK(cf_diag.n[CFD_AUDIO]==128&&cf_diag.dropped[CFD_AUDIO]==2,"saturation keeps earliest evidence and counts omissions");
    CFD_EVENT(CFD_USB,CFD_USB_RING,1,2,3);
    CHECK(cf_diag.n[CFD_USB]==1&&cf_diag.events[192].a==2,"full audio lane does not consume USB capacity");
    cf_diag_start(2,3,fm1_ms);
    CHECK(cf_diag.generation==3&&cf_diag.n[CFD_AUDIO]==0&&!cf_diag.dropped[CFD_AUDIO],"next MODE creates a clean generation");
#endif
    printf("EVENT TRACE preset=%u enabled=%u frames=%u checks=%u failures=%u\n",preset,CF_EVENT_DIAG,frames,checks,failed);
    return failed?1:0;
}
