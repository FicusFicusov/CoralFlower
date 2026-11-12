/* SPDX-License-Identifier: GPL-3.0-only */
/* Real storage_hw.c + storage.c and the production RAM transaction algorithm.
 * NOR/SFC/IRQ are models; this cannot establish physical bus timing or boot ROM behavior.
 * cc -std=c11 -O1 -g -fsanitize=address,undefined tests/cf_flash_test.c -o /tmp/cf_flash_test */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
static unsigned checks, failed;
#define CHECK(c,m) do { checks++; if (!(c)) { failed++; if(failed<20) fprintf(stderr,"FAIL %s:%d %s\n",__FILE__,__LINE__,m); } } while(0)
#include "cf_flash_model.h"
static uint8_t nor[0x100000], snapshot[0x100000];
static uint32_t abuf[256], st_holes, fm1_ms, audio_halves;
static int irq_off;
static uint32_t irq_save(void) { CHECK(!irq_off,"storage starts outside IRQ"); irq_off=1; return 0; }
static void irq_restore(uint32_t f) { (void)f; CHECK(!fnor.busy,"IRQs require readable flash");irq_off=0; }
static uint32_t fm1_ticks(void) { if(!irq_off)audio_halves++; return fls_ticks(); }
static void fm1_wdt_feed(void) {}
static int audio_loud(void) { return 1; }
static int fl_read_slice_ram(uint32_t off,void *dst,uint32_t n) {
    CHECK(!fnor.busy&&!fnor.suspended,"no data reads until full transaction completed");
    if(off>sizeof nor||n>sizeof nor-off)return -8;
    return fls_read_bytes(off,dst,n);
}
#define CF_FLASH_COOP 1
#define FM1_TICKS_PER_US 24u
#define FL_FAR(f) (f)
#define FL_STORE_OK(a,n) (((a)>=0x97000u && (a)<=0xE0000u && (n)<=0xE0000u-(a)) || ((a)>=0xFC000u && (a)<=0xFF000u && (n)<=0xFF000u-(a)))
#ifdef __APPLE__
#define __attribute__(x)
#endif
#define ST_LOG_OBJ (OBJ_PROJECT0+3u)
#define CF_LOOP_STORE 1
#include "../firmware/src/storage_hw.c"
#include "../firmware/src/storage.c"

static void start(unsigned chip,unsigned swap) {
    fnor_reset(nor,chip,swap);memset(&st_flash,0,sizeof st_flash);
    memset(st_blank,0,sizeof st_blank);flash_ok=st_runtime=1;irq_off=0;
#ifdef ST_ASYNC
    memset(&st_async,0,sizeof st_async);memset(&st_io_job,0,sizeof st_io_job);
    st_io_active=st_io_result=0;
#endif
}
static void transactions(unsigned chip,unsigned swap) {
    uint8_t data[256];for(unsigned i=0;i<256;i++)data[i]=(uint8_t)(i^0xA5);
    start(chip,swap);memset(nor,0xFF,sizeof nor);
    CHECK(st_prog(0x9D000,data,sizeof data)==0,"full page write finishes");
    CHECK(!memcmp(nor+0x9D000,data,sizeof data),"program data exact");
    CHECK(fnor.programs==8&&fnor.suspends>0&&fnor.resumes>0,"32-byte commands actually suspend/resume");
    CHECK(st_erase(0x9D000)==0,"sector erase finishes");
    for(unsigned i=0;i<4096;i++)CHECK(nor[0x9D000+i]==255,"erase leaves all bytes erased");
    CHECK(!fnor.bad_progress&&!fnor.bad_read,"datasheet progress/read constraints");
    CHECK(!st_flash.over_budget&&st_flash.max_off_us<=450,"modeled blackout within USB budget");
    CHECK(st_holes==0,"cooperative erase does not silence analogue DMA");
    unsigned commands=fnor.commands;
    CHECK(st_prog(0x5000,data,1)<0&&st_erase(0x5000)<0,"app/loader writes rejected");
    CHECK(commands==fnor.commands,"invalid address does not touch SPI");
    uint8_t readback[33];
    CHECK(st_read(0,readback,32)==0,"updater can read boot header");
    CHECK(st_read(0xE0000,readback,32)==0,"updater can read staging area");
    CHECK(st_read(0xFFFFF,readback,1)==0,"last chip byte remains readable");
    commands=fnor.commands;
    CHECK(st_read(0xFFFFF,readback,2)<0&&st_read(0xFFFFFFFEu,readback,4)<0&&
          st_read(0,readback,0xFFFFFFFFu)<0,"out-of-chip and wrapping reads rejected");
    CHECK(commands==fnor.commands,"invalid full read range touches no SPI or destination");
    flash_ok=0;CHECK(st_prog(0x9D000,data,1)<0&&st_erase(0x9D000)<0,"unknown chip rejected");
    printf("TRANSACTIONS chip=%s sus=%s max_off_us=%u slices=%u\n",chip?"UCUN":"Puya",swap?"swapped":"section9.33",st_flash.max_off_us,st_flash.slices);
}
static void faults(unsigned chip,unsigned swap) {
    uint8_t data=0x42;
    start(chip,swap);fnor.ignore_suspend=1;
    CHECK(st_prog(0x9D000,&data,1)==-11,"ignored suspend reports failure");
    CHECK(!fnor.busy&&!fnor.suspended,"error drains operation before XIP");
    unsigned ops=fnor.commands;CHECK(st_prog(0x9D001,&data,1)<0&&fnor.commands==ops,"failure latches further writes until reboot");
    start(chip,swap);fnor.ignore_resume=1;
    CHECK(setjmp(fnor_jump)==0? (st_prog(0x9D000,&data,1),0) : fnor.stop_count==1,"ignored resume cannot return to XIP with pending write");
    start(chip,swap);fnor.reject_wren=1;
    CHECK(st_prog(0x9D000,&data,1)==-3&&!fnor.busy,"write protection is a safe error");
    start(chip,swap);fnor.stuck_busy=1;fnor.ignore_suspend=1;
    CHECK(setjmp(fnor_jump)==0? (st_erase(0x9D000),0) : fnor.stop_count==1,"stuck busy is fail-stop in RAM");
    start(chip,swap);fnor.spi_fail=1;
    CHECK(setjmp(fnor_jump)==0? (st_prog(0x9D000,&data,1),0) : fnor.stop_count==1,"SPI failure is fail-stop");
    start(chip,swap);fnor.pp_us=1;
    CHECK(st_prog(0x9D000,&data,1)==0&&!fnor.suspends,"fast completion is not mistaken for suspend");
    start(chip,swap);fnor.pp_us=250;
    CHECK(st_prog(0x9D000,&data,1)==0&&!fnor.suspended,"completion races suspend safely");
    start(chip,swap);fnor.ticks=0xFFFFFF00ull;
    CHECK(st_prog(0x9D000,&data,1)==0,"TIMER4 wrap keeps bounded progress");
}
static void save_case(unsigned kind,const uint8_t *data) {
#ifdef ST_ASYNC
    if(kind==2) (void)st_async_tidy(ST_LOG_OBJ,0);
    else (void)st_async_put(ST_LOG_OBJ,data,200,kind==1?ST_NOERASE:ST_ROLL,0);
    st_async_drain();
#else
    if(kind==2) (void)st_tidy(ST_LOG_OBJ);
    else (void)st_put(ST_LOG_OBJ,data,200,kind==1?ST_NOERASE:ST_ROLL);
#endif
}
static void cuts(unsigned chip,unsigned swap,unsigned kind) {
    uint8_t before[200],after[200],got[200];
    memset(before,0x32,sizeof before);memcpy(after,before,sizeof after);after[7]=0x17;after[99]=0x51;
    start(chip,swap);memset(nor,255,sizeof nor);
    CHECK(st_put(ST_LOG_OBJ,before,200,ST_ROLL)==0,"initial valid commit");
    /* Force an erase for the full save; a log delta for the tidy case. */
    if(kind==0)memset(nor+st_sector(ST_LOG_OBJ,1),0x55,4096);
    if(kind==2) { CHECK(st_put(ST_LOG_OBJ,after,200,ST_NOERASE)==0,"delta before tidy");memcpy(before,after,200); }
    memcpy(snapshot,nor,sizeof nor);
    start(chip,swap);save_case(kind,after);unsigned total=fnor.events;
    CHECK(st_load(ST_LOG_OBJ,got,sizeof got)==200&&!memcmp(got,after,200),"completed save/tidy reloads exact value");
    /* Power cut at every command boundary, during active and suspended states. */
    for(unsigned cut=1;cut<=total;cut++) {
        memcpy(nor,snapshot,sizeof nor);start(chip,swap);fnor.cut_at=cut;
        int jump=setjmp(fnor_jump);
        if(!jump)save_case(kind,after);
        else CHECK(jump==2,"expected injected power cut, not driver panic");
        start(chip,swap);
        CHECK(st_load(ST_LOG_OBJ,got,sizeof got)==200,"power cut retains a valid copy");
        CHECK(!memcmp(got,before,200)||!memcmp(got,after,200),"power cut exposes only complete old/new record");
    }
    /* Warm reset with the NOR still suspended: drain, then load. */
    memcpy(nor,snapshot,sizeof nor);start(chip,swap);
    fls_job_t j={0};j.off=st_sector(ST_LOG_OBJ,1);j.src=after;j.n=32;
    CHECK(fl_slice_ram(&j)==1,"warm reset fixture is actually suspended");
    fls_drain();CHECK(!fnor.busy&&!fnor.suspended,"warm reset recovery finishes without resetting QE");
    memset(st_blank,0,sizeof st_blank);
    CHECK(st_load(ST_LOG_OBJ,got,sizeof got)==200,"warm reset retains readable committed record");
    printf("POWER_CUT chip=%s sus=%u kind=%u cut_points=%u\n",chip?"UCUN":"Puya",swap,kind,total);
}
int main(int argc,char **argv) {
    for(unsigned chip=0;chip<2;chip++)for(unsigned swap=0;swap<2;swap++) {
        transactions(chip,swap);if(argc>1)continue;faults(chip,swap);
        for(unsigned kind=0;kind<3;kind++)cuts(chip,swap,kind);
    }
    printf("FLASH checks=%u failures=%u\n",checks,failed);return failed?1:0;
}
