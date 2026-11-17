/* SPDX-License-Identifier: GPL-3.0-only */
/* Real FM/VA kernels on a pthread worker. Compare PCM and voice coverage with
 * the serial run AND the pre-change build. Host cadence is not hardware FPS.
 * Args: serial|parallel|gated|refuse|nostart|partial preset output.pcm */
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int host_run(void (*fn)(uint32_t), uint32_t arg);
static int host_wait(void);
static void host_hold(void);
#define CR_C1_HOST_RUN host_run
#define CR_C1_HOST_WAIT host_wait
#define CR_C1_HOST_HOLD host_hold
#include "../tools/emu/web/emu_web.c"

static unsigned checks, failed, frames, signal_frames, peak_voices, part_mask;
static uint64_t coverage = 1469598103934665603ull;
#define CHECK(c,m) do { checks++; if (!(c)) { if (failed++ < 16) fprintf(stderr,"FAIL %d %s\n",__LINE__,m); } } while (0)
static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t worker_cv = PTHREAD_COND_INITIALIZER;
static pthread_t worker;
static void (*pending_fn)(uint32_t);
static uint32_t pending_arg;
static int pending, permit, finished, quitting, gated, refuse, partial, fault_result;
static unsigned posts, joins, holds;
#if CF_CORE1
static fm6_voice_t partial_state;
static va_voice_t partial_va;
static int32_t partial_filter[8];
static int partial_fm;
static voice_t *partial_voice;
static track_t *partial_track;
static void partial_job(void)
{
    CHECK(cf_c1.first > 0,"partial failure exercises an actual voice");
    partial_fm = ENGINES[cf_c1.t->engine] == &ENG_FM6;
    memset(cf_c1.mid, 0, sizeof cf_c1.mid); memset(cf_c1.side, 0, sizeof cf_c1.side);
    cf_c1_voice(0, 1, cf_c1.mid, cf_c1.side);
    partial_voice = cf_c1.item[0].v; partial_track = cf_c1.t;
    if (partial_fm) partial_state = *fm6_state(partial_track, partial_voice);
    else { partial_va = *va_voice(partial_track, partial_voice); memcpy(partial_filter,partial_voice->s,sizeof partial_filter); }
    cf_c1.progress = 1;
}
#endif
static void *thread_main(void *unused)
{
    (void)unused;
    pthread_mutex_lock(&mu);
    for (;;) {
        while (!quitting && (!pending || !permit)) pthread_cond_wait(&worker_cv, &mu);
        if (quitting) break;
        void (*fn)(uint32_t) = pending_fn; uint32_t arg = pending_arg;
        int inject = partial;
        pthread_mutex_unlock(&mu);
#if CF_CORE1
        if (inject && fn == cf_c1_voice_job) partial_job(); else
#endif
        fn(arg);
        pthread_mutex_lock(&mu);
        fault_result = inject ? -2 : 0;
        pending = 0; finished = 1;
        pthread_cond_broadcast(&worker_cv);
    }
    pthread_mutex_unlock(&mu); return NULL;
}
static int host_run(void (*fn)(uint32_t), uint32_t arg)
{
    if (refuse) return 0;
    pthread_mutex_lock(&mu);
    CHECK(!pending && !finished,"a completed job was joined before the next post");
    pending_fn = fn; pending_arg = arg; pending = 1; permit = !gated; posts++;
    pthread_cond_broadcast(&worker_cv); pthread_mutex_unlock(&mu); return 1;
}
static int host_wait(void)
{
    pthread_mutex_lock(&mu);
    permit = 1; pthread_cond_broadcast(&worker_cv);
    while (!finished) pthread_cond_wait(&worker_cv, &mu);
    int rc = fault_result; finished = 0; joins++;
    pthread_mutex_unlock(&mu); return rc;
}
static void host_hold(void)
{
    pthread_mutex_lock(&mu);
    permit = 1; pthread_cond_broadcast(&worker_cv);
    while (pending) pthread_cond_wait(&worker_cv, &mu);
    finished = 0; holds++; pthread_mutex_unlock(&mu);
}
static FILE *pcm;
static void pump(unsigned ms)
{
    while (ms--) {
        web_step(1);
        pthread_mutex_lock(&mu);
        int live = pending || finished;
        pthread_mutex_unlock(&mu);
        CHECK(!live,"audio/UI callback never returns with an unjoined job");
        if (live) { host_hold(); exit(4); } /* deterministic missing-join mutant */
        unsigned voices = 0;
        for (unsigned t = 0; t < NSND; t++) for (unsigned v = 0; v < NVOICE; v++) {
            voice_t *s = &trk[t].v[v];
            unsigned state = s->active | s->gate << 1 | s->note << 2;
            coverage = (coverage ^ state) * 1099511628211ull;
            if (s->active) { voices++; part_mask |= 1u << t; }
        }
        if (voices > peak_voices) peak_voices = voices;
        while (web_avail() >= 128) {
            web_render(128);
            for (unsigned i = 0; i < 128; i++) {
                int16_t pair[2] = {(int16_t)(web_out_l()[i] * 32768), (int16_t)(web_out_r()[i] * 32768)};
                if (pcm) { fwrite(pair, sizeof pair, 1, pcm); signal_frames += pair[0] != 0 || pair[1] != 0; }
            }
            if (pcm) frames += 128;
        }
        while (web_midi_out_take()) {}
    }
}
int main(int argc, char **argv)
{
    if (argc != 4) return 2;
    int serial = !strcmp(argv[1],"serial"), startup_fail = !strcmp(argv[1],"nostart");
    gated = !strcmp(argv[1],"gated");
    cr_c1_off = serial;
    cr_c1_test_nostart = startup_fail;
    CHECK(!pthread_create(&worker, NULL, thread_main, NULL),"worker created");
    web_boot(); pump(2200);
#if CF_CORE1
    CHECK(cr_dbg.c1 == (serial ? C1_OFF : startup_fail ? C1_FAILED : C1_OK),"startup/fallback status");
    CHECK(cr_dbg.rail_now[0] == 11 && cr_dbg.rail_now[1] == 3,"boot supply settings preserved");
#endif
    web_enc(EMU_E_PRESETS, atoi(argv[2]) - (int)cf_snd_pos(0)); pump(300);
    CHECK(cf_snd_pos(0) == (unsigned)atoi(argv[2]),"actual requested preset selected");
    refuse = !strcmp(argv[1],"refuse");
    pcm = fopen(argv[3],"wb"); CHECK(pcm != NULL,"PCM artifact opened"); if (!pcm) return 3;
    CHECK(cf_key_role(cs.chord.algo,5,NULL)==CF_ROLE_BUTTON && cf_key_role(cs.chord.algo,16,NULL)==CF_ROLE_ROOT,"scenario uses a chord button and root key");
    web_keys((1u<<5)|(1u<<16)); pump(1200);
    CHECK(peak_voices >= 3,"real polyphonic chord was rendered");
#if CF_CORE1
    if (!strcmp(argv[1],"partial")) {
        partial = 1;
        int32_t mid[CTL], side[CTL]; uint8_t side_on;
        unsigned before = cf_c1.faults;
        track_render_x(&trk[0],mid,side,&side_on,CTL,NULL);
        CHECK(cf_c1.faults == before + 1 && holds && cr_dbg.c1 == C1_GAVE_UP,"failure holds worker and disables it");
        if (partial_fm)
            CHECK(!memcmp(&partial_state,fm6_state(partial_track,partial_voice),sizeof partial_state),"partly advanced FM voice was not rendered twice");
        else {
            CHECK(!memcmp(&partial_va,va_voice(partial_track,partial_voice),sizeof partial_va) &&
                  !memcmp(partial_filter,partial_voice->s,7*sizeof(int32_t)),"VA oscillators, filters and envelopes were not rendered twice");
            CHECK(partial_filter[7] > 0,"VA was sounding before its output was omitted");
            CHECK(partial_voice->s[7] == 0,"VA next block ramps from the omitted output's zero gain");
        }
        CHECK(partial_voice->gate && partial_voice->active && !cf_c1.inflight,"held note survives the fallback");
        partial = 0; pump(300);
    }
#endif
    web_keys((1u<<9)|(1u<<16)); pump(800);
    web_keys((1u<<13)|(1u<<21)); pump(800);
    /* Preserve the actual sequential six-effect chain, including reordered FX. */
    cs.fx.on = (1u<<CFX_N)-1; cf_fx_apply(&cs.fx); pump(800);
    uint8_t first = cs.fx.order[0]; cs.fx.order[0] = cs.fx.order[CFX_N-1]; cs.fx.order[CFX_N-1] = first;
    cf_fx_apply(&cs.fx); pump(800);
    /* Direct input maps only CHORD/BASS. Exercise the sequencer layer's actual
     * voice entry points with its own polyphonic sound, without inventing a
     * nonexistent MIDI route to that part. */
    cf_snd_load_index(CF_PART_LAYER, (unsigned)atoi(argv[2])); pump(80);
    for (unsigned note = 60; note <= 67; note += note == 60 ? 4 : 3)
        trk_note_on(&trk[CF_PART_LAYER], note, 96);
    pump(800);
    CHECK((part_mask & 7u) == 7u,"chord, bass and layer all rendered");
    for (unsigned note = 60; note <= 67; note += note == 60 ? 4 : 3)
        trk_note_off(&trk[CF_PART_LAYER], note);
    pump(200);
    web_enc(EMU_E_PRESETS,1); pump(300); /* outgoing voice tail during preset/engine change */
    web_keys(0); pump(5000);
    for (unsigned t = 0; t < NSND; t++) for (unsigned v = 0; v < NVOICE; v++)
        CHECK(!trk[t].v[v].gate,"no held notes after release");
    fclose(pcm); pcm = NULL;
    CHECK(signal_frames > 44100 && frames > 400000,"full sample coverage with sustained sound");
#if CF_CORE1
    if (!serial && !startup_fail && !refuse) {
        CHECK(cf_c1.jobs > 100 && cf_c1.voices0 && cf_c1.voices1,"both cores render actual voices");
        CHECK(posts == joins,"all submitted jobs joined including startup");
    }
    CHECK(cf_c1.saved_ticks == 0,"host never invents hardware time savings");
#if CF_EVENT_DIAG
    const uint8_t query[] = {0x7d,0x46,0x4c,85}, bad[] = {0x7d,0x46,0x4c,85,0};
    uint8_t state[sizeof cf_c1], settings[sizeof cs];
    memcpy(state,&cf_c1,sizeof cf_c1); memcpy(settings,&cs,sizeof cs);
    unsigned writes=emu_flash_writes;
    CHECK(crb_handle(query,sizeof query),"passive worker diagnostics available");
    CHECK(CRB_OUT[4]==85 && CRB_OUT[5]==1 && CRB_OUT[6]==37 && crb_n==193,"exact bounded schema");
    CHECK(crb_r32(CRB_OUT+7+2*5)==0 && crb_r32(CRB_OUT+7+6*5)==cf_c1.jobs,"host marked nonphysical; actual completed jobs exposed");
    CHECK(!memcmp(state,&cf_c1,sizeof cf_c1) && !memcmp(settings,&cs,sizeof cs) && writes==emu_flash_writes,"passive read changes no state or flash");
    CHECK(!crb_handle(bad,sizeof bad),"no reset or write arguments accepted");
#endif
#endif
    pthread_mutex_lock(&mu); quitting = 1; pthread_cond_broadcast(&worker_cv); pthread_mutex_unlock(&mu);
    pthread_join(worker,NULL);
    printf("CORE1 mode=%s preset=%s frames=%u signal_frames=%u coverage=%016llx peak=%u parts=%x posts=%u joins=%u checks=%u failures=%u\n",
           argv[1],argv[2],frames,signal_frames,(unsigned long long)coverage,peak_voices,part_mask,posts,joins,checks,failed);
    return failed ? 1 : 0;
}
