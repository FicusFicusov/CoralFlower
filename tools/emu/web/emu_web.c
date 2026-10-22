/* SPDX-License-Identifier: GPL-3.0-only */
/* CoralFlower in the browser: the emulator's firmware side (../emu_fw.c: the firmware through emu_firmware.h,
 * the HAL of emu_hal_fw.h, the emu_fw_* hooks) compiled to a standalone WebAssembly module (no Emscripten JS
 * runtime) and run inside an AudioWorklet (worklet.js). Each render quantum asks for 128 frames; producing them
 * runs the device clock one millisecond at a time exactly as emu.c's headless loop does (tick, then a UI frame
 * every 15 ms or the idle input scan, then the 128-frame audio blocks that have fallen due), so the same input at
 * the same millisecond gives the same samples as build/host/emu --headless.
 *
 * The emulator's threads become one: the CPU lock is a no-op (compat/os/lock.h), time is simulated (emu_sim).
 * The 1 MiB flash is emu_hal_fw.h's RAM image; its file is replaced by the page's IndexedDB copy: the page writes
 * the saved image into web_flash_stage() before web_boot (emu_flash_open "reads" it from there), and saves
 * web_flash() back when web_flash_dirty() moves.
 *
 *   sh tools/emu/web/build_web.sh  ->  build/emu-web/coralflower.wasm (+ the page)
 * tools/emu/cf_headless.c runs this same unit natively, from a script (tools/emu/cf_headless.sh). */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ---- the flash "file": emu_flash_open / emu_flash_sync use stdio on emu_flash_path; here it is the stage ---- */
#define WEB_FLASH_SIZE 0x100000u
static uint8_t web_stage[WEB_FLASH_SIZE];
static uint32_t web_stage_len;                    /* bytes the page put there (0: a fresh flash) */
static uint32_t web_stage_pos;
static FILE *const WEB_FLASH_F = (FILE *)&web_stage_pos;   /* a handle nothing dereferences */
static FILE *web_fopen(const char *path, const char *mode)
{
    if (strcmp(path, "web:flash"))
        return NULL;                              /* (no other file in the browser) */
    web_stage_pos = 0;
    return mode[0] == 'r' && !web_stage_len ? NULL : WEB_FLASH_F;
}
static size_t web_fread(void *dst, size_t sz, size_t n, FILE *f)
{
    size_t want = sz * n, have;
    if (f != WEB_FLASH_F)
        return fread(dst, sz, n, f);
    have = web_stage_pos < web_stage_len ? web_stage_len - web_stage_pos : 0;
    if (want > have)
        want = have;
    memcpy(dst, web_stage + web_stage_pos, want);
    web_stage_pos += (uint32_t)want;
    return sz ? want / sz : 0;
}
static size_t web_fwrite(const void *src, size_t sz, size_t n, FILE *f)
{
    if (f != WEB_FLASH_F)
        return fwrite(src, sz, n, f);
    return n;                                     /* emu_flash[] is the truth; the page reads it (web_flash) */
}
static int web_fseek(FILE *f, long off, int wh) { return f == WEB_FLASH_F ? 0 : fseek(f, off, wh); }
static int web_fflush(FILE *f) { return f == WEB_FLASH_F ? 0 : fflush(f); }
#define fopen web_fopen
#define fread web_fread
#define fwrite web_fwrite
#define fseek web_fseek
#define fflush web_fflush

#include "../emu_fw.c"

#undef fopen
#undef fread
#undef fwrite
#undef fseek
#undef fflush

#undef __attribute__                               /* (hostsim.c defines it away for the firmware) */
#define WEB_EXPORT __attribute__((used, visibility("default")))

/* ------------------------------------------------------------ the device clock --- */
#define RING 8192u                                /* stereo frames, float */
static float ring_l[RING], ring_r[RING];
static uint32_t ring_w, ring_r_;
static uint32_t dev_ms, last_frame;               /* emu.c bench(): ms, last_frame */
static uint64_t frames_done;
static uint8_t booted;
uint32_t emu_render_ms;                           /* (tests, cf_headless CF_RENDER_MS: how long a frame draws on the
                                                   * device, the keys not read while it does; 0: as the emulator) */

/* tests (cf_headless `perf`): the audio blocks' cost as host instructions, and the device estimate emu.c makes from
 * them (5882 host instructions a sample = the whole block; tests/fm6_test.c's ratio from the device) */
#if defined(__APPLE__) && !defined(__EMSCRIPTEN__)
#include <libproc.h>
#include <unistd.h>
static uint64_t perf_instr(void)
{
    struct rusage_info_v4 ri;
    return proc_pid_rusage(getpid(), RUSAGE_INFO_V4, (rusage_info_t *)&ri) ? 0u : ri.ri_instructions;
}
#else
static uint64_t perf_instr(void) { return 0; }
#endif
void web_load_model(uint32_t milli)              /* cf_headless CF_LOADMODEL: the ISR's clock (emu_hal_fw.h) */
{
    uint64_t a, b, lo = ~(uint64_t)0;
    uint32_t k, r;
    for (r = 0; r < 8u; r++) {                    /* a read's own cost: the least of a few runs */
        a = perf_instr();
        for (k = 0; k < 64u; k++)
            perf_instr();
        b = perf_instr();
        lo = (b - a) / 65u < lo ? (b - a) / 65u : lo;
    }
    emu_model_ovh = (uint32_t)lo;
    emu_model_milli = milli;
    emu_model_instr = milli ? perf_instr : 0;
}
uint8_t web_perf_on;
static uint64_t perf_n, perf_sum, perf_max, perf_over70;
void web_perf_reset(void) { perf_n = perf_sum = perf_max = perf_over70 = 0; web_perf_on = 1; }
void web_perf_print(const char *label)
{
    double full = 5882.0 * EMU_BLOCK;
    printf("perf %s: %llu blocks, device estimate avg %.1f %%, max %.1f %%, blocks over 70 %%: %llu\n", label,
           (unsigned long long)perf_n, perf_n ? 100.0 * (double)perf_sum / (double)perf_n / full : 0.0,
           100.0 * (double)perf_max / full, (unsigned long long)perf_over70);
}

static void run_ms(void)                          /* one millisecond of emu.c's headless loop (bench) */
{
    uint32_t ms = dev_ms++;
    emu_fw_tick(ms);
    if (ms == 0 || ms - last_frame >= 15u + emu_render_ms) {
        last_frame = ms;
        emu_fw_frame();
    } else if (ms - last_frame >= emu_render_ms) {
        emu_fw_idle();
    }
    while (frames_done + EMU_BLOCK <= (uint64_t)ms * EMU_FS / 1000u) {
        int16_t blk[EMU_BLOCK * 2];
        uint32_t k;
        if (web_perf_on) {
            uint64_t c0 = emu_model_calls, i0 = perf_instr(), di;
            emu_fw_audio(blk, EMU_BLOCK);
            di = perf_instr() - i0;
            if (emu_model_instr) {                /* (CF_LOADMODEL: the clock's own reads out) */
                uint64_t own = (emu_model_calls - c0 + 1u) * emu_model_ovh;
                di = di > own ? di - own : 0u;
            }
            perf_n++;
            perf_sum += di;
            perf_max = di > perf_max ? di : perf_max;
            perf_over70 += di > (uint64_t)(0.7 * 5882.0 * EMU_BLOCK);
        } else {
            emu_fw_audio(blk, EMU_BLOCK);
        }
        for (k = 0; k < EMU_BLOCK; k++) {
            uint32_t i = ring_w++ % RING;
            ring_l[i] = (float)blk[2 * k] * (1.0f / 32768.0f);
            ring_r[i] = (float)blk[2 * k + 1] * (1.0f / 32768.0f);
        }
        frames_done += EMU_BLOCK;
    }
}

static float out_l[1024], out_r[1024];
WEB_EXPORT float *web_out_l(void) { return out_l; }
WEB_EXPORT float *web_out_r(void) { return out_r; }

/* n stereo frames (<= 1024) into web_out_l / web_out_r: the device runs until they exist */
WEB_EXPORT void web_render(uint32_t n)
{
    uint32_t k;
    if (n > 1024u)
        n = 1024u;
    if (!booted) {
        memset(out_l, 0, n * sizeof out_l[0]);
        memset(out_r, 0, n * sizeof out_r[0]);
        return;
    }
    while (ring_w - ring_r_ < n)
        run_ms();
    for (k = 0; k < n; k++) {
        uint32_t i = ring_r_++ % RING;
        out_l[k] = ring_l[i];
        out_r[k] = ring_r[i];
    }
}
/* tests: run the clock N ms without taking the audio (take it with web_avail + web_render) */
WEB_EXPORT void web_step(uint32_t n)
{
    while (booted && n--)
        run_ms();
}
WEB_EXPORT uint32_t web_avail(void) { return ring_w - ring_r_; }
WEB_EXPORT uint32_t web_ms(void) { return dev_ms; }

/* ------------------------------------------------------------------- power-on --- */
WEB_EXPORT uint8_t *web_flash_stage(void) { return web_stage; }
WEB_EXPORT void web_flash_stage_len(uint32_t n) { web_stage_len = n > WEB_FLASH_SIZE ? WEB_FLASH_SIZE : n; }
WEB_EXPORT void web_boot(void)
{
    if (booted)
        return;
    emu_fw_options("web:flash", 0, 0, 0);         /* a flash with a "file": the stage in, nothing out */
    emu_hal.ready = 2;                            /* simulated time (fm1_ticks = the device clock) */
    emu_fw_init(0);
    booted = 1;
}

/* ---------------------------------------------------------------------- input --- */
/* keys: bit n = note key n (0 = F3 .. 26 = G5); a key newly down also taps (a press shorter than 1 ms counts) */
WEB_EXPORT void web_keys(uint32_t m)
{
    m &= (1u << EMU_NKEY) - 1u;
    emu_hal.keys_tap |= m & ~emu_hal.keys;
    emu_hal.keys = m;
}
static uint32_t btn_matrix(uint32_t m)            /* label bits (EMU_B_*) -> matrix bits */
{
    uint32_t i, b = 0;
    for (i = 0; i < EMU_NB; i++)
        if (m & (1u << i))
            b |= 1u << emu_hal.btn_id[i];
    return b;
}
/* buttons: bit i = label i in EMU_B_* order (FX SEL ENV LFO EDIT GLO HOME SAVE ARP SEQ PLAY REC OCT- OCT+) */
WEB_EXPORT void web_buttons(uint32_t m)
{
    uint32_t b = btn_matrix(m);
    emu_hal.buttons_tap |= b & ~emu_hal.buttons;
    emu_hal.buttons = b;
}
WEB_EXPORT void web_keys_tap(uint32_t m) { emu_hal.keys_tap |= m & ((1u << EMU_NKEY) - 1u); }
WEB_EXPORT void web_buttons_tap(uint32_t m) { emu_hal.buttons_tap |= btn_matrix(m); }
/* role: EMU_E_* (SELECT ALGORITHM PRESETS KNOB1..4 MASTER); n detents, + clockwise (MASTER: n x 16 of 1023) */
WEB_EXPORT void web_enc(int role, int32_t n)
{
    if (role == EMU_E_MASTER) {
        int32_t m = emu_hal.master + n * 16;
        emu_hal.master = m < 0 ? 0 : m > 1023 ? 1023 : m;
    } else if (role >= 0 && role < EMU_NE - 1) {
        emu_hal.enc[emu_hal.enc_id[role]] += n * emu_hal.enc_dir[role];
    }
}
WEB_EXPORT void web_master(int32_t v) { emu_hal.master = v < 0 ? 0 : v > 1023 ? 1023 : v; }
WEB_EXPORT int32_t web_master_get(void) { return emu_hal.master; }

/* --------------------------------------------------------------------- output --- */
WEB_EXPORT uint16_t *web_fb(void) { return emu_hal.lcd; }          /* 240 x 240 RGB565, big-endian */
WEB_EXPORT uint32_t web_lcd_writes(void) { return emu_hal.lcd_writes; }
WEB_EXPORT uint32_t web_led(uint32_t col) { return col < EMU_NCOL ? emu_hal.led[col] : 0; }
WEB_EXPORT uint32_t web_led_dim(uint32_t col) { return col < EMU_NCOL ? emu_hal.led_dim[col] : 0; }
WEB_EXPORT const int8_t *web_keymap(void) { return &emu_keymap[0][0]; }   /* [6][EMU_NCOL]: id at (row, col) */
WEB_EXPORT const uint8_t *web_btn_id(void) { return emu_hal.btn_id; }
WEB_EXPORT const uint8_t *web_enc_id(void) { return emu_hal.enc_id; }
WEB_EXPORT const int8_t *web_enc_dir(void) { return emu_hal.enc_dir; }
WEB_EXPORT uint32_t web_play_green(void) { return emu_hal.led_play_green; }   /* (col << 3) | row */

/* every LED at once: [0..26] note keys, [27..40] buttons in label order, [41] PLAY's green; 0 off 1 dim 2 lit
 * 3 mid (fm1_led_dim2: the middle level, Key Mode's tonic) 4 faint (fm1_led_faint: the idle chord keys) */
static uint8_t leds[42];
static int led_of(int id)
{
    int r, c;
    for (r = 1; r < 5; r++)
        for (c = 0; c < EMU_NCOL; c++)
            if (emu_keymap[r][c] == id)
                return (emu_hal.led[c] >> r) & 1u ? 2 : (emu_hal.led_mid[c] >> r) & 1u ? 3 : (emu_hal.led_dim[c] >> r) & 1u ? 1
                       : (emu_hal.led_faint[c] >> r) & 1u ? 4 : 0;
    return 0;
}
WEB_EXPORT uint8_t *web_leds(void)
{
    uint32_t i, q = emu_hal.led_play_green;
    for (i = 0; i < EMU_NKEY; i++)
        leds[i] = (uint8_t)led_of(14 + (int)i);
    for (i = 0; i < EMU_NB; i++)
        leds[EMU_NKEY + i] = (uint8_t)led_of(emu_hal.btn_id[i]);
    leds[41] = (uint8_t)(((emu_hal.led[q >> 3] >> (q & 7u)) & 1u) * 2u);
    return leds;
}

/* MIDI: USB-MIDI event packets (byte 0 = cable << 4 | CIN in bits 0..7, then status, data1, data2) */
WEB_EXPORT uint32_t web_midi_out_take(void)
{
    uint32_t p;
    return booted && emu_fw_midi_out_take(&p) ? p : 0u;
}
WEB_EXPORT int web_midi_in(uint32_t pkt) { return booted ? emu_fw_midi_in(pkt) : 0; }

/* the flash: the page keeps it in IndexedDB */
WEB_EXPORT uint8_t *web_flash(void) { return emu_flash; }
WEB_EXPORT uint32_t web_flash_size(void) { return EMU_FLASH_SIZE; }
WEB_EXPORT uint32_t web_flash_dirty(void) { return emu_flash_writes; }  /* erases + programs so far */

WEB_EXPORT const char *web_version(void) { return FELUCCA_VERSION; }
WEB_EXPORT uint32_t web_cpu(void) { return song.cpu_q8 * 100u / 256u; }
WEB_EXPORT void web_dump(void) { emu_fw_dump(); }                    /* to stdout (fd_write) */
