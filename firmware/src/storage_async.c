/* SPDX-License-Identifier: GPL-3.0-only */
/* One background autosave, in the existing A/B + delta format. Only planning
 * uses st_buf. Progress/verification must leave that shared scratch untouched:
 * a foreground st_put may drain this job with its own source already in it. */
#define ST_ASYNC_MAX 512u
#ifndef ST_ASYNC_IO
/* Emulator without the NOR timing model: the same transaction state machine,
 * with one immediate low-level command per step. */
static int st_io_result;
static int st_io_begin(uint32_t off, const uint8_t *src, uint32_t n)
{
    st_io_result = n ? st_prog(off, src, n) : st_erase(off);
    return st_io_result;
}
static int st_io_poll(void) { return st_io_result; }
#else
static int st_io_begin(uint32_t off, const uint8_t *src, uint32_t n);
static int st_io_poll(void);
#endif
#ifndef ST_ASYNC_WAIT
#define ST_ASYNC_WAIT() ((void)0)
#endif
enum { STA_IDLE, STA_ERASE, STA_ERASING, STA_BLANK, STA_DATA, STA_WRITING,
       STA_HEADER, STA_COMMITTING, STA_VERIFY };
static struct {
    uint8_t data[ST_ASYNC_MAX];
    st_drec_t delta[ST_DMAX];
    st_hdr_t header;
    void (*done)(int);
    uint32_t obj, copy, entry, at, count, pos, len;
    uint8_t phase, is_delta, erase_only;
} st_async __attribute__((section(".pool"), aligned(4)));

static int st_async_busy(void) { return st_async.phase != STA_IDLE; }
static void st_async_end(int rc)
{
    void (*done)(int) = st_async.done;
    st_async.phase = STA_IDLE;
    if (done) done(rc);
}
static int st_async_cached(uint32_t obj, void *dst, uint32_t max)
{
    if (!st_async_busy() || st_async.erase_only || obj != st_async.obj) return -2;
    if (st_async.len > max) return -1;
    memcpy(dst, st_async.data, st_async.len);
    return (int)st_async.len;
}
/* 0 queued, <0 rejected. The caller owns changes after this snapshot; only the
 * completion callback may mark this exact snapshot as persisted. */
static int st_async_put(uint32_t obj, const void *src, uint32_t len, uint32_t how, void (*done)(int))
{
    st_hdr_t h;
    int cur, n;
    uint32_t copy, e = 0, seq;
    if (st_async_busy() || ST_BLOCKED() || obj >= OBJ_COUNT || len > ST_ASYNC_MAX) return -1;
    memcpy(st_async.data, src, len);                /* src may be st_buf */
    st_async.obj = obj; st_async.len = len; st_async.done = done;
    st_async.is_delta = st_async.erase_only = 0; st_async.pos = 0;
    cur = st_current(obj, &h);
    if (ST_LOGGED(obj) && cur >= 0 && !(how & (ST_ROLL | ST_WHOLE)) && len == h.len &&
        (n = st_dprepare(obj, cur, &h, st_async.data, len, st_async.delta)) >= 0) {
        st_async.is_delta = 1; st_async.copy = (uint32_t)cur;
        st_async.at = st_dnext; st_async.count = (uint32_t)n * 32u;
        st_async.phase = n ? STA_DATA : STA_VERIFY;
        return 0;
    }
    seq = cur < 0 ? 0u : h.seq;
    copy = cur == 0 ? 1u : 0u;
    if (cur < 0 && !st_is_blank(obj, 0) && st_is_blank(obj, 1)) copy = 1;
    if (ST_LOGGED(obj) && cur >= 0 && !(how & ST_ROLL) && len <= ST_LMAX(obj) &&
        (e = st_free(obj, (uint32_t)cur, st_cur_e + 1u)) != 0) copy = (uint32_t)cur;
    if (!e && !st_is_blank(obj, copy) && (how & ST_NOERASE)) return -1;
    st_async.copy = copy; st_async.entry = e;
    st_async.at = st_sector(obj, copy) + st_data(obj, e); st_async.count = len;
    h.magic = ST_MAGIC; h.type = (uint16_t)obj; h.slot = (uint16_t)copy;
    h.seq = seq + 1u; h.len = len; h.crc = st_crc32(st_async.data, len);
    h.rsv[0] = e ? e : 0xFFFFFFFFu; h.rsv[1] = 0xFFFFFFFFu;
    h.hcrc = st_crc32(&h, sizeof h - 4u); st_async.header = h;
    st_async.phase = !e && !st_is_blank(obj, copy) ? STA_ERASE : STA_DATA;
    st_set_blank(obj, copy, 0);
    return 0;
}
/* 1 already tidy, 0 one step queued, <0 rejected. */
static int st_async_tidy(uint32_t obj, void (*done)(int))
{
    st_hdr_t h;
    int cur;
    uint32_t c;
    if (st_async_busy() || ST_BLOCKED() || obj >= OBJ_COUNT) return -1;
    if ((cur = st_current(obj, &h)) < 0) return 1;
    if (st_cur_e || st_cur_d) return st_async_put(obj, st_buf, h.len, ST_ROLL, done);
    c = cur == 0 ? 1u : 0u;
    if (st_is_blank(obj, c)) return 1;
    st_async.obj = obj; st_async.copy = c; st_async.done = done;
    st_async.erase_only = 1; st_async.pos = 0; st_async.phase = STA_ERASE;
    return 0;
}
static void st_async_poll(void)
{
    uint8_t b[32];
    const uint8_t *data = st_async.is_delta ? (const uint8_t *)st_async.delta : st_async.data;
    uint32_t base = st_sector(st_async.obj, st_async.copy), n, i;
    int rc = 0;
    if (!st_async_busy()) return;
    switch (st_async.phase) {
    case STA_ERASE:
        rc = st_io_begin(base, 0, 0); st_async.phase = STA_ERASING; break;
    case STA_ERASING:
        rc = st_io_poll();
        if (!rc) { st_async.pos = 0; st_async.phase = STA_BLANK; }
        break;
    case STA_BLANK:
        rc = st_read(base + st_async.pos, b, sizeof b);
        for (i = 0; !rc && i < sizeof b; i++) if (b[i] != 255u) rc = -7;
        st_async.pos += sizeof b;
        if (!rc && st_async.pos == ST_SECTOR) {
            st_async.pos = 0;
            if (st_async.erase_only) { st_set_blank(st_async.obj, st_async.copy, 1); st_async_end(0); }
            else st_async.phase = STA_DATA;
        }
        break;
    case STA_DATA:
        n = st_async.count - st_async.pos;
        if (!n) { st_async.pos = 0; st_async.phase = st_async.is_delta ? STA_VERIFY : STA_HEADER; break; }
        if (n > 32u) n = 32u;
        if (n > 256u - ((st_async.at + st_async.pos) & 255u)) n = 256u - ((st_async.at + st_async.pos) & 255u);
        rc = st_io_begin(st_async.at + st_async.pos, data + st_async.pos, n);
        st_async.pos += n; st_async.phase = STA_WRITING; break;
    case STA_WRITING:
        rc = st_io_poll(); if (!rc) st_async.phase = STA_DATA; break;
    case STA_HEADER:
        rc = st_io_begin(base + st_entry(st_async.obj, st_async.entry), (const uint8_t *)&st_async.header, 32u);
        st_async.phase = STA_COMMITTING; break;
    case STA_COMMITTING:
        rc = st_io_poll(); if (!rc) st_async.phase = STA_VERIFY; break;
    case STA_VERIFY:
        /* Exact readback also verifies every prepared CRC/commit byte. No
         * st_current here: a foreground caller can own st_buf during drain. */
        if (!st_async.is_delta && !st_async.pos) {
            rc = st_read(base + st_entry(st_async.obj, st_async.entry), b, sizeof b);
            if (!rc && memcmp(b, &st_async.header, sizeof b)) rc = -7;
        }
        n = st_async.count - st_async.pos;
        if (n > sizeof b) n = sizeof b;
        if (!rc && n) {
            rc = st_read(st_async.at + st_async.pos, b, n);
            if (!rc && memcmp(b, data + st_async.pos, n)) rc = -7;
        }
        st_async.pos += n;
        if (!rc && st_async.pos == st_async.count) st_async_end(0);
        break;
    }
    if (rc < 0) st_async_end(rc);
}
static void st_async_drain(void)
{
    while (st_async_busy()) { ST_ASYNC_WAIT(); st_async_poll(); }
}
