#include "flora_canvas.h"
#include "flora_pal.h"

static const uint8_t bayer4[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};

void flc_clear(flc_t *c, uint8_t col)
{
    int n = c->w * c->h, i;
    for (i = 0; i < n; i++)
        c->px[i] = col;
}

void flc_pset(flc_t *c, int x, int y, uint8_t col)
{
    if ((unsigned)x < (unsigned)c->w && (unsigned)y < (unsigned)c->h)
        c->px[y * c->w + x] = col;
}

uint8_t flc_ramp_px(int ramp, int32_t t, int x, int y)
{
    int32_t step, frac;
    if (t < 0)
        t = 0;
    if (t > 768)
        t = 768;
    step = t >> 8;
    frac = t & 255;
    /* pixel-art banding: solid steps with a short checkered seam between them */
    if (step < 3 && (frac > 170 || (frac > 85 && ((x ^ y) & 1))))
        step++;
    (void)bayer4;
    return FL_C(ramp, step);
}

static void flc_span(flc_t *c, int y, int xa, int xb, uint8_t col)
{
    int x;
    if ((unsigned)y >= (unsigned)c->h)
        return;
    if (xa < 0)
        xa = 0;
    if (xb >= c->w)
        xb = c->w - 1;
    for (x = xa; x <= xb; x++)
        c->px[y * c->w + x] = col;
}

/* disc spans for radius 0..5: half-widths per row offset */
static const int8_t disc_hw[6][6] = {
    {0, -1, -1, -1, -1, -1},
    {1, 0, -1, -1, -1, -1},
    {1, 1, 0, -1, -1, -1},
    {2, 2, 1, 0, -1, -1},
    {3, 3, 2, 2, 0, -1},
    {4, 4, 4, 3, 2, 0},
};

void flc_dot(flc_t *c, int32_t x, int32_t y, int r, uint8_t col)
{
    int px = (int)(x >> 8), py = (int)(y >> 8), dy;
    if (r <= 0) {
        flc_pset(c, px, py, col);
        return;
    }
    if (r > 5)
        r = 5;
    for (dy = -r; dy <= r; dy++) {
        int hw = disc_hw[r][dy < 0 ? -dy : dy];
        if (hw >= 0)
            flc_span(c, py + dy, px - hw, px + hw, col);
    }
}

static int32_t flc_iabs(int32_t v) { return v < 0 ? -v : v; }

void flc_line(flc_t *c, int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint8_t col)
{
    int32_t dx = x1 - x0, dy = y1 - y0;
    int32_t n = (flc_iabs(dx) > flc_iabs(dy) ? flc_iabs(dx) : flc_iabs(dy)) >> 8, i;
    if (n < 1)
        n = 1;
    for (i = 0; i <= n; i++)
        flc_pset(c, (int)((x0 + dx * i / n) >> 8), (int)((y0 + dy * i / n) >> 8), col);
}

void flc_stroke(flc_t *c, int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t w0, int32_t w1,
                int ramp, int32_t t0, int32_t t1)
{
    int32_t dx = x1 - x0, dy = y1 - y0;
    int32_t n = (flc_iabs(dx) > flc_iabs(dy) ? flc_iabs(dx) : flc_iabs(dy)) >> 7, i; /* half-pixel steps */
    if (n < 1)
        n = 1;
    for (i = 0; i <= n; i++) {
        int32_t x = x0 + dx * i / n, y = y0 + dy * i / n;
        int32_t w = w0 + (w1 - w0) * i / n;
        int32_t t = t0 + (t1 - t0) * i / n;
        int px = (int)(x >> 8), py = (int)(y >> 8);
        int r = (int)((w + 128) >> 9); /* radius = width / 2, rounded */
        if (r <= 0) {
            flc_pset(c, px, py, flc_ramp_px(ramp, t, px, py));
        } else {
            /* a lit cylinder: light from the upper left, shadow on the lower right */
            int ddy;
            if (r > 5)
                r = 5;
            for (ddy = -r; ddy <= r; ddy++) {
                int hw = disc_hw[r][ddy < 0 ? -ddy : ddy], xx;
                for (xx = px - hw; xx <= px + hw && hw >= 0; xx++) {
                    int32_t lit = -((xx - px) + ddy) * 200 / (r + 1);
                    flc_pset(c, xx, py + ddy, flc_ramp_px(ramp, t - lit, xx, py + ddy));
                }
            }
        }
    }
}

void flc_tri(flc_t *c, int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x2, int32_t y2, uint8_t col)
{
    int32_t minx, maxx, miny, maxy, px, py, area;
    minx = x0 < x1 ? x0 : x1;
    if (x2 < minx)
        minx = x2;
    maxx = x0 > x1 ? x0 : x1;
    if (x2 > maxx)
        maxx = x2;
    miny = y0 < y1 ? y0 : y1;
    if (y2 < miny)
        miny = y2;
    maxy = y0 > y1 ? y0 : y1;
    if (y2 > maxy)
        maxy = y2;
    minx >>= 8;
    maxx >>= 8;
    miny >>= 8;
    maxy >>= 8;
    if (minx < 0)
        minx = 0;
    if (miny < 0)
        miny = 0;
    if (maxx >= c->w)
        maxx = c->w - 1;
    if (maxy >= c->h)
        maxy = c->h - 1;
    /* work in Q4 to keep the edge products inside 32 bits */
    x0 >>= 4; y0 >>= 4; x1 >>= 4; y1 >>= 4; x2 >>= 4; y2 >>= 4;
    area = (x1 - x0) * (y2 - y0) - (y1 - y0) * (x2 - x0);
    if (area == 0)
        return;
    for (py = miny; py <= maxy; py++) {
        int32_t cy = (py << 4) + 8;
        for (px = minx; px <= maxx; px++) {
            int32_t cx = (px << 4) + 8;
            int32_t e0 = (x1 - x0) * (cy - y0) - (y1 - y0) * (cx - x0);
            int32_t e1 = (x2 - x1) * (cy - y1) - (y2 - y1) * (cx - x1);
            int32_t e2 = (x0 - x2) * (cy - y2) - (y0 - y2) * (cx - x2);
            if (area > 0 ? (e0 >= 0 && e1 >= 0 && e2 >= 0) : (e0 <= 0 && e1 <= 0 && e2 <= 0))
                c->px[py * c->w + px] = col;
        }
    }
}
