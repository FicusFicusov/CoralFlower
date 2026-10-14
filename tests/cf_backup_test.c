/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca); CoralFlower contributors */
/* Host test of CoralFlower's backup / restore SysEx handler (firmware/src/cf_backup.c), after ChoralRoot's
 * tests/cr_backup_test.c: the firmware as the emulator builds it (tools/emu/emu_firmware.h, storage.c on the RAM NOR),
 * requests fed to crb_handle as the main loop would, the replies taken at CRB_SEND.
 *   cc -std=gnu11 -O1 -w -Ibuild/gen -Ifirmware/src -Itests -o build/host/cf_backup_test tests/cf_backup_test.c -lm
 *   ./build/host/cf_backup_test
 * Checks: INFO (the version, the tags); LIST of the 8 objects; a sound, the settings and a loop saved, then backed up
 * whole (GET in 256-byte pieces, every CRC as LIST says); the flash erased and everything restored (PUT): LIST and the
 * bytes identical, the sound back in its list; refusals with nothing written: a CRC error, a settings record of
 * another magic, an unsorted loop, ChoralRoot's ids 1 and 40, a begin while the loop plays (busy); stale sessions;
 * the settings lock after a restore; RESTART. */
#include <os/lock.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../tools/emu/emu_hooks.h"

emu_hal_t emu_hal;
const int8_t emu_keymap[6][EMU_NCOL] = {
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    { 5, 11,  4, 10,  3,  9,  2,  8, -1, -1, -1},
    {34, 35, 36, 37, 38, 40, 39, 13,  7,  6, 12},
    {23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33},
    { 0,  1, 15, 14, 17, 16, 19, 18, 20, 21, 22},
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
};

static uint8_t rx[4096];
static uint32_t rx_n, rx_count;
#define CRB_SEND(p, n) (memcpy(rx, (p), (n)), rx_n = (n), rx_count++)
#include "../tools/emu/emu_firmware.h"

static int fails, checks;
#define CHECK(c, ...)                                                   \
    do {                                                                \
        checks++;                                                       \
        if (!(c)) {                                                     \
            fails++;                                                    \
            printf("  FAIL %s:%d: ", __FILE__, __LINE__);               \
            printf(__VA_ARGS__);                                        \
            printf("\n");                                               \
        }                                                               \
    } while (0)

/* ------------------------------------------------------------------- the wire --- */
static uint8_t tx[1024];
static uint32_t txn;
static void t_begin(uint32_t cmd) { tx[0] = 0x7D; tx[1] = 0x46; tx[2] = 0x4C; tx[3] = (uint8_t)cmd; txn = 4; }
static void t_b(uint32_t v) { tx[txn++] = (uint8_t)(v & 127u); }
static void t_u32(uint32_t v) { for (int i = 0; i < 5; i++) t_b(v >> (7 * i)); }
static void t_pack(const uint8_t *p, uint32_t n)
{
    while (n) {
        uint32_t k = n > 7u ? 7u : n, m = 0, i;
        for (i = 0; i < k; i++) m |= (uint32_t)(p[i] >> 7) << i;
        t_b(m);
        for (i = 0; i < k; i++) t_b(p[i]);
        p += k; n -= k;
    }
}
static const uint8_t *t_send(uint32_t *n)
{
    uint32_t before = rx_count;
    crb_handle(tx, txn);
    if (rx_count == before) { *n = 0; return 0; }
    if (rx_n < 6 || rx[0] != 0xF0 || rx[1] != 0x7D || rx[2] != 0x46 || rx[3] != 0x4C || rx[4] != tx[3] || rx[rx_n - 1] != 0xF7) {
        printf("  bad reply frame\n");
        fails++;
        *n = 0;
        return 0;
    }
    for (uint32_t i = 1; i + 1 < rx_n; i++)
        if (rx[i] > 127u) { printf("  8-bit byte in a reply\n"); fails++; }
    *n = rx_n - 6;
    return rx + 5;
}
static uint32_t r32(const uint8_t *p) { return p[0] | p[1] << 7 | p[2] << 14 | (uint32_t)p[3] << 21 | (uint32_t)p[4] << 28; }
static uint32_t unpack(const uint8_t *a, uint32_t na, uint8_t *out)
{
    uint32_t n = 0;
    while (na) {
        uint32_t m = *a++, k;
        na--;
        k = na > 7 ? 7 : na;
        for (uint32_t j = 0; j < k; j++, na--) out[n++] = (uint8_t)(*a++ | ((m >> j) & 1u) << 7);
    }
    return n;
}

/* ------------------------------------------------------------------ the objects --- */
typedef struct { uint8_t id; uint32_t size, crc; uint8_t bytes[8192]; } obj_t;
static obj_t man[16];
static uint32_t nman;
static int t_list(void)                              /* -> rc; man[] filled (sizes, CRCs) */
{
    uint32_t n, i;
    const uint8_t *a;
    t_begin(65);
    a = t_send(&n);
    if (!a || n < 3 || a[0] != 1)
        return -1;
    nman = a[2];
    if (n != 3u + nman * 11u)
        return -2;
    for (i = 0; i < nman; i++) {
        man[i].id = a[3 + i * 11];
        man[i].size = r32(a + 4 + i * 11);
        man[i].crc = r32(a + 9 + i * 11);
    }
    return a[1];
}
static int t_get_all(obj_t *o)                       /* every byte of o (after a LIST), 256 at a time -> 0 */
{
    uint32_t off = 0, n;
    const uint8_t *a;
    while (off < o->size) {
        uint32_t c = o->size - off > 256u ? 256u : o->size - off;
        t_begin(66); t_b(o->id); t_u32(off); t_b(c & 127u); t_b(c >> 7);
        a = t_send(&n);
        if (!a || n < 8 || a[1] != 0 || r32(a + 2) != off)
            return -1;
        if (unpack(a + 9, n - 9, o->bytes + off) != c)
            return -2;
        off += c;
    }
    return st_crc32(o->bytes, o->size) == o->crc ? 0 : -3;
}
static int t_put(uint8_t id, const uint8_t *p, uint32_t len, uint32_t crc)   /* -> the commit's rc (or an earlier one) */
{
    uint32_t n, off = 0;
    const uint8_t *a;
    t_begin(67); t_b(0); t_b(id); t_u32(len); t_u32(crc);
    a = t_send(&n);
    if (!a || n != 3 || a[2])
        return a ? a[2] : -1;
    while (off < len) {
        uint32_t c = len - off > 256u ? 256u : len - off;
        t_begin(67); t_b(1); t_b(id); t_u32(off); t_pack(p + off, c);
        a = t_send(&n);
        if (!a || a[2])
            return a ? a[2] : -1;
        off += c;
    }
    t_begin(67); t_b(2); t_b(id);
    a = t_send(&n);
    return a ? a[2] : -1;
}
static obj_t *t_find(uint8_t id)
{
    for (uint32_t i = 0; i < nman; i++)
        if (man[i].id == id)
            return &man[i];
    return 0;
}

/* a loop of nev events, saved as SAVE > Loop does (the length of what it holds) */
static uint32_t make_loop(uint32_t nev, int sorted)
{
    static cfl_data_t d;
    uint32_t i, len;
    memset(&d, 0, sizeof d);
    d.magic = CFL_MAGIC;
    d.len = 4u * CFL_PPQN * 8u;                     /* (long enough: the events stay in time order) */
    d.nlayers = 1;
    d.beats = 4;
    d.nev = (uint16_t)nev;
    for (i = 0; i < nev; i++) {
        d.ev[i].t = (uint16_t)(sorted ? i * 24u : (nev - i) * 24u);
        d.ev[i].key = (uint8_t)(i % 27u);
        d.ev[i].f = (uint8_t)(i & 1u);
    }
    len = (uint32_t)__builtin_offsetof(cfl_data_t, ev) + nev * (uint32_t)sizeof(cfl_ev_t);
    memcpy(cfb_raw, &d, len);                        /* (the bytes, for the refusal test below) */
    if (sorted)
        st_save(OBJ_CFLOOP, &d, len);
    return len;
}

int main(void)
{
    uint32_t n, i, k;
    const uint8_t *a;
    static obj_t ref[16];
    uint32_t nref;

    emu_flash_open();
    memset(emu_flash, 0xFF, sizeof emu_flash);
    cr_bank_boot();
    cf_ui_init();

    /* ---- INFO ---- */
    t_begin(1);
    a = t_send(&n);
    CHECK(a && n > 12 && !memcmp(a, "CoralFlower", 11) && a[n - 6] == 0x42 && a[n - 4] == 3 && a[n - 3] == 0x43 &&
          a[n - 1] == 2, "INFO: \"CoralFlower ...\", tags 42 01 03 and 43 01 02 (%s)", a ? (const char *)a : "none");
    t_begin(2); t_b(0); t_b(0);
    CHECK(!t_send(&n), "an editor command (2) gets no reply");

    /* ---- GET before LIST: rc 5 ---- */
    t_begin(66); t_b(6); t_u32(0); t_b(16); t_b(0);
    a = t_send(&n);
    CHECK(a && a[1] == 5, "GET before LIST: 5");

    /* ---- a fresh device: the 8 objects, nothing stored but what the boot wrote ---- */
    CHECK(t_list() == 0 && nman == 8, "LIST on a fresh flash: 8 objects (%u)", nman);

    /* ---- content: a user sound, the settings, a loop ---- */
    CHECK(cf_snd_save(CF_PART_CHORD, 1, "BACKUP TEST") == 0, "a chord sound saved as new");
    cs.bpm = 97;
    CHECK(cf_settings_write() == 1, "the settings written");
    make_loop(40, 1);
    CHECK(t_list() == 0, "LIST");
    for (i = 0, k = 0; i < nman; i++) k += man[i].size != 0;
    CHECK(t_find(6)->size && t_find(30)->size && t_find(31)->size, "LIST: the bank, the settings and the loop hold data");
    for (i = 0; i < nman; i++)
        CHECK(t_get_all(&man[i]) == 0, "GET of object %u (%u B): every byte, the CRC as LIST says", man[i].id, man[i].size);
    memcpy(ref, man, sizeof man);
    nref = nman;

    /* ---- a fresh device again, everything restored ---- */
    memset(emu_flash, 0xFF, sizeof emu_flash);
    cr_bank_boot();
    CHECK(t_list() == 0, "LIST on the erased flash");
    for (i = 0; i < nref; i++)
        if (ref[i].size)
        {
            int rc = t_put(ref[i].id, ref[i].bytes, ref[i].size, ref[i].crc);
            CHECK(rc == 0, "PUT of object %u (%u B): rc %d", ref[i].id, ref[i].size, rc);
        }
    CHECK(cfs_restore_lock == 1, "the settings restored: the lock is on (no save before the restart)");
    CHECK(t_list() == 0 && nman == nref, "LIST after the restore");
    for (i = 0; i < nref; i++) {
        obj_t *o = t_find(ref[i].id);
        CHECK(o && o->size == ref[i].size && o->crc == ref[i].crc, "object %u: size and CRC as backed up", ref[i].id);
        if (o && o->size && t_get_all(o) == 0)
            CHECK(!memcmp(o->bytes, ref[i].bytes, o->size), "object %u: the same bytes", ref[i].id);
    }
    {                                                /* the sound is back in the chord list */
        uint32_t e, pos, found = 0;
        char nm[13];
        for (i = 0; i < cf_snd_count(CF_PART_CHORD); i++) {
            cf_snd_at(CF_PART_CHORD, i, &e, &pos);
            pool_name(e, pos, nm);
            found |= !strcmp(nm, "BACKUP TEST");
        }
        CHECK(found, "the restored sound is in the chord list");
    }
    {
        cf_rec_t r;
        CHECK(st_load(CF_SET_OBJ, &r, sizeof r) > 0 && r.s.bpm == 97, "the restored settings hold the tempo 97");
    }

    /* ---- refusals: nothing written ---- */
    {
        obj_t *s = t_find(30);
        uint32_t crc_before = s->crc;
        CHECK(t_put(30, s->bytes, s->size, s->crc ^ 1u) == 2, "a CRC error: 2");
        s->bytes[0] ^= 0x55;                         /* another magic */
        CHECK(t_put(30, s->bytes, s->size, st_crc32(s->bytes, s->size)) == 2, "a settings record of another magic: 2");
        s->bytes[0] ^= 0x55;
        CHECK(t_list() == 0 && t_find(30)->crc == crc_before, "nothing written by the refused PUTs");
    }
    {
        uint32_t len = make_loop(10, 0);
        CHECK(t_put(31, cfb_raw, len, st_crc32(cfb_raw, len)) == 2, "a loop out of time order: 2");
    }
    {
        static uint8_t z[64];
        CHECK(t_put(1, z, sizeof z, st_crc32(z, sizeof z)) == 1, "ChoralRoot's settings (id 1): 1");
        CHECK(t_put(40, z, sizeof z, st_crc32(z, sizeof z)) == 1, "ChoralRoot's loop slot (id 40): 1");
    }
    {
        obj_t *s = t_find(31);
        cf_sn.loop_state = CFL_PLAYING;
        CHECK(t_put(31, s->bytes, s->size, s->crc) == 3, "a begin while the loop plays: 3 (busy)");
        cf_sn.loop_state = CFL_STOPPED;
        CHECK(t_put(31, s->bytes, s->size, s->crc) == 0, "the same once it stops: 0");
    }
    {                                                /* a stale session: a data packet without a begin */
        static uint8_t z[16];
        t_begin(67); t_b(1); t_b(31); t_u32(0); t_pack(z, sizeof z);
        a = t_send(&n);
        CHECK(a && a[2] == 5, "data without a begin: 5");
    }
    {                                                /* the lock: a settings save does nothing */
        cf_rec_t before, after;
        st_load(CF_SET_OBJ, &before, sizeof before);
        cs.bpm = 133;
        settings_save();
        st_load(CF_SET_OBJ, &after, sizeof after);
        CHECK(!memcmp(&before, &after, sizeof before), "locked: settings_save writes nothing");
    }

    /* ---- RESTART ---- */
    t_begin(72);
    a = t_send(&n);
    CHECK(a && n == 1 && a[0] == 0 && crb.reboot == 1, "RESTART: rc 0, a restart owed");

    printf("cf_backup_test: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
