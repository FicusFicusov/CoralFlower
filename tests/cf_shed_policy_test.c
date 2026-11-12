/* SPDX-License-Identifier: GPL-3.0-only */
/* Production voice selection. A logical gate on a one-shot is not a held key.
 * Extra unison voices and drum tails must give way before chord pitches. */
#include <stdio.h>
#include <string.h>
#include "../tools/emu/web/emu_web.c"
static unsigned checks, failed;
#define CHECK(c,m) do { checks++; if (!(c)) { failed++; fprintf(stderr,"FAIL %s:%d %s\n",__FILE__,__LINE__,m); } } while (0)
static void clear_voices(void) {
    for (unsigned p=0;p<NSND;p++) {
        memset(trk[p].v,0,sizeof trk[p].v);
    }
    memset(vsq,0,sizeof vsq);
    cfb.on=0;voice_kills=voice_steals=0;shed_count=0;
}
static voice_t *held(unsigned p,unsigned i,unsigned note,unsigned age) {
    voice_t *v=&trk[p].v[i];
    v->active=v->gate=1;v->stage=2;v->note=note;v->age=age;v->env=1<<24;
    return v;
}
int main(void) {
    web_boot();
    const unsigned priorities[CFD_PART + 1] = {
        1,1,1,1,1,1,1,3,2,1,2,3,1,1,1,1,2,2,3,2,1,1,1
    };
    for(unsigned kind=1;kind<=CFD_PART;kind++)
        CHECK(cf_diag_priority(kind)==priorities[kind],"RAM-safe scoring retains every event priority");
    for(unsigned p=0;p<3;p++){trk[p].engine=ENGI_FM6;trk[p].p[P_VOICE]=V_POLY;}
    clear_voices();
    voice_t *root=held(0,0,60,1),*fifth=held(0,1,67,2);
    trk[1].p[P_VOICE]=V_UNISON;
    voice_t *lead=held(1,0,48,3),*extra=held(1,1,48,4);
    shed_voice();
    CHECK(extra->stage==4&&root->gate&&fifth->gate&&lead->gate,
          "extra unison gives way before a distinct held chord pitch");
    clear_voices();trk[1].p[P_VOICE]=V_POLY;
    root=held(0,0,60,1);fifth=held(0,1,67,2);
    trk[3].engine=ENGI_DRUM;
    /* Real drum note-on sets gate=1; unlike a melodic voice it ends itself. */
    trk_note_on(&trk[3],38,100);
    unsigned drum=NVOICE;
    for(unsigned i=0;i<NVOICE;i++)if(trk[3].v[i].active){drum=i;break;}
    CHECK(drum<NVOICE&&trk[3].v[drum].gate&&ENGINES[trk[3].engine]->oneshot,
          "real one-shot is sounding with its logical gate still high");
    shed_voice();
    CHECK(drum<NVOICE&&trk[3].v[drum].stage==4&&root->gate&&fifth->gate,
          "one-shot yields before a distinct held chord pitch");
    clear_voices();root=held(0,0,60,1);fifth=held(0,1,67,2);
    voice_t *tail=held(2,0,72,3);tail->gate=0;tail->stage=3;
    shed_voice();
    CHECK(tail->stage==4&&root->gate&&fifth->gate,"released voice is still the first musical victim");
    clear_voices();root=held(0,0,60,1);fifth=held(0,1,67,2);
    shed_voice();
    CHECK(fifth->stage==4&&root->gate&&voice_kills==1,
          "real overload still removes a held fifth when no cheaper musical victim remains");
    clear_voices();root=held(0,0,60,1);lead=held(1,0,48,2);trk[1].p[P_VOICE]=V_MONO;
    shed_voice();
    CHECK(root->gate&&lead->gate&&!voice_kills,"lowest poly pitch and mono lead stay protected");
    cf_diag_start(0,1,0);unsigned start=cf_diag.t0;
    for(unsigned second=0;second<10;second++)for(unsigned i=0;i<50;i++){
        emu_sim_us=start+second*1000000u+i*19000u;
        CFD_EVENT(CFD_AUDIO,CFD_ARP_NOTE,60,i,second);
    }
    unsigned last=0;
    for(unsigned i=0;i<cf_diag.n[CFD_AUDIO];i++){
        cf_diag_event_t event;
        CHECK(cf_diag_read_event(CFD_AUDIO,i,&event),"every advertised detail can be read");
        unsigned t=event.ticks;
        if(t>last)last=t;
    }
    CHECK(last>=9000000u,"saturated audio diagnostics retain evidence in the tenth second");
    /* Review P2: operations before the cutoff count in their own second,
     * even when the half containing them finishes beyond ten seconds. */
    clear_voices();cf_diag_start(1,2,0);start=cf_diag.t0;
    emu_sim_us=start+9999000u;
    voice_kill(held(1,0,48,1));
    trk[0].engine=ENGI_VA;trk[0].p[P_VOICE]=V_POLY;voice_fade_steal=0;
    for(unsigned i=0;i<NVOICE;i++)held(0,i,48+i,i+1);
    unsigned steals_before=voice_steals;
    CHECK(voice_alloc(&trk[0],90,100)!=NULL&&voice_steals==steals_before+1,"real VA allocator actually stole a voice before cutoff");
    emu_sim_us=start+10001000u;cf_diag_poll();
    CHECK(cf_diag.second[9].kills==1&&cf_diag.second[9].steals==1,
          "kill and steal before cutoff survive a half ending beyond ten seconds");
    printf("SHED POLICY checks=%u failures=%u\n",checks,failed);
    return failed?1:0;
}
