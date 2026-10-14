/* CoralFlower palette: ten four-step ramps after the "TLP" 31-colour pixel palette, on a warm near-black.
 *
 * Every pixel the flora engine and the UI draw is an index into this table; the platform turns the
 * indices into RGB565 once per frame (fl_pal_rgb). A ramp runs from light (step 0) to dark (step 3).
 */
#ifndef FLORA_PAL_H
#define FLORA_PAL_H

#include <stdint.h>

enum {
    FL_BG = 0,      /* the screen background, also "transparent" for sprites */
    FL_BG1 = 1,     /* panels */
    FL_BG2 = 2,     /* raised panels, inactive outlines */
    FL_BG3 = 3,     /* dividers */
    FL_INK = 4,     /* primary text */
    FL_INK2 = 5,    /* secondary text */
    FL_INK3 = 6,    /* disabled text */
    FL_BLACK = 7,   /* pure shadow */
    FL_RAMP0 = 8    /* first ramp entry */
};

enum {
    FL_R_FIRE = 0,   /* yellow -> orange -> red -> maroon */
    FL_R_PEACH,      /* peach -> terracotta -> brown */
    FL_R_COPPER,     /* peach -> copper -> rust -> brown */
    FL_R_LIME,       /* lime -> leaf green -> forest -> deep */
    FL_R_JADE,       /* lime -> jade -> teal -> deep teal */
    FL_R_SEA,        /* aqua -> sky -> cobalt -> indigo */
    FL_R_ORCHID,     /* blush -> pink -> magenta -> plum */
    FL_R_ROSE,       /* blush -> rose -> red -> wine */
    FL_R_SAND,       /* bone -> sand -> umber -> dark umber */
    FL_R_SAGE,       /* mist -> sage -> slate -> night */
    FL_NRAMPS
};

#define FL_PAL_SIZE (FL_RAMP0 + FL_NRAMPS * 4)

/* palette index of ramp r, step s (0 light .. 3 dark) */
#define FL_C(r, s) ((uint8_t)(FL_RAMP0 + (r) * 4 + (s)))

/* 0xRRGGBB */
static const uint32_t fl_pal_rgb[FL_PAL_SIZE] = {
    0x110A02, 0x1A120B, 0x271B12, 0x3A2B22, 0xF1ECE0, 0xA89A88, 0x5E5048, 0x050200,
    0xF9C62A, 0xF1750D, 0xB1301B, 0x6C0E26,   /* FIRE */
    0xF6BA7A, 0xDE6E50, 0x8D3E32, 0x46201E,   /* PEACH */
    0xF6BA7A, 0xCA7535, 0x893E36, 0x46201E,   /* COPPER */
    0xD6E34C, 0x5BB42F, 0x186C44, 0x0E393C,   /* LIME */
    0xD3E34D, 0x22B572, 0x1F6C6B, 0x0D383F,   /* JADE */
    0x48E6D9, 0x0D97D5, 0x1B4D9F, 0x231F6F,   /* SEA */
    0xFAB2A5, 0xE6658B, 0x962179, 0x47115E,   /* ORCHID */
    0xF7B1AC, 0xE86879, 0xB12D24, 0x680C26,   /* ROSE */
    0xEEEEE3, 0xAEA292, 0x6D5E59, 0x3D2F2F,   /* SAND */
    0xE3F0EC, 0x82ABA2, 0x466A74, 0x2A354A,   /* SAGE */
};

/* the backdrop's dark tints, dark to light: dusk behind the plant, deep water behind the coral.
 * Palette entries 56..63 (48..55 are the UI theme's accent and secondary ramps). */
enum { FL_L0 = 56, FL_L1, FL_L2, FL_L3, FL_S0, FL_S1, FL_S2, FL_S3 };
static const uint32_t fl_back_rgb[8] = {
    0x1A1026, 0x2C1838, 0x4E2440, 0x84402E,   /* dusk: violet -> plum -> wine -> the glow */
    0x071420, 0x0B2232, 0x0F3346, 0x15495C,   /* water: deep -> the light from the surface */
};

#endif
