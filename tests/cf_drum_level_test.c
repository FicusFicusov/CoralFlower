/* SPDX-License-Identifier: GPL-3.0-only */
/* A sounding clap's attack must not be ranked as silence when a quieter
 * drum tail is available. Both overload and shared-budget selection use
 * the production engine's level callback. The sound is measured separately. */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "../tools/emu/web/emu_web.c"
static unsigned checks, failed;
#define CHECK(c,m) do { checks++; if (!(c)) { failed++; fprintf(stderr,"FAIL %s:%d %s\n",__FILE__,__LINE__,m); } } while (0)
static void pump(unsigned ms) {
    while(ms--) { web_step(1); while(web_avail()>=128)web_render(128); }
}
/* Inspect a block of actual drum PCM, then restore every state it touched.
 * The oracle does not use the level estimate it is checking. */
static double block_energy(track_t *t, voice_t *v) {
    drum_lane_t *lane=drum_lane_of(t,v), saved=*lane;
    int32_t pcm[CTL]={0};
    vmod_t mod={0};mod.amp0=mod.amp1=v->vel*258;
    drum_render(t,v,pcm,CTL,&mod);
    double sum=0;for(unsigned i=0;i<CTL;i++)sum+=(double)pcm[i]*pcm[i];
    *lane=saved;
    return sum/CTL;
}
int main(int argc, char **argv) {
    unsigned kit=argc>1?(unsigned)strtoul(argv[1],0,10):0;
    if(kit>DS_NKIT)return 2;
    web_boot();pump(2500);cfb.on=0;
    web_keys((1u<<7)|(1u<<11)|(1u<<14));pump(100);
    track_t *t=&trk[CF_PART_DRUMS];
    trk_note_on(t,36,100);pump(10);
    voice_t *quiet=drum_reuse(t,36);
    CHECK(quiet!=NULL,"real kick started");if(!quiet)return 1;
    unsigned waited=0;
    while(quiet->active&&drum_level(t,quiet)>=64&&waited++<1500)pump(1);
    CHECK(quiet->active&&drum_level(t,quiet)>0&&drum_level(t,quiet)<64,"audible kick has decayed to a quiet but active tail");
    /* The lane selector is set by the normal kit control before a hit.
     * 0 is the classic engine; 1..DS_NKIT select the production X kits. */
    drum_lane_knobs[0][DV_CLAP][8]=(int16_t)kit;
    trk_note_on(t,39,100);
    voice_t *clap=drum_reuse(t,39);
    CHECK(clap!=NULL,"real clap started");if(!clap)return 1;
    CHECK(drum_level(t,clap)>drum_level(t,quiet),"pending hit is not reported quieter than a decayed tail before its first sample");
    pump(3);
    drum_lane_t *lane=drum_lane_of(t,clap);
    CHECK(lane&&(kit?(lane->n.teeth>0&&lane->n.qn==0):
                     (lane->v.cnt>0&&lane->v.q[0]==0)),
          "clap is in its real multi-impulse attack, before its slow tail");
    double attack_energy=block_energy(t,clap),tail_energy=block_energy(t,quiet);
    CHECK(attack_energy>10000&&attack_energy>tail_energy*100,"actual clap PCM is over20dB stronger than the available tail");
    CHECK(drum_level(t,clap)>drum_level(t,quiet),"sounding clap attack is not ranked as silence");
    uint32_t part=99,slot=99,reason=99;
    voice_t *victim=voice_shed_victim(&part,&slot,&reason);
    CHECK(victim==quiet&&part==CF_PART_DRUMS&&reason==5,"overload chooses the quiet drum tail, preserving the new clap");
    track_t *budget_part=0;
    unsigned budget_slot=voice_victim(&trk[CF_PART_CHORD],0,&budget_part);
    CHECK(budget_part==t&&budget_slot==(unsigned)(quiet-t->v),"shared budget also chooses the genuinely quiet tail");
    unsigned gates=0;for(unsigned i=0;i<NVOICE;i++)gates+=trk[CF_PART_CHORD].v[i].active&&trk[CF_PART_CHORD].v[i].gate;
    CHECK(gates>=3,"held chord supplies a real competing melodic part");
    shed_over=shed_req=0;shed_check(SHED_TICKS+1,0,0);
    CHECK(!shed_req,"one overloaded half does not request removal");
    shed_check(SHED_TICKS+1,0,0);CHECK(shed_req,"two overloaded halves still request removal");
    shed_voice();
    CHECK(quiet->stage==4&&clap->active&&clap->stage!=4,"protection actually fades the quiet tail, not the clap attack");
    unsigned after=0;for(unsigned i=0;i<NVOICE;i++)after+=trk[CF_PART_CHORD].v[i].active&&trk[CF_PART_CHORD].v[i].gate;
    CHECK(after==gates,"fix does not transfer the removal to the held chord");
    /* This is an accurate ranking, not a blanket exemption for drums. */
    for(unsigned p=0;p<NPART;p++)for(unsigned i=0;i<NVOICE;i++)
        if(&trk[p].v[i]!=clap)trk[p].v[i].active=trk[p].v[i].gate=0;
    clap->active=clap->gate=1;clap->stage=2;
    shed_voice();CHECK(clap->stage==4,"real overload can still fade the sole available drum");
    trk_note_on(t,39,100);
    clap=drum_reuse(t,39);lane=drum_lane_of(t,clap);
    CHECK(clap->stage!=4&&drum_level(t,clap)>0,"same-lane retrigger restores the attack estimate");
    /* Render through the actual last tooth: the slow body is still zero
     * at that boundary, so the remaining fast envelope must count too. */
    vmod_t mod={0};mod.amp0=mod.amp1=clap->vel*258;
    unsigned blocks=0;
    do {
        int32_t pcm[CTL]={0};drum_render(t,clap,pcm,CTL,&mod);blocks++;
    } while((kit?lane->n.teeth:lane->v.cnt)&&blocks<200);
    CHECK(blocks<200&&(kit?lane->n.qn:lane->v.q[0])==0,"reached the real last-tooth boundary before the slow body starts");
    CHECK(drum_level(t,clap)>0&&block_energy(t,clap)>1,"last tooth still has a positive estimate and actual PCM");
    int32_t pcm[CTL]={0};drum_render(t,clap,pcm,CTL,&mod);
    CHECK((kit?lane->n.qn:lane->v.q[0])>0&&drum_level(t,clap)>0,"transition to the slow body remains nonzero");
    printf("DRUM LEVEL kit=%s checks=%u failures=%u attack_rms=%.3f quiet_tail_rms=%.3f\n",kit?DS_KITS[kit-1].name:"Classic",checks,failed,sqrt(attack_energy),sqrt(tail_energy));
    return failed?1:0;
}
