/* SPDX-License-Identifier: GPL-3.0-only */
/* Run via cf_voice_budget_test.py, which extracts the current production code. */
/* Synthetic timing, real production ISR/admission functions. Not a device benchmark. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#ifdef __APPLE__
#define __attribute__(x)
#endif
#define CF_EVENT_DIAG 0
#define CF_CORE1 1
#define CF_UI_RESERVE_PERCENT 18u
#define CF_VLOAD 1
#define CF_NTAIL 0
#define FM1_TICKS_PER_US 24u
#define HALF_FRAMES 128u
#define FS 44100u
#define CTL 32u
#define FELUCCA_UAC 0
#define NPART 1u
#define NSND 1u
#define NVOICE 8u
#define VA_POLY 8u
#define ENGI_VA 1u
#define V_POLY 0u
#define FM1_AUDIO_HALF 0x80u
#define CFD_EVENT(...) ((void)0)
#define CFD_FLASH_CONTEXT(...) ((void)0)
typedef struct { uint32_t active,gate,stage,age,note; int32_t env; } voice_t;
typedef struct { uint32_t engine; voice_t v[NVOICE]; } track_t;
typedef struct { uint32_t oneshot,ownenv; int32_t (*level)(const track_t *,const voice_t *); } engine_t;
static const engine_t engine={0};
static const engine_t *ENGINES[]={&engine,&engine};
static track_t trk[NPART];
static struct { uint32_t cpu_q8; } song;
static struct { uint32_t stale; } cr_dbg;
static struct { uint32_t saved_ticks,irq_us,last_wall_us,last_serial_us,wall_total,serial_total,main_total,wait_total,busy_total,voices0,voices1; } cf_c1;
static volatile uint32_t t5_nested_ticks;
static uint32_t now,block,irq_per_block=25,voice_kills,requested=3;
static uint32_t main_kernel=75,worker_kernel=75;
static int refuse, mode_flip, refused_flip, after_part_flip;
static int dual=1;
static int cr_dualcore_active(void) { return dual; }
static uint32_t cf_c1_cpu_ticks(void) { return now-t5_nested_ticks; }
static uint32_t va_cost(const track_t *t) { (void)t;return 100; }
static void voice_kill(voice_t *v) { v->gate=0;v->stage=4;voice_kills++; }
static voice_t *voice_shed_victim(uint32_t *p,uint32_t *s,uint32_t *r) { (void)p;(void)s;(void)r;return 0; }
static unsigned trk_vmode(const track_t *t) { (void)t;return V_POLY; }
static uint8_t voice_tight;
static int fx_usb_fixed,fx_out_blk;
static int32_t out_frac[2*CTL];
static uint32_t fm1_ticks(void) { return now; }
static uint8_t fm1_audio_pending(void) { return FM1_AUDIO_HALF; }
static void fm1_audio_ack_aux(uint8_t p) { (void)p; }
static void fm1_audio_ack_half(void) {}
static uint32_t fm1_audio_free_half(void) { return now/(HALF_FRAMES*544u)&1u; }
static void fm1_audio_init(int32_t *a,uint32_t n,void (*f)(void),uint32_t p) { (void)a;(void)n;(void)f;(void)p; }
void isr_alnk0(void) {}
static void cr_prof_window(void) {}
static void cr_prof_half(void) {}
static uint32_t cr_prof_entry(uint32_t t,uint32_t per) { (void)t;(void)per;return 0; }
static void cr_prof_whole(uint32_t u,int l,uint32_t a,uint32_t t,uint32_t p) { (void)u;(void)l;(void)a;(void)t;(void)p; }
static void usb_fixed_dac(int32_t *out,uint32_t n) { (void)out;(void)n; }
#include "budget_functions.inc"
#include "budget_clock.inc"

static void mix_part(track_t *t,uint32_t n) {
    (void)t;(void)n;
    if (mode_flip && block==2) dual=0;
    if (refused_flip && block==2) refuse=1;
    uint32_t wv = !dual || refuse || requested==1 ? 0 : (requested+1)/2, mv=requested-wv;
    uint32_t main=mv*main_kernel*24u,worker=wv*worker_kernel*24u;
    uint32_t wait=worker>main?worker-main:0;
    now+=requested*20u*24u+main+wait;
    if (dual) {
        cf_c1.main_total+=main;cf_c1.wait_total+=wait;cf_c1.busy_total+=worker;
        cf_c1.voices0+=mv;cf_c1.voices1+=wv;
        cf_c1.saved_ticks+=worker-wait;
    }
}
#include "mix_macro.inc"
static void mix_block(int32_t *out,uint32_t n) {
    for(unsigned i=0;i<n*2;i++)out[i]=100;
    MIX_PART(&trk[0],n);
    /* Another part can lose C1 after this part's final measured render. */
    if(after_part_flip&&block==3)dual=0;
    now+=(300u+irq_per_block)*24u;
    t5_nested_ticks+=irq_per_block*24u;block++;
}
#include "audio.c"
static unsigned checks,failed;
#define CHECK(c,m) do { checks++;if(!(c)){failed++;fprintf(stderr,"FAIL %s:%d %s\n",__FILE__,__LINE__,m);} }while(0)
static void render(void) {
    now=((now/(HALF_FRAMES*544u))+2u)*(HALF_FRAMES*544u);block=0;fm1_alnk0_irq();
    CHECK(block==4,"all four production ISR blocks rendered");
}

static void voices(unsigned n) {
    requested=n;
    for(unsigned i=0;i<NVOICE;i++){trk[0].v[i].active=trk[0].v[i].gate=i<n;trk[0].v[i].note=60+i*3;}
}
int main(void) {
    trk[0].engine=ENGI_VA;
    main_kernel=worker_kernel=50;after_part_flip=1;
    CHECK(!vl_cost[0],"first VA half has no cached price");
    voices(4);render();
    CHECK(!dual,"another part lost C1 after the last VA render");
    CHECK(!vl_cost[0],"first split observation is discarded when the half finishes serial");
    CHECK(vl_cost_of(&trk[0])==275,"first fallback uses the unmeasured serial estimate");
    CHECK(vl_cap(&trk[0])==3,"first fallback cannot admit five voices using split cost");
    dual=1;main_kernel=worker_kernel=75;after_part_flip=0;
    voices(3);render();
    printf("triad: wall=%u cpu=%u serial=%u irq=%u part=%u cost=%u cap=%u target=%u\n",cf_c1.last_wall_us,vl_all,cf_c1.last_serial_us,cf_c1.irq_us,vl_us[0],vl_cost_of(&trk[0]),vl_cap(&trk[0]),VL_TARGET);
    CHECK(VL_TARGET==2379,"original 18% HOME target unchanged");
    CHECK(vl_all==2040&&vl_us[0]==840&&cf_c1.irq_us==100,"actual CPU and separate IRQ enter real production admission");
    CHECK(vl_cap(&trk[0])==4,"three voices can grow to four in the same two kernel layers");
    voices(4);render();
    printf("quartet: wall=%u cpu=%u serial=%u irq=%u part=%u cost=%u cap=%u\n",cf_c1.last_wall_us,vl_all,cf_c1.last_serial_us,cf_c1.irq_us,vl_us[0],vl_cost_of(&trk[0]),vl_cap(&trk[0]));
    CHECK(vl_cap(&trk[0])==4,"fifth voice requires a third kernel layer and exceeds remaining budget");
    CHECK(cf_c1.last_wall_us==2220&&cf_c1.last_serial_us==2720,"physical and serial-equivalent diagnostics stay distinct");
    irq_per_block=100;render();
    CHECK(vl_cap(&trk[0])==3,"extra IRQ work consumes common budget; minimum triad exception retained");
    irq_per_block=25;voices(1);render();
    CHECK(vl_cap(&trk[0])==4,"one voice does not overwrite measured split prices with a cheaper dispatch-free sample");
    voices(3);refuse=1;render();
    CHECK(vl_cap(&trk[0])==3,"refused posting removes the cached pair discount");
    refuse=0;main_kernel=100;worker_kernel=50;render();
    CHECK(vl_cap(&trk[0])==3,"slower core0 prevents underpricing the new fourth voice");
    dual=0;
    CHECK(vl_cost_of(&trk[0])==275,"fallback immediately invalidates parallel measurements");
    dual=1;main_kernel=worker_kernel=75;mode_flip=1;render();
    CHECK(!dual&&!vl_cost[0]&&vl_cost_of(&trk[0])==275,"mid-half fallback does not label mixed parallel timing as a serial sample");
    mode_flip=0;render();
    CHECK(vl_cost_of(&trk[0])==380,"the following full serial half teaches actual serial cost");
    dual=1;render();render(); /* establish a fresh coherent split observation */
    refused_flip=1;render();
    CHECK(dual&&!vl_cost[0]&&vl_cost_of(&trk[0])==275,"mixed successful/refused jobs invalidate a cheap sample even while C1 reports OK");
    refused_flip=0;render();
    CHECK(vl_cost_of(&trk[0])==380,"the following entirely refused half learns serial cost");
    printf("BUDGET checks=%u failures=%u\n",checks,failed);return !!failed;
}
