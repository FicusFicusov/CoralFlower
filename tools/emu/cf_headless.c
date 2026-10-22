/* SPDX-License-Identifier: GPL-3.0-only
 * CoralFlower: the browser emulator's firmware side (web/emu_web.c) run natively and headless, from a script.
 *
 *   sh tools/emu/cf_headless.sh SCRIPT [OUTDIR]
 *
 * dump also counts what went out on MIDI since the start (clock, start, stop, note-ons by channel).
 * Script lines ('#' lines are comments): wait MS | key K down|up (K: 0..26 or a note name such as C4; keyq: the same with
 *   no time after it, for keys pressed at once) | clock BPM MS (MIDI clock in) | midi start|continue|stop | mnote NOTE VEL
 *   (an outside keyboard's note, channel 1; VEL 0 lets it go) | btn NAME (a tap: FX SEL
 * ENV LFO EDIT GLO HOME SAVE ARP SEQ PLAY REC OCT- OCT+) | hold NAME down|up | turn ROLE N (SELECT ALGORITHM PRESETS
 * K1..K4 MASTER) | shot NAME (OUTDIR/NAME.ppm) | dump (the firmware's state) | wav on|off (OUTDIR/out.wav)
 * CF_BOOT_MS (default 1600): how long it runs before the script; a small one lets shots catch the intro.
 * CF_FLASH=PATH: the flash image read before the boot and written after the script (a file that is not there yet:
 * a fresh flash), so a second run sees what the first one saved.
 * CF_LOADMODEL=X (macOS): the audio ISR's clock counts the host's instructions as the unit's time, X times the
 * model's (1.9: the unit as measured), so the firmware's load guard and voice count (voice.c CF_VLOAD) act as on the
 * unit; perf then also counts the clock's own reads. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern void web_boot(void);
extern void web_step(uint32_t n);
extern void web_perf_reset(void);
extern void web_perf_print(const char *label);
extern void web_load_model(uint32_t milli);
extern void web_buttons(uint32_t m);
extern void web_keys(uint32_t m);
extern int web_midi_in(uint32_t pkt);           /* a USB-MIDI packet (a 1-byte message: 0x0F | status << 8) */
extern void web_enc(int role, int32_t n);
extern void web_dump(void);
extern uint16_t *web_fb(void);
extern void web_render(uint32_t n);
extern float *web_out_l(void);
extern float *web_out_r(void);
extern uint8_t *web_flash_stage(void);
extern void web_flash_stage_len(uint32_t n);
extern uint8_t *web_flash(void);
extern uint32_t web_flash_size(void);

static const char *const BTN[14] = {"FX", "SEL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE", "ARP", "SEQ", "PLAY",
                                    "REC", "OCT-", "OCT+"};
static const char *const ROLE[8] = {"SELECT", "ALGORITHM", "PRESETS", "K1", "K2", "K3", "K4", "MASTER"};
static const char *const NN[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
static FILE *wav;
static uint32_t wav_frames;

static int find(const char *const *t, int n, const char *s)
{
    int i;
    for (i = 0; i < n; i++)
        if (!strcmp(t[i], s))
            return i;
    return -1;
}
static int key_of(const char *s)
{
    int k;
    char b[8];
    for (k = 0; k < 27; k++) {
        int m = 53 + k;
        snprintf(b, sizeof b, "%s%d", NN[m % 12], m / 12 - 1);
        if (!strcmp(b, s))
            return k;
    }
    return atoi(s);
}
static void wav_hdr(FILE *f, uint32_t frames)
{
    uint32_t data = frames * 4u, v;
    fseek(f, 0, SEEK_SET);
    fwrite("RIFF", 1, 4, f); v = 36 + data; fwrite(&v, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f);
    v = 16; fwrite(&v, 4, 1, f); v = 0x00020001u; fwrite(&v, 4, 1, f); v = 44100; fwrite(&v, 4, 1, f);
    v = 44100 * 4; fwrite(&v, 4, 1, f); v = 0x00100004u; fwrite(&v, 4, 1, f); fwrite("data", 1, 4, f); fwrite(&data, 4, 1, f);
}
extern uint32_t web_midi_out_take(void);
static uint32_t mo_count[256];                   /* MIDI out, drained as it runs: packets by status byte (dump) */
static void mo_drain(void)
{
    uint32_t p;
    while ((p = web_midi_out_take()) != 0u)
        mo_count[(p >> 8) & 0xFFu]++;
}
extern uint32_t emu_render_ms;                   /* (emu_web.c: a device's frame time, CF_RENDER_MS) */
static void step(uint32_t ms)
{
    if (!wav) {
        while (ms) {                             /* (10 ms at a time: MIDI out drained before its queue fills) */
            uint32_t k = ms > 10u ? 10u : ms;
            web_step(k);
            mo_drain();
            ms -= k;
        }
        return;
    }
    while (ms--) {   /* 1 ms = 44.1 frames: render in 441-frame chunks every 10 ms */
        static uint32_t acc;
        web_step(1);
        mo_drain();
        if (++acc >= 10u) {
            uint32_t i;
            int16_t s[441 * 2];
            acc = 0;
            web_render(441);
            for (i = 0; i < 441u; i++) {
                float l = web_out_l()[i], r = web_out_r()[i];
                s[2 * i] = (int16_t)(l > 1 ? 32767 : l < -1 ? -32767 : l * 32767);
                s[2 * i + 1] = (int16_t)(r > 1 ? 32767 : r < -1 ? -32767 : r * 32767);
            }
            fwrite(s, 4, 441, wav);
            wav_frames += 441;
        }
    }
}
static void shot(const char *dir, const char *name)
{
    char p[512];
    FILE *f;
    uint16_t *fb = web_fb();
    int i;
    snprintf(p, sizeof p, "%s/%s.ppm", dir, name);
    f = fopen(p, "wb");
    if (!f)
        return;
    fprintf(f, "P6\n240 240\n255\n");
    for (i = 0; i < 240 * 240; i++) {
        uint16_t u = (uint16_t)((fb[i] >> 8) | (fb[i] << 8));
        unsigned char c[3] = {(unsigned char)(((u >> 11) & 31) * 255 / 31), (unsigned char)(((u >> 5) & 63) * 255 / 63),
                              (unsigned char)((u & 31) * 255 / 31)};
        fwrite(c, 1, 3, f);
    }
    fclose(f);
}

int main(int argc, char **argv)
{
    char line[256], dir[256];
    FILE *s;
    uint32_t keys = 0, buttons = 0;
    if (argc < 2) {
        fprintf(stderr, "usage: cf_headless SCRIPT [OUTDIR]\n");
        return 2;
    }
    s = fopen(argv[1], "r");
    if (!s) {
        perror(argv[1]);
        return 2;
    }
    snprintf(dir, sizeof dir, "%s", argc > 2 ? argv[2] : "build/cf-run");
    if (getenv("CF_FLASH")) {                    /* a flash image to start from (a later run: the settings kept) */
        FILE *fl = fopen(getenv("CF_FLASH"), "rb");
        if (fl) {
            size_t n = fread(web_flash_stage(), 1, web_flash_size(), fl);
            web_flash_stage_len((uint32_t)n);
            fclose(fl);
        }
    }
    if (getenv("CF_RENDER_MS"))                 /* (a device's frame: the keys wait while it draws) */
        emu_render_ms = (uint32_t)atoi(getenv("CF_RENDER_MS"));
    if (getenv("CF_LOADMODEL"))
        web_load_model((uint32_t)(atof(getenv("CF_LOADMODEL")) * 1000.0 + 0.5));
    web_boot();
    step(getenv("CF_BOOT_MS") ? (uint32_t)atoi(getenv("CF_BOOT_MS")) : 1600u);   /* (0: the script sees the intro) */
    while (fgets(line, sizeof line, s)) {
        char cmd[32] = "", a[32] = "", b[32] = "";
        const char *q = line;
        while (*q == ' ' || *q == '\t')
            q++;
        if (*q == '#')                           /* a comment line ('#' elsewhere is a sharp: A#4) */
            continue;
        if (sscanf(line, "%31s %31s %31s", cmd, a, b) < 1)
            continue;
        if (!strcmp(cmd, "wait"))
            step((uint32_t)atoi(a));
        else if (!strcmp(cmd, "key") || !strcmp(cmd, "keyq")) {   /* keyq: no time after it (keys at once) */
            int k = key_of(a);
            keys = !strcmp(b, "up") ? keys & ~(1u << k) : keys | (1u << k);
            web_keys(keys);
            if (!strcmp(cmd, "key"))
                step(20);
        } else if (!strcmp(cmd, "btn") || !strcmp(cmd, "hold")) {
            int i = find(BTN, 14, a);
            if (i < 0)
                continue;
            if (!strcmp(cmd, "btn")) {
                web_buttons(buttons | (1u << i));
                step(60);
                web_buttons(buttons);
                step(60);
            } else {
                buttons = !strcmp(b, "up") ? buttons & ~(1u << i) : buttons | (1u << i);
                web_buttons(buttons);
                step(20);
            }
        } else if (!strcmp(cmd, "clock")) {        /* clock BPM MS: MIDI clock pulses (24 a beat) for MS ms */
            double per = 60000.0 / (atof(a) * 24.0), t = 0, next = 0;
            int ms = atoi(b), done = 0;
            while (done < ms) {
                if (t >= next) {
                    web_midi_in(0x0Fu | 0xF8u << 8);
                    next += per;
                }
                step(1);
                done++;
                t += 1.0;
            }
        } else if (!strcmp(cmd, "mnote")) {        /* mnote NOTE VEL: an outside keyboard's note on channel 1 (VEL 0: off) */
            int nt = atoi(a) & 127, vl = atoi(b) & 127;
            web_midi_in(vl ? 0x09u | 0x90u << 8 | (uint32_t)nt << 16 | (uint32_t)vl << 24
                           : 0x08u | 0x80u << 8 | (uint32_t)nt << 16);
            step(1);
        } else if (!strcmp(cmd, "midi")) {         /* midi start | continue | stop */
            web_midi_in(0x0Fu | (!strcmp(a, "start") ? 0xFAu : !strcmp(a, "continue") ? 0xFBu : 0xFCu) << 8);
            step(1);
        } else if (!strcmp(cmd, "turn")) {
            int r = find(ROLE, 8, a);
            if (r >= 0)
                web_enc(r, atoi(b));
            step(40);
        } else if (!strcmp(cmd, "shot")) {
            step(40);
            shot(dir, a);
        } else if (!strcmp(cmd, "perf")) {         /* perf reset | perf LABEL: the audio blocks' cost since the reset */
            if (!strcmp(a, "reset"))
                web_perf_reset();
            else
                web_perf_print(a);
        } else if (!strcmp(cmd, "dump")) {
            uint32_t on = 0, c;
            web_dump();
            for (c = 0x90u; c <= 0x9Fu; c++)
                on += mo_count[c];
            printf("  midi out: clock F8 %u, start FA %u, stop FC %u, notes on %u (ch 1 %u, 2 %u, 3 %u, 10 %u)\n",
                   (unsigned)mo_count[0xF8], (unsigned)mo_count[0xFA], (unsigned)mo_count[0xFC], (unsigned)on,
                   (unsigned)mo_count[0x90], (unsigned)mo_count[0x91], (unsigned)mo_count[0x92], (unsigned)mo_count[0x99]);
        } else if (!strcmp(cmd, "wav")) {
            if (!strcmp(a, "on") && !wav) {
                char p[512];
                snprintf(p, sizeof p, "%s/out.wav", dir);
                wav = fopen(p, "wb");
                if (wav)
                    wav_hdr(wav, 0);
            } else if (wav) {
                wav_hdr(wav, wav_frames);
                fclose(wav);
                wav = 0;
            }
        }
    }
    if (wav) {
        wav_hdr(wav, wav_frames);
        fclose(wav);
    }
    if (getenv("CF_FLASH")) {                    /* .. and the flash as the run left it */
        FILE *fl = fopen(getenv("CF_FLASH"), "wb");
        if (fl) {
            fwrite(web_flash(), 1, web_flash_size(), fl);
            fclose(fl);
        }
    }
    return 0;
}
