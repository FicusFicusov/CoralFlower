/* SPDX-License-Identifier: GPL-3.0-only */
/* Admission counts voices; it must not hide physical slots after load changes.
 * Real chord changes and drum DSP below cover requested pitches, not just totals.
 * Cost snapshots are controlled host inputs, not measurements of FM-1 timing. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../tools/emu/web/emu_web.c"
static unsigned checks,failed;
#define CHECK(c,m) do { checks++;if(!(c)){failed++;fprintf(stderr,"FAIL %s:%d %s\n",__FILE__,__LINE__,m);} } while(0)
static void pump(unsigned ms) {
    while(ms--) { web_step(1);while(web_avail()>=128)web_render(128);while(web_midi_out_take()){} }
}
static void clear_voices(void) {
    for(unsigned p=0;p<NSND;p++)memset(trk[p].v,0,sizeof trk[p].v);
    memset(vsq,0,sizeof vsq);cfb.on=0;voice_tight=0;voice_kills=voice_steals=0;
}
static void set_cap(unsigned count) {
    if(trk[0].engine!=ENGI_VA) {
        CHECK(trk_nvoice(&trk[0])>=count,"FM engine admits the requested chord");return;
    }
    memset(vl_us,0,sizeof vl_us);vl_aside=0;cf_c1.irq_us=0;
    vl_cost[0]=200;vl_mark[0]=vl_mark_of(&trk[0]);vl_all=VL_TARGET-count*200;
    CHECK(trk_nvoice(&trk[0])==count,"controlled VA admission count reached");
}
static unsigned active(unsigned part) {
    unsigned n=0;for(unsigned i=0;i<NVOICE;i++)n+=!!trk[part].v[i].active;return n;
}
static void check_notes(const uint8_t *notes,unsigned count) {
    unsigned held=0;
    for(unsigned i=0;i<NVOICE;i++)held+=trk[0].v[i].active&&trk[0].v[i].gate&&trk[0].v[i].stage!=4;
    CHECK(held==count,"held voice count equals the complete requested chord");
    for(unsigned n=0;n<count;n++) {
        unsigned found=0;
        for(unsigned i=0;i<NVOICE;i++)found+=trk[0].v[i].active&&trk[0].v[i].gate&&trk[0].v[i].stage!=4&&trk[0].v[i].note==notes[n];
        if(found!=1)fprintf(stderr,"pitch=%u copies=%u\n",notes[n],found);
        CHECK(found==1,"each requested pitch sounds exactly once");
    }
}
static void render_blocks(unsigned blocks) {
    int32_t out[CTL*2];while(blocks--)mix_block(out,CTL);
    while(web_midi_out_take()){}
}
int main(int argc,char **argv) {
    unsigned preset=argc>1?(unsigned)atoi(argv[1]):25u;
    web_boot();pump(2200);web_enc(EMU_E_PRESETS,25-(int)cf_snd_pos(0));pump(300);
    CHECK(trk[0].engine==ENGI_VA&&trk_vmode(&trk[0])==V_POLY,"real polyphonic VA preset selected");
    CHECK(trk[CF_PART_DRUMS].engine==ENGI_DRUM,"real drum engine selected");
    if(failed)return 2;
    clear_voices();trk[0].rr=5;set_cap(8);
    const uint8_t triad[]={60,64,67};
    for(unsigned i=0;i<3;i++)trk_note_on(&trk[0],triad[i],100);
    CHECK(trk[0].v[5].gate&&trk[0].v[6].gate&&trk[0].v[7].gate,"round-robin actually used high physical slots");
    for(unsigned i=0;i<3;i++)trk_note_off(&trk[0],triad[i]);
    set_cap(3);
    for(unsigned i=0;i<3;i++)trk_note_on(&trk[0],triad[i],100);
    check_notes(triad,3);
    CHECK(active(0)==3,"same triad retriggers its three slots without duplicate release tails");
    render_blocks(3);
    trk_note_on(&trk[0],69,100);
    render_blocks(4);
    const uint8_t replacement[]={60,64,69};
    check_notes(replacement,3);
    CHECK(active(0)==3,"one new pitch at capacity replaces a voice across all physical slots");
    web_enc(EMU_E_PRESETS,(int)preset-(int)cf_snd_pos(0));pump(300);
    CHECK(cf_snd_pos(0)==preset,"requested full-chord preset selected");
    const uint8_t chords[][6]={{60,64,67,71,74,78},{62,65,69,72,76,79},{59,62,65,69,72,76}};
    const uint8_t drum_notes[]={36,38,42};
    unsigned requested=0,rendered=0;
    for(unsigned count=4;count<=6;count+=2) {
        clear_voices();trk[0].rr=5;
        uint8_t old[CF_MAX_NOTES]={0},nold=0;
        unsigned drum_count=count==4?3u:2u;
        for(unsigned repeat=0;repeat<30;repeat++) {
            set_cap(repeat%3==0?count+2u:3u);
            for(unsigned i=0;i<drum_count;i++)trk_note_on(&trk[CF_PART_DRUMS],drum_notes[i],100);
            const uint8_t *notes=chords[(repeat/2)%3];
            cfp_change(0,old,&nold,notes,(int)count,0);requested+=count;
            render_blocks(8);check_notes(notes,count);
            for(unsigned i=0;i<drum_count;i++) {
                voice_t *v=trk_voice_of(&trk[CF_PART_DRUMS],drum_notes[i]);
                CHECK(v&&v->env_out,"each requested drum hit also reaches its real DSP render");
            }
            for(unsigned i=0;i<count;i++) {
                voice_t *v=trk_voice_of(&trk[0],notes[i]);
                CHECK(v&&v->env_out,"requested voice reached its real DSP render");
                rendered+=v&&v->env_out;
            }
        }
        cfp_change(0,old,&nold,chords[0],(int)count,0);
        if(trk[0].engine==ENGI_VA)CHECK(vsq[0].n>0,"release scenario actually has deferred replacements");
        cfp_change(0,old,&nold,NULL,0,0);render_blocks(8);
        CHECK(!vsq[0].n,"release cancels every deferred reservation");
        for(unsigned i=0;i<NVOICE;i++)CHECK(!trk[0].v[i].gate,"release leaves no held chord notes");
    }
    CHECK(requested==300&&rendered==requested,"all repeated and changing chord pitches rendered with drums");
    printf("VOICE ADMISSION preset=%u checks=%u failures=%u requested=%u rendered=%u\n",preset,checks,failed,requested,rendered);
    return failed?1:0;
}
