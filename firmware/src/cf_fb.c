/* SPDX-License-Identifier: GPL-3.0-only
 * CoralFlower: the screen as an indexed framebuffer.
 *
 * 240 x 240 bytes, one palette index a pixel, in the POOL room gfx.c's canvas had (the two are never used at the
 * same time: gfx.c draws only the crash, update and SAFE MODE screens, after which CoralFlower's UI does not run).
 * Every frame the screens draw the whole picture; cf_present() hashes it in 16 x 16 tiles and sends only the tiles
 * that changed, as RGB565 through two DMA buffers (one converts while the other goes out). The panel's SPI link
 * moves a full screen in ~77 ms; the plant animates its own region and the rest costs nothing while it stays still.
 * A palette change (a colour theme) resends everything.
 *
 * Drawing: rectangles, lines, pixel-font text at 1x or 2x, and the flora canvas blown up 2x. No blending: the
 * look is pixel art. Included after gfx.c (its canvas), lcd.c and flora.
 */
#include "cf_fonts.h"

#define FB_W 240
#define FB_H 240
#define FB_TS 16
#define FB_TX (FB_W / FB_TS)
#define FB_TY (FB_H / FB_TS)
#define FB_PAL 64

_Static_assert(sizeof(cv_px) >= FB_W * FB_H, "cf_fb: the framebuffer lives in gfx.c's canvas (CV_ROWS 120)");
#define fb ((uint8_t *)cv_px)

static uint16_t fb_lut[FB_PAL];                  /* index -> RGB565, byte-swapped as the panel takes it */
static uint32_t fb_hash[FB_TX * FB_TY];
static uint16_t fb_dma[2][FB_W * FB_TS] __attribute__((section(".pool")));
static uint8_t fb_full = 1;                       /* resend every tile at the next present */
static uint32_t fb_tiles_sent;                    /* diagnostics: tiles sent since power-on */
static int16_t fb_cx0, fb_cy0, fb_cx1 = FB_W, fb_cy1 = FB_H;   /* the clip rectangle */

#ifndef CF_PUMP
#define CF_PUMP() ((void)0)                       /* the keys read between tile rows (cf_ui.c) */
#endif

static void fb_palette(const uint32_t *rgb, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n && i < FB_PAL; i++) {
        uint32_t c = rgb[i];
        uint32_t v = ((c >> 8) & 0xF800u) | ((c >> 5) & 0x07E0u) | ((c >> 3) & 0x001Fu);
        uint16_t s = (uint16_t)(((v >> 8) & 0xFFu) | ((v & 0xFFu) << 8));
        if (fb_lut[i] != s) {
            fb_lut[i] = s;
            fb_full = 1;
        }
    }
}

static void fb_clip(int x0, int y0, int x1, int y1)
{
    fb_cx0 = (int16_t)(x0 < 0 ? 0 : x0);
    fb_cy0 = (int16_t)(y0 < 0 ? 0 : y0);
    fb_cx1 = (int16_t)(x1 > FB_W ? FB_W : x1);
    fb_cy1 = (int16_t)(y1 > FB_H ? FB_H : y1);
}
static void fb_unclip(void) { fb_clip(0, 0, FB_W, FB_H); }

static inline void fb_pset(int x, int y, uint8_t c)
{
    if (x >= fb_cx0 && x < fb_cx1 && y >= fb_cy0 && y < fb_cy1)
        fb[y * FB_W + x] = c;
}

static void fb_rect(int x, int y, int w, int h, uint8_t c)
{
    int x1 = x + w, y1 = y + h, i, j;
    if (x < fb_cx0)
        x = fb_cx0;
    if (y < fb_cy0)
        y = fb_cy0;
    if (x1 > fb_cx1)
        x1 = fb_cx1;
    if (y1 > fb_cy1)
        y1 = fb_cy1;
    for (j = y; j < y1; j++) {
        uint8_t *p = fb + j * FB_W;
        for (i = x; i < x1; i++)
            p[i] = c;
    }
}
static void fb_fill(uint8_t c) { fb_rect(0, 0, FB_W, FB_H, c); }
static void fb_hline(int x, int y, int w, uint8_t c) { fb_rect(x, y, w, 1, c); }
static void fb_vline(int x, int y, int h, uint8_t c) { fb_rect(x, y, 1, h, c); }
static void fb_frame(int x, int y, int w, int h, uint8_t c)
{
    fb_hline(x, y, w, c);
    fb_hline(x, y + h - 1, w, c);
    fb_vline(x, y, h, c);
    fb_vline(x + w - 1, y, h, c);
}
/* a pixel-art rounded box: the corner pixels left out (r 1) or a 2-step corner (r 2) */
static void fb_rbox(int x, int y, int w, int h, int r, uint8_t c)
{
    if (r <= 0 || w < 4 || h < 4) {
        fb_rect(x, y, w, h, c);
        return;
    }
    fb_rect(x + r, y, w - 2 * r, h, c);
    fb_rect(x, y + r, r, h - 2 * r, c);
    fb_rect(x + w - r, y + r, r, h - 2 * r, c);
    if (r >= 2) {
        fb_rect(x + 1, y + 1, 1, 1, c);
        fb_rect(x + w - 2, y + 1, 1, 1, c);
        fb_rect(x + 1, y + h - 2, 1, 1, c);
        fb_rect(x + w - 2, y + h - 2, 1, 1, c);
    }
}
static void fb_rframe(int x, int y, int w, int h, uint8_t c)
{
    fb_hline(x + 1, y, w - 2, c);
    fb_hline(x + 1, y + h - 1, w - 2, c);
    fb_vline(x, y + 1, h - 2, c);
    fb_vline(x + w - 1, y + 1, h - 2, c);
}
static void fb_line(int x0, int y0, int x1, int y1, uint8_t c)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
    int dy = y1 > y0 ? y0 - y1 : y1 - y0, sy = y0 < y1 ? 1 : -1, e = dx + dy;
    for (;;) {
        int e2 = 2 * e;                          /* (taken once: both steps test the same error) */
        fb_pset(x0, y0, c);
        if (x0 == x1 && y0 == y1)
            break;
        if (e2 >= dy) {
            e += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            e += dx;
            y0 += sy;
        }
    }
}
/* a checkered (50 %) rectangle: a dithered shade over what is there */
static void fb_checker(int x, int y, int w, int h, uint8_t c)
{
    int i, j;
    for (j = y; j < y + h; j++)
        for (i = x + ((j ^ x) & 1); i < x + w; i += 2)
            fb_pset(i, j, c);
}

/* three pixels in four of the area to c (a deeper dimming than fb_checker's half) */
static void fb_veil(int x, int y, int w, int h, uint8_t c)
{
    int i, j;
    for (j = y; j < y + h; j++)
        for (i = x; i < x + w; i++)
            if ((i | j) & 1)
                fb_pset(i, j, c);
}

/* ------------------------------------------------------------------ text --- */
static int fb_glyph(const cf_font_t *f, uint8_t ch, int x, int y, uint8_t c, int s)
{
    const cf_glyph_t *g = &f->g[ch];
    const uint8_t *b;
    int r, i, adv = g->adv;
    if (!adv) {
        if (ch >= 'a' && ch <= 'z' && f->g[ch - 32].adv)   /* the micro face has few lower-case letters */
            return fb_glyph(f, (uint8_t)(ch - 32), x, y, c, s);
        return 0;
    }
    b = f->bits + g->off;
    for (r = 0; r < f->h; r++) {
        uint8_t row = b[r];
        for (i = 0; row; i++, row = (uint8_t)(row << 1))
            if (row & 0x80u) {
                if (s == 1)
                    fb_pset(x + i, y + r, c);
                else
                    fb_rect(x + i * s, y + r * s, s, s, c);
            }
    }
    return adv * s;
}
/* text at (x, y = the top of the font's cell); '#' and 'b' after a note letter can be drawn as sharp and flat signs
 * by passing them as 0x80 / 0x81. Returns the width */
static int fb_text(const cf_font_t *f, int x, int y, const char *s, uint8_t c, int scale)
{
    int x0 = x;
    while (*s)
        x += fb_glyph(f, (uint8_t)*s++, x, y, c, scale);
    return x - x0;
}
static int fb_text_w(const cf_font_t *f, const char *s, int scale)
{
    int w = 0;
    for (; *s; s++) {
        uint8_t ch = (uint8_t)*s;
        int a = f->g[ch].adv;
        if (!a && ch >= 'a' && ch <= 'z')
            a = f->g[ch - 32].adv;
        w += a * scale;
    }
    return w;
}
static int fb_text_c(const cf_font_t *f, int cx, int y, const char *s, uint8_t c, int scale)
{
    return fb_text(f, cx - fb_text_w(f, s, scale) / 2, y, s, c, scale);
}
static int fb_text_r(const cf_font_t *f, int xr, int y, const char *s, uint8_t c, int scale)
{
    return fb_text(f, xr - fb_text_w(f, s, scale), y, s, c, scale);
}
/* text with a one-pixel halo (readable over the plant) */
static int fb_text_halo(const cf_font_t *f, int x, int y, const char *s, uint8_t c, uint8_t halo, int scale)
{
    int dx, dy;
    for (dy = -1; dy <= 1; dy++)
        for (dx = -1; dx <= 1; dx++)
            if (dx || dy)
                fb_text(f, x + dx, y + dy, s, halo, scale);
    return fb_text(f, x, y, s, c, scale);
}

/* a note name with real sharp / flat signs ("F#" -> F + 0x80) into b (8 bytes) */
static void fb_signs(const char *in, char *b, uint32_t n)
{
    uint32_t k = 0;
    int i;
    for (i = 0; in[i] && k + 1 < n; i++) {
        char ch = in[i];
        if (i > 0 && ch == '#')
            ch = (char)0x80;
        else if (i > 0 && ch == 'b' && (in[i - 1] == 'b' || (in[i - 1] >= 'A' && in[i - 1] <= 'G')))
            ch = (char)0x81;
        b[k++] = ch;
    }
    b[k] = 0;
}

/* ------------------------------------------------------------ the plant --- */
/* the flora canvas (w x h indices) at 2x into the framebuffer at (x, y) */
static void fb_blit2x(int x, int y, const uint8_t *src, int w, int h)
{
    int i, j;
    for (j = 0; j < h; j++) {
        int yy = y + 2 * j;
        const uint8_t *s = src + j * w;
        uint8_t *d0, *d1;
        if (yy < fb_cy0 || yy + 1 >= fb_cy1)
            continue;
        d0 = fb + yy * FB_W;
        d1 = d0 + FB_W;
        for (i = 0; i < w; i++) {
            int xx = x + 2 * i;
            if (xx >= fb_cx0 && xx + 1 < fb_cx1) {
                d0[xx] = d0[xx + 1] = d1[xx] = d1[xx + 1] = s[i];
            }
        }
    }
}

/* ------------------------------------------------------------- present --- */
static uint32_t fb_tile_hash(int tx, int ty)
{
    const uint8_t *p = fb + ty * FB_TS * FB_W + tx * FB_TS;
    uint32_t h = 2166136261u;
    int r, k;
    for (r = 0; r < FB_TS; r++, p += FB_W) {
        const uint8_t *q = p;
        for (k = 0; k < FB_TS; k += 4, q += 4) {
            uint32_t w = (uint32_t)q[0] | (uint32_t)q[1] << 8 | (uint32_t)q[2] << 16 | (uint32_t)q[3] << 24;
            /* (the rotation brings each word's high byte down: with the multiply alone a change there only ever
             * reached the hash's top byte, where two rows' changes could cancel, and a tile's right column was then
             * not sent: a stale line on the panel) */
            h = (h ^ w) * 16777619u;
            h = h << 13 | h >> 19;
        }
    }
    return h;
}

static void cf_present(void)
{
    int tx, ty, which = 0;
    for (ty = 0; ty < FB_TY; ty++) {
        uint16_t dirty = 0;
        for (tx = 0; tx < FB_TX; tx++) {
            uint32_t h = fb_tile_hash(tx, ty);
            if (fb_full || h != fb_hash[ty * FB_TX + tx]) {
                fb_hash[ty * FB_TX + tx] = h;
                dirty |= (uint16_t)(1u << tx);
            }
        }
        tx = 0;
        while (dirty && tx < FB_TX) {
            int t0, t1, x0, w, r, i;
            uint16_t *d;
            if (!((dirty >> tx) & 1u)) {
                tx++;
                continue;
            }
            t0 = tx;
            while (tx < FB_TX && ((dirty >> tx) & 1u))
                tx++;
            t1 = tx;
            x0 = t0 * FB_TS;
            w = (t1 - t0) * FB_TS;
            d = fb_dma[which];
            which ^= 1;
            for (r = 0; r < FB_TS; r++) {
                const uint8_t *s = fb + (ty * FB_TS + r) * FB_W + x0;
                for (i = 0; i < w; i++)
                    *d++ = fb_lut[s[i] & (FB_PAL - 1)];
            }
            lcd_blit((uint32_t)x0, (uint32_t)(ty * FB_TS), (uint32_t)w, FB_TS, fb_dma[which ^ 1]);
            fb_tiles_sent += (uint32_t)(t1 - t0);
        }
        if (dirty)
            CF_PUMP();
    }
    fb_full = 0;
}
