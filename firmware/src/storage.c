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
 * copy is the one whose entry 0 is newer, as older firmware picks it; in it the last intact entry wins.
 *
 * 0.6: a change is written as what changed (the owner, "А сохранять без затормаживания нельзя?", "Да, делаем": about
 * 200 changes before an erase, not 29). After the newest whole entry, to the copy's end, sit delta records of 32 bytes
 * (st_drec_t), each a few runs of changed bytes; a change takes one to ST_DMAX of them, numbered, the last marked, and
 * counts only whole (a record torn or missing: the change before it stands). They apply to the whole entry before
 * them (bseq: its seq); a bigger change is a whole entry again, in the next erased one. A record starts with a byte that
 * is no magic's, so older firmware sees an entry it cannot read: it skips it, never writes into it, and takes the
 * newest whole entry (the state of the last quiet moment, when st_tidy wrote it whole into entry 0). Records are
 * written one after another, each inside a page, read back before the save counts */
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
static uint32_t st_cur_d;                              /* .. the changes on it (0: none, st_at's bytes are the record) */
#ifdef ST_LOG_OBJ
#define ST_DTAG 0xD1u                                  /* a delta record's first byte (ST_MAGIC's is 0x46) */
#define ST_DPAY 20u
#define ST_DMAX 4u                                     /* records a change, at most */
typedef struct {
    uint8_t tag, idx;                                  /* idx: its place in the change, bit 7: the change's last */
    uint16_t cseq;                                     /* the change's number: one more than the last one's */
    uint32_t bseq;                                     /* the whole entry it changes: its seq */
    uint8_t pay[ST_DPAY];                              /* runs: offset (2 bytes, low first), length, the bytes; a length 0 ends */
    uint32_t crc;                                      /* st_crc32 of what is above */
} st_drec_t;
_Static_assert(sizeof(st_drec_t) == 32u, "storage: a delta record's layout");
static uint32_t st_dnext;                              /* st_current's find: where its next record goes (0: no room) */
static uint16_t st_dcseq;                              /* .. the last record's change number */
#endif

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

#ifdef ST_LOG_OBJ
/* a change's records r[0 .. n - 1] onto the record in st_buf (len bytes): 0, or -1 (a run outside it: nothing done) */
static int st_dapply(const st_drec_t *r, uint32_t n, uint32_t len)
{
    uint32_t pass, k, p, off, m, i;
    for (pass = 0; pass < 2u; pass++)                  /* (every run checked first, then applied) */
        for (k = 0; k < n; k++)
            for (p = 0; p + 3u <= ST_DPAY && (m = r[k].pay[p + 2u]) != 0; p += 3u + m) {
                off = (uint32_t)r[k].pay[p] | (uint32_t)r[k].pay[p + 1u] << 8;
                if (p + 3u + m > ST_DPAY || off + m > len)
                    return -1;
                for (i = 0; pass && i < m; i++)
                    st_buf[off + i] = r[k].pay[p + 3u + i];
            }
    return 0;
}

/* the changes after whole entry e of a copy, onto its record in st_buf (*h its header; its crc becomes the result's):
 * st_cur_d, st_dnext, st_dcseq. A record that is not erased but not intact (torn, or a torn whole entry's bytes)
 * drops the change it is in; the next record goes after the last one not erased */
static void st_deltas(uint32_t obj, uint32_t copy, uint32_t e, st_hdr_t *h)
{
    union { uint8_t b[256]; st_drec_t r[8]; } c;
    st_drec_t g[ST_DMAX];                              /* (a change's records, until its last) */
    uint32_t at = st_sector(obj, copy) + st_entry(obj, e + 1u), end = st_sector(obj, copy) + ST_SECTOR, gn = 0, j, i, k;
    st_dnext = at;
    st_dcseq = 0;
    for (; at < end; at += sizeof c.b) {
        if (st_read(at, c.b, sizeof c.b)) {
            st_dnext = end;                            /* (unread: no change goes after it) */
            break;
        }
        for (j = 0; j < 8u; j++) {
            const st_drec_t *r = &c.r[j];
            for (i = 0; i < 32u && c.b[j * 32u + i] == 0xFFu; i++)
                ;
            if (i == 32u) {                            /* (erased) */
                gn = 0;
                continue;
            }
            st_dnext = at + (j + 1u) * 32u;
            if (r->tag != ST_DTAG || r->bseq != h->seq || r->crc != st_crc32(r, sizeof *r - 4u)) {
                gn = 0;
                continue;
            }
            st_dcseq = r->cseq;
            k = r->idx & 127u;
            if (!k)
                gn = 0;
            else if (k != gn || r->cseq != g[0].cseq || gn >= ST_DMAX) {
                gn = 0;                                /* (a change's middle without its start) */
                continue;
            }
            g[gn++] = *r;
            if (r->idx & 128u) {
                if (st_dapply(g, gn, h->len) == 0)
                    st_cur_d++;
                gn = 0;
            }
        }
    }
    if (st_cur_d)
        h->crc = st_crc32(st_buf, h->len);
}
#endif

/* a copy's newest intact record, *h its valid entry 0 on the way in: 0, the payload in st_buf, *h, st_cur_e, st_at
 * set. A log entry counts only when it is newer than entry 0. The changes written after it go onto it (st_deltas) */
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
    st_cur_d = 0;
#ifdef ST_LOG_OBJ
    if (ST_LOGGED(obj))
        st_deltas(obj, copy, e, h);
#endif
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
#define ST_WHOLE 4u                                    /* .. a whole entry, not a change (a backup reads it in one piece) */

#ifdef ST_LOG_OBJ
/* a logged object's save as a change onto the current record (st_current's, in st_buf; *h its header): 0 written (and
 * read back), 1 it does not fit (too big, or no room after the last record: a whole entry then), < 0 a flash error.
 * The runs: the bytes that differ, two closer than a run's head (3 bytes) taken as one */
static int st_dput(uint32_t obj, int cur, const st_hdr_t *h, const uint8_t *s, uint32_t len, uint32_t how)
{
    st_drec_t r[ST_DMAX];
    uint32_t n = 0, p = 0, i, j, k, m, at = st_dnext, end = st_sector(obj, (uint32_t)cur) + ST_SECTOR;
    int rc;
    st_hdr_t chk;
    for (i = 0; i < sizeof r; i++)
        ((uint8_t *)r)[i] = 0;
    for (i = 0; i < len; i++) {
        if (s[i] == st_buf[i])
            continue;
        for (k = j = i + 1u; j < len && j < k + 3u; j++)
            if (s[j] != st_buf[j])
                k = j + 1u;                            /* (the run: i .. k - 1) */
        while (i < k) {
            if (p + 4u > ST_DPAY) {                    /* (no room for a byte more: the next record) */
                if (++n >= ST_DMAX)
                    return 1;
                p = 0;
            }
            m = k - i < ST_DPAY - p - 3u ? k - i : ST_DPAY - p - 3u;
            r[n].pay[p] = (uint8_t)i;
            r[n].pay[p + 1u] = (uint8_t)(i >> 8);
            r[n].pay[p + 2u] = (uint8_t)m;
            for (j = 0; j < m; j++)
                r[n].pay[p + 3u + j] = s[i + j];
            p += 3u + m;
            i += m;
        }
        i = k - 1u;
    }
    if (!n && !p)
        return 0;                                      /* (nothing changed) */
    n++;
    if (at < end - ST_SECTOR || at > end || n * 32u > end - at)
        return 1;
    for (j = 0; j < n; j++) {
        r[j].tag = (uint8_t)ST_DTAG;
        r[j].idx = (uint8_t)(j | (j + 1u == n ? 128u : 0u));
        r[j].cseq = (uint16_t)(st_dcseq + 1u);
        r[j].bseq = h->seq;
        r[j].crc = st_crc32(&r[j], sizeof r[j] - 4u);
    }
    for (j = 0; j < n; j++) {
        if (how & ST_NOERASE)
            ST_PROG_SYNC();
        if ((rc = st_prog(at + j * 32u, &r[j], sizeof r[j])) != 0)
            return rc;
    }
    if (st_current(obj, &chk) != cur || chk.len != len)   /* read back: the record now is what was asked */
        return -7;
    for (i = 0; i < len; i++)
        if (st_buf[i] != s[i])
            return -7;
    return 0;
}
#endif
/* a save: 0 written (and read back), 1 not now (ST_NOERASE), < 0 a flash error. A logged object's goes as a change
 * when it fits (st_dput), else whole */
static int st_put(uint32_t obj, const void *src, uint32_t len, uint32_t how)
{
    uint32_t seq, copy, e = 0, base, off, n;
    int cur, rc;
    st_hdr_t h;
    if (ST_BLOCKED() || obj >= OBJ_COUNT || len > ST_PAYLOAD_MAX)
        return -1;
    cur = st_current(obj, &h);
#ifdef ST_LOG_OBJ
    if (ST_LOGGED(obj) && cur >= 0 && !(how & (ST_ROLL | ST_WHOLE)) && len == h.len &&
        (rc = st_dput(obj, cur, &h, (const uint8_t *)src, len, how)) != 1)
        return rc;                                     /* (the change written, or a flash error) */
#endif
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

/* a quiet moment's work on a logged object (each erase holds the audio ~45 ms): the newest record (with its changes)
 * moved whole to entry 0 of the other copy, where older firmware reads it, then the copy left behind erased for the
 * next saves. One step a
 * call: 1 one done, 0 nothing left, < 0 a flash error. The newest record stays readable at every point */
static int st_tidy(uint32_t obj)
{
    st_hdr_t h;
    uint32_t c;
    int cur;
    if (ST_BLOCKED() || obj >= OBJ_COUNT || (cur = st_current(obj, &h)) < 0)
        return 0;
    if (st_cur_e || st_cur_d)                          /* (st_put takes it from st_buf, where st_current left it) */
        return st_put(obj, st_buf, h.len, ST_ROLL) ? -1 : 1;
    c = cur == 0 ? 1u : 0u;
    if (st_is_blank(obj, c))
        return 0;
    if (st_erase(st_sector(obj, c)))
        return -1;
    st_set_blank(obj, c, 1);
    return 1;
}
