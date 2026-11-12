/* SPDX-License-Identifier: GPL-3.0-only */
/* A 670 us frame contains 180 us of audio, 70 us of TIMER5, and 200 us
 * polling LCD. Nested TIMER5 must not also become audio or LCD CPU time. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define CF_EVENT_DIAG 1
#define FM1_TICKS_PER_US 1u
static uint32_t clock_ticks;
#define CFR_CLOCK() clock_ticks
#ifndef CFR_HEADER
#define CFR_HEADER "../firmware/src/cf_render_diag.h"
#endif
#include CFR_HEADER
static unsigned checks, errors;
#define CHECK(c,m) do { checks++; if(!(c)){ errors++;fprintf(stderr,"FAIL line%d: %s\n",__LINE__,m); } }while(0)
static void timer(uint32_t ticks) { uint32_t start=clock_ticks;clock_ticks+=ticks;CFR_TIMER_END(start); }
static void audio(uint32_t cpu, uint32_t nested) { CFR_AUDIO_BEGIN();clock_ticks+=cpu;timer(nested);CFR_AUDIO_END(); }
int main(void)
{
    clock_ticks=1000;CFR_FRAME_BEGIN(1);
    clock_ticks+=100;timer(30);audio(100,20);clock_ticks+=70;CFR_COMPOSE_END();
    { CFR_WAIT_BEGIN();clock_ticks+=200;audio(80,20);CFR_WAIT_END(); }
    CFR_SUBMITTED();CFR_SUBMITTED();clock_ticks+=50;CFR_FRAME_END();
    CHECK(cf_render.v[CFR_FRAMES]==1&&cf_render.v[CFR_UPDATES]==1,"one physical submission per changed frame");
    CHECK(cf_render.v[CFR_WALL]==670&&cf_render.v[CFR_CPU]==420,"frame wall and exclusive CPU");
    CHECK(cf_render.v[CFR_COMPOSE_WALL]==320&&cf_render.v[CFR_COMPOSE_CPU]==170,"compose excludes IRQ time");
    CHECK(cf_render.v[CFR_WAIT_WALL]==300&&cf_render.v[CFR_WAIT_CPU]==200,"nested IRQ is not LCD polling");
    CHECK(cf_render.v[CFR_AUDIO]==180&&cf_render.v[CFR_TIMER]==70,"nested TIMER5 counted exactly once");
    clock_ticks=31620;CFR_FRAME_BEGIN(1);CFR_COMPOSE_END();CFR_SUBMITTED();CFR_FRAME_END();
    CHECK(cf_render.v[CFR_GAPS]==1&&cf_render.v[CFR_GAP_TICKS]==30000&&cf_render.v[CFR_GAP_33]==1,"observed 30 ms interval, not fixed-loop FPS");
    clock_ticks+=15000;CFR_FRAME_BEGIN(1);CFR_COMPOSE_END();CFR_FRAME_END();
    CHECK(cf_render.v[CFR_FRAMES]==3&&cf_render.v[CFR_UPDATES]==2,"unchanged image is not an animation update");
    CFR_FRAME_BEGIN(0);clock_ticks+=100000;CFR_SUBMITTED();CFR_FRAME_END();
    CHECK(cf_render.v[CFR_FRAMES]==3&&cf_render.v[CFR_UPDATES]==2,"non-HOME and popup frames excluded");
    uint32_t wall=cf_render.v[CFR_WALL],cpu=cf_render.v[CFR_CPU];
    clock_ticks=0xfffffff0u;cf_render.timer=0xfffffff0u;cf_render.audio=0xfffffff0u;
    CFR_FRAME_BEGIN(1);audio(40,30);clock_ticks+=10;CFR_COMPOSE_END();CFR_SUBMITTED();CFR_FRAME_END();
    CHECK(cf_render.v[CFR_WALL]-wall==80&&cf_render.v[CFR_CPU]-cpu==10,"clock and both IRQ counters wrap safely");
    uint32_t epoch=cf_render.v[CFR_SCENE_EPOCH];
    CFR_FRAME_BEGIN(0);CFR_FRAME_BEGIN(1);CFR_COMPOSE_END();CFR_FRAME_END();
    CHECK(cf_render.v[CFR_SCENE_EPOCH]==epoch+2,"popup entirely between reads invalidates that interval");
    cf_render.have_update=0;clock_ticks=0xfffffff0u;
    uint32_t gaps=cf_render.v[CFR_GAPS],sum=cf_render.v[CFR_GAP_TICKS];
    for(unsigned i=0;i<3;i++) {
        CFR_FRAME_BEGIN(1);CFR_COMPOSE_END();CFR_SUBMITTED();CFR_FRAME_END();clock_ticks+=16;
    }
    CHECK(cf_render.v[CFR_GAPS]-gaps==2&&cf_render.v[CFR_GAP_TICKS]-sum==32,"a submission at timestamp zero remains valid");
    printf("RENDER_TIMING checks=%u failures=%u state_bytes=%zu\n",checks,errors,sizeof cf_render);
    return errors?1:0;
}
