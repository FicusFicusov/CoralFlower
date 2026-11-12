/* SPDX-License-Identifier: GPL-3.0-only */
/* UI diagnostic contract, not a reproduction of the owner's physical stall.
 * A known 1375 ms phase must be retained after recovery and repeated reads;
 * a snapshot must not consume input, arm tracing or change persistent state. */
#include "../tools/emu/web/emu_web.c"
static unsigned checks, failed;
#define CHECK(c, m) do { checks++; if (!(c)) { failed++; fprintf(stderr,"FAIL %s:%d %s\n",__FILE__,__LINE__,m); } } while (0)
static const uint8_t query[] = {0x7d,0x46,0x4c,83};
static uint32_t word(unsigned i) { return crb_r32(CRB_OUT + 7u + i * 5u); }
static void tap(unsigned b) { web_buttons(1u<<b);web_step(40);web_buttons(0);web_step(40); }

int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    web_boot();web_step(1800);
    tap(EMU_B_SEQ);tap(EMU_B_SEQ);
    CHECK(cu.scr==SCR_SQ,"real panel enters SEQ");
#if CF_EVENT_DIAG
    CHECK(cf_ui_diag.input_calls>0&&cf_ui_diag.buttons==2,"real UI hooks count calls and two buttons");
    unsigned enc=cf_ui_diag.encoders, old=squ.trk;
    web_enc(EMU_E_ALGO,1);web_step(40);
    CHECK(squ.trk==old+1&&cf_ui_diag.encoders==enc+1,"track selection and encoder observer both advance");

    /* Simulated slow phase: the production recorder receives the real
     * boundary timestamps; this injects neither sleep nor a device change. */
    cf_ui_mark(CFU_SOUNDS,10000);
    cf_ui_mark(CFU_CONTROLS,11375);
    cf_ui_mark(CFU_IDLE,11380);
    CHECK(cf_ui_diag.max_ms[CFU_SOUNDS]==1375,"slow sound phase is attributed exactly");
    cf_ui_mark(CFU_DRAW,0xfffffff0u);
    cf_ui_mark(CFU_IDLE,0x25u);
    CHECK(cf_ui_diag.max_ms[CFU_DRAW]==53,"millisecond wrap does not create a false huge draw stall");
    cf_ui_mark(CFU_IDLE,fm1_ms);
    cf_ui_input_mark(12000);cf_ui_input_mark(13500);
    CHECK(cf_ui_diag.input_gap_max_ms>=1500,"input service gap is visible after recovery");
    CFU_ENCODER(4u,-3);

    host_pressed=1u<<EMU_B_EDIT;host_notes=1u<<7;host_enc[2]=-2;
    uint8_t saved[sizeof cs], pattern[sizeof sq_st], probe[sizeof cf_ui_diag], trace[sizeof cf_diag];
    memcpy(saved,&cs,sizeof cs);memcpy(pattern,&sq_st,sizeof sq_st);
    memcpy(probe,&cf_ui_diag,sizeof cf_ui_diag);memcpy(trace,&cf_diag,sizeof cf_diag);
    unsigned writes=emu_flash_writes;
    for(unsigned i=0;i<3;i++) {
        CHECK(crb_handle(query,sizeof query),"passive command83 replies without an ARP MODE capture");
        CHECK(CRB_OUT[4]==83&&CRB_OUT[5]==1&&CRB_OUT[6]==51,"wire command/schema/exact51-word layout");
        CHECK(crb_n==263&&CRB_OUT[crb_n-1]==0xf7,"bounded complete SysEx reply");
        CHECK(word(1)==0,"emulator marks physical scan counters unavailable");
        CHECK(word(13)==1375&&word(19)==53,"slow phase and wrapped draw survive every read");
        CHECK(word(8)==0xfffffffdu,"signed encoder delta kept on wire");
        CHECK(word(21)==SCR_SQ&&word(23)==squ.pat&&word(24)==squ.trk,"snapshot identifies actual SEQ screen/pattern/track");
        CHECK(word(31)==(1u<<EMU_B_EDIT)&&word(33)==(1u<<7)&&word(45)==0xfffffffeu,
              "pending button/note/encoder input is observed without consumption");
    }
    CHECK(host_pressed==(1u<<EMU_B_EDIT)&&host_notes==(1u<<7)&&host_enc[2]==-2,"pending input remains available to UI");
    CHECK(!memcmp(saved,&cs,sizeof cs)&&!memcmp(pattern,&sq_st,sizeof sq_st),"read preserves settings and patterns");
    CHECK(!memcmp(probe,&cf_ui_diag,sizeof cf_ui_diag)&&!memcmp(trace,&cf_diag,sizeof cf_diag),"read retains maxima and does not arm/reset ARP trace");
    CHECK(emu_flash_writes==writes,"read never writes flash");
    FILE *f=fopen(argv[1],"wb");CHECK(f!=NULL,"wire evidence opens");
    if(f){fwrite(CRB_OUT+5,1,crb_n-6,f);fclose(f);}
    uint8_t bad[]={0x7d,0x46,0x4c,83,0};
    CHECK(!crb_handle(bad,sizeof bad),"unexpected command83 arguments rejected");
#else
    CHECK(!crb_handle(query,sizeof query),"production build excludes command83 and passive state");
#endif
    printf("UI_DIAG enabled=%u checks=%u failures=%u\n",CF_EVENT_DIAG,checks,failed);
    return failed?1:0;
}
