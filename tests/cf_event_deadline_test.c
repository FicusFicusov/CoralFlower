/* SPDX-License-Identifier: GPL-3.0-only */
/* Production ALNK ISR and shedding, with explicit independent DMA time and
 * per-block costs (not a benchmark or a claim about the physical FM-1).
 * A slow first block must be visible even when all/4 would miss it. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#ifdef __APPLE__
#define __attribute__(x)             /* ELF section labels are target-only, as in hostsim.c */
#endif
#define CF_EVENT_DIAG 1
#define FM1_TICKS_PER_US 24u
#define HALF_FRAMES 128u
#define FS 44100u
#define CTL 32u
#define FELUCCA_UAC 0
#define CF_VLOAD 0
#define NPART 1u
#define NVOICE 4u
#define V_POLY 0u
#define FM1_AUDIO_HALF 0x80u
typedef struct { uint32_t active, gate, stage, age, note; int32_t env; } voice_t;
typedef struct { voice_t v[NVOICE]; } track_t;
static track_t trk[NPART];
static struct { uint32_t cpu_q8; } song;
static struct { uint32_t stale; } cr_dbg;
static uint32_t now, costs[4], block, lag, nested_us, voice_kills, voice_steals;
static uint32_t log_irq_ticks, clock_irq_ticks;
static uint32_t test_trace_ticks(void);
static void clock_interrupt(void);
#define CFD_TICKS() test_trace_ticks()
static uint8_t voice_tight;
static int fx_usb_fixed, fx_out_blk;
static int32_t out_frac[2*CTL];
static uint32_t fm1_ticks(void) { clock_interrupt(); return now; }
static void fm1_irq_off(void) {}
static void fm1_irq_on(void) {}
#include "../firmware/src/cf_diag.c"
static uint8_t fm1_audio_pending(void) { return FM1_AUDIO_HALF; }
static void fm1_audio_ack_aux(uint8_t p) { (void)p; }
static void fm1_audio_ack_half(void) {}
static uint32_t fm1_audio_free_half(void) { return now/(HALF_FRAMES*544u)&1u; }
static void fm1_audio_init(int32_t *a,uint32_t n,void (*f)(void),uint32_t p) { (void)a;(void)n;(void)f;(void)p; }
void isr_alnk0(void) {}
static void cr_prof_window(void) {}
static void cr_prof_half(void) {}
static uint32_t cr_prof_entry(uint32_t t,uint32_t per) { (void)t;(void)per;return lag; }
static void cr_prof_whole(uint32_t us,int late,uint32_t all,uint32_t t5,uint32_t per) {
    (void)us;(void)late;(void)t5;
    if(lag+all/4>per||lag+all>per+per/4*3)cr_dbg.stale++;
}
static unsigned trk_vmode(const track_t *t) { (void)t;return V_POLY; }
static unsigned lowest_held(const track_t *t) { (void)t;return 0; }
static void voice_kill(voice_t *v) { v->gate=0;v->stage=4;voice_kills++; }
static void usb_fixed_dac(int32_t *out,uint32_t n) { (void)out;(void)n; }
static void mix_block(int32_t *out,uint32_t n);
#include "../firmware/src/audio.c"
static void clock_interrupt(void) {
    if (clock_irq_ticks) { now+=clock_irq_ticks;t5_nested_ticks+=clock_irq_ticks;clock_irq_ticks=0; }
}
static uint32_t test_trace_ticks(void) {
    if (log_irq_ticks) { now+=log_irq_ticks;t5_nested_ticks+=log_irq_ticks;log_irq_ticks=0; }
    return now;
}
static void mix_block(int32_t *out,uint32_t n) {
    for(unsigned i=0;i<n*2;i++)out[i]=100;
    now += costs[block++]*24u;
    t5_nested_ticks += nested_us*24u;
}
static unsigned checks, failed;
#define CHECK(c,m) do {checks++;if(!(c)){failed++;fprintf(stderr,"FAIL %s:%d %s\n",__FILE__,__LINE__,m);}}while(0)
static unsigned count(unsigned kind) {
    unsigned n=0;
    for(unsigned i=0;i<cf_diag.n[CFD_AUDIO];i++)n+=(cf_diag.events[64+i].tag&65535u)==kind;
    return n;
}
static void render(unsigned a,unsigned b,unsigned c,unsigned d,unsigned entry_lag) {
    now=((now/(HALF_FRAMES*544u))+2u)*(HALF_FRAMES*544u)+entry_lag*24u;
    lag=entry_lag*24u;costs[0]=a;costs[1]=b;costs[2]=c;costs[3]=d;block=0;
    fm1_alnk0_irq();
    CHECK(block==4,"real ISR renders all four blocks");
}
int main(void) {
    now=100000u;cf_diag_start(0,1,0);
    render(450,450,450,450,0);
    CHECK(!count(CFD_BLOCK_LATE)&&!count(CFD_AUDIO_LATE)&&!count(CFD_SHED_REQUEST),"healthy schedule does not report lateness or overload");
    cf_diag_start(1,2,0);cr_dbg.stale=0;
    render(3400,30,30,30,0);
    CHECK(cr_dbg.stale==0,"existing equal-quarter stale estimate misses the injected uneven cost");
    CHECK(count(CFD_BLOCK_LATE)==1&&count(CFD_AUDIO_LATE)==1,"actual first-block completion exposes the injected missed deadline");
    for(unsigned i=0;i<cf_diag.n[CFD_AUDIO];i++) {
        const volatile cf_diag_event_t *e=&cf_diag.events[64+i];
        if((e->tag&65535u)==CFD_BLOCK_LATE)CHECK(e->tag>>16==1u&&e->a==3400u*24u-HALF_FRAMES*544u,"first-block mask and lateness match independent DMA deadline");
    }
    cf_diag_start(2,3,0);shed_over=0;shed_req=0;voice_kills=0;
    trk[0].v[0]=(voice_t){1,1,1,1,48,1000};
    trk[0].v[1]=(voice_t){1,0,2,2,64,500};
    cf_diag.flash_at=100;cf_diag.flash_ticks=297u*24;cf_diag.flash_seq=1;
    nested_us=50;
    render(650,650,650,650,400);
    render(650,650,650,650,400);
    CHECK(shed_req&&count(CFD_SHED_REQUEST)==1,"two overloaded halves request a voice release");
    render(450,450,450,450,0);
    CHECK(voice_kills==1&&trk[0].v[1].stage==4&&trk[0].v[0].gate,"actual shedding releases the eligible voice and preserves the lowest held note");
    CHECK(count(CFD_SHED)==1&&count(CFD_VOICES)==1,"actual voice loss is dated even without a PCM zero gap");
    CHECK(count(CFD_FLASH_WINDOW)==1,"most recent flash window accompanies anomaly once per lane");
    CHECK(felucca_dbg.last_us==1600u,"nested TIMER5 cost is distinct from render time");
    /* Review P2: a nested tick in logging is AFTER the render snapshot. It must
     * not be subtracted from that earlier render or affect the voice budget. */
    cf_diag_start(3,4,0);shed_over=1;t5_nested_ticks=0;log_irq_ticks=50u*24u;
    CHECK(shed_check(2400u*24u,100u*24u,0)==2400u,"nested IRQ during event logging does not alter captured render time");
    CHECK(t5_nested_ticks==50u*24u&&count(CFD_SHED_REQUEST)==1,"post-snapshot nested IRQ actually injected");
    uint32_t t0=now,snapshot_nested;
    t5_nested_ticks=0;now+=2400u*24u;clock_irq_ticks=50u*24u;
    uint32_t all=audio_wall_snapshot(t0,&snapshot_nested);
    CHECK(all==2450u*24u&&snapshot_nested==50u*24u,"timestamp/counter snapshot retries through a nested IRQ");
    printf("EVENT DEADLINES checks=%u failures=%u\n",checks,failed);
    return failed?1:0;
}
