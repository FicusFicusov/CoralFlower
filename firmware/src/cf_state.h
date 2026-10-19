/* SPDX-License-Identifier: GPL-3.0-only
 * CoralFlower: the UI's state and settings (the main loop's; the audio side is told through cf_out.c's queue).
 * Included after cf_out.c, cf_sound.c and flora; before cf_draw.c and cf_ui.c. */

/* ------------------------------------------------------------- screens --- */
enum {
    SCR_HOME = 0,  /* HOME: play mode */
    SCR_SEL,       /* SEL: the chord editor */
    SCR_ENV,       /* ENV: the envelope */
    SCR_BASS,      /* LFO: the bass editor */
    SCR_EDIT,      /* EDIT: the sound editor */
    SCR_GLO,       /* GLO: global settings */
    SCR_FX,        /* FX: effects */
    SCR_ARP,       /* ARP: the arpeggiator */
    SCR_SEQ,       /* SEQ: the metronome */
    SCR_SAVE,      /* SAVE */
    SCR_SQ,        /* SEQ again: the sequencer, a pattern's tracks or the song (cf_sequi.c) */
    SCR_SQED,      /* .. a track open: its steps */
    SCR_N
};
static const char *const SCR_NAME[SCR_N] = {"HOME", "CHORD", "ENVELOPE", "BASS", "SOUND", "GLOBAL", "EFFECTS",
                                             "ARPEGGIO", "METRONOME", "SAVE", "SEQUENCER",
                                             "PATTERN"};

/* themes: the one interface colour (GLO > Display > COLOR), a palette ramp, after the M-VAVE synths' panel
 * colours and a few more */
typedef struct { const char *name; uint8_t ramp; uint32_t light; } cf_theme_t;   /* light: another lightest step
                                                                                     * (0xRRGGBB), 0 = the ramp's */
static const cf_theme_t CF_THEMES[] = {
    {"Coral", FL_R_ROSE, 0}, {"Amber", FL_R_FIRE, 0}, {"Peach", FL_R_PEACH, 0},
    {"Lime", FL_R_LIME, 0xB0E34Cu},            /* (the lime ramp's top turned greener: the UI's own) */
    {"Jade", FL_R_JADE, 0x5BB42Fu},            /* (the jade ramp starts yellow-green: the leaf green instead) */
    {"Sky", FL_R_SEA, 0}, {"Orchid", FL_R_ORCHID, 0}, {"Bone", FL_R_SAND, 0}, {"Mist", FL_R_SAGE, 0},
};
#define CF_NTHEMES ((int)(sizeof CF_THEMES / sizeof CF_THEMES[0]))

/* ------------------------------------------------------------ settings --- */
/* everything the user sets and the unit keeps (cf_settings.c stores it) */
typedef struct {
    cf_chord_cfg_t chord;        /* algorithm, Quantize, Key, Scale, octave, the chord buttons' forms */
    uint16_t glide_ms;           /* HOME K4 */
    uint8_t glide_always;        /* GLO > Play: Always (from the last chord too) or Legato (held chords only) */
    uint8_t vel;                 /* the fixed velocity */
    uint8_t bass_on, octaver;
    int8_t bass_oct;
    uint8_t bass_level;          /* the bass part's P_LEVEL */
    uint8_t metro_level, beats;  /* SEQ: level 0..100, time signature (beats a bar) */
    uint16_t bpm;
    uint8_t accent;              /* the interface colour: a CF_THEMES index */
    uint8_t env_link;            /* ENV edits the chord and the bass part together */
    uint8_t midi_out, ch_chord, ch_bass;
    uint8_t lights;              /* key lights: 0 played only, 1 + the scale */
    uint8_t click;               /* the metronome's sound (cf_out.c CF_CLICKS) */
    uint8_t backdrop;            /* what is behind the plant (FL_BD_*, GLO > Display) */
    cf_fx_t fx;                  /* the effects chain: its order, what is on, the values (FX; cf_fx.c) */
    uint16_t snd[2];             /* the parts' sounds: indices in the one row (cf_sound.c) */
    /* (new fields go here, at the end: cf_settings.c reads an older, shorter record and keeps the defaults past it) */
    uint8_t retrig;              /* a chord changed under held keys (GLO > Play > RETRIG): CF_RT_WAIT a chord button
                                  * there waits for the next root key, another root moves the sounding voices with no
                                  * new attack; CF_RT_MOVE a button moves them too, only the tones a chord adds start
                                  * (the behaviour before WAIT); CF_RT_STRIKE the new notes strike; CF_RT_CHORD
                                  * the whole new chord strikes, the shared tones too, gliding from the last chord */
    cf_arp_t arp;                /* the arpeggiator's settings (ARP; on / off is not kept: off at power-on) */
    cf_loop_t loop;              /* the looper's settings up to 0.3.1 (unused: kept for the record's layout) */
    uint8_t clock_in;            /* GLO > MIDI > CLOCK: 0 Off; 1 In, MIDI clock's pulses run the beat; 2 Out, the unit sends its beat as MIDI clock */
    uint8_t boot_sound;          /* GLO > Display > BOOT SOUND: the chime with the intro */
    uint8_t level_chord, level_layer, level_drums;   /* the parts' P_LEVEL (the sequencer's tracks, K1; the bass's:
                                                      * bass_level) */
    uint16_t snd_layer;          /* the LAYER's sound (part 2): its index in the chord sounds' row; 0xFFFF: the default */
    uint8_t ch_layer, ch_drums;  /* GLO > Channels: the sequencer's LAYER and DRUMS out on MIDI, 0..15 */
    uint8_t seq_hint;            /* the SEQUENCER's hints shown once (0.4, cf_sequi.c SQH_*): bit 0 after the first
                                  * take from HOME, bit 1 HOME after it, bit 2 the first track opened by EDIT */
} cf_set_t;

static cf_set_t cs;

static void cs_defaults(cf_set_t *s)
{
    cf_chord_defaults(&s->chord);
    s->glide_ms = 0;
    s->glide_always = 0;              /* Legato: a glide between held chords only */
    s->vel = 100;
    s->bass_on = 1;
    s->octaver = 0;
    s->bass_oct = 0;
    s->bass_level = 96;
    s->metro_level = 0;
    s->beats = 4;
    s->bpm = 100;
    s->accent = 3;                   /* Lime */
    s->env_link = 1;
    s->midi_out = 1;
    s->ch_chord = 0;
    s->ch_bass = 1;
    s->lights = 1;
    s->click = 0;
    s->backdrop = FL_BD_SCENE;
    s->fx = CFX_DEFAULT;
    s->snd[0] = s->snd[1] = 0;
    s->retrig = CF_RT_WAIT;
    {
        static const cf_arp_t A = CF_ARP_DEFAULT;
        static const cf_loop_t L = CF_LOOP_DEFAULT;
        s->arp = A;
        s->loop = L;
    }
    s->boot_sound = 1;
    s->level_chord = s->level_layer = 104;       /* (params.c P_LEVEL's own) */
    s->level_drums = 104;
    s->snd_layer = 0xFFFFu;
    s->ch_layer = 2;                             /* (channel 3) */
    s->ch_drums = 9;                             /* (channel 10: General MIDI's drums) */
    s->seq_hint = 0;
}

/* --------------------------------------------------------- the UI's own --- */
typedef struct {
    uint8_t scr;                 /* the screen */
    uint8_t page[SCR_N];         /* each screen's page (SELECT / PRESETS) */
    uint8_t sel_btn;             /* SEL: the chord button being edited */
    uint8_t edit_sec;            /* EDIT: the section (OP1..OP6, PIT, GLO) */
    uint8_t edit_part;           /* EDIT: the sound edited, 0 chord 1 bass */
    uint8_t env_tgt;             /* ENV: 0 chord + bass, 1 chord, 2 bass, 3 the sequencer's LAYER */
    uint8_t fx_pos;              /* FX: the selected place in the chain */
    uint8_t arp_on;              /* the arpeggiator plays (the ARP button; off at power-on, as the stock FM-1's) */
    uint8_t save_new;            /* SAVE: as a new sound (else over itself) */
    char save_name[13];          /* .. its name, being edited */
    uint8_t save_cur;            /* .. the letter K3 changes */
    uint32_t save_hold;          /* .. when SAVE went down on the SAVE screen (0: not held) */
    uint32_t rec_hold;           /* when REC went down (0: up): a tap or a hold (cf_rec_poll) */
    uint8_t rec_fired;           /* .. the hold acted: its release does nothing */
    uint8_t save_del;            /* .. ACTION Delete (a user sound only) */
    uint32_t now, last_ms;       /* ms */
    uint32_t frame_dt;           /* ms since the frame before */
    uint32_t keys;               /* the note keys down (as last read) */
    uint32_t knob_ms[4];         /* when each knob last moved (its cell lights) */
    uint8_t knob_last;
    /* popups over the plant */
    uint8_t pop;                 /* POP_* */
    uint32_t pop_until;
    char msg[64];                /* (two lines at most, a '\n' between them) */
    uint8_t booted;
    uint32_t frames;
    uint32_t idle_ms;            /* since the last touch */
    uint8_t upd_left, upd_stub;  /* the update gesture's countdown (cf_shim.c cr_update_poll) */
    uint8_t edited[3];           /* a part's sound changed since it was loaded (SAVE keeps it): chord, bass, layer */
} cf_ui_t;
enum { POP_NONE, POP_SOUND_CHORD, POP_SOUND_BASS, POP_ALGO, POP_OCTAVE, POP_MSG, POP_SOUND_LAYER };
static cf_ui_t cu;

/* the plant */
#define FLW 120
#define FLH 70
static uint8_t fl_px[FLW * FLH] __attribute__((section(".pool")));
static flora_t flo __attribute__((section(".pool")));
static flc_t fl_cv = {fl_px, FLW, FLH};

/* ---------------------------------------------------------- colours --- */
/* palette entries 48..55: the interface colour's ramp, twice (C_ACC and C_SEC: the drawing still tells the two roles
 * apart, they share the one colour) */
#define C_ACC(s) ((uint8_t)(48 + (s)))
#define C_SEC(s) ((uint8_t)(52 + (s)))
static uint32_t cf_pal[64];

#ifndef CF_EMU
/* The FM-1's panel lifts the dark tones and washes out soft colours: the palette, made on a monitor, came out pale and
 * grey there (the owner's photos, 2026-10-15). On the device every colour is recalculated once, here: its saturation
 * raised away from its own luma, then a gamma of 1.2 (darks a little deeper, white and black kept). The interface's
 * neutrals (entries 0..7: background, panels, text) get x1.6. The colours (the plant's ramps, the accent, the sky) get
 * more the softer they are: x2.5 for a pastel down to x1.5 for a full colour, by the square of the colour's own
 * saturation, so the pale pinks rise to where the aqua already was. The emulator shows the palette as designed.
 * CF_PANEL_PLAIN builds the device without it. (rc6 flat x1.4: "much better"; rc8 flat x1.6: "super", but the blue
 * flower far stronger than the rest; the owner: raise the others.) */
static const uint8_t CF_PANEL_GAMMA[256] = {
      0,   0,   1,   1,   2,   2,   3,   3,   4,   5,   5,   6,   7,   7,   8,   9,
      9,  10,  11,  11,  12,  13,  13,  14,  15,  16,  16,  17,  18,  19,  20,  20,
     21,  22,  23,  24,  24,  25,  26,  27,  28,  28,  29,  30,  31,  32,  33,  34,
     34,  35,  36,  37,  38,  39,  40,  40,  41,  42,  43,  44,  45,  46,  47,  48,
     49,  49,  50,  51,  52,  53,  54,  55,  56,  57,  58,  59,  60,  61,  62,  62,
     63,  64,  65,  66,  67,  68,  69,  70,  71,  72,  73,  74,  75,  76,  77,  78,
     79,  80,  81,  82,  83,  84,  85,  86,  87,  88,  89,  90,  91,  92,  93,  94,
     95,  96,  97,  98,  99, 100, 101, 102, 103, 104, 105, 106, 107, 108, 109, 110,
    112, 113, 114, 115, 116, 117, 118, 119, 120, 121, 122, 123, 124, 125, 126, 127,
    128, 130, 131, 132, 133, 134, 135, 136, 137, 138, 139, 140, 141, 142, 144, 145,
    146, 147, 148, 149, 150, 151, 152, 153, 155, 156, 157, 158, 159, 160, 161, 162,
    163, 165, 166, 167, 168, 169, 170, 171, 172, 173, 175, 176, 177, 178, 179, 180,
    181, 183, 184, 185, 186, 187, 188, 189, 191, 192, 193, 194, 195, 196, 197, 199,
    200, 201, 202, 203, 204, 205, 207, 208, 209, 210, 211, 212, 214, 215, 216, 217,
    218, 219, 221, 222, 223, 224, 225, 226, 228, 229, 230, 231, 232, 234, 235, 236,
    237, 238, 239, 241, 242, 243, 244, 245, 247, 248, 249, 250, 251, 253, 254, 255,
};
static uint32_t cf_panel_fix(uint32_t c, int colour)
{
    int32_t ch[3] = {(int32_t)((c >> 16) & 255u), (int32_t)((c >> 8) & 255u), (int32_t)(c & 255u)};
    int32_t y = (ch[0] * 77 + ch[1] * 150 + ch[2] * 29) >> 8, i, v, mx, mn, sat, k;
    uint32_t out = 0;
    mx = ch[0] > ch[1] ? ch[0] : ch[1];
    mx = mx > ch[2] ? mx : ch[2];
    mn = ch[0] < ch[1] ? ch[0] : ch[1];
    mn = mn < ch[2] ? mn : ch[2];
    sat = mx ? (mx - mn) * 256 / mx : 0;          /* the colour's own saturation, 0..256 */
    k = colour ? 637 - ((258 * ((sat * sat) >> 8)) >> 8) : 410;   /* in 1/256: x2.49 .. x1.48; the neutrals x1.6 */
    for (i = 0; i < 3; i++) {
        v = y + (((ch[i] - y) * k) >> 8);
        v = v < 0 ? 0 : v > 255 ? 255 : v;
        out = (out << 8) | CF_PANEL_GAMMA[v];
    }
    return out;
}
#endif

static void cf_palette_build(void)
{
    uint32_t i;
    for (i = 0; i < FL_PAL_SIZE; i++)
        cf_pal[i] = fl_pal_rgb[i];
    for (i = 0; i < 4u; i++) {
        const cf_theme_t *th = &CF_THEMES[cs.accent % CF_NTHEMES];
        cf_pal[48 + i] = !i && th->light ? th->light : fl_pal_rgb[FL_C(th->ramp, i)];
        cf_pal[52 + i] = cf_pal[48 + i];
    }
    for (i = 0; i < 8u; i++)
        cf_pal[56 + i] = fl_back_rgb[i];
#if !defined CF_EMU && !defined CF_PANEL_PLAIN
    for (i = 0; i < 64u; i++)
        cf_pal[i] = cf_panel_fix(cf_pal[i], i >= FL_RAMP0);
#endif
    fb_palette(cf_pal, 64);
}
