/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca); CoralFlower contributors */
/* Host test of CoralFlower's backup / restore SysEx handler (firmware/src/cf_backup.c), after ChoralRoot's
 * tests/cr_backup_test.c: the firmware as the emulator builds it (tools/emu/emu_firmware.h, storage.c on the RAM NOR),
 * requests fed to crb_handle as the main loop would, the replies taken at CRB_SEND.
 *   cc -std=gnu11 -O1 -w -Ibuild/gen -Ifirmware/src -Itests -o build/host/cf_backup_test tests/cf_backup_test.c -lm
 *   ./build/host/cf_backup_test
 * Checks: INFO (the version, the tags); LIST of the 26 objects; a sound, the settings (the newest in their log), the bass sound's
 * unsaved edits (put back at the restart), a loop and sequencer patterns
 * (a change not yet saved: LIST writes it) saved, then backed up whole (GET in 256-byte pieces, every CRC as LIST
 * says); the flash erased and everything restored (PUT): LIST and the bytes identical, the sound back in its list, the
 * patterns and song read in at the restart; refusals with nothing written: a CRC error, a settings record of another
 * magic, an unsorted loop, a sequencer head of another magic or a part of another length, ChoralRoot's ids 1 and 40, a
 * loop restored while the sequencer plays (never busy since 0.4), a loop becoming a pattern at power-on; stale sessions; the lock after a restore (settings, sequencer); RESTART. */
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
static obj_t man[CRB_N];
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
    static obj_t ref[CRB_N];
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
    CHECK(t_list() == 0 && nman == 26, "LIST on a fresh flash: 26 objects (%u)", nman);
    CHECK(t_find(50) && !t_find(50)->size && t_find(55) && !t_find(55)->size, "LIST: no patterns stored yet");

    /* ---- content: a user sound, the settings, a loop ---- */
    CHECK(cf_snd_save(CF_PART_CHORD, 1, "BACKUP TEST") == 0, "a chord sound saved as new");
    cs.bpm = 95;
    CHECK(cf_settings_write(0) == 1, "the settings written");
    cs.bpm = 96;
    CHECK(cf_settings_write(ST_NOERASE) == 1, "a change written with no erase (the log)");
    cs.bpm = 97;
    CHECK(cf_settings_write(ST_NOERASE) == 1, "another one");
    {
        st_hdr_t h;
        CHECK(st_current(CF_SET_OBJ, &h) >= 0 && st_cur_e >= 2u, "the newest settings are in the log, not at its entry 0 (entry %u)", st_cur_e);
    }
    fm1_irq_off();                                   /* the bass sound edited, not SAVEd (LIST writes it) */
    trk[CF_PART_BASS].p[P_REL] = (int16_t)(trk[CF_PART_BASS].p[P_REL] == 77 ? 78 : 77);
    fm1_irq_on();
    cf_edited(CF_PART_BASS);
    make_loop(40, 1);
    sq_st.song.n = 3;                                /* patterns and a song, not saved yet (LIST writes them) */
    sq_st.song.pat[0] = 2, sq_st.song.pat[1] = 7, sq_st.song.pat[2] = 2;
    sq_st.pat[2].hit[0][1] = 0x1111u;
    sq_st.pat[7].dlev[3] = 42;                       /* (in the last object) */
    sq_dirty = 1;
    CHECK(t_list() == 0, "LIST");
    for (i = 0, k = 0; i < nman; i++) k += man[i].size != 0;
    CHECK(t_find(6)->size && t_find(30)->size && t_find(31)->size, "LIST: the bank, the settings and the loop hold data");
    CHECK(t_find(57)->size == sizeof(cfk_rec_t) && t_find(56)->size == 0 && t_find(58)->size == 0,
          "LIST wrote the bass sound's edit (57: %u B), the chord and the LAYER have none", t_find(57)->size);
    CHECK(!sq_dirty && t_find(50)->size == SQ_CHUNK && t_find(55)->size == sq_chunk_len(5) && sq_chunk_len(5),
          "LIST wrote the sequencer's change: its six objects hold the store (%u .. %u B)", t_find(50)->size, t_find(55)->size);
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
        st_hdr_t h, h0;
        CHECK(st_load(CF_SET_OBJ, &r, sizeof r) > 0 && r.s.bpm == 97, "the restored settings hold the tempo 97");
        CHECK(st_current(CF_SET_OBJ, &h) >= 0 && st_cur_e == 0u && st_read(st_sector(CF_SET_OBJ, h.slot), &h0, sizeof h0) == 0 &&
              !memcmp(&h, &h0, sizeof h) && st_read(st_at, &r, sizeof r) == 0 && st_at == st_sector(CF_SET_OBJ, h.slot) + 256u &&
              r.s.bpm == 97, "restored where 0.4.1 reads it: the header at the copy's start, the record at 256");
    }
    {                                                /* the restart: the bass's edit back on its sound */
        int16_t rel = trk[CF_PART_BASS].p[P_REL];
        cfk_on[CF_PART_BASS] = 0;
        cu.edited[CF_PART_BASS] = 0;
        fm1_irq_off();
        trk[CF_PART_BASS].p[P_REL] = 0;
        fm1_irq_on();
        cfk_load();                              /* (the stores read in; each part takes its tweak as it loads) */
        cfk_take(CF_PART_CHORD, 0);
        cfk_take(CF_PART_BASS, 0);
        CHECK(cfk_on[CF_PART_BASS] && cu.edited[CF_PART_BASS] && trk[CF_PART_BASS].p[P_REL] == rel && !cfk_on[CF_PART_CHORD],
              "the restored edit put back on the bass at the restart (REL %d)", trk[CF_PART_BASS].p[P_REL]);
    }
    memset(&sq_st, 0, sizeof sq_st);                 /* the restart: the sequencer read in from flash */
    sq_load();
    CHECK(sq_st.magic == SQ_MAGIC && sq_st.song.n == 3 && sq_st.song.pat[1] == 7 && sq_st.pat[2].hit[0][1] == 0x1111u &&
          sq_st.pat[7].dlev[3] == 42 && sq_st.pat[0].dlev[0] == 0, "the restored patterns and song read in whole (sq_load's checks)");

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
        obj_t *q = t_find(50);
        uint32_t crc_before = q->crc;
        q->bytes[0] ^= 0x55;
        CHECK(t_put(50, q->bytes, q->size, st_crc32(q->bytes, q->size)) == 2, "a sequencer head of another magic: 2");
        q->bytes[0] ^= 0x55;
        CHECK(t_put(51, q->bytes, 1000, st_crc32(q->bytes, 1000)) == 1, "a sequencer part of another length: 1");
        CHECK(t_put(68, q->bytes, 16, st_crc32(q->bytes, 16)) == 1, "an id past the tweaked pattern sounds (68): 1");
        CHECK(t_list() == 0 && t_find(50)->crc == crc_before, "nothing written by the refused sequencer PUTs");
    }
    {
        obj_t *e = t_find(57);
        uint32_t crc_before = e->crc;
        CHECK(t_put(56, e->bytes, e->size, e->crc) == 2, "the bass's edit as the chord's (56): 2");
        e->bytes[0] ^= 0x55;
        CHECK(t_put(57, e->bytes, e->size, st_crc32(e->bytes, e->size)) == 2, "an edit of another magic: 2");
        e->bytes[0] ^= 0x55;
        CHECK(t_put(57, e->bytes, 100, st_crc32(e->bytes, 100)) == 1, "an edit of another length: 1");
        CHECK(t_list() == 0 && t_find(57)->crc == crc_before, "nothing written by the refused edit PUTs");
    }
    {                                                /* a sequencer restore alone locks too: RAM does not go over it */
        obj_t *q = t_find(55);
        uint32_t crc_before = q->crc;
        cfs_restore_lock = 0;
        sq_st.pat[7].dlev[3] = 99;
        sq_dirty = 1;
        CHECK(t_put(55, q->bytes, q->size, q->crc) == 0 && cfs_restore_lock == 1 && !sq_dirty,
              "a sequencer part restored: the lock on, the change in RAM dropped");
        sq_dirty = 1;
        cu.booted = 1;
        sq_changed_ms = cfs_sound_ms = cu.now - 60000u;
        sq_retry_ms = 0;
        cf_sn.sq_on = 0;
        sq_save_poll();
        CHECK(t_list() == 0 && t_find(55)->crc == crc_before, "locked: the sequencer's save writes nothing");
    }
    {
        static uint8_t z[64];
        CHECK(t_put(1, z, sizeof z, st_crc32(z, sizeof z)) == 1, "ChoralRoot's settings (id 1): 1");
        CHECK(t_put(40, z, sizeof z, st_crc32(z, sizeof z)) == 1, "ChoralRoot's loop slot (id 40): 1");
    }
    {                                                /* (0.4: no looper, never busy; the sequencer plays from RAM) */
        obj_t *s = t_find(31);
        cf_sn.sq_on = 1;
        CHECK(t_put(31, s->bytes, s->size, s->crc) == 0, "a loop restored while the sequencer plays: 0 (never busy)");
        cf_sn.sq_on = 0;
    }
    {                                                /* 0.4: a loop in flash (kept by 0.3.1, or restored) becomes the
                                                      * first free pattern's CHORDS at power-on, and leaves flash */
        static cfl_data_t d;
        static const cfl_ev_t EV[] = {{0, 15, CFL_DOWN}, {1, 15, 0}, {2, 7, CFL_DOWN}, {380, 7, 0}, {384, 4, CFL_DOWN},
                                      {760, 4, 0}};   /* (Top Lock: m7 tapped, then C and A, a bar each) */
        uint8_t z[16];
        uint32_t i, nchords = 0;
        int free0;
        memset(&d, 0, sizeof d);
        d.magic = CFL_MAGIC;
        d.len = 2u * 4u * CFL_PPQN;
        d.nlayers = 1;
        d.algo = CF_ALG_TOP_LOCK;
        d.beats = 4;
        d.nev = (uint16_t)(sizeof EV / sizeof EV[0]);
        memcpy(d.ev, EV, sizeof EV);
        CHECK(st_save(OBJ_CFLOOP, &d, (uint32_t)__builtin_offsetof(cfl_data_t, ev) + d.nev * (uint32_t)sizeof(cfl_ev_t)) == 0,
              "a 0.3.1 loop in flash");
        cs.beats = 4;
        free0 = sq_home_free();
        sq_from_loop();
        CHECK(free0 >= 0 && sq_has((uint32_t)free0), "the loop is pattern %c now", 'A' + free0);
        if (free0 >= 0) {
            const sq_pat_t *p = &sq_st.pat[free0];
            for (i = 0; i < p->nev; i++)
                if (p->ev[i].trk == SQ_CHORDS) {
                    nchords++;
                    CHECK(p->ev[i].n >= 3 && p->ev[i].bar == nchords - 1u && p->ev[i].len >= 90u,
                          "chord %u: %u notes, bar %u, %u long", nchords, p->ev[i].n, p->ev[i].bar + 1u, p->ev[i].len);
                }
            CHECK(nchords == 2 && p->tb[SQ_CHORDS] == 2 && p->follow, "two chords, 2 bars, following the key");
        }
        CHECK(st_load(OBJ_CFLOOP, z, sizeof z) == 16 && !z[0] && !z[1] && !z[2] && !z[3], "the loop left flash");
        memset(&sq_st, 0, sizeof sq_st);
        sq_load();
        CHECK(free0 >= 0 && sq_has((uint32_t)free0), "the pattern was written to flash before the loop went");
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

    /* ---- 0.5's mark: an older record's looper bytes are not NOTES IN and the ARP ---- */
    {
        cf_set_t s;
        cs_defaults(&s);
        CHECK(s.mark == CF_SET_MARK05, "defaults: 0.5's mark set (%02x)", s.mark);
        s.notes_in = 1;                          /* (0.3.1's looper: BARS 1, MODE Overdub, QUANT 0, COUNT-IN 1) */
        s.arp_keep = 1;
        s.mark = 0;
        cfs_sanitize(&s);
        CHECK(s.notes_in == CF_NI_KEYS && !s.arp_keep && s.mark == CF_SET_MARK05 && s.rsv_ci == 1,
              "an older record (no mark): NOTES IN FM-1 keys, ARP off, marked (%u %u %02x)", s.notes_in, s.arp_keep,
              s.mark);
        s.notes_in = CF_NI_DIRECT;
        s.arp_keep = 1;
        cfs_sanitize(&s);
        CHECK(s.notes_in == CF_NI_DIRECT && s.arp_keep == 1, "a marked record: NOTES IN and the ARP kept (%u %u)",
              s.notes_in, s.arp_keep);
        s.notes_in = 2;
        s.arp_keep = 0;
        s.mark = 2;                              /* (QUANT 1/16 set in 0.3.1: still no mark) */
        cfs_sanitize(&s);
        CHECK(s.notes_in == CF_NI_KEYS && !s.arp_keep, "BARS 2 and QUANT 2 of 0.3.1: NOTES IN FM-1 keys (%u)",
              s.notes_in);
    }

    /* ---- RESTART ---- */
    t_begin(72);
    a = t_send(&n);
    CHECK(a && n == 1 && a[0] == 0 && crb.reboot == 1, "RESTART: rc 0, a restart owed");

    printf("cf_backup_test: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
