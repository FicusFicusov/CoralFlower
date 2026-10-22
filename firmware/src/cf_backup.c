/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca); CoralFlower contributors */
/* CoralFlower: backup and restore over USB-MIDI SysEx, after ChoralRoot's cr_backup.c (its protocol and its safety,
 * on CoralFlower's objects). The web installer (web/fm1backup.js) and tools/fm1_install.py use it: INFO tells them
 * which firmware runs (an update of CoralFlower over CoralFlower is allowed by it), LIST / GET make the backup, PUT
 * restores it, RESTART loads what was restored.
 *
 *   1 INFO      "CoralFlower <version>", no editor (0 engines), tags 42 01 03 (backup read + restore), 43 01 02 (RESTART)
 *   65 LIST     1, rc, count, per object: id, size u32, crc u32 (CRC-32 zlib of the bytes GET returns)
 *   66 GET      id, offset u32, count (<= 256) -> id, rc, offset u32, count, pack7 data
 *   67 PUT      0 begin (id, size u32, crc u32) / 1 data (id, offset u32, pack7) / 2 commit (id) / 3 abort (id)
 *   72 RESTART  -> rc; the device restarts once the host has the reply
 *
 * Objects. Those CoralFlower shares with ChoralRoot keep ChoralRoot's ids and formats (the sounds: a ChoralRoot backup's
 * sounds restore here, and back): 6, 7 the user sound banks (upreset.c, 2 x 16 records), 8 the FM6 patch bank
 * (fm6_bank.c; Felucca 1.0's and Melodee's older banks too, converted at the next boot), 9 the VA patch store
 * (va_store.c), 10, 11 the FM6 patch store (fm6_ustore.c, slots 1..16, 17..32). CoralFlower's own have ids of their
 * own, so a ChoralRoot record never lands in them: 30 the settings (cf_settings.c cf_rec_t, "CFS1"), 31 the loop
 * (cf_loop.c cfl_data_t, "CFL1": 0.4 has no looper, a loop restored becomes a pattern at the next power-on), 50..55 the sequencer's patterns and song (cf_sequi.c sq_store_t, "CFQ6", in its six
 * objects as flash keeps them; 0.3.1), 56..58 the chord, bass and LAYER sounds' unsaved edits (cf_settings.c
 * cfk_rec_t, "CFK1"; 0.4.2: older firmware does not list them, so their restore skips them), 59 the patterns' own
 * sounds (cf_settings.c cfps_rec_t, "CFR1"; 0.5), 60..67 the eight tweaked pattern sounds (cfk_rec_t; 0.5). ChoralRoot's 1 (its settings) and 40..49 (its loops) are refused (rc 1: the page
 * reports them skipped).
 *
 * Reads come from flash (the current record of each object, checked at LIST: storage.c st_current) in 256-byte windows: no RAM copy, the audio
 * keeps running. LIST first writes a settings or a sequencer change not yet saved. A PUT is staged in cfb_raw and checked whole
 * before one commit through storage.c's protocol (st_save: the other copy, the header last): a torn write leaves the
 * previous copy. A begin or a commit while the loop plays or records answers 3 (busy: the page retries); the commit's
 * erase holds the audio about 45 ms, which a restore accepts. A restored settings record locks the settings
 * (cfs_restore_lock: no save until the restart, so the state in RAM cannot overwrite it).
 * Included after cf_settings.c, cf_ui.c, the stores and usb.c, before cf_shim.c. */
#if FELUCCA_FLASH
#define CRB_HDR0 0x7Du
#define CRB_HDR1 0x46u
#define CRB_HDR2 0x4Cu
enum { CRB_INFO = 1, CRB_LIST = 65, CRB_GET, CRB_PUT, CRB_RESTART = 72 };
#define CRB_VA 9u
#define CRB_FM6S 10u                                 /* 10, 11: the FM6 patch store's halves */
#define CFB_SET 30u                                  /* CoralFlower's settings record */
#define CFB_LOOP 31u                                 /* CoralFlower's loop */
#define CFB_SEQ 50u                                  /* 50..55: the sequencer's six objects (OBJ_CFSEQ0..5) */
#define CFB_EDIT 56u                                 /* 56..58: the sounds' unsaved edits (OBJ_CFEDIT0..2) */
#define CFB_PSND 59u                                 /* 59: the patterns' own sounds (OBJ_CFPSND, 0.5) */
#define CFB_TWK 60u                                  /* 60..67: the tweaked pattern sounds (OBJ_CFTWK0..7, 0.5) */
static const uint8_t CRB_IDS[] = {6, 7, 8, CRB_VA, CRB_FM6S, CRB_FM6S + 1u, CFB_SET, CFB_LOOP,
                                  CFB_SEQ, CFB_SEQ + 1u, CFB_SEQ + 2u, CFB_SEQ + 3u, CFB_SEQ + 4u, CFB_SEQ + 5u,
                                  CFB_EDIT, CFB_EDIT + 1u, CFB_EDIT + 2u, CFB_PSND,
                                  CFB_TWK, CFB_TWK + 1u, CFB_TWK + 2u, CFB_TWK + 3u, CFB_TWK + 4u, CFB_TWK + 5u,
                                  CFB_TWK + 6u, CFB_TWK + 7u};
_Static_assert(SQ_NCHUNK == 6u, "backup: one id per sequencer object");
#define CRB_N ((uint32_t)sizeof CRB_IDS)
/* an FM6 bank of earlier firmware, as fm6_bank.c fm6_bank_import takes it at boot: Felucca 1.0's (27 records, version
 * 1) or Melodee's before its version 2 (32 packed voices) */
#define CRB_FM6_FEL (16u + 27u * 128u)
#define CRB_FM6_MEL (4u + FM6_BANK_N * FM6_BANK_PK)
#define CRB_VA_V1 (16u + UP_SLOTS * VA_BLOB1)
#define CFB_MAX2(a, b) ((a) > (b) ? (a) : (b))
#define CFB_RAW_MAX CFB_MAX2(CFB_MAX2(CFB_MAX2(sizeof(up_bank_t), sizeof(fm6_bank_t)), CFB_MAX2(sizeof(va_store_t), sizeof(fm6u_t))), \
                             CFB_MAX2(CFB_MAX2(CFB_MAX2(sizeof(cf_rec_t), sizeof(cfl_data_t)), SQ_CHUNK), \
                                      CFB_MAX2(CRB_FM6_FEL, CFB_MAX2(CRB_FM6_MEL, CRB_VA_V1))))
static uint8_t cfb_raw[CFB_RAW_MAX] __attribute__((section(".pool"), aligned(4)));   /* a PUT, staged */
_Static_assert(ST_PAYLOAD_MAX >= 2048u + 256u, "backup: the reply and the data share st_buf");
_Static_assert(CFB_RAW_MAX <= ST_PAYLOAD_MAX, "backup: every object fits one storage record");

#ifndef CRB_SEND
#if FELUCCA_OTA
#define CRB_SEND(p, n) ota_wire_send((p), (n))
#else
#define CRB_SEND(p, n) ((void)(p), (void)(n))        /* (the emulator: no SysEx transport) */
#endif
#endif

static struct {
    uint8_t listed, put, id;                         /* a LIST snapshot; a PUT session and its id */
    uint32_t at[CRB_N];                              /* the current record's payload in flash at LIST (storage.c st_at) */
    uint32_t size[CRB_N];                            /* the object's size at LIST */
    uint32_t usb, len, crc, pos, ms;                 /* usb.resets at LIST / begin; the PUT */
    uint32_t reboot;                                 /* a restart is owed (72 RESTART; crb_leave): 0 none, 1 asked, 2 its
                                                      * reply marked in the ring, 3 the reply seen taken */
    uint32_t reboot_ms, mark, gone_ms;               /* fm1_ms at the request; the reply's end; when the host had it */
} crb;

/* ------------------------------------------------------------------ the reply --- */
#define CRB_OUT st_buf                               /* the reply (<= 400 bytes, written after any storage call) .. */
#define CRB_DATA (st_buf + 2048u)                    /* .. and GET's flash data */
static uint32_t crb_n;
static void crb_b(uint32_t v)
{
    if (crb_n < 1024u - 1u)
        CRB_OUT[crb_n++] = (uint8_t)(v & 0x7Fu);
}
static void crb_u32(uint32_t v)
{
    uint32_t i;
    for (i = 0; i < 5u; i++)
        crb_b(v >> (7u * i));
}
static uint32_t crb_r32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 7 | (uint32_t)p[2] << 14 | (uint32_t)p[3] << 21 | (uint32_t)p[4] << 28;
}
static void crb_pack(const uint8_t *p, uint32_t n)  /* pack7: a byte of top bits, then up to 7 low-7-bit bytes */
{
    while (n) {
        uint32_t k = n > 7u ? 7u : n, m = 0, i;
        for (i = 0; i < k; i++)
            m |= (uint32_t)(p[i] >> 7) << i;
        crb_b(m);
        for (i = 0; i < k; i++)
            crb_b(p[i]);
        p += k;
        n -= k;
    }
}
static uint32_t crb_unpack(const uint8_t *a, uint32_t na, uint8_t *out, uint32_t max)   /* 0: malformed / too long */
{
    uint32_t n = 0;
    while (na && n < max) {
        uint32_t m = *a++, j, k;
        na--;
        k = na > 7u ? 7u : na;
        if (!k || n + k > max || (m >> k))
            return 0;
        for (j = 0; j < k; j++, na--)
            out[n++] = (uint8_t)(*a++ | ((m >> j) & 1u) << 7);
    }
    return na ? 0u : n;
}
static void crb_send(uint32_t cmd, uint32_t from)   /* the reply's bytes are CRB_OUT[from..crb_n) */
{
    CRB_OUT[from - 5u] = 0xF0;
    CRB_OUT[from - 4u] = CRB_HDR0;
    CRB_OUT[from - 3u] = CRB_HDR1;
    CRB_OUT[from - 2u] = CRB_HDR2;
    CRB_OUT[from - 1u] = (uint8_t)cmd;
    CRB_OUT[crb_n++] = 0xF7;
    CRB_SEND(CRB_OUT + from - 5u, crb_n - (from - 5u));
}

/* ------------------------------------------------------------------ the objects --- */
static int crb_index(uint32_t id)
{
    uint32_t i;
    for (i = 0; i < CRB_N; i++)
        if (CRB_IDS[i] == id)
            return (int)i;
    return -1;
}
static uint32_t crb_obj(uint32_t id)                 /* storage.c's object of an id */
{
    if (id == CFB_SET)
        return CF_SET_OBJ;
    if (id == CFB_LOOP)
        return OBJ_CFLOOP;
    if (id >= CFB_TWK)
        return (uint32_t)OBJ_CFTWK0 + id - CFB_TWK;
    if (id == CFB_PSND)
        return OBJ_CFPSND;
    if (id >= CFB_EDIT)
        return (uint32_t)OBJ_CFEDIT0 + id - CFB_EDIT;
    if (id >= CFB_SEQ)
        return (uint32_t)OBJ_CFSEQ0 + id - CFB_SEQ;
    return id == 8u ? (uint32_t)OBJ_FM6BANK : id == CRB_VA ? (uint32_t)OBJ_VASTORE
         : id >= CRB_FM6S ? (uint32_t)OBJ_FM6STORE0 + id - CRB_FM6S : (uint32_t)OBJ_UPRESET0 + id - 6u;
}

/* GLOBAL > Reset (cf_ui.c cf_reset_poll): everything the user made goes, both copies of every object a backup has: the
 * sounds (6 .. 11: the FM bank back to the factory one), the settings, the loop, the sequencer's patterns and song. The firmware, the boot stub and the panel stay. Then the unit starts again on the defaults (the emulator, which
 * cannot: the caller says so). Each erase holds the audio and the lights ~45 ms; the watchdog is fed between them */
static void cf_factory_erase(void)
{
    uint32_t i, c;
    cfs_restore_lock = 1;                            /* (nothing saved from RAM over it any more) */
    sq_dirty = 0;
    for (i = 0; i < CRB_N; i++)
        for (c = 0; c < 2u; c++) {
            fm1_wdt_feed();
            (void)st_erase(st_sector(crb_obj(CRB_IDS[i]), c));
        }
#if FELUCCA_OTA && !defined(CF_EMU)
    bootguard.pending = 0;                           /* (a wanted restart, not a crash) */
    usb_detach();
    fm1_delay_ms(30);
    fm1_reboot();
#endif
}
static int crb_fm6_old(const uint8_t *raw, uint32_t len)
{
    uint32_t magic = raw[0] | (uint32_t)raw[1] << 8 | (uint32_t)raw[2] << 16 | (uint32_t)raw[3] << 24;
    return magic == FM6_BANK_MAGIC && (len == CRB_FM6_MEL || (len == CRB_FM6_FEL && raw[4] == 1u && raw[6] == 27u));
}
/* busy: never since 0.4 (it was the looper playing or recording: a flash erase cut it). The sequencer plays from RAM;
 * a restore ends in a restart anyway */
static int crb_busy(void) { return 0; }

/* ------------------------------------------------------------------ LIST / GET --- */
static void crb_list(void)
{
    uint32_t i, crc[CRB_N];
    st_hdr_t h;
    if (!cr_safe && !cfs_restore_lock && cfs_fnv(&cs, sizeof cs) != cfs_saved)
        cf_settings_write(0);                        /* a change not yet saved goes in first .. */
    if (!cr_safe && !cfs_restore_lock && sq_dirty)
        sq_save_write();                             /* .. the sequencer's too (playing or not: a backup is taken now) */
    if (!cr_safe && !cfs_restore_lock) {
        cfk_flush();                                 /* .. and the sounds' edits, the patterns' own sounds */
        cfps_flush();
    }
    for (i = 0; i < CRB_N; i++) {
        uint32_t id = CRB_IDS[i];
        crb.at[i] = crb.size[i] = crc[i] = 0;
        if (flash_ok && st_current(crb_obj(id), &h) >= 0) {   /* (the newest record, the settings' log too) */
            crb.at[i] = st_at;
            crb.size[i] = h.len;
            crc[i] = h.crc;
        }
        fm1_wdt_feed();
    }
    crb.listed = 1;
    crb.put = 0;
    crb.usb = usb.resets;
    crb_b(1);
    crb_b(0);
    crb_b(CRB_N);
    for (i = 0; i < CRB_N; i++) {
        crb_b(CRB_IDS[i]);
        crb_u32(crb.size[i]);
        crb_u32(crc[i]);
    }
}
static void crb_get(const uint8_t *a, uint32_t n)
{
    uint32_t off = n >= 6u ? crb_r32(a + 1) : 0u, count = n == 8u ? (uint32_t)a[6] | (uint32_t)a[7] << 7 : 0u, rc;
    int i = n ? crb_index(a[0]) : -1;
    if (!crb.listed || crb.usb != usb.resets)
        rc = 5;
    else if (n != 8u || a[5] > 15u || i < 0 || !count || count > 256u || off > crb.size[i] || count > crb.size[i] - off)
        rc = 1;
    else
        rc = st_read(crb.at[i] + off, CRB_DATA, count) ? 4u : 0u;
    crb_b(n ? a[0] : 127u);
    crb_b(rc);
    crb_u32(off);
    crb_b(rc ? 0u : count & 127u);
    crb_b(rc ? 0u : count >> 7);
    if (!rc)
        crb_pack(CRB_DATA, count);
}

/* ------------------------------------------------------------------ PUT --- */
static int crb_size_ok(uint32_t id, uint32_t len)   /* a begin's size for this id (0: the object erased) */
{
    if (id == 6u || id == 7u)
        return !len || len == sizeof(up_bank_t);
    if (id == 8u)
        return !len || len == sizeof(fm6_bank_t) || len == CRB_FM6_FEL || len == CRB_FM6_MEL;
    if (id == CRB_FM6S || id == CRB_FM6S + 1u)
        return !len || len == sizeof(fm6u_t);
    if (id == CRB_VA)                                /* version 3 / 2, or version 1's 104-byte blobs */
        return !len || len == sizeof(va_store_t) || len == CRB_VA_V1;
    if (id == CFB_SET)                               /* an older build's record is shorter: fields only grow */
        return len >= __builtin_offsetof(cf_rec_t, s) && len <= sizeof(cf_rec_t);
    if (id == CFB_LOOP)
        return !len || (len >= __builtin_offsetof(cfl_data_t, ev) && len <= sizeof(cfl_data_t));
    if (id >= CFB_EDIT && id < CFB_EDIT + 3u)
        return !len || len == sizeof(cfk_rec_t);
    if (id == CFB_PSND)
        return !len || len == sizeof(cfps_rec_t);
    if (id >= CFB_TWK && id < CFB_TWK + CFK_NSLOT)
        return !len || len == sizeof(cfk_rec_t);
    if (id >= CFB_SEQ && id < CFB_SEQ + SQ_NCHUNK)  /* (a later format reads this one's lengths in, as sq_load does) */
        return !len || len == sq_chunk_len(id - CFB_SEQ);
    return 0;
}
static int cfb_set_ok(const cf_rec_t *r, uint32_t len)   /* cf_settings_load's checks (len: the record as written,
                                                          * its tail padding with it) */
{
    return r->magic == CF_SET_MAGIC && r->version == CF_SET_VERSION && r->size <= sizeof(cf_set_t) &&
           len >= __builtin_offsetof(cf_rec_t, s) + r->size && r->check == cfs_fnv(&r->s, r->size);
}
static int cfb_loop_ok(const cfl_data_t *d, uint32_t len)   /* the power-on load's checks, and an event's own */
{
    uint32_t i;
    if (d->magic != CFL_MAGIC || d->nev > CFL_MAX_EV || len != __builtin_offsetof(cfl_data_t, ev) + d->nev * sizeof(cfl_ev_t))
        return 0;
    for (i = 1; i < d->nev; i++)
        if (d->ev[i].t < d->ev[i - 1u].t)
            return 0;                                /* (in time order, as the player needs them) */
    return 1;
}
static uint32_t crb_commit(void)
{
    uint8_t *raw = cfb_raw;
    uint32_t id = crb.id, len = crb.len;
    if (crb.pos != len || st_crc32(raw, len) != crb.crc)
        return 2;
    if (id == CFB_SET) {
        if (!cfb_set_ok((const cf_rec_t *)raw, len))
            return 2;
        if (st_save(CF_SET_OBJ, raw, len))
            return 4;
        cfs_restore_lock = 1;                        /* no settings save before the restart */
        cf_message("Settings restored: restart");
        return 0;
    }
    if (id == CFB_LOOP) {
        if (len && !cfb_loop_ok((const cfl_data_t *)raw, len))
            return 2;
        if (st_save(OBJ_CFLOOP, raw, len))
            return 4;
        return 0;                                    /* (the restart loads it, stopped, as at power-on) */
    }
    if (id >= CFB_TWK) {                             /* a tweaked pattern sound: read in at the restart */
        const cfk_rec_t *r = (const cfk_rec_t *)raw;
        if (len && !(cfk_valid(r, r->part) && r->part < 3u && (!r->edited || (r->pat1 && r->pat1 <= SQ_NPAT))))
            return 2;
        if (st_save(crb_obj(id), raw, len))
            return 4;
        cfs_restore_lock = 1;
        cf_message("Patterns restored: restart");
        return 0;
    }
    if (id == CFB_PSND) {                            /* the patterns' own sounds: read in at the restart */
        if (len && !cfps_valid((const cfps_rec_t *)raw))
            return 2;
        if (st_save(OBJ_CFPSND, raw, len))
            return 4;
        cfs_restore_lock = 1;
        cf_message("Patterns restored: restart");
        return 0;
    }
    if (id >= CFB_EDIT) {                            /* a part's unsaved edits: put back at the restart, if made on the
                                                      * sound the part then has (cf_settings.c cfk_load) */
        if (len && !cfk_valid((const cfk_rec_t *)raw, id - CFB_EDIT))
            return 2;
        if (st_save(crb_obj(id), raw, len))
            return 4;
        cfs_restore_lock = 1;                        /* (nothing saved from RAM over it before the restart) */
        cf_message("Sounds restored: restart");
        return 0;
    }
    if (id >= CFB_SEQ) {                             /* a part of the sequencer's store: the first holds its head; the
                                                      * whole is checked at the restart (sq_load: its FNV) */
        uint32_t head[2];                            /* magic, size (read as bytes: the store outgrows cfb_raw) */
        memcpy(head, raw, sizeof head);
        if (len && id == CFB_SEQ && (head[0] != SQ_MAGIC || head[1] != sizeof sq_st ||
                                     raw[__builtin_offsetof(sq_store_t, song.n)] > SQ_SONG))
            return 2;
        if (st_save(crb_obj(id), raw, len))
            return 4;
        sq_dirty = 0;                                /* (the patterns in RAM are not saved over it before the restart) */
        cfs_restore_lock = 1;
        cf_message("Patterns restored: restart");
        return 0;
    }
    if (id == 6u || id == 7u) {
        const up_bank_t *b = (const up_bank_t *)raw;
        if (len && (b->magic != UP_BANK_MAGIC || b->rsize != sizeof(up_rec_t) || b->nslot != UP_PER_BANK))
            return 2;
    } else if (id == 8u) {
        if (len && !(len == sizeof(fm6_bank_t) ? fm6_bank_valid((const fm6_bank_t *)raw) : crb_fm6_old(raw, len)))
            return 2;                                /* (an older layout: imported by fm6_bank_boot, as at power-on) */
    } else if (id == CRB_FM6S || id == CRB_FM6S + 1u) {
        if (len && !fm6u_valid((const fm6u_t *)raw, id - CRB_FM6S))
            return 2;
    } else if (id == CRB_VA) {
        const va_store_t *s = (const va_store_t *)raw;
        if (len && (s->magic != VA_STORE_MAGIC || s->nslot != UP_SLOTS ||
                    !(len == sizeof *s ? (s->ver == 3u && va_store_valid(s)) || (s->ver == 2u && s->blob == VA_BLOB)
                                       : s->ver == 1u && s->blob == VA_BLOB1)))
            return 2;                                /* (versions 1 / 2: converted by va_store_boot, as at power-on) */
    } else {
        return 1;
    }
    if (st_save(crb_obj(id), raw, len))
        return 4;
    if (id == 8u) {                                  /* the mirror from flash; parts on a bank patch reload theirs */
        uint32_t t;
        fm6_bank_boot();
        for (t = 0; t < NTRK; t++)
            if (fm6_slot[t] >= FM6_NFAC)
                fm6_slot[t] = 0xFFu;
    } else {
        up_boot();                                   /* both banks, the VA and FM6 stores from flash */
        up_gen++;
        cf_snd_resync();                             /* (the parts keep their sounds; the lists follow) */
    }
    return 0;
}
static uint32_t crb_put(const uint8_t *a, uint32_t n)
{
    uint32_t rc;
    if (n < 2u || a[0] > 3u)
        return 1;
    if (a[0] == 0u) {
        uint32_t len;
        if (n != 12u || a[6] > 15u || a[11] > 15u || !crb_size_ok(a[1], len = crb_r32(a + 2)))
            return 1;
        if (!flash_ok || cr_safe)
            return 4;
        if (crb_busy())
            return 3;
        crb.listed = 0;                              /* (a begin ends the snapshot, as Felucca) */
        crb.put = 1;
        crb.id = a[1];
        crb.len = len;
        crb.crc = crb_r32(a + 7);
        crb.pos = 0;
        crb.usb = usb.resets;
        crb.ms = fm1_ms;
        return 0;
    }
    if (!crb.put || crb.id != a[1] || crb.usb != usb.resets || fm1_ms - crb.ms > 15000u) {
        crb.put = 0;                                 /* stale: no begin, another id, a USB reset, 15 s */
        return 5;
    }
    crb.ms = fm1_ms;
    if (a[0] == 3u) {
        crb.put = 0;
        return n == 2u ? 0u : 1u;
    }
    if (a[0] == 2u) {
        if (n != 2u)
            return 1;
        if (crb_busy())
            return 3;                                /* (the session stays: the page sends the commit again) */
        rc = crb_commit();
        crb.put = rc == 3u;
        return rc;
    }
    if (n < 9u || a[6] > 15u || crb_r32(a + 2) != crb.pos)
        return 1;
    {
        uint32_t left = crb.len - crb.pos, k = crb_unpack(a + 7, n - 7u, cfb_raw + crb.pos, left < 256u ? left : 256u);
        if (!k)
            return 1;
        crb.pos += k;
    }
    return 0;
}

/* ------------------------------------------------------------------ the dispatcher --- */
/* f: the bytes between F0 and F7 (7D 46 4C cmd args); 1 = answered */
static int crb_handle(const uint8_t *f, uint32_t n)
{
    const uint8_t *a = f + 4;
    uint32_t cmd, na, from = 5u;
    if (n < 4u || f[0] != CRB_HDR0 || f[1] != CRB_HDR1 || f[2] != CRB_HDR2)
        return 0;
    cmd = f[3];
    na = n - 4u;
    crb_n = from;
    switch (cmd) {
    case CRB_INFO: {                                 /* Felucca's INFO layout, no editor: 0 engines, 0 tracks */
        const char *v = FELUCCA_VERSION;             /* ("CoralFlower X.Y": the installers tell CoralFlower by it) */
        uint32_t i;
        if (na)
            return 0;
        for (i = 0; v[i] && i < 24u; i++)
            crb_b((uint8_t)v[i]);
        crb_b(0);
        for (i = 0; i < 7u; i++)
            crb_b(0);                                /* NENGINES P_COUNT G_COUNT NSTEP P_E0, NTRK, CHAIN_ROWS */
        crb_b(0x42);
        crb_b(1);
        crb_b(3);                                    /* backup read + restore */
        crb_b(0x43);
        crb_b(1);
        crb_b(2);                                    /* RESTART; no samples */
        break;
    }
    case CRB_LIST:
        if (na) {
            crb_b(1);
            crb_b(1);
            crb_b(0);
        } else {
            crb_list();
        }
        break;
    case CRB_GET:
        crb_get(a, na);
        break;
    case CRB_PUT: {
        uint32_t rc = crb_put(a, na);
        crb_b(na ? a[0] : 127u);
        crb_b(na >= 2u ? a[1] : 127u);
        crb_b(rc);
        break;
    }
    case CRB_RESTART:
        if (na)
            return 0;
        crb_b(0);
        crb.put = crb.listed = 0;                    /* ed_service restarts once the host has the reply (crb_leave) */
        if (!crb.reboot)
            crb.reboot_ms = fm1_ms;
        crb.reboot = 1;
        break;
    default:
        return 0;
    }
    crb_send(cmd, from);
    return 1;
}

#if FELUCCA_OTA && !defined(CF_EMU)
#ifndef CRB_REBOOT
#define CRB_REBOOT() do { bootguard.pending = 0; usb_detach(); fm1_delay_ms(30); fm1_reboot(); } while (0)
#endif
#define CR_BACKUP_SERVICE 1                          /* cf_shim.c: no ed_service stub */
/* The restart a reply promised (72 RESTART), ChoralRoot's timing (cr_backup.c crb_leave): after the reply's last USB
 * packet was taken by the host and 5 ms more, not before 150 ms from the request (the tools close their ports), and
 * 500 ms at the latest, taken or not */
#define CRB_LEAVE_GRACE 5u
#define CRB_LEAVE_MIN 150u
#define CRB_LEAVE_MAX 500u
static void crb_leave(void)
{
    uint32_t now = fm1_ms, age = now - crb.reboot_ms;
    if (crb.reboot == 1u) {                          /* (crb_handle has returned: the reply is in the ring) */
        crb.mark = ota_wire_mark();
        crb.reboot = 2;
    }
    if (crb.reboot == 2u && ota_wire_gone(crb.mark)) {
        crb.gone_ms = now;
        crb.reboot = 3;
    }
    if (age < CRB_LEAVE_MAX && (crb.reboot != 3u || age < CRB_LEAVE_MIN || now - crb.gone_ms < CRB_LEAVE_GRACE))
        return;
    crb.reboot = 0;
    CRB_REBOOT();
}
/* main.c's main loop: our frames (7D 46 4C), anything else stays for ota_service (M-UPGRADE) */
static void ed_service(void)
{
    const uint8_t *p;
    uint32_t n;
    if (crb.reboot)
        crb_leave();
    if (!ota_frame_get(&p, &n) || n < 4u || p[0] != CRB_HDR0 || p[1] != CRB_HDR1 || p[2] != CRB_HDR2)
        return;
    crb_handle(p, n);                                /* (in place: the frame is released after the reply) */
    ota_frame_done();
}
#endif
#endif /* FELUCCA_FLASH */
