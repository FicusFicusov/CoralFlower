/* SPDX-License-Identifier: GPL-3.0-only */
/* Full CoralFlower audio + real autosave/SAVE/tidy, production storage coordinator
 * and NOR transaction algorithm, against independent USB SOF / audio clocks.
 * Command/SFC/IRQ timing is modeled: 2..4 us SPI bytes, 80% audio render load.
 * A target measurement is still required. Args: preset rate chip wav [spi_us]. */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
static unsigned checks, failed;
#define CHECK(c,m) do { checks++; if (!(c)) { failed++; if(failed<24)fprintf(stderr,"FAIL %s:%d %s\n",__FILE__,__LINE__,m); } } while(0)
static void test_clock_advance(uint64_t end);
#define FNOR_ADVANCE(t) test_clock_advance(t)
#include "cf_flash_model.h"
#define CF_USB_MOCK_CHECK(c,m) CHECK(c,m)
#include "cf_usb_mock.h"
static int test_flash_erase(uint32_t off);
static int test_flash_program(uint32_t off,const void *src,uint32_t n);
static int test_flash_read(uint32_t off,void *dst,uint32_t n);
#define EMU_FLASH_READ test_flash_read
#define EMU_FLASH_ERASE test_flash_erase
#define EMU_FLASH_PROGRAM test_flash_program
#define FELUCCA_UAC 1
#define FELUCCA_CDC 1
#define CF_FLASH_COOP 1
#define ST_ASYNC_IO 1
static void test_flash_wait(void);
#define ST_ASYNC_WAIT() test_flash_wait()
#include "../tools/emu/web/emu_web.c"

static int sim_on, sim_irq_off, sim_audio, pcm_started, source_invalid;
static uint64_t next_sof,next_timer,next_audio,next_midi;
static unsigned sim_base,rate,packets,missing,analog_late,midi_clocks;
static unsigned render_us=2300,frames,ledger_n,ledger_r;
static int16_t ledger[4*48000*2]; /* max four seconds of unconsumed source; compacted after packets */
static FILE *wave;
static uint64_t seq_edit_at,seq_release_at;
static unsigned seq_edit_done;
static unsigned input_on,input_step,input_checks,input_pending,input_notes;
static uint64_t input_next,input_due;
#ifndef CF_FLASH_INPUT_HOOK
#define CF_FLASH_INPUT_HOOK() ((void)0)
#endif
static void input_schedule(void) {
    if (!sim_irq_off) CF_FLASH_INPUT_HOOK();
    if(!sim_irq_off&&seq_edit_at&&fnor.ticks>=seq_edit_at) {
        web_keys(1u);seq_edit_at=0;seq_edit_done++;
    }
    if(!sim_irq_off&&seq_release_at&&fnor.ticks>=seq_release_at) {
        web_keys(0);seq_release_at=0;
    }
    if(!input_on||sim_irq_off)return;
    if(input_pending&&fnor.ticks>=input_due) {
        CHECK(cu.pkeys==emu_hal.keys,"physical release/new chord reaches UI during save");
        CHECK(!!cu.xnote[60]==!!(input_notes&1)&&!!cu.xnote[64]==!!(input_notes&2),"MIDI Roots/FM-1 note changes reach player during save");
        CHECK(!!cfa_k.n==!!(emu_hal.keys||input_notes),"live ARP chord/release reaches audio during save");
        CHECK(cf_xq_w==cf_xq_r,"musical input queue drains inside save");
        input_pending=0;input_checks++;
    }
    if(fnor.ticks<input_next)return;
    unsigned phase=input_step++%6u;
    if(phase<3)web_keys(phase==1?1u<<9:0u);
    if(phase==3){web_midi_in(9u|0x90u<<8|60u<<16|100u<<24);input_notes=1;}
    if(phase==4){web_midi_in(8u|0x80u<<8|60u<<16);web_midi_in(9u|0x90u<<8|64u<<16|100u<<24);input_notes=2;}
    if(phase==5){web_midi_in(8u|0x80u<<8|64u<<16);input_notes=0;}
    input_pending=1;input_due=fnor.ticks+60000u*24u;input_next=fnor.ticks+100000u*24u;
}
static void ua_service_paced(uint32_t t,int isr) { (void)t;(void)isr;ua_service(); }
static void advance_us(unsigned us) { for(unsigned i=0;i<us;i++)fnor_advance(24); }
static uint32_t test_irq_save(void) { CHECK(!sim_irq_off,"storage is not called from ISR");sim_irq_off=1;return 0; }
static void test_irq_restore(uint32_t f) { (void)f;CHECK(!fnor.busy,"SFC/IRQ restored only with readable flash");sim_irq_off=0;test_clock_advance(fnor.ticks); }
static uint32_t test_ticks(void) { fnor_advance(24);return (uint32_t)fnor.ticks; }
static int fl_read_slice_ram(uint32_t off,void *dst,uint32_t n) {
    CHECK(!fnor.busy&&!fnor.suspended,"main-loop data reads never overlap write");
    if(off>sizeof emu_flash||n>sizeof emu_flash-off)return -8;
    return fls_read_bytes(off,dst,n);
}
#define CF_FLASH_COOP 1
#define FL_FAR(f) (f)
#define FL_STORE_OK(a,n) (((a)>=0x97000u && (a)<=0xE0000u && (n)<=0xE0000u-(a)) || ((a)>=0xFC000u && (a)<=0xFF000u && (n)<=0xFF000u-(a)))
#define irq_save test_irq_save
#define irq_restore test_irq_restore
#define fm1_ticks test_ticks
#undef FM1_TICKS_PER_US
#define FM1_TICKS_PER_US 24u
#define flash_ok hw_flash_ok
#undef st_erases
#define st_erases hw_st_erases
#define st_read hw_st_read
#define st_prog hw_st_prog
#define st_erase hw_st_erase
#define ST_FLASH_PUMP() cf_flash_input()
#define st_io_begin hw_st_io_begin
#define st_io_poll hw_st_io_poll
#define st_io_result hw_st_io_result
#undef ST_ASYNC_WAIT
#include "../firmware/src/storage_hw.c"
#undef st_io_begin
#undef st_io_poll
#undef st_io_result
#undef st_read
#undef st_prog
#undef st_erase
#undef flash_ok
#undef fm1_ticks
#undef irq_save
#undef irq_restore
#undef FM1_TICKS_PER_US
#define FM1_TICKS_PER_US 1u

static int test_immediate_result;
static int st_io_begin(uint32_t off, const uint8_t *src, uint32_t n) {
    if(sim_on)return hw_st_io_begin(off,src,n);
    test_immediate_result=n?test_flash_program(off,src,n):test_flash_erase(off);
    return test_immediate_result;
}
static int st_io_poll(void) { return sim_on?hw_st_io_poll():test_immediate_result; }
static void test_flash_wait(void) { if(sim_on)(void)st_io_finish(); }

#ifdef CF_FLASH_LEGACY
/* Extracted unchanged fl_wait_ready/fl_prog_ram/fl_erase4k_ram from rc3. */
typedef int fl_saved_t;
static int fl_enter(fl_saved_t *s) { *s=0;return 0; }
static void fl_exit(const fl_saved_t *s) { (void)s;CHECK(!fnor.busy,"legacy returns ready"); }
static void fl_inval(uint32_t off,uint32_t n) { (void)off;(void)n; }
#define RAMINL static inline
#define RAMFN
#define FL_RANGE_OK FL_STORE_OK
#define T4_CNT fls_ticks()
#define fl_cs fls_cs
#define fl_tx fls_tx
#define fl_rx fls_rx
static void fl_wren(void) { fls_command(6); }
static uint8_t fl_rdsr(uint8_t cmd) { uint8_t v;fls_cs(0);fls_tx(cmd);v=fls_rx();fls_cs(1);return v; }
static void fl_addr(uint32_t a) { fls_tx(a>>16);fls_tx(a>>8);fls_tx(a); }
#include "../build/usb-flash-20261112/legacy-ram.h"
#endif

static int test_flash_erase(uint32_t off) {
    if(!sim_on) { memset(emu_flash+off,255,4096);emu_flash_sync(off,4096);return 0; }
#ifdef CF_FLASH_LEGACY
    uint32_t took,f=test_irq_save();int rc=fl_erase4k_ram(off,&took);test_irq_restore(f);return rc;
#else
    return hw_st_erase(off);
#endif
}
static int test_flash_read(uint32_t off,void *dst,uint32_t n) {
    if(!sim_on) { memcpy(dst,emu_flash+off,n);return 0; }
    return hw_st_read(off,dst,n);
}
static int test_flash_program(uint32_t off,const void *src,uint32_t n) {
    if(!sim_on) { for(unsigned i=0;i<n;i++)emu_flash[off+i]&=((const uint8_t*)src)[i];emu_flash_sync(off,n);return 0; }
#ifdef CF_FLASH_LEGACY
    uint32_t took,f=test_irq_save();int rc=fl_prog_ram(off,src,n,&took);test_irq_restore(f);return rc;
#else
    return hw_st_prog(off,src,n);
#endif
}
static void host_take(void) {
    packets++;
    if(!(cf_usb_mock_reg_ep[2][1]&1u)) { missing++;CHECK(0,"USB host found no packet at 1ms deadline");return; }
    CHECK(!memcmp(cf_usb_mock_dma_tx[2],cf_usb_mock_dma_capture,cf_usb_mock_dma_size[2]),"DMA payload is stable until taken");
    const int16_t *p=(const int16_t*)cf_usb_mock_dma_capture;
    unsigned n=cf_usb_mock_dma_size[2]/4u,nonzero=0;
    for(unsigned i=0;i<n*2;i++)nonzero|=p[i]!=0;
    /* Initial ring priming is the protocol's preexisting startup silence.
     * After first source PCM, every frame must equal the original source FIFO. */
    if(!source_invalid&&(nonzero||pcm_started)) {
        pcm_started=1;
        CHECK(ledger_r+n<=ledger_n,"USB cannot invent frames beyond produced audio");
        if(ledger_r+n<=ledger_n) {
            CHECK(!memcmp(p,ledger+2*ledger_r,4*n),"no duplicated/substituted/silent PCM in place of source");
            ledger_r+=n;
        } else source_invalid=1;
    }
    fwrite(p,4,n,wave);frames+=n;
    cf_usb_mock_reg_ep[2][1]&=(uint8_t)~1u;
    if(ledger_r>48000) {
        memmove(ledger,ledger+2*ledger_r,4*(ledger_n-ledger_r));ledger_n-=ledger_r;ledger_r=0;
    }
}
static void test_clock_advance(uint64_t end) {
    if(!sim_on)return;
    emu_sim_us=sim_base+(uint32_t)(end/24);fm1_ms=emu_sim_us/1000;
    while(end>=next_sof) {
        unsigned f=(unsigned)(next_sof/24000)&2047u;
        cf_usb_mock_reg_global[S_FRAME1]=f&255;cf_usb_mock_reg_global[S_FRAME2]=f>>8;
        host_take();next_sof+=24000;
    }
    while(end>=next_midi) {
        CHECK(web_midi_in(15u|0xf800u),"MIDI Clock continues to enter while saving");
        midi_clocks++;next_midi+=500000; /*120 BPM at24MHz*/
    }
    if(sim_irq_off)return;
    if(end>=next_timer) { next_timer=end+2400;ua_service();input_schedule();emu_fw_tick(fm1_ms); }
    if(!sim_audio&&end>=next_audio) {
        if(end-next_audio>=128u*544u)analog_late++;
        next_audio+=128u*544u;
        sim_audio=1;
        for(unsigned b=0;b<128;b+=CTL) {
            int32_t out[2*CTL];unsigned at=ua.cw;
            audio_block(out,CTL);
            unsigned n=ua.cw-at;
            if(n>CTL*2u) {
                CHECK(0,"producer reset during render lost continuous source PCM");
                source_invalid=1;n=0; /* report broken stream without overflowing the test's own arrays */
            }
            CHECK(ledger_n+n<sizeof ledger/4,"source FIFO capacity");
            for(unsigned i=0;i<n;i++)for(unsigned c=0;c<2;c++) {
                int16_t pcm=ua.cap[((at+i)&(UA_CAP_RING-1))*2+c];
                if(rate==44100)CHECK(pcm==(int16_t)(out[2*i+c]>>8),"captured source equals real final DAC including FX/MASTER");
                if(ledger_n+n<sizeof ledger/4)ledger[2*(ledger_n+i)+c]=pcm;
            }
            ledger_n+=n;advance_us(render_us/4); /* timer nests in modeled render load */
        }
        audio_halves++;sim_audio=0;
        /* Model runtime timing remains explicit; do not pretend host execution cost is target cost. */
        felucca_dbg.last_us=render_us;
    }
}
static void flash_run_ms(unsigned ms,int poll) {
    uint64_t until=fnor.ticks+(uint64_t)ms*24000,last=0;
    while(fnor.ticks<until) {
        advance_us(100);
        cf_flash_input(); st_async_poll(); /* advance autosave between the modeled main-loop iterations */
        if(poll&&fnor.ticks-last>15000u*24u) {
            last=fnor.ticks;cu.now=fm1_ms;settings_poll();
        }
    }
}
static void sequence_save_inputs(void) {
    static sq_store_t disk;
    sim_on=0;
    cu.scr=SCR_SQED;squ.pat=1;squ.trk=SQ_DRUMS;squ.lane=squ.bar=0;
    sq_fresh(&sq_st.pat[1],4);sq_save_write();
    sq_st.pat[1].hit[0][0]=2;sq_dirty=1;
    sim_on=1;seq_edit_at=fnor.ticks+100000u*24u;seq_release_at=seq_edit_at+50000u*24u;
    sq_save_write();
    CHECK(seq_edit_done==1&&sq_st.pat[1].hit[0][0]==3,"drum key edits live RAM during sequence save");
    CHECK(sq_dirty,"edit received during save remains pending");
    for(unsigned k=0;k<SQ_NCHUNK&&sq_chunk_len(k);k++)
        CHECK(st_load(OBJ_CFSEQ0+k,(uint8_t*)&disk+k*SQ_CHUNK,sq_chunk_len(k))==(int)sq_chunk_len(k),"saved sequence chunk reads back");
    CHECK(disk.pat[1].hit[0][0]==2,"save holds a stable snapshot across pattern/chunk boundaries");
    CHECK(disk.check==cfs_fnv(&disk.song,sizeof disk-__builtin_offsetof(sq_store_t,song)),"stored sequence has a consistent global checksum");
    sq_save_write();
    CHECK(!sq_dirty,"following save retains the new key edit");
    for(unsigned k=0;k<SQ_NCHUNK&&sq_chunk_len(k);k++)
        CHECK(st_load(OBJ_CFSEQ0+k,(uint8_t*)&disk+k*SQ_CHUNK,sq_chunk_len(k))==(int)sq_chunk_len(k),"updated sequence chunk reads back");
    CHECK(disk.pat[1].hit[0][0]==3&&disk.check==cfs_fnv(&disk.song,sizeof disk-__builtin_offsetof(sq_store_t,song)),"next snapshot preserves edit and checksum");
    printf("SEQUENCE INPUT injected=%u stored_first=2 stored_next=%u dirty=%u\n",seq_edit_done,disk.pat[1].hit[0][0],sq_dirty);
    cu.scr=SCR_HOME;cf_send_config();
}
static void backup_roundtrip(void) {
    uint32_t sizes[CRB_N],crc[CRB_N];uint8_t saved[ST_PAYLOAD_MAX],query[12]={0x7d,0x46,0x4c,CRB_LIST};
    CHECK(crb_handle(query,4)&&CRB_OUT[5]==1&&CRB_OUT[6]==0&&CRB_OUT[7]==CRB_N,"real backup LIST succeeds during capture");
    for(unsigned i=0;i<CRB_N;i++) {
        CHECK(CRB_OUT[8+i*11]==CRB_IDS[i],"backup list IDs preserve schema");
        sizes[i]=crb_r32(CRB_OUT+9+i*11);crc[i]=crb_r32(CRB_OUT+14+i*11);
    }
    unsigned present=0;
    for(unsigned i=0;i<CRB_N;i++)if(sizes[i]) {
        present++;CHECK(sizes[i]<=sizeof saved,"backup object fits buffer");
        if(sizes[i]>sizeof saved)continue;
        for(unsigned off=0;off<sizes[i];) {
            unsigned n=sizes[i]-off;if(n>256)n=256;
            query[3]=CRB_GET;query[4]=CRB_IDS[i];
            for(unsigned j=0;j<5;j++)query[5+j]=(off>>(7*j))&127;
            query[10]=n&127;query[11]=n>>7;
            CHECK(crb_handle(query,12)&&CRB_OUT[6]==0&&crb_r32(CRB_OUT+7)==off,"real backup GET accepts offset");
            CHECK(crb_unpack(CRB_OUT+14,crb_n-15,saved+off,n)==n,"backup payload decodes exact byte count");
            off+=n;
        }
        CHECK(st_crc32(saved,sizes[i])==crc[i],"GET payload matches LIST checksum");
    }
    CHECK(present>=8,"backup includes settings, sound and sequence objects");
    printf("BACKUP LIST/GET objects=%u checksums=checked\n",present);
}
static void wav_header(unsigned n) {
    uint32_t u;uint16_t s;
    fwrite("RIFF",1,4,wave);u=36+4*n;fwrite(&u,4,1,wave);fwrite("WAVEfmt ",1,8,wave);
    u=16;fwrite(&u,4,1,wave);s=1;fwrite(&s,2,1,wave);s=2;fwrite(&s,2,1,wave);
    u=rate;fwrite(&u,4,1,wave);u=rate*4;fwrite(&u,4,1,wave);s=4;fwrite(&s,2,1,wave);s=16;fwrite(&s,2,1,wave);
    fwrite("data",1,4,wave);u=n*4;fwrite(&u,4,1,wave);
}
#ifndef CF_FLASH_AUDIO_HELPERS_ONLY
int main(int argc,char **argv) {
    if(argc<5||argc>6)return 2;
    unsigned preset=atoi(argv[1]),chip=atoi(argv[3]);rate=atoi(argv[2]);
    web_boot();web_step(5000);web_enc(EMU_E_PRESETS,preset);web_step(1500);
    CHECK(cf_snd[0].engine==(preset?ENGI_VA:ENGI_FM6),"correct FM/VA code path selected");
    cs.fx.on=(1u<<CFX_N)-1;cf_fx_apply(&cs.fx);cs.clock_in=1;cs.bass_on=1;cs.arp.rate=6;
    cu.arp_on=1;cf_send_config();web_keys(1u<<7);web_step(600);
    CHECK(cfa_k.n>0&&!cfm.running,"manual ARP sounds during Clock In pause");
    wave=fopen(argv[4],"wb");if(!wave)return 3;wav_header(0);
    fnor_reset(emu_flash,chip,1);fnor.byte_us=argc==6?atoi(argv[5]):2;
    memset(&st_flash,0,sizeof st_flash);st_flash.jedec=chip?0xB36014u:0x856014u;hw_flash_ok=st_runtime=1;
    sim_base=fm1_ms*1000;next_sof=24000;next_timer=2400;next_audio=128u*544u;next_midi=500000;
    cf_usb_mock_attach(ep0buf);usb.up=usb.config=1;usb_cdc_on=0;
    ua_reset();ua_cfg_build();ua_rate_set(rate);ua_set_interface(UA_IF_CAP,1);ua_service();sim_on=1;
    flash_run_ms(300,0);
    unsigned programs=fnor.programs;cs.bpm++;cf_send_config();flash_run_ms(750,1);
    CHECK(fnor.programs>programs&&cfs_saved==cfs_fnv(&cs,sizeof cs),"real 500ms autosave persists while sound and capture continue");
    /* Exhaust the settings log with actual valid whole records (not direct
     * memory corruption). The next timed autosave must erase while ARP plays. */
    /* Prepare two full valid logs using the normal store, without charging
     * fixture construction as measured save time. */
    sim_on=0;
    for(unsigned i=0;i<2*st_ln(CF_SET_OBJ);i++)CHECK(cf_settings_write(ST_WHOLE)==1,"fill valid log");
    st_hdr_t full_header;int full_copy=st_current(CF_SET_OBJ,&full_header);
    for(unsigned i=0;i<st_ln(CF_SET_OBJ)&&st_cur_e+1u<st_ln(CF_SET_OBJ);i++) {
        CHECK(cf_settings_write(ST_WHOLE)==1,"consume remaining log entries");
        full_copy=st_current(CF_SET_OBJ,&full_header);
    }
    CHECK(full_copy>=0&&st_cur_e+1u==st_ln(CF_SET_OBJ)&&cfa_k.n,"full-log fixture holds a live chord");
    sim_on=1;cs.notes_in=CF_NI_ROOTS;cf_send_config();input_on=1;input_next=fnor.ticks+24000;
    unsigned full_erases=fnor.erases;cs.bpm=137;cf_send_config();flash_run_ms(750,1);
    /* The old synchronous poll overshot the 750ms loop deadline by a whole
     * save. Await its asynchronous completion explicitly, retaining capture
     * and the held/updated chord throughout; this is not a latency oracle. */
    for(unsigned i=0;st_async_busy()&&i<5000;i++)flash_run_ms(1,0);
    CHECK(fnor.erases>full_erases&&cfs_saved==cfs_fnv(&cs,sizeof cs),"full-log autosave erases and commits without waiting for DAW silence");
    unsigned saved_bpm=cs.bpm;
    programs=fnor.programs;
    CHECK(cf_snd_save(0,1,"Flash test")==0,"real SAVE callback succeeds during audio capture");
    CHECK(input_checks>64,"more than one MIDI queue capacity processed inside saves");
    input_on=0;input_pending=0;web_keys(0);web_midi_in(8u|0x80u<<8|60u<<16);web_midi_in(8u|0x80u<<8|64u<<16);
    emu_fw_tick(fm1_ms);cf_ui_pump();flash_run_ms(100,0);
    cs.notes_in=CF_NI_KEYS;cf_send_config();input_on=1;input_next=fnor.ticks+24000;input_step=input_notes=0;
    CHECK(cf_snd_save(0,1,"Flash keys")==0,"SAVE handles FM-1 mapped MIDI notes");
    input_on=0;input_pending=0;web_keys(0);web_midi_in(8u|0x80u<<8|60u<<16);web_midi_in(8u|0x80u<<8|64u<<16);
    emu_fw_tick(fm1_ms);cf_ui_pump();flash_run_ms(100,0);cf_ui_pump();
    CHECK(fnor.programs>programs,"SAVE actually programs user sound/patch");
    sequence_save_inputs();
    flash_run_ms(200,0);
    /* Stop sounding, retain the open DAW stream, then let normal quiet tidy run. */
    web_keys(0);emu_fw_tick(fm1_ms);emu_fw_idle();flash_run_ms(3500,1);
    unsigned erases=fnor.erases;
    cs.bpm++;cf_send_config();flash_run_ms(3500,1);
    CHECK(fnor.erases>erases,"real quiet autosave cleanup performs sector erase");
    saved_bpm=cs.bpm;
    backup_roundtrip();
    CHECK(!st_flash.fault,"no flash fault");
    CHECK(!source_invalid,"source PCM remained continuous and comparable");
    CHECK(!missing,"one newly armed packet for every USB frame during saves");
    CHECK(!ua.cap_underruns&&!ua.cap_overruns,"no synthesized audio lost in ring during saves");
    CHECK(!analog_late,"audio service never falls a complete DMA half behind");
    CHECK(!fnor.bad_progress,"both NOR minimum progress timings observed");
    CHECK(!st_flash.over_budget,"modeled blackout stays below450us");
    /* Stop the test's producer, then allow the next normal audio ISR to drain
     * its final packet. Sampling an active producer between ISR boundaries
     * can legitimately find one just-arrived Clock in the queue. */
    printf("MIDI final_before_drain queued=%u overflow=%u received=%u\n",mi_w-mi_r,midi_in_overflow,midi_clocks);
    next_midi=UINT64_MAX;
    flash_run_ms(4,0);
    CHECK(midi_clocks>200&&mi_r==mi_w&&!midi_in_overflow,"parallel MIDI received and consumed");
    CHECK(!cfm.running&&!sqp.on&&!dmp.on,"clock without transport cannot start stored playback");
    unsigned commands=fnor.commands,packets_before=ua.tx_packets;
    const uint8_t query[]={0x7d,0x46,0x4c,CRB_FLASH_AUDIO};
    CHECK(crb_handle(query,sizeof query)&&crb_n==58&&CRB_OUT[5]==1&&CRB_OUT[6]==10,"flash diagnostic schema is returned");
    CHECK(fnor.commands==commands&&ua.tx_packets==packets_before,"flash diagnostic is read-only");
    char stats_path[1024];snprintf(stats_path,sizeof stats_path,"%s.flash-stats",argv[4]);
    FILE *stats=fopen(stats_path,"wb");CHECK(stats!=NULL,"diagnostic evidence opens");
    if(stats){fwrite(CRB_OUT+5,1,crb_n-6,stats);fclose(stats);}
    sim_on=0;
    CHECK(cf_settings_load()&&cs.bpm==saved_bpm,"settings reload returns final saved BPM");
    CHECK(up_used(cf_snd[0].slot),"saved sound slot remains present");
    fseek(wave,0,SEEK_SET);wav_header(frames);fclose(wave);
    printf("LIVE INPUT checkpoints=%u modes=Roots,FM-1-keys physical=release,new-chord\n",input_checks);
    printf("FLASH AUDIO preset=%u rate=%u chip=%s spi_us=%u load_us=%u programs=%u erases=%u suspends=%u packets=%u missing=%u underrun=%u overrun=%u analog_late=%u max_off_us=%u frames=%u checks=%u failures=%u\n",preset,rate,chip?"UCUN":"Puya",fnor.byte_us,render_us,fnor.programs,fnor.erases,fnor.suspends,packets,missing,ua.cap_underruns,ua.cap_overruns,analog_late,st_flash.max_off_us,frames,checks,failed);
    return failed?1:0;
}

#endif
