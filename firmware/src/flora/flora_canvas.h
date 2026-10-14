/* An indexed-colour canvas and the few primitives the flora engine needs.
 *
 * Coordinates of the shape primitives are Q8 (1 pixel = 256), so shapes move smoothly by sub-pixels while
 * the pixels themselves stay hard-edged (no anti-aliasing: the look is chunky pixel art).
 * Colour gradients are ordered-dithered across the four steps of a palette ramp.
 */
#ifndef FLORA_CANVAS_H
#define FLORA_CANVAS_H

#include <stdint.h>

typedef struct {
    uint8_t *px;    /* w * h palette indices, row-major */
    int16_t w, h;
} flc_t;

void flc_clear(flc_t *c, uint8_t col);
void flc_pset(flc_t *c, int x, int y, uint8_t col);
/* a dot of radius r (pixels, 0 = one pixel) centred at a Q8 point */
void flc_dot(flc_t *c, int32_t x, int32_t y, int r, uint8_t col);
/* a straight line of one colour, Q8 ends */
void flc_line(flc_t *c, int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint8_t col);
/* a filled triangle, Q8 corners, pixel centres inside are filled */
void flc_tri(flc_t *c, int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x2, int32_t y2, uint8_t col);
/* a stroke whose width (Q8 px) and ramp position (Q8: 0 = step 0 .. 768 = step 3) run from the first end to
 * the second; the colour is dithered between neighbouring ramp steps: a gradient along the line */
void flc_stroke(flc_t *c, int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t w0, int32_t w1,
                int ramp, int32_t t0, int32_t t1);
/* the dithered ramp colour at pixel (x, y) for ramp position t (Q8 over 0..768) */
uint8_t flc_ramp_px(int ramp, int32_t t, int x, int y);

#endif
