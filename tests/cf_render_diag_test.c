/* SPDX-License-Identifier: GPL-3.0-only */
/* Actual HOME render hooks and command84; no physical FPS claim on host. */
#include "../tools/emu/web/emu_web.c"
static unsigned checks, errors;
#define CHECK(c,m) do { checks++;if(!(c)){ errors++;fprintf(stderr,"FAIL line%d: %s\n",__LINE__,m); } }while(0)
static const uint8_t query[]={0x7d,0x46,0x4c,84};
static uint32_t word(unsigned i) { return crb_r32(CRB_OUT+7u+i*5u); }
int main(int argc,char **argv)
{
    if(argc!=2)return 2;
    web_boot();web_step(4500);web_keys(1u<<7);web_step(600);
#if CF_EVENT_DIAG
    CHECK(cu.scr==SCR_HOME&&cf_render.v[CFR_FRAMES]>10,"real HOME render path observed");
    CHECK(cf_render.v[CFR_UPDATES]>10,"changed-flora LCD submissions observed");
    uint8_t settings[sizeof cs],profile[sizeof cf_render],trace[sizeof cf_diag];
    memcpy(settings,&cs,sizeof cs);memcpy(profile,&cf_render,sizeof cf_render);memcpy(trace,&cf_diag,sizeof cf_diag);
    uint32_t writes=emu_flash_writes;host_pressed=1u<<EMU_B_ENV;host_notes=1u<<8;host_enc[0]=2;
    for(unsigned i=0;i<3;i++) {
        CHECK(crb_handle(query,sizeof query),"passive84 available without arming or playing MODE");
        CHECK(CRB_OUT[4]==84&&CRB_OUT[5]==1&&CRB_OUT[6]==36&&crb_n==188,"exact bounded wire schema");
        CHECK(word(3)==0,"host never advertises hardware timing");
        CHECK(word(13+CFR_UPDATES)==cf_render.v[CFR_UPDATES],"wire returns actual observer count");
    }
    CHECK(!memcmp(settings,&cs,sizeof cs)&&!memcmp(profile,&cf_render,sizeof cf_render)&&!memcmp(trace,&cf_diag,sizeof cf_diag),"reads preserve settings, profile and trace");
    CHECK(emu_flash_writes==writes&&host_pressed==(1u<<EMU_B_ENV)&&host_notes==(1u<<8)&&host_enc[0]==2,"reads preserve flash and input edges");
    FILE *f=fopen(argv[1],"wb");CHECK(f!=0,"wire fixture opens");if(f){fwrite(CRB_OUT+5,1,crb_n-6,f);fclose(f);}
    uint8_t bad[]={0x7d,0x46,0x4c,84,0};CHECK(!crb_handle(bad,sizeof bad),"no write/clear/reset arguments accepted");
    host_pressed=host_notes=0;host_enc[0]=0;
    uint32_t epoch=cf_render.v[CFR_SCENE_EPOCH];
    web_buttons(1u<<EMU_B_OCTUP);web_step(40);web_buttons(0);web_step(1100);
    CHECK(cu.scr==SCR_HOME&&!cu.pop,"real short octave popup ends between endpoints");
    CHECK(cf_render.v[CFR_SCENE_EPOCH]>=epoch+2,"actual popup/scene excursion increments epoch");
#else
    CHECK(!crb_handle(query,sizeof query),"ordinary release excludes optional84");
#endif
    printf("RENDER_DIAG enabled=%u checks=%u failures=%u\n",CF_EVENT_DIAG,checks,errors);return errors?1:0;
}
