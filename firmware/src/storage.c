/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Persistent storage on the SPI NOR.
 *
 * Every object has an A/B sector pair. A save goes to the copy that is not
 * the current one: erase the sector, program the payload pages (from offset
 * 256), then the 32-byte header at offset 0 LAST. The header is the commit
 * record (magic, seq, length, payload CRC, header CRC); on load the valid
 * copy with the newest seq wins (including wrap), so a write torn at any point leaves the
 * previous copy in charge. One object may also keep a log after that record (ST_LOG_OBJ, below
 * st_buf): saves without an erase.
 *
 * Flash access goes through three hooks (also used by the host test):
 *   st_read(off, dst, n)   st_erase(off)   st_prog(off, src, n)
 */
#define ST_MAGIC 0x554C4546u                   /* "FELU" */
#define ST_SECTOR 4096u
#define ST_PAYLOAD_OFF 256u
#define ST_PAYLOAD_MAX (ST_SECTOR - ST_PAYLOAD_OFF)

/* flash map (FL_DATA 0x97000..0xDFFFF, FL_GLOB 0xFC000..): settings 0xFC000, projects 0x97000..0x9EFFF,
 * user sample slots 0xA0000..0xDBFFF (eng_sample.c; ChoralRoot, FELUCCA_SAMPLE 0: slots 1-2, 0xA0000..0xC7FFF, stay
 * reserved and unused, slot 3's 0xC8000..0xDBFFF holds the loops, cr_ui.c CRL_FL_BASE), user preset banks 0xDC000..0xDFFFF (upreset.c), the FM6
 * patch bank (fm6_bank.c): copy A 0x9F000, copy B 0xFE000 (the two free sectors).
 * ChoralRoot's project pairs: 0x97000 the VA patch store (va_store.c), 0x99000 / 0x9B000 the FM6 patch store
 * (fm6_ustore.c), 0x9D000 the CZ-1 tone store's slots 1..16 (cz_ustore.c, OBJ_PROJECT0 + 3). FELUCCA_CZ: on the user
 * sample slot 1's flash (unused since the all-synth change, FELUCCA_SAMPLE 0): 0xA0000 / 0xA1000 the CZ-1 tone store's
 * slots 17..32 (OBJ_CZSTORE1), 0xA2000..0xB1FFF the eight CZ-1 banks A..H (cz_bank.c, OBJ_CZBANK0 + k: an A/B pair
 * each). FELUCCA_QUAD: 0xB2000 / 0xB3000 the QUAD patch store (quad_store.c, OBJ_QUADSTORE: the user sample slot 2's
 * flash); 0xB4000 / 0xB5000 the session (cr_settings.c, OBJ_SESSION: where the unit was left, 0.18);
 * 0xB6000..0xC7FFF stays free (CoralFlower: 0xB6000 / 0xB7000 its loop, OBJ_CFLOOP; 0xB8000..0xC3FFF its sequencer's
 * patterns and song, OBJ_CFSEQ0..5, six A/B pairs; 0xC4000..0xC7FFF and 0xC8000 / 0xC9000 (ChoralRoot's loop slots,
 * not used here) the parts' unsaved sound edits, OBJ_CFEDIT0..2, three A/B pairs, cf_settings.c; 0xCA000 / 0xCB000
 * the patterns' own sounds, OBJ_CFPSND, 0.5; 0xCC000 .. 0xDBFFF, the rest of ChoralRoot's loop slots, eight tweaked
 * pattern sounds, OBJ_CFTWK0..7, 0.5) */
enum { OBJ_SETTINGS, OBJ_PROJECT0, OBJ_UPRESET0 = OBJ_PROJECT0 + 4, OBJ_FM6BANK = OBJ_UPRESET0 + 2,
#if FELUCCA_CZ && FELUCCA_QUAD
       OBJ_CZSTORE1, OBJ_CZBANK0, OBJ_QUADSTORE = OBJ_CZBANK0 + 8, OBJ_SESSION, OBJ_COUNT
#elif FELUCCA_CZ
       OBJ_CZSTORE1, OBJ_CZBANK0, OBJ_COUNT = OBJ_CZBANK0 + 8
#else
#ifdef CF_LOOP_STORE
       OBJ_CFLOOP,                             /* CoralFlower's loop (cf_loop.c): 0xB6000 / 0xB7000, free in every build */
       OBJ_CFSEQ0,                             /* .. the sequencer's (cf_sequi.c): 0xB8000.., six pairs */
       OBJ_CFSEQ5 = OBJ_CFSEQ0 + 5,
       OBJ_CFEDIT0,                            /* .. the chord, bass and LAYER sounds' unsaved edits (cf_settings.c): 0xC4000.. */
       OBJ_CFEDIT2 = OBJ_CFEDIT0 + 2,
       OBJ_CFPSND,                             /* .. the patterns' own sounds (0.5, cf_settings.c): 0xCA000 / 0xCB000 */
       OBJ_CFTWK0,                             /* .. eight tweaked pattern sounds (0.5, cf_settings.c): 0xCC000 .. 0xDBFFF */
       OBJ_CFTWK7 = OBJ_CFTWK0 + 7,
#endif
       OBJ_COUNT
#endif
};

typedef struct {
    uint32_t magic;
    uint16_t type, slot;
    uint32_t seq, len, crc, rsv[2];
    uint32_t hcrc;
} st_hdr_t;
_Static_assert(sizeof(st_hdr_t) == 32u, "storage commit record layout");

static int st_read(uint32_t off, void *dst, uint32_t n);
#ifndef ST_BLOCKED
#define ST_BLOCKED() 0                         /* (core.h: SAFE MODE, the objects neither loaded nor saved) */
#endif
static int st_erase(uint32_t off);
static int st_prog(uint32_t off, const void *src, uint32_t n);

static uint32_t st_crc32(const void *p, uint32_t n)   /* zlib CRC-32, 4 bits per step */
{
    static const uint32_t T[16] = {
        0x00000000u, 0x1DB71064u, 0x3B6E20C8u, 0x26D930ACu, 0x76DC4190u, 0x6B6B51F4u, 0x4DB26158u, 0x5005713Cu,
        0xEDB88320u, 0xF00F9344u, 0xD6D6A3E8u, 0xCB61B38Cu, 0x9B64C2B0u, 0x86D3D2D4u, 0xA00AE278u, 0xBDBDF21Cu};
    const uint8_t *b = p;
    uint32_t c = 0xFFFFFFFFu;
    while (n--) {
        c ^= *b++;
        c = (c >> 4) ^ T[c & 15u];
        c = (c >> 4) ^ T[c & 15u];
    }
    return ~c;
}

static uint32_t st_sector(uint32_t obj, uint32_t copy)  /* flash offset of copy A (0) / B (1) */
{
    if (obj == OBJ_SETTINGS)
        return 0xFC000u + copy * ST_SECTOR;
    if (obj == OBJ_FM6BANK)
        return copy ? 0xFE000u : 0x9F000u;
#if FELUCCA_CZ && FELUCCA_QUAD
    if (obj == OBJ_QUADSTORE)
        return 0xB2000u + copy * ST_SECTOR;
    if (obj == OBJ_SESSION)
        return 0xB4000u + copy * ST_SECTOR;
#endif
#if FELUCCA_CZ
    if (obj == OBJ_CZSTORE1)
        return 0xA0000u + copy * ST_SECTOR;
    if (obj >= OBJ_CZBANK0)
        return 0xA2000u + (obj - OBJ_CZBANK0) * 2u * ST_SECTOR + copy * ST_SECTOR;
#endif
#if defined(CF_LOOP_STORE) && !FELUCCA_CZ
    if (obj == OBJ_CFLOOP)
        return 0xB6000u + copy * ST_SECTOR;
    if (obj >= OBJ_CFSEQ0 && obj <= OBJ_CFSEQ5)
        return 0xB8000u + (obj - OBJ_CFSEQ0) * 2u * ST_SECTOR + copy * ST_SECTOR;
    if (obj >= OBJ_CFEDIT0 && obj <= OBJ_CFEDIT2)
        return 0xC4000u + (obj - OBJ_CFEDIT0) * 2u * ST_SECTOR + copy * ST_SECTOR;
    if (obj == OBJ_CFPSND)
        return 0xCA000u + copy * ST_SECTOR;
    if (obj >= OBJ_CFTWK0 && obj <= OBJ_CFTWK7)
        return 0xCC000u + (obj - OBJ_CFTWK0) * 2u * ST_SECTOR + copy * ST_SECTOR;
#endif
    if (obj >= OBJ_UPRESET0)
        return 0xDC000u + (obj - OBJ_UPRESET0) * 2u * ST_SECTOR + copy * ST_SECTOR;
    return 0x97000u + (obj - OBJ_PROJECT0) * 2u * ST_SECTOR + copy * ST_SECTOR;
}

static uint8_t st_buf[ST_PAYLOAD_MAX] __attribute__((aligned(4)));

/* Some objects keep a log (CoralFlower: its settings, ST_LOG_OBJ, the sounds' unsaved edits, OBJ_CFEDIT0..2, and the
 * patterns' own sounds, OBJ_CFPSND; written half a second after a change while the audio plays). Entries of S bytes
 * (the edits 512, the others 256):
 * entry 0 is the record above (its header at 0, its payload at 256: all that 0.4.1 and older firmware read); entry e
 * of 1 .. (3840 / S) - 1 sits at 256 + S e, its header first, then its payload (up to S - 32 bytes), rsv[0] = e: 15
 * entries a copy for the settings, 7 for an edit. A save appends to the current copy's next erased entry: page
 * programs of about 1 ms, no erase. When none is left, it opens the other copy at entry 0, if that copy is erased.
 * Only that needs an erase (about 45 ms of silence), so the caller picks a quiet moment for it (st_tidy). The current
 * copy is the one whose entry 0 is newer, as older firmware picks it; in it the last intact entry wins */
#ifdef ST_LOG_OBJ
#define ST_LOGGED(obj) ((obj) == (uint32_t)(ST_LOG_OBJ) || ((obj) >= OBJ_CFEDIT0 && (obj) <= OBJ_CFTWK7))
#define ST_LSZ(obj) ((obj) == (uint32_t)(ST_LOG_OBJ) || (obj) == OBJ_CFPSND ? 256u : 512u)
#else
#define ST_LOGGED(obj) 0
#define ST_LSZ(obj) 256u
#endif
#define ST_LOG_MAX (256u - (uint32_t)sizeof(st_hdr_t))          /* the settings' entry */
#define ST_LMAX(obj) (ST_LSZ(obj) - (uint32_t)sizeof(st_hdr_t))  /* an entry's payload, at most */
static uint32_t st_ln(uint32_t obj) { return (ST_SECTOR - 256u) / ST_LSZ(obj); }   /* entries a copy */
static uint32_t st_entry(uint32_t obj, uint32_t e) { return e ? 256u + e * ST_LSZ(obj) : 0u; }   /* an entry's header .. */
static uint32_t st_data(uint32_t obj, uint32_t e)                                                 /* .. its payload */
{
    return e ? st_entry(obj, e) + (uint32_t)sizeof(st_hdr_t) : ST_PAYLOAD_OFF;
}
static uint32_t st_cur_e, st_at;                       /* st_current's find: its entry, its payload's flash offset */

/* the copies known to be erased (all 0xFF): a save into one needs no erase. Bit obj * 2 + copy; RAM: unknown (0) at
 * power-on, set by st_check_blank or st_tidy, cleared by a save into it */
static uint8_t st_blank[(OBJ_COUNT * 2u + 7u) / 8u];
static int st_is_blank(uint32_t obj, uint32_t copy) { uint32_t b = obj * 2u + copy; return (st_blank[b >> 3] >> (b & 7u)) & 1u; }
static void st_set_blank(uint32_t obj, uint32_t copy, int on)
{
    uint32_t b = obj * 2u + copy;
    if (on)
        st_blank[b >> 3] |= (uint8_t)(1u << (b & 7u));
    else
        st_blank[b >> 3] &= (uint8_t)~(1u << (b & 7u));
}

static int st_head(uint32_t obj, uint32_t copy, uint32_t e, st_hdr_t *h)   /* commit record valid: 0 */
{
    if (obj >= OBJ_COUNT || copy > 1u)
        return -1;
    if (st_read(st_sector(obj, copy) + st_entry(obj, e), h, sizeof *h))
        return -1;
    if (h->magic != ST_MAGIC || h->type != obj || h->slot != copy || h->len > (e ? ST_LMAX(obj) : ST_PAYLOAD_MAX) ||
        (e && h->rsv[0] != e) || h->hcrc != st_crc32(h, sizeof *h - 4u))
        return -1;
    return 0;
}

static int st_body(uint32_t obj, uint32_t copy, uint32_t e, const st_hdr_t *h)   /* payload -> st_buf, CRC ok: 0 */
{
    if (st_read(st_sector(obj, copy) + st_data(obj, e), st_buf, h->len) || st_crc32(st_buf, h->len) != h->crc)
        return -1;
    return 0;
}

/* a copy's newest intact record, *h its valid entry 0 on the way in: 0, the payload in st_buf, *h, st_cur_e, st_at
 * set. A log entry counts only when it is newer than entry 0 */
static int st_newest(uint32_t obj, uint32_t copy, st_hdr_t *h)
{
    uint32_t e;
    st_hdr_t x;
    for (e = ST_LOGGED(obj) ? st_ln(obj) - 1u : 0u; e; e--)
        if (st_head(obj, copy, e, &x) == 0 && x.seq - h->seq - 1u < 0x80000000u && st_body(obj, copy, e, &x) == 0)
            break;
    if (e)
        *h = x;
    else if (st_body(obj, copy, 0, h))
        return -1;
    st_cur_e = e;
    st_at = st_sector(obj, copy) + st_data(obj, e);
    return 0;
}

/* the current copy: the newest valid sequence at entry 0 (A on a tie), -1 when
 * neither is valid. Headers first, so only the winner's payload is read (it
 * is left in st_buf); *h gets its header. */
static int st_current(uint32_t obj, st_hdr_t *h)
{
    st_hdr_t a, b;
    int va = st_head(obj, 0, 0, &a) == 0, vb = st_head(obj, 1, 0, &b) == 0;
    if (vb && (!va || (b.seq != a.seq && b.seq - a.seq < 0x80000000u))) {
        if (st_newest(obj, 1, &b) == 0) {
            *h = b;
            return 1;
        }
        vb = 0;
    }
    if (va && st_newest(obj, 0, &a) == 0) {
        *h = a;
        return 0;
    }
    if (vb && st_newest(obj, 1, &b) == 0) {
        *h = b;
        return 1;
    }
    return -1;
}

/* load the whole object into dst; returns its length, or -1 if it does not fit */
static int st_load(uint32_t obj, void *dst, uint32_t max)
{
    uint32_t i;
    st_hdr_t h;
    if (ST_BLOCKED() || obj >= OBJ_COUNT || st_current(obj, &h) < 0 || h.len > max)
        return -1;
    for (i = 0; i < h.len; i++)
        ((uint8_t *)dst)[i] = st_buf[i];
    return (int)h.len;
}

/* the first erased entry of a copy from entry e on, 0: none */
static uint32_t st_free(uint32_t obj, uint32_t copy, uint32_t e)
{
    uint32_t off, i;
    uint8_t b[64];
    for (; e < st_ln(obj); e++) {
        for (off = 0; off < ST_LSZ(obj); off += sizeof b) {
            if (st_read(st_sector(obj, copy) + st_entry(obj, e) + off, b, sizeof b))
                return 0;
            for (i = 0; i < sizeof b && b[i] == 0xFFu; i++)
                ;
            if (i < sizeof b)
                break;
        }
        if (off >= ST_LSZ(obj))
            return e;
    }
    return 0;
}

#ifndef ST_PROG_SYNC
#define ST_PROG_SYNC() ((void)0)                       /* before each page program of an ST_NOERASE write (storage_hw.c) */
#endif
#define ST_NOERASE 1u                                  /* st_put: no erase (the audio plays): 1 back if only one would make room */
#define ST_ROLL 2u                                     /* .. entry 0 of the other copy (what older firmware reads) */
/* a save: 0 written (and read back), 1 not now (ST_NOERASE), < 0 a flash error */
static int st_put(uint32_t obj, const void *src, uint32_t len, uint32_t how)
{
    uint32_t seq, copy, e = 0, base, off, n;
    int cur, rc;
    st_hdr_t h;
    if (ST_BLOCKED() || obj >= OBJ_COUNT || len > ST_PAYLOAD_MAX)
        return -1;
    cur = st_current(obj, &h);
    seq = cur < 0 ? 0u : h.seq;
    copy = cur == 0 ? 1u : 0u;                         /* the other copy (none current: an erased one, if any) .. */
    if (cur < 0 && !st_is_blank(obj, 0) && st_is_blank(obj, 1))
        copy = 1;
    if (ST_LOGGED(obj) && cur >= 0 && !(how & ST_ROLL) && len <= ST_LMAX(obj) &&
        (e = st_free(obj, (uint32_t)cur, st_cur_e + 1u)) != 0)
        copy = (uint32_t)cur;                          /* .. or the current one's next erased entry */
    if (!e && !st_is_blank(obj, copy)) {
        if (how & ST_NOERASE)
            return 1;
        if ((rc = st_erase(st_sector(obj, copy))) != 0)
            return rc;
    }
    st_set_blank(obj, copy, 0);                        /* (from here it holds data, or a torn part of it) */
    base = st_sector(obj, copy);
    for (off = 0; off < len; off++)
        st_buf[off] = ((const uint8_t *)src)[off];    /* the driver wants RAM sources */
    for (off = 0; off < len; off += n) {               /* (page by page: a log entry's payload starts mid-page) */
        uint32_t at = base + st_data(obj, e) + off;
        n = 256u - (at & 0xFFu);
        if (n > len - off)
            n = len - off;
        if (how & ST_NOERASE)
            ST_PROG_SYNC();
        if ((rc = st_prog(at, st_buf + off, n)) != 0)
            return rc;
    }
    h.magic = ST_MAGIC;
    h.type = (uint16_t)obj;
    h.slot = (uint16_t)copy;
    h.seq = seq + 1u;
    h.len = len;
    h.crc = st_crc32(st_buf, len);
    h.rsv[0] = e ? e : 0xFFFFFFFFu;
    h.rsv[1] = 0xFFFFFFFFu;
    h.hcrc = st_crc32(&h, sizeof h - 4u);
    if (how & ST_NOERASE)
        ST_PROG_SYNC();
    if ((rc = st_prog(base + st_entry(obj, e), &h, sizeof h)) != 0)   /* the commit record, last */
        return rc;
    {   /* read back: a write-protected or failing part must not report SAVED */
        st_hdr_t chk;
        if (st_head(obj, copy, e, &chk) || memcmp(&chk, &h, sizeof h) || st_body(obj, copy, e, &chk))
            return -7;
    }
    return 0;
}

static int st_save(uint32_t obj, const void *src, uint32_t len)   /* the other copy, erased first: 0 written */
{
    return st_put(obj, src, len, ST_ROLL);
}

/* power-on: which copies, other than the current one, are erased already? (read: every byte 0xFF) */
static void st_check_blank(uint32_t obj)
{
    uint32_t c, off, i;
    uint8_t b[64];
    st_hdr_t h;
    int cur;
    if (ST_BLOCKED() || obj >= OBJ_COUNT)
        return;
    cur = st_current(obj, &h);
    for (c = 0; c < 2u; c++) {
        if ((int)c == cur)
            continue;
        for (off = 0; off < ST_SECTOR; off += sizeof b) {
            if (st_read(st_sector(obj, c) + off, b, sizeof b))
                return;
            for (i = 0; i < sizeof b && b[i] == 0xFFu; i++)
                ;
            if (i < sizeof b)
                break;
        }
        if (off >= ST_SECTOR)
            st_set_blank(obj, c, 1);
    }
}

/* a quiet moment's work on a logged object (each erase holds the audio ~45 ms): the newest record moved to entry 0 of
 * the other copy, where older firmware reads it, then the copy left behind erased for the next saves. One step a
 * call: 1 one done, 0 nothing left, < 0 a flash error. The newest record stays readable at every point */
static int st_tidy(uint32_t obj)
{
    st_hdr_t h;
    uint32_t c;
    int cur;
    if (ST_BLOCKED() || obj >= OBJ_COUNT || (cur = st_current(obj, &h)) < 0)
        return 0;
    if (st_cur_e)                                      /* (st_put takes it from st_buf, where st_current left it) */
        return st_put(obj, st_buf, h.len, ST_ROLL) ? -1 : 1;
    c = cur == 0 ? 1u : 0u;
    if (st_is_blank(obj, c))
        return 0;
    if (st_erase(st_sector(obj, c)))
        return -1;
    st_set_blank(obj, c, 1);
    return 1;
}
