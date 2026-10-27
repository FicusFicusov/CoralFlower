/* SPDX-License-Identifier: GPL-3.0-only
 * CoralFlower: the UI - the keys, buttons and knobs, the screens, the key lights, the plant's food.
 * The main loop's (ui_input / ui_leds / ui_draw through cf_shim.c). Included after cf_draw.c.
 *
 * Every screen has the same grammar (docs/SPEC.md): MASTER is the volume; SELECT, PRESETS and ALGORITHM keep
 * their roles (bass sound, chord sound, chord algorithm) unless a screen needs pages; KNOB 1-4 are the screen's.
 * A screen's button opens it; pressed again (or HOME) it goes back to HOME. Pages turn only with an encoder.
 */

static void cf_message(const char *s);
#ifndef CF_KEY_T0
#ifdef CU_KEY_T0
#define CF_KEY_T0(k) CU_KEY_T0(k)                /* (the emulator's: tools/emu/emu_hal_fw.h) */
#else
#define CF_KEY_T0(k) fm1_in.note_t0[k]           /* the first scan that saw key k closed (hal/fm1_input.h) */
#endif
#endif
static void cf_pop(uint32_t kind, uint32_t ms);
static int cf_editing_part(void);
static void cf_edited(uint32_t part);
static void cf_glide_migrate(void);
static void ui_snd_heard(uint32_t part);
static void cf_save_open(void);
static void cf_save_name_init(void);
static void snd_num(uint32_t idx, char *b);
static const char CF_NAME_CHARS[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-.+#/";   /* (SAVE: a name's letters) */

/* -------------------------------------------------------- the engine --- */
static uint8_t sq_live_part(void);              /* (cf_sequi.c) */
static uint8_t sq_live_bass(void);              /* (cf_sequi.c) */
static uint8_t sq_keys_arp(void);               /* (cf_sequi.c: the keys' ARP, a track's own while it is open) */
static void cf_send_config(void)                 /* the chord settings -> the audio side */
{
    fm1_irq_off();
    cfc_shadow = cs.chord;
    cfp_cfg.glide_always = cs.glide_always;
    cfp_cfg.vel = cs.vel;
    cfp_cfg.bass_on = cs.bass_on;
    cfp_cfg.bass_oct = cs.bass_oct;
    cfp_cfg.octaver = cs.octaver;
    cfp_cfg.midi_out = cs.midi_out;
    cfp_cfg.ch_chord = cs.ch_chord;
    cfp_cfg.ch_bass = cs.ch_bass;
    cfp_cfg.ch_layer = cs.ch_layer;
    cfp_cfg.ch_drums = cs.ch_drums;
    cfp_cfg.retrig = cs.retrig;
    cfp_cfg.notes_in = cs.notes_in;
    cfp_cfg.arp_on = sq_keys_arp();              /* (a track open: the keys play as it plays, its own ARP) */
    cfp_cfg.arp = cs.arp;
    cfp_cfg.bpm = cs.bpm;
    cfp_cfg.clock_in = cs.clock_in == 1u;
    cfp_cfg.clock_out = cs.clock_in == 2u;
    trk[CF_PART_BASS].p[P_LEVEL] = cs.bass_level;
    trk[CF_PART_CHORD].p[P_LEVEL] = cs.level_chord;
    trk[CF_PART_LAYER].p[P_LEVEL] = cs.level_layer;
    trk[CF_PART_DRUMS].p[P_LEVEL] = cs.level_drums;
    cfp_cfg.cpart = sq_live_part();              /* (the keys play the LAYER's sound while it is open) */
    cfp_cfg.bass_only = sq_live_bass();          /* (.. the bass alone in the frame's BASS) */
    fm1_irq_on();
    cf_fx_apply(&cs.fx);
    cf_post(CFE_CONFIG, 0, 0, 0);
}
static void cf_send_metro(void)
{
    sq_beats = (uint8_t)(cs.beats >= 2u && cs.beats <= 7u ? cs.beats : 4u);   /* (the sequencer's bar: cf_seqd.c) */
    cf_post(CFE_TEMPO, 0, 0, cs.bpm);
    cf_post(CFE_SIG, 0, 0, cs.beats);
    cf_post(CFE_METRO, 0, cs.metro_level, 0);   /* (the click only while the beat runs, PLAY: the owner, 2026-10-19;
                                                  * it sounded all the time the METRONOME or the SEQUENCER was open) */
}

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static int wrapi(int v, int n) { return ((v % n) + n) % n; }

/* the arpeggiator's names: a knob cell's (7 characters at most), the screen's title */
static const char *const CF_ARP_SHORT[CF_ARP_NMODES] = {"Up", "Down", "Incl", "Excl", "Random", "Order", "Repeat",
                                                        "Strum\x84", "Strum\x85"};
static const char *const CF_ARP_NAME[CF_ARP_NMODES] = {"Up", "Down", "Inclusive", "Exclusive", "Random", "Order",
                                                       "Repeat", "Strum up", "Strum down"};
static const char *const CF_ARP_RATE[CF_ARP_NRATES] = {"1/1", "1/2", "1/4", "1/4T", "1/8", "1/8T", "1/16", "1/16T",
                                                       "1/32"};

/* ------------------------------------------------------------ spelling --- */
static cf_spell_t cu_spell;
static void cu_spell_update(void)
{
    if (cs.chord.quantize == CF_Q_OFF)
        cf_key_spell(0, CF_SC_MAJOR, &cu_spell);
    else
        cf_key_spell(cs.chord.tonic, cs.chord.scale, &cu_spell);
}

/* GLOBAL > Reset's steps (cf_reset_poll below) */
static void cf_factory_erase(void);
static struct {
    uint8_t arm, fin, go;        /* K4 at Reset; the hold done (the last question); erasing */
    uint32_t hold, fin_ms, go_ms;   /* SAVE went down; the question came; the erase was asked */
} cfr;
#define CF_RESET_HOLD_MS 3000u
#define CF_RESET_ASK_MS 6000u

/* --------------------------------------------------------------- knobs --- */
static const char *const Q_NAME[3] = {"Off", "Basic", "Full"};
static const char *const ALG_NAME[CF_NALGS] = {"Top", "Top Lock", "Side", "Side Lock"};

static void kfmt(char *b, uint32_t n, const char *s) { str_cpy(b, s, n); }
static void kint(char *b, int v, const char *unit)
{
    char t[12];
    int k = 0, neg = v < 0, i;
    unsigned u = (unsigned)(neg ? -v : v);
    do {
        t[k++] = (char)('0' + u % 10u);
        u /= 10u;
    } while (u && k < 10);
    i = 0;
    if (neg)
        b[i++] = '-';
    while (k)
        b[i++] = t[--k];
    while (unit && *unit && i < 13)
        b[i++] = *unit++;
    b[i] = 0;
}
static void kpc(char *b, int pc)
{
    char n[4];
    cf_pc_name(pc, &cu_spell, n);
    fb_signs(n, b, 8);
}

#include "cf_edit.c"                             /* (the ENV and EDIT screens' engine side) */
#include "cf_fxui.c"                             /* (the FX screen's controls) */
#include "cf_granui.c"                           /* (GLO's GRAIN pages: cf_gran.c's controls) */
static void cv_turned(int d);
#include "cf_sequi.c"                            /* (the SEQUENCER screens' controls) */

/* EDIT's part keys, as printed on the unit: MONO (upper key 8) the bass, POLY (9) the chords, the next (10) the LAYER */
static uint32_t edit_key_part(int u) { return u == 8 ? CF_PART_BASS : u == 9 ? CF_PART_CHORD : CF_PART_LAYER; }
static uint32_t env_part(void)
{
    return cu.env_tgt == 2u ? CF_PART_BASS : cu.env_tgt == 3u ? CF_PART_LAYER : CF_PART_CHORD;
}
/* SAVE's pages: the chord's sound, the bass's, the LAYER's (the sequencer's third sound: the owner, 2026-10-19) */
#define CF_SAVE_NPG 3u
static uint32_t cf_save_part(void) { return cu.page[SCR_SAVE] < CF_SAVE_NPG ? cu.page[SCR_SAVE] : 0u; }

/* the first chord button SEL can edit: the first that is not Off (Side has no Off) */
static uint8_t sel_first(void)
{
    uint8_t b = 0;
    while (b + 1u < CF_TOP_NBTN && cf_is_off(&cs.chord, b))
        b++;
    return b;
}

/* the chord button SEL edits (the callers leave Off alone: cf_is_off) */
static cf_form_t *sel_form(void)
{
    int top = cs.chord.algo == CF_ALG_TOP || cs.chord.algo == CF_ALG_TOP_LOCK;
    if (top)
        return &cs.chord.top[cu.sel_btn < CF_TOP_NBTN ? cu.sel_btn : sel_first()];
    return &cs.chord.side[cu.sel_btn < CF_SIDE_NBTN ? cu.sel_btn : 0];
}

static void ui_knob_info(int k, cf_knob_t *o)
{
    o->bar = -1;
    switch (cu.scr) {
    case SCR_HOME:
        switch (k) {
        case 0: kfmt(o->name, 12, "QUANTIZE"); kfmt(o->val, 14, Q_NAME[cs.chord.quantize]); break;
        case 1:
            kfmt(o->name, 12, "KEY");
            kpc(o->val, cs.chord.tonic);
            o->dis = cs.chord.quantize == CF_Q_OFF;
            break;
        case 2:
            kfmt(o->name, 12, "SCALE");
            kfmt(o->val, 14, CF_SCALES[cs.chord.scale].shrt);
            o->dis = cs.chord.quantize == CF_Q_OFF;
            break;
        default:
            kfmt(o->name, 12, "GLIDE");          /* (the chord sound's own, 0.5: cf_play.c cfp_glide_g) */
            if (trk[CF_PART_CHORD].p[P_GLIDE] > 0)
                kint(o->val, (int)cfe_glide_ms(trk[CF_PART_CHORD].p[P_GLIDE]), "ms");
            else
                kfmt(o->val, 14, "Off");
            o->bar = (int16_t)(clampi(trk[CF_PART_CHORD].p[P_GLIDE], 0, 127) * 255 / 127);
            break;
        }
        break;
    case SCR_SEL: {
        cf_form_t *f = sel_form();
        if (cf_is_off(&cs.chord, cu.sel_btn)) {
            if (k == 0) {
                kfmt(o->name, 12, "BUTTON");
                kfmt(o->val, 14, "Hold");
            }
            return;
        }
        switch (k) {
        case 0: kfmt(o->name, 12, "TYPE"); kfmt(o->val, 14, cf_type_name(f->type)); break;
        case 1: kfmt(o->name, 12, "ADD"); kfmt(o->val, 14, cf_adds_name(cf_adds_index(f->add))); break;
        case 2:
            kfmt(o->name, 12, "INV");             /* (a knob cell holds 8 characters) */
            kfmt(o->val, 14, f->inv == CF_INV_AUTO ? "Auto" : f->inv == 0 ? "Root" : f->inv == 1 ? "1st" : f->inv == 2 ? "2nd" : "3rd");
            break;
        default: {
            static const char *const DEG[] = {"Root", "2nd tone", "3rd tone", "4th tone", "5th tone", "6th tone"};
            kfmt(o->name, 12, "BASS");
            kfmt(o->val, 14, DEG[f->bass < 6 ? f->bass : 0]);
            break;
        }
        }
        break;
    }
    case SCR_ENV: {
        static const char *const N[4] = {"ATTACK", "DECAY", "SUSTAIN", "RELEASE"};
        int v = cf_env_get(env_part(), k);
        kfmt(o->name, 12, N[k]);
        cf_env_fmt(o->val, k, cf_env_val(env_part(), k));
        o->bar = (int16_t)(v * 255 / 99);
        break;
    }
    case SCR_EDIT:
        if (!cfe_info(cu.edit_part, cu.edit_sec, cu.page[SCR_EDIT], (uint32_t)k, o->name, o->val, &o->bar))
            o->name[0] = 0;
        break;
    case SCR_FX:
        cfx_knob_info(k, o);
        break;
    case SCR_BASS:
        switch (k) {
        case 0: kfmt(o->name, 12, "BASS"); kfmt(o->val, 14, cs.bass_on ? "On" : "Off"); break;
        case 1:
            kfmt(o->name, 12, "LEVEL");
            kint(o->val, cs.bass_level * 100 / 127, "%");
            o->bar = (int16_t)(cs.bass_level * 255 / 127);
            break;
        case 2:
            kfmt(o->name, 12, "OCTAVE");
            kint(o->val, cs.bass_oct, 0);
            if (cs.bass_oct > 0)
                o->val[0] = '+', kint(o->val + 1, cs.bass_oct, 0);
            break;
        default: kfmt(o->name, 12, "OCTAVER"); kfmt(o->val, 14, cs.octaver ? "On" : "Off"); break;
        }
        break;
    case SCR_SAVE: {
        uint32_t part = cf_save_part();
        if (k == 0) {
            kfmt(o->name, 12, "SAVE AS");
            kfmt(o->val, 14, cu.save_new ? "New" : "Replace");
            o->dis = cf_snd[part].kind == PK_INIT;     /* (INIT: only as new) */
        } else if (k == 1) {
            kfmt(o->name, 12, "LETTER");
            kint(o->val, cu.save_cur + 1, 0);
        } else if (k == 2) {
            char ch = cu.save_name[cu.save_cur];
            kfmt(o->name, 12, "CHAR");
            if (!ch || ch == ' ')
                kfmt(o->val, 14, "space");
            else
                o->val[0] = ch, o->val[1] = 0;
        } else {                                 /* ACTION: Save, or Delete (your sounds only) */
            kfmt(o->name, 12, "ACTION");
            kfmt(o->val, 14, cu.save_del ? "Delete" : "Save");
            o->dis = cf_snd[part].kind != PK_USER;
        }
        if (cu.save_del && k < 3)                /* (deleting: the name and the way do not matter) */
            o->dis = 1;
        break;
    }
    case SCR_ARP:
        if (cu.page[SCR_ARP] == 0) {             /* Pattern: ON/OFF MODE OCTAVE RATE */
            switch (k) {
            case 0: kfmt(o->name, 12, "ON/OFF"); kfmt(o->val, 14, cu.arp_on ? "On" : "Off"); break;
            case 1: kfmt(o->name, 12, "MODE"); kfmt(o->val, 14, CF_ARP_SHORT[cs.arp.mode % CF_ARP_NMODES]); break;
            case 2: kfmt(o->name, 12, "OCTAVE"); kint(o->val, cs.arp.oct, 0); break;
            default:
                kfmt(o->name, 12, "RATE");
                if (CF_ARP_STRUMMING(cs.arp.mode))   /* (Strum: the time between its notes) */
                    kint(o->val, cs.arp.strum_ms, " ms");
                else
                    kfmt(o->val, 14, CF_ARP_RATE[cs.arp.rate % CF_ARP_NRATES]);
                break;
            }
        } else {                                 /* Feel: TEMPO GATE SWING LATCH (Strum: held, no swing: dim) */
            int strum = CF_ARP_STRUMMING(cs.arp.mode);
            switch (k) {
            case 0:
                kfmt(o->name, 12, "TEMPO");
                kint(o->val, cs.bpm, 0);
                o->dis = cf_sn.ext_clock;        /* (MIDI clock in: the clock's tempo) */
                break;
            case 1:
                kfmt(o->name, 12, "GATE");
                kint(o->val, cs.arp.gate, "%");
                o->bar = (int16_t)((cs.arp.gate - 20) * 255 / 80);
                o->dis = (uint8_t)strum;
                break;
            case 2:
                kfmt(o->name, 12, "SWING");
                kint(o->val, cs.arp.swing, "%");
                o->bar = (int16_t)((cs.arp.swing - 50) * 255 / 25);
                o->dis = (uint8_t)strum;
                break;
            default: kfmt(o->name, 12, "LATCH"); kfmt(o->val, 14, cs.arp.latch ? "On" : "Off"); break;
            }
        }
        break;
    case SCR_SEQ: metro_knob_info(k, o); break;
    case SCR_SQ:
    case SCR_SQED: sq_knob_info(k, o); break;
    case SCR_GLO:
        if (cu.page[SCR_GLO] >= CG_PAGE0) {      /* the GRAIN pages */
            cg_knob_info(cu.page[SCR_GLO] - CG_PAGE0, k, o);
            break;
        }
        switch (cu.page[SCR_GLO]) {
        case 0: /* Display */
            if (k == 0) { kfmt(o->name, 12, "COLOR"); kfmt(o->val, 14, CF_THEMES[cs.accent % CF_NTHEMES].name); }
            if (k == 1) {
                static const char *const BD[FL_NBD] = {"Scene", "Ripples", "None"};
                kfmt(o->name, 12, "BACKDROP");
                kfmt(o->val, 14, BD[cs.backdrop % FL_NBD]);
            }
            if (k == 2) { kfmt(o->name, 12, "BOOT SND"); kfmt(o->val, 14, cs.boot_sound ? "On" : "Off"); }
            break;
        case 1: /* MIDI: out on or off, the clock in (the channels: the next page, four of them since the sequencer's
                 * LAYER and DRUMS) */
            if (k == 0) { kfmt(o->name, 12, "MIDI OUT"); kfmt(o->val, 14, cs.midi_out ? "On" : "Off"); }
            if (k == 1) {                        /* MIDI clock: In (its tempo, its start / stop) or Out (the unit's) */
                kfmt(o->name, 12, "CLOCK");
                kfmt(o->val, 14, cs.clock_in == 2u ? "Out" : !cs.clock_in ? "Off" : cf_sn.ext_clock ? "In: lock" : "In");
            }
            if (k == 2) {                        /* an outside keyboard's notes (0.5): the FM-1's keys, roots, Direct */
                static const char *const NI[CF_NI_N] = {"FM-1 keys", "Roots", "Direct"};
                kfmt(o->name, 12, "NOTES IN");
                kfmt(o->val, 14, NI[cs.notes_in % CF_NI_N]);
            }
            break;
        case 2: /* Channels */
            if (k == 0) { kfmt(o->name, 12, "CHORD CH"); kint(o->val, cs.ch_chord + 1, 0); }
            if (k == 1) { kfmt(o->name, 12, "BASS CH"); kint(o->val, cs.ch_bass + 1, 0); }
            if (k == 2) { kfmt(o->name, 12, "LAYER CH"); kint(o->val, cs.ch_layer + 1, 0); }
            if (k == 3) { kfmt(o->name, 12, "DRUMS CH"); kint(o->val, cs.ch_drums + 1, 0); }
            break;
        case 4: /* Reset: K4 the first step */
            if (k == 3) {
                kfmt(o->name, 12, "ACTION");
                kfmt(o->val, 14, cfr.arm ? "Reset" : "Keep");
            }
            break;
        default: /* Play */
            if (k == 0) {
                kfmt(o->name, 12, "VELOCITY");
                kint(o->val, cs.vel, 0);
                o->bar = (int16_t)(cs.vel * 255 / 127);
            }
            if (k == 1) {
                kfmt(o->name, 12, "GLIDE");
                kfmt(o->val, 14, cs.glide_always ? "Always" : "Legato");
            }
            if (k == 2) { kfmt(o->name, 12, "LIGHTS"); kfmt(o->val, 14, cs.lights ? "Scale" : "Played"); }
            if (k == 3) {
                static const char *const RT[CF_NRT] = {"Wait", "Notes", "Slide", "Chord"};   /* (stored: STRIKE, MOVE) */
                kfmt(o->name, 12, "RETRIG");
                kfmt(o->val, 14, RT[cs.retrig % CF_NRT]);
            }
            break;
        }
        break;
    default:
        break;
    }
}

#define CF_MSG_QOFF "QUANTIZE is Off: turn it on"   /* (KEY or SCALE turned while they are dimmed) */
static void ui_knob_turn(int k, int d)
{
    cu.knob_ms[k] = cu.now ? cu.now : 1u;
    cu.knob_last = (uint8_t)k;
    switch (cu.scr) {
    case SCR_HOME:
        if (k == 0) {
            cs.chord.quantize = (uint8_t)clampi(cs.chord.quantize + d, 0, 2);
            if (cs.chord.quantize != CF_Q_OFF && cu.pop == POP_MSG && !memcmp(cu.msg, CF_MSG_QOFF, sizeof CF_MSG_QOFF))
                cu.pop = POP_NONE;                /* (the hint below: done) */
        } else if (k == 1 && cs.chord.quantize != CF_Q_OFF)
            cs.chord.tonic = (uint8_t)wrapi(cs.chord.tonic + d, 12);
        else if (k == 2 && cs.chord.quantize != CF_Q_OFF)
            cs.chord.scale = (uint8_t)clampi(cs.chord.scale + d, 0, CF_NSCALES - 1);
        else if (k == 1 || k == 2)                /* dimmed (QUANTIZE Off): it says what wakes it, every turn (the
                                                  * owner, 2026-10-20, after a reboot: "key и scale заблокировались") */
            cf_message(CF_MSG_QOFF);
        else if (k == 3) {
            /* GLIDE: the chord sound's (0.5, the owner: "Если переключается инструмент, глайд переключается на тот
             * что был у другого инструмента"): an edit of the sound, kept as the others are (a pattern's own sound:
             * in the pattern; SAVE) */
            /* steps of about a quarter more each: Off, 6, 13, 23, 33, 44, 57, 71, 87, 105, 131, 160, 192, 236, 285,
             * 349, 420, 510, 625, 768, 944, 1158, 1463 ms (voice.c's TIME: 1 + g^2 / 8 control blocks) */
            static const uint8_t GS[] = {0, 8, 12, 16, 19, 22, 25, 28, 31, 34, 38, 42, 46, 51, 56, 62, 68, 75, 83, 92,
                                         102, 113, 127};
            int g = trk[CF_PART_CHORD].p[P_GLIDE], i = 0;
            while (i + 1 < (int)NELEM(GS) && GS[i + 1] <= g)
                i++;
            if (d < 0 && GS[i] < g)
                i++;                             /* (a value between two steps: down goes to the lower) */
            g = GS[clampi(i + d, 0, (int)NELEM(GS) - 1)];
            fm1_irq_off();
            trk[CF_PART_CHORD].p[P_GLIDE] = (int16_t)g;
            fm1_irq_on();
            cf_edited(CF_PART_CHORD);
        }
        cu_spell_update();
        cf_send_config();
        break;
    case SCR_SEL: {
        cf_form_t *f = sel_form();
        if (cf_is_off(&cs.chord, cu.sel_btn))
            break;
        if (k == 0)
            f->type = (uint8_t)clampi(f->type + d, 0, CF_NTYPES - 1);
        else if (k == 1) {
            int i = cf_adds_index(f->add);
            f->add = CF_ADDS[clampi((i < 0 ? 0 : i) + d, 0, CF_NADDS - 1)];
        } else if (k == 2)
            f->inv = (int8_t)clampi(f->inv + d, CF_INV_AUTO, 3);
        else
            f->bass = (uint8_t)clampi(f->bass + d, 0, 5);
        cf_send_config();
        break;
    }
    case SCR_ENV:
        if (cu.env_tgt == 3u) {                  /* the LAYER's own */
            cf_env_step(CF_PART_LAYER, k, d);
            cf_edited(2u);
            break;
        }
        if (cu.env_tgt != 2u)
            cf_env_step(CF_PART_CHORD, k, d);
        if (cu.env_tgt == 2u)
            cf_env_step(CF_PART_BASS, k, d);
        else if (cu.env_tgt == 0u)                   /* linked: the bass takes the chord's value */
            cf_env_set_val(CF_PART_BASS, k, cf_env_val(CF_PART_CHORD, k));
        cf_edited(cu.env_tgt == 2u ? 1u : 0u);
        if (!cu.env_tgt)
            cf_edited(1u);
        break;
    case SCR_EDIT:
        cfe_turn(cu.edit_part, cu.edit_sec, cu.page[SCR_EDIT], (uint32_t)k, d);
        cf_edited(cu.edit_part);
        break;
    case SCR_FX:
        cfx_knob_turn(k, d);
        break;
    case SCR_BASS:
        if (k == 0)
            cs.bass_on = (uint8_t)(d > 0);
        else if (k == 1)
            cs.bass_level = (uint8_t)clampi(cs.bass_level + d * 2, 0, 127);
        else if (k == 2)
            cs.bass_oct = (int8_t)clampi(cs.bass_oct + d, -2, 2);
        else
            cs.octaver = (uint8_t)(d > 0);
        cf_send_config();
        break;
    case SCR_SAVE: {
        uint32_t part = cf_save_part(), n;
        if (k == 0 && cf_snd[part].kind != PK_INIT) {
            cu.save_new = (uint8_t)(d > 0);
            cf_save_name_init();
        } else if (k == 1) {
            cu.save_cur = (uint8_t)clampi(cu.save_cur + d, 0, 11);
            for (n = str_len(cu.save_name); n <= cu.save_cur; n++)   /* (past the end: spaces up to it) */
                cu.save_name[n] = ' ';
            cu.save_name[n > str_len(cu.save_name) ? n : str_len(cu.save_name)] = 0;
        } else if (k == 3) {
            cu.save_del = (uint8_t)(d > 0 && cf_snd[part].kind == PK_USER);
        } else if (k == 2) {
            const char *p;
            int i, nc = (int)sizeof CF_NAME_CHARS - 1;
            char ch = cu.save_name[cu.save_cur] ? cu.save_name[cu.save_cur] : ' ';
            for (p = CF_NAME_CHARS; *p && *p != ch; p++)
                ;
            i = *p ? (int)(p - CF_NAME_CHARS) : 0;
            cu.save_name[cu.save_cur] = CF_NAME_CHARS[wrapi(i + d, nc)];
            if (cu.save_cur >= str_len(cu.save_name))
                cu.save_name[cu.save_cur + 1u] = 0;
        }
        break;
    }
    case SCR_ARP:
        if (cu.page[SCR_ARP] == 0) {
            if (k == 0)
                cu.arp_on = (uint8_t)(d > 0);
            else if (k == 1)
                cs.arp.mode = (uint8_t)clampi(cs.arp.mode + d, 0, CF_ARP_NMODES - 1);
            else if (k == 2)
                cs.arp.oct = (uint8_t)clampi(cs.arp.oct + d, 1, 4);
            else if (CF_ARP_STRUMMING(cs.arp.mode))
                cs.arp.strum_ms = (uint8_t)clampi(cs.arp.strum_ms + d * (cs.arp.strum_ms < 40 ? 1 : 5), 5, 250);
            else
                cs.arp.rate = (uint8_t)clampi(cs.arp.rate + d, 0, (int)CF_ARP_NRATES - 1);
        } else {
            if (k == 0 && !cf_sn.ext_clock) {
                cs.bpm = (uint16_t)clampi(cs.bpm + d, 30, 300);
                cf_send_metro();
            } else if (k == 1) {
                cs.arp.gate = (uint8_t)clampi(cs.arp.gate + d * 5, 20, 100);
            } else if (k == 2) {
                cs.arp.swing = (uint8_t)clampi(cs.arp.swing + d, 50, 75);
            } else if (k == 3) {
                cs.arp.latch = (uint8_t)(d > 0);
            }
        }
        cf_send_config();
        break;
    case SCR_SEQ: metro_knob_turn(k, d); break;
    case SCR_SQ:
    case SCR_SQED: sq_knob_turn(k, d); break;
    case SCR_GLO:
        if (cu.page[SCR_GLO] >= CG_PAGE0) {      /* the GRAIN pages */
            cg_knob_turn(cu.page[SCR_GLO] - CG_PAGE0, k, d);
            break;
        }
        switch (cu.page[SCR_GLO]) {
        case 0:
            if (k == 0)
                cs.accent = (uint8_t)wrapi(cs.accent + d, CF_NTHEMES);
            if (k == 1)
                cs.backdrop = (uint8_t)wrapi(cs.backdrop + d, FL_NBD);
            if (k == 2) {
                cs.boot_sound = (uint8_t)(d > 0);
                if (cs.boot_sound)
                    cf_boot_go = 1;              /* (heard as it is set) */
            }
            cf_palette_build();
            break;
        case 1:
            if (k == 0)
                cs.midi_out = (uint8_t)(d > 0);
            if (k == 1)                          /* Off, In, Out */
                cs.clock_in = (uint8_t)clampi(cs.clock_in + (d > 0 ? 1 : -1), 0, 2);
            if (k == 2)                          /* FM-1 keys, Roots, Direct */
                cs.notes_in = (uint8_t)clampi(cs.notes_in + (d > 0 ? 1 : -1), 0, CF_NI_N - 1);
            cf_send_config();
            break;
        case 2:
            if (k == 0)
                cs.ch_chord = (uint8_t)clampi(cs.ch_chord + d, 0, 15);
            if (k == 1)
                cs.ch_bass = (uint8_t)clampi(cs.ch_bass + d, 0, 15);
            if (k == 2)
                cs.ch_layer = (uint8_t)clampi(cs.ch_layer + d, 0, 15);
            if (k == 3)
                cs.ch_drums = (uint8_t)clampi(cs.ch_drums + d, 0, 15);
            cf_send_config();
            break;
        case 4:                                  /* Reset: K4 arms it (the steps after: SAVE) */
            if (k == 3 && !cfr.go) {
                cfr.arm = (uint8_t)(d > 0);
                cfr.fin = 0;
                cfr.hold = 0;
            }
            break;
        default:
            if (k == 0)
                cs.vel = (uint8_t)clampi(cs.vel + d * 4, 1, 127);
            if (k == 1)
                cs.glide_always = (uint8_t)(d > 0);
            if (k == 2)
                cs.lights = (uint8_t)(d > 0);
            if (k == 3)
                {                        /* Wait, Move, Strike: from holding the chord to striking it */
                    static const uint8_t ORD[CF_NRT] = {CF_RT_WAIT, CF_RT_MOVE, CF_RT_STRIKE, CF_RT_CHORD};
                    int i = 0;
                    while (i < CF_NRT - 1 && ORD[i] != cs.retrig)
                        i++;
                    cs.retrig = ORD[clampi(i + (d > 0 ? 1 : -1), 0, CF_NRT - 1)];
                }
            cf_send_config();
            break;
        }
        break;
    default:
        break;
    }
}

/* --------------------------------------------------------- the left side --- */
static void cf_pop(uint32_t kind, uint32_t ms)
{
    cu.pop = (uint8_t)kind;
    cu.pop_until = cu.now + ms;
}
static void cf_message(const char *s)
{
    uint32_t i;
    str_cpy(cu.msg, s, sizeof cu.msg);
    for (i = 0; cu.msg[i] && cu.msg[i] != '\n'; i++)
        ;
    cf_pop(POP_MSG, cu.msg[i] ? 2000u : 1200u);  /* (two lines: longer to read; 2 s, as the first take's hint) */
}
/* what the kept files (upreset.c, fm6_store.c) say goes into the message popup */
static void ui_message(const char *s) { cf_message(s); }
static void ui_say(const char *a, const char *b)
{
    char m[24];
    str_cpy(m, a, sizeof m);
    str_cpy(m + str_len(m), b, (uint32_t)(sizeof m - str_len(m)));
    cf_message(m);
}

static uint32_t screen_pages(uint32_t scr)
{
    if (scr == SCR_GLO)
        return 5u + CG_NPAGE;                    /* Display, MIDI, Channels, Play, Reset, Grain, Pitch, Hold */
    if (scr == SCR_ENV)
        return 3u;                               /* chord + bass, chord, bass */
    if (scr == SCR_ARP)
        return 2u;                               /* Pattern, Feel */
    if (scr == SCR_SAVE)
        return CF_SAVE_NPG;                      /* the chord's sound, the bass's, the LAYER's (a pattern's copy: OCT+
                                                  * held on the SEQUENCER, cf_sequi.c) */
    if (scr == SCR_EDIT)
        return cfe_npages(cu.edit_part, cu.edit_sec);
    return 1u;
}

/* a page turned (SELECT): which way, when; the picture slides in from that side, that arrow lights (cf_views.c) */
static uint32_t cv_turn_ms;
static int8_t cv_turn_dir;
static void cv_turned(int d)
{
    cv_turn_ms = cu.now ? cu.now : 1u;
    cv_turn_dir = (int8_t)(d > 0 ? 1 : -1);
}

static void ui_select(int d)                     /* SELECT: the bass sound, or a screen's page */
{
    if (cu.scr == SCR_SQ) {                      /* SEQUENCER: the track, the song's place */
        sq_pick(d);
        return;
    }
    if (cu.scr == SCR_SQED) {                    /* a track: its step (DRUMS: its bar) */
        sq_carry_turn(d);
        return;
    }
    if (cu.scr == SCR_ENV) {                     /* chord + bass (linked: the bass follows), chord, bass, the LAYER */
        cu.env_tgt = (uint8_t)wrapi(cu.env_tgt + d, 4);
        cv_turned(d);
        if (cu.env_tgt < 3u) {                   /* (the LAYER's page leaves the chord and bass linked or not) */
            cs.env_link = (uint8_t)(cu.env_tgt == 0u);
            if (cs.env_link)
                cf_env_follow(), cf_edited(1u);
        }
        cf_send_config();                        /* (the keys play the LAYER on its page) */
        return;
    }
    if (cu.scr == SCR_EDIT) {
        int was = cu.page[SCR_EDIT];
        cu.page[SCR_EDIT] = (uint8_t)clampi(cu.page[SCR_EDIT] + d, 0, (int)screen_pages(SCR_EDIT) - 1);
        if (cu.page[SCR_EDIT] != was)
            cv_turned(d);
        return;
    }
    if (cu.scr == SCR_FX) {                      /* FX: the effect along the chain */
        cu.fx_pos = (uint8_t)clampi(cu.fx_pos + d, 0, CFX_N - 1);
        return;
    }
    if (screen_pages(cu.scr) > 1u) {
        cu.page[cu.scr] = (uint8_t)wrapi(cu.page[cu.scr] + d, (int)screen_pages(cu.scr));
        cv_turned(d);
        if (cu.scr == SCR_SAVE)
            cf_send_config();                    /* (the LAYER's page: the keys play its sound) */
        if (cu.scr == SCR_SAVE) {                /* the other part: its sound, its name */
            uint32_t part = cf_save_part();
            cu.save_del = 0;
            cu.save_new = cf_snd[part].kind == PK_INIT || !(cu.edited[part] || cf_snd[part].kind == PK_USER);
            cf_save_name_init();
        }
        return;
    }
    if (cu.scr == SCR_SEL) {                     /* SEL: SELECT picks the button to edit, as the keys do */
        int nb = (cs.chord.algo == CF_ALG_TOP || cs.chord.algo == CF_ALG_TOP_LOCK) ? CF_TOP_NBTN : CF_SIDE_NBTN;
        cu.sel_btn = (uint8_t)wrapi(cu.sel_btn + d, nb);
        return;
    }
    cf_snd_step(CF_PART_BASS, d);
    if (cs.env_link)
        cf_env_follow();
    ui_snd_heard(CF_PART_BASS);
    cf_pop(POP_SOUND_BASS, 1200);
}
static void ui_presets(int d)                    /* PRESETS: the chord sound (a sequencer's track open: its sound) */
{
    if (cu.scr == SCR_SQED || (cu.scr == SCR_SQ && squ.pat < SQ_NPAT)) {
        sq_presets(d);                           /* (the pattern's own sound for the frame's track: 0.5) */
        return;
    }
    cf_snd_step(CF_PART_CHORD, d);
    if (cs.env_link)                             /* linked: the bass takes the new sound's envelope */
        cf_env_follow();
    ui_snd_heard(CF_PART_CHORD);
    cf_pop(POP_SOUND_CHORD, 1200);
}
static void ui_algo(int d)                       /* ALGORITHM: the chord algorithm (FX: moves the effect) */
{
    if (cu.scr == SCR_FX) {
        cfx_move(d);
        return;
    }
    if (cu.scr == SCR_SQ) {                      /* SEQUENCER: the pattern, then the song */
        sq_algo(d);
        return;
    }
    {   /* ALGORITHM turns through the Lock variants first (the owner's order): Top Lock, Top, Side Lock, Side. The
         * stored ids stay CF_ALG_*: only the knob's order is this one. */
        static const uint8_t ORDER[CF_NALGS] = {CF_ALG_TOP_LOCK, CF_ALG_TOP, CF_ALG_SIDE_LOCK, CF_ALG_SIDE};
        int at = 0;
        while (at < CF_NALGS - 1 && ORDER[at] != cs.chord.algo)
            at++;
        cs.chord.algo = ORDER[clampi(at + d, 0, CF_NALGS - 1)];
    }
    cu.sel_btn = cs.chord.algo >= CF_ALG_SIDE ? 0u : sel_first();
    cf_send_config();
    cf_pop(POP_ALGO, 1400);
}

/* --------------------------------------------------------------- buttons --- */
/* the sound of the sequencer's track left last (the chord's, the bass's, the LAYER's; 0xFF: none yet): EDIT, ENV and
 * SAVE open on it (the owner, 2026-10-19: what was edited before HOME is what the editors take) */
static uint8_t cu_ctx = 0xFF, cu_env_prev;
static void ui_ctx_from_track(uint32_t trk)
{
    static const uint8_t PART[SQ_NTRK] = {CF_PART_CHORD, CF_PART_BASS, CF_PART_LAYER, 0xFF};
    uint8_t p = PART[trk % SQ_NTRK];
    if (p == 0xFFu)
        return;                                  /* (DRUMS: the editors stay as they were) */
    cu_ctx = p;
    cu.edit_part = p;
    cu.page[SCR_EDIT] = 0;
    if (p == CF_PART_LAYER && cu.env_tgt != 3u) {   /* (ENV: the LAYER's page; back to where it was after) */
        cu_env_prev = cu.env_tgt;
        cu.env_tgt = 3;
    } else if (p != CF_PART_LAYER && cu.env_tgt == 3u) {
        cu.env_tgt = cu_env_prev < 3u ? cu_env_prev : (cs.env_link ? 0u : 1u);
    }
}

static void ui_open(uint32_t scr)
{
    uint32_t was = cu.scr;
    if (sq_screen(was) && !sq_screen(scr) && scr != SCR_SEQ)
        ui_ctx_from_track(squ.trk);
    cu.scr = (uint8_t)(scr == was && scr != SCR_HOME ? SCR_HOME : scr);
    if (cu.scr == SCR_SEL && cf_is_off(&cs.chord, cu.sel_btn))
        cu.sel_btn = sel_first();
    if ((was == SCR_SEQ || sq_screen(was)) != (cu.scr == SCR_SEQ || sq_screen(cu.scr)))
        cf_send_metro();
    if (sq_screen(was) || sq_screen(cu.scr) || was == SCR_EDIT || cu.scr == SCR_EDIT || was == SCR_ENV ||
        cu.scr == SCR_ENV || was == SCR_SAVE || cu.scr == SCR_SAVE)   /* (the LAYER open or left: where the keys play,
                                                  * cf_send_config) */
        cf_send_config();
    squ.help = squ.help_hold = 0;
    if (cu.scr != was) {                         /* a hold begun on one screen does nothing on the next: OCT+ held in a
                                                  * track and let go on the SEQUENCER copied a pattern, SEL held cleared
                                                  * a track there at once, REC let go on HOME started a take (a review,
                                                  * 2026-10-19) */
        squ.oct_down = squ.sel_down = 0;
        squ.ocpy = squ.cpy = 0;
        cu.rec_hold = 0;
    }
}

/* GLOBAL > Reset: everything the user made goes (cf_backup.c cf_factory_erase), after three steps, each its own
 * gesture (the owner, 2026-10-19: "полный резет, с подтверждениями несколькими"): K4 to Reset; SAVE held 3 s; SAVE
 * once more within 6 s. Leaving the page, letting SAVE go early or the 6 s passing undoes them */
static int cf_reset_page(void) { return cu.scr == SCR_GLO && cu.page[SCR_GLO] == 4u; }
static void cf_reset_poll(void)                  /* every frame */
{
    uint32_t now = cu.now;
    if (cfr.go) {                                /* (a frame or two of "Resetting" on the screen first) */
        if (now - cfr.go_ms > 250u && cfr.go == 1u) {
            cfr.go = 2;
            cf_factory_erase();                  /* (the unit restarts; the emulator says what to do) */
            cf_message("Reset done: restart");
        }
        return;
    }
    if (!cf_reset_page()) {
        cfr.arm = cfr.fin = 0;
        cfr.hold = 0;
        return;
    }
    if (cfr.hold) {
        if (!((fm1_in.buttons >> panel.btn[B_SAVE]) & 1u)) {
            cfr.hold = 0;                        /* (let go early: nothing) */
            cf_message("Hold SAVE 3 seconds");
        } else if (now - cfr.hold >= CF_RESET_HOLD_MS) {
            cfr.hold = 0;
            cfr.fin = 1;
            cfr.fin_ms = now;
        }
    }
    if (cfr.fin && now - cfr.fin_ms > CF_RESET_ASK_MS) {
        cfr.fin = cfr.arm = 0;
        cf_message("Reset cancelled");
    }
}
/* SAVE on the Reset page: the next step, or how to begin */
static void cf_reset_save(void)
{
    if (cfr.go)
        return;
    if (cfr.fin) {
        cfr.fin = 0;
        cfr.go = 1;
        cfr.go_ms = cu.now;
        cf_post(CFE_SEQ, 0, 0, 0);               /* (the sequencer stops) */
    } else if (cfr.arm) {
        cfr.hold = cu.now ? cu.now : 1u;
    } else {
        cf_message("Turn ACTION to Reset");
    }
}

static void ui_button(uint32_t b)
{
    if (cfr.go)                                  /* (erasing: no button any more) */
        return;
    if (cf_reset_page() && b == B_SAVE) {
        cf_reset_save();
        return;
    }
    if (cfr.fin && b != B_SAVE) {                /* (the last question: any other button says no) */
        cfr.fin = cfr.arm = 0;
        cf_message("Reset cancelled");
        return;
    }
    if (sq_screen(cu.scr)) {                     /* the sequencer's own: EDIT / SEL open, SEL clears, SAVE back, GLO */
        if (cu.scr == SCR_SQED && b == B_ARP) {  /* ARP in a track: its own arpeggiator on / off (CHORDS, LAYER) */
            sq_arp_toggle();
            return;
        }
        if (cu.scr == SCR_SQED && b == B_EDIT) { /* EDIT held in a track: SELECT moves the frame's chord */
            sq_edit_press();
            return;
        }
        if (b == B_FX || b == B_ENV || b == B_LFO || b == B_ARP ||
            (cu.scr == SCR_SQ && b == B_SAVE))   /* (SAVE: nothing on the SEQUENCER; a pattern's copy is OCT+'s) */
            return;                              /* (no way out to another screen here but HOME and SEQ: the owner,
                                                  * 2026-10-19; their lights are off) */
        if (cu.scr == SCR_SQ && b == B_EDIT) {
            if (sq_open()) {
                ui_open(SCR_SQED);
                squ.nofr = 0;                    /* (opened: the frame where it was) */
                sq_hint_once(SQH_TRACK);         /* (the first track opened: the way back) */
            }
            return;
        }
        if (cu.scr == SCR_SQ && b == B_SCL) {      /* SEL takes one thing out of the frame, never many: on the song its
                                                  * pattern (the pattern stays); a track: nothing at a tap, held 1 s it
                                                  * is cleared (sq_sel_poll) */
            if (squ.pat == SQ_SONGPG) {
                sq_song_remove();
            } else {
                squ.sel_down = cu.now ? cu.now : 1u;
                squ.cpy = 0;
                sqh.busy = 0;                    /* (an action of its own: one undo) */
                sq_undo_poll();
            }
            return;
        }
        if (cu.scr == SCR_SQED && b == B_SCL) {   /* (a tap clears on its release; held, SELECT copies the bar:
                                                  * sq_sel_poll) */
            squ.sel_down = cu.now ? cu.now : 1u;
            squ.cpy = 0;
            sqh.busy = 0;                        /* (SEL begins an action of its own: undo takes back the erase
                                                  * alone, not what was written just before) */
            sq_undo_poll();
            return;
        }
        if (cu.scr == SCR_SQED && b == B_SAVE) {
            ui_open(SCR_SQ);
            return;
        }
        if (b == B_GLO) {
            squ.glo_down = cu.now ? cu.now : 1u;
            return;
        }
        if (b == B_PLAY) {                       /* the pattern shown round, or the song from the place picked */
            sq_play();
            return;
        }
        if (b == B_REC) {                        /* a tap: a take; held 1 s: undo (cf_loop_poll) */
            cu.rec_hold = cu.now ? cu.now : 1u;
            cu.rec_fired = 0;
            return;
        }
    }
    switch (b) {
    case B_HOME: ui_open(SCR_HOME); break;
    case B_SCL: ui_open(SCR_SEL); break;
    case B_ENV: ui_open(SCR_ENV); break;
    case B_LFO: ui_open(SCR_BASS); break;
    case B_EDIT: ui_open(SCR_EDIT); break;
    case B_GLO: ui_open(SCR_GLO); break;
    case B_FX: ui_open(SCR_FX); break;
    case B_ARP: ui_open(SCR_ARP); break;         /* (its ON/OFF is K1 there, as an effect's: the owner's) */
    case B_SEQ:                                  /* METRONOME, again the SEQUENCER, again HOME */
        if (cu.scr == SCR_SEQ)
            ui_open(SCR_SQ);
        else
            ui_open(sq_screen(cu.scr) ? SCR_HOME : SCR_SEQ);
        break;
    case B_SAVE:                                 /* on SAVE: held 1 s saves, a tap goes HOME (cf_save_poll) */
        if (cu.scr == SCR_SAVE) {
            cu.save_hold = cu.now ? cu.now : 1u;
        } else {
            cf_save_open();
        }
        break;
    case B_PLAY:                                 /* the last pattern plays / stops (cf_sequi.c sq_home_play) */
        sq_home_play();
        break;
    case B_REC:                                  /* a tap records a new pattern, a hold undoes (cf_rec_poll) */
        cu.rec_hold = cu.now ? cu.now : 1u;
        cu.rec_fired = 0;
        break;
    case B_OCTDN:
    case B_OCTUP:
        if (b == B_OCTUP && (cu.scr == SCR_SQED || (cu.scr == SCR_SQ && squ.pat < SQ_NPAT)) &&
            !((fm1_in.buttons >> panel.btn[B_OCTDN]) & 1u)) {   /* held with SELECT turned (a track) it copies the
                                                  * frame's bar, with ALGORITHM turned (SEQUENCER) the pattern; a tap is
                                                  * OCT+ on its release (sq_oct_poll) */
            squ.oct_down = cu.now ? cu.now : 1u;
            squ.ocpy = 0;
            break;
        }
        if ((fm1_in.buttons & ((1u << panel.btn[B_OCTDN]) | (1u << panel.btn[B_OCTUP]))) ==
            ((1u << panel.btn[B_OCTDN]) | (1u << panel.btn[B_OCTUP]))) {
            cs.chord.octave = 0;                 /* both: back to 0, and every note let go */
            squ.oct_down = 0;                    /* (the OCT+ waiting for its release on the sequencer's screens: not
                                                  * after this; it left the octave at +1) */
            squ.ocpy = 0;
            cf_post(CFE_PANIC, 0, 0, 0);
        } else {
            cs.chord.octave = (int8_t)clampi(cs.chord.octave + (b == B_OCTUP ? 1 : -1), -3, 3);
        }
        cf_send_config();
        cf_pop(POP_OCTAVE, 900);
        break;
    default: break;
    }
}

/* ---------------------------------------------------------------- SAVE --- */
/* SAVE keeps a part's sound (cf_sound.c cf_snd_save): SELECT the part (the one being edited, else the chord, opens),
 * K1 over itself or as new (a sound not edited: as new), K2 the letter, K3 what it is; SAVE held 1 s saves */
static void cf_save_name_init(void)
{
    uint32_t part = cf_save_part(), n;
    str_cpy(cu.save_name, cf_snd[part].name, sizeof cu.save_name);
    n = str_len(cu.save_name);
    if (cu.save_new && n + 2u <= 12u)            /* a new one: its name with a 2, to tell them apart */
        str_cpy(cu.save_name + n, " 2", 3);
    cu.save_cur = (uint8_t)(str_len(cu.save_name) ? str_len(cu.save_name) - 1u : 0u);
}
static void cf_save_open(void)
{
    uint32_t part = cu.scr == SCR_EDIT ? cu.edit_part : cu.scr == SCR_ENV ? env_part() : cu_ctx < 3u ? cu_ctx :
                    (!cu.edited[0] && cu.edited[1]) ? 1u : (!cu.edited[0] && cu.edited[2]) ? 2u : 0u;
    cu.page[SCR_SAVE] = (uint8_t)part;
    cu.save_new = cf_snd[part].kind == PK_INIT || !(cu.edited[part] || cf_snd[part].kind == PK_USER);
    cu.save_hold = 0;
    cu.save_del = 0;
    cf_save_name_init();
    ui_open(SCR_SAVE);
}
/* a user sound replaced (m: it now) or deleted (m 0): every pattern that keeps it as its own takes it as it is now,
 * or goes back to HOME's sound (its tweak on it with it), so no pattern keeps a sound that is gone (its letter on HOME,
 * PRESETS turning its sound) */
static void ui_snd_slot_changed(uint32_t slot, const cf_snd_t *m)
{
    uint32_t p, t;
    for (p = 0; p < SQ_NPAT; p++)
        for (t = 0; t < 3u; t++) {
            const cf_sref_t *r = &cfps.r[p][t];
            if (!r->own || r->kind != PK_USER || r->slot != slot)
                continue;
            if (m) {
                cfps_set(p, t, m);
            } else {
                cfps_set(p, t, 0);
                cfk_drop(p + 1u, t);
            }
        }
}
static void cf_save_delete(void)
{
    uint32_t part = cf_save_part();
    char b[24], nm[13];
    int res;
    cf_sref_t home[3];
    uint32_t cur = sq_cur_pat(), slot = cf_snd[part].slot;
    str_cpy(nm, cf_snd[part].name, sizeof nm);
    sq_home_keep(home);
    res = cf_snd_delete(part);
    if (res) {
        cf_message(res == -1 ? "Only your sounds delete" : res == -3 ? "Safe mode: no saving" : "Delete error");
        return;
    }
    sq_home_put(home);                           /* (the row moved: HOME's sounds follow, as what they are) */
    cfk_drop(cur < SQ_NPAT && cfps.r[cur][part].own ? cur + 1u : 0u, part);
    ui_snd_slot_changed(slot, 0);                /* (only a user sound deletes: its slot) */
    cu.edited[part] = 0;
    cu.save_del = 0;
    str_cpy(b, "Deleted ", sizeof b);
    str_cpy(b + str_len(b), nm, 13);
    cf_message(b);
    ui_open(SCR_HOME);
}
static void cf_save_do(void)
{
    uint32_t part = cf_save_part(), i, n;
    char name[13], b[24], num[4];
    int res;
    str_cpy(name, cu.save_name, sizeof name);
    for (n = str_len(name); n && name[n - 1u] == ' '; n--)   /* (no trailing spaces) */
        name[n - 1u] = 0;
    for (i = 0; name[i] == ' '; i++)
        ;
    {
        cf_sref_t home[3];
        uint32_t cur = sq_cur_pat();
        int own = cur < SQ_NPAT && cfps.r[cur][part].own;
        sq_home_keep(home);
        res = cf_snd_save(part, cu.save_new, name + i);
        if (res) {
            cf_message(res == -1 ? "No free slot (32 used)" : res == -2 ? "INIT: save it as new" :
                       res == -3 ? "Safe mode: no saving" : "Save error");
            return;
        }
        sq_home_put(home);                       /* (the row moved: HOME's sounds follow, as what they are) */
        if (!cu.save_new && cf_snd[part].kind == PK_USER)
            ui_snd_slot_changed(cf_snd[part].slot, &cf_snd[part]);   /* (replaced, maybe renamed: who keeps it) */
        if (own) {                               /* (the pattern's own sound is the saved one now, its tweak in it) */
            cfps_set(cur, part, &cf_snd[part]);
            cfk_drop(cur + 1u, part);
            cfk_take(part, cur + 1u);
        } else {
            if (part == CF_PART_LAYER)
                cs.snd_layer = (uint16_t)cf_snd_pos(part);
            else
                cs.snd[part] = (uint16_t)cf_snd_pos(part);
            cfk_drop(0, part);
            cfk_take(part, 0);
        }
    }
    cu.edited[part] = 0;
    snd_num(cf_snd_pos(part), num);
    str_cpy(b, "Saved ", sizeof b);
    str_cpy(b + str_len(b), num, 4);
    str_cpy(b + str_len(b), " ", 2);
    str_cpy(b + str_len(b), cf_snd[part].name, 13);
    cf_message(b);
    ui_open(SCR_HOME);
}
/* every frame: PLAY and REC, a tap (on its release) or a hold. PLAY held 0.6 s: the LOOPER screen; REC tapped: the
 * loop's action and the LOOPER screen; REC held 1 s: the last layer undone (as ChoralRoot's) */
static void cf_rec_poll(void)
{
    if (cf_sn.ext_clock && cf_sn.ext_bpm >= 30u && cf_sn.ext_bpm <= 300u && cf_sn.ext_bpm != cs.bpm) {
        cs.bpm = cf_sn.ext_bpm;                  /* (MIDI clock in: the tempo it makes, on the knobs; the audio side
                                                  * has it already) */
        fm1_irq_off();
        cfp_cfg.bpm = cs.bpm;
        fm1_irq_on();
    }
    if (cu.rec_hold && sq_screen(cu.scr)) {      /* the sequencer: a tap records, held 1 s undoes (as on HOME) */
        int held = (fm1_in.buttons >> panel.btn[B_REC]) & 1u;
        if (held && !cu.rec_fired && cu.now - cu.rec_hold >= 1000u) {
            cu.rec_fired = 1;
            sq_home_undo();                      /* (as on HOME: a take under way dropped, else the last change; an
                                                  * undone take shows the pattern before it) */
            if (cu.scr == SCR_SQED && sq_empty(sq_cur()))
                ui_open(SCR_SQ);                 /* (the pattern back to nothing: its tracks) */
            else if (cu.scr == SCR_SQED)
                sq_clamp();
        } else if (!held) {
            if (!cu.rec_fired)
                sq_rec_tap();
            cu.rec_hold = 0;
        }
        return;
    }
    if (cu.rec_hold) {                           /* away from the sequencer: a tap a new pattern, held 1 s undo */
        int held = (fm1_in.buttons >> panel.btn[B_REC]) & 1u;
        if (held && !cu.rec_fired && cu.now - cu.rec_hold >= 1000u) {
            cu.rec_fired = 1;
            sq_home_undo();
        } else if (!held) {
            if (!cu.rec_fired)
                sq_home_rec();
            cu.rec_hold = 0;
        }
    }
}

/* every frame: SAVE held on the SAVE screen */
static void cf_save_poll(void)
{
    int held;
    if (!cu.save_hold)
        return;
    held = (fm1_in.buttons >> panel.btn[B_SAVE]) & 1u;
    if (cu.scr != SCR_SAVE) {
        cu.save_hold = 0;
    } else if (held && cu.now - cu.save_hold >= 1000u) {
        cu.save_hold = 0;
        if (cu.save_del)
            cf_save_delete();
        else
            cf_save_do();
    } else if (!held) {                          /* a tap: back HOME, as every screen's own button */
        cu.save_hold = 0;
        ui_open(SCR_HOME);
    }
}

/* ------------------------------------------------------------- the keys --- */
/* on SEL a chord button pressed is also picked for editing (it still plays) */
static void ui_note_key_aged(int key, int down, int age_ms);
/* HOME's PRESETS / SELECT changed the sound you hear (0.5): the current pattern's own, if it has one, else HOME's */
static void ui_snd_heard(uint32_t part)
{
    uint32_t cur = sq_cur_pat();
    if (cur < SQ_NPAT && cfps.r[cur][part].own) {
        cfps_set(cur, part, &cf_snd[part]);
        cfk_drop(cur + 1u, part);                /* (another sound: its tweak goes) */
        cfk_take(part, cur + 1u);
        return;
    }
    if (part == CF_PART_CHORD)
        cs.snd[0] = (uint16_t)cf_snd_pos(CF_PART_CHORD);
    else if (part == CF_PART_BASS)
        cs.snd[1] = (uint16_t)cf_snd_pos(CF_PART_BASS);
    cfk_drop(0, part);
    cfk_take(part, 0);
}
static void ui_note_key(int key, int down) { ui_note_key_aged(key, down, -1); }
static void ui_note_key_aged(int key, int down, int age_ms)   /* age_ms: -1 the scan's (CF_KEY_T0) */
{
    int idx;
    if (sq_key(key, down))                       /* (DRUMS: the keys pick the drum and set the steps) */
        return;
    if (cu.scr == SCR_EDIT && cf_key_is_upper(key)) {   /* EDIT: OP1..OP6 PIT GLO pick, MONO / POLY: bass / chords */
        int u = 0, k;
        for (k = 0; k < key; k++)
            u += cf_key_is_upper(k);
        if (down) {
            if (u < CFE_NSEC) {
                cu.edit_sec = (uint8_t)u;
                cu.page[SCR_EDIT] = (uint8_t)clampi(cu.page[SCR_EDIT], 0, (int)cfe_npages(cu.edit_part, (uint32_t)u) - 1);
            } else if (u >= 8 && u <= 10) {      /* MONO the bass's sound, POLY the chord's (the keys' print: the
                                                  * owner, 2026-10-19), the next the LAYER's */
                cu.edit_part = (uint8_t)edit_key_part(u);
                cu.page[SCR_EDIT] = 0;
                cf_send_config();                /* (the keys play the sound edited: the LAYER's too) */
            }
        }
        return;
    }
    if (down && cu.scr == SCR_SEL && cf_key_role(cs.chord.algo, key, &idx) == CF_ROLE_BUTTON &&
        !cf_is_off(&cs.chord, idx))
        cu.sel_btn = (uint8_t)idx;
    {                                            /* (a key down goes with its age: how long ago the scan saw it closed,
                                                  * hal/fm1_input.h note_t0. A frame may draw for tens of ms before the
                                                  * keys are read: a take puts the chord where it was played, not where
                                                  * the frame let it through. The owner, 2026-10-20: a take from HOME
                                                  * "квантизируется не супер по таймингу") */
        uint32_t age = !down ? 0u : age_ms >= 0 ? (uint32_t)age_ms : (fm1_ticks() - CF_KEY_T0(key % 27)) / FM1_TICKS_PER_US / 1000u;
        cf_post(CFE_KEY, (uint32_t)key, age <= 250u ? age : 0u, down);   /* (longer: stale, none) */
    }
}

/* the keys, between the frame's tile rows too. Keys that changed in one scan went at once: the ones let go first,
 * then the chord buttons pressed, then the roots, so the new chord sounds with its new buttons (in key order a root
 * could come first and strike with the old ones: the owner heard the last chord slip in) */
/* an outside keyboard's notes (NOTES IN FM-1 keys or Roots; cf_out.c cf_xq): FM-1 keys folds each note by octaves
 * into F3..G5 and presses that key (the FM-1's own key down too: nothing more); Roots sends it as a root at its
 * pitch, but on the SEQUENCER's DRUMS row, where the keys are drum pads, as a key. A note goes up the way it went down */
static void cf_xq_drain(void)
{
    uint32_t w = cf_xq_w;
    while (cf_xq_r != w) {
        uint32_t e = cf_xq[cf_xq_r % CF_XQ], n = e & 0x7Fu, down = (e >> 7) & 1u, k, v;
        uint32_t age = fm1_ms - cf_xq_ms[cf_xq_r % CF_XQ];
        cf_xq_r++;
        cu.idle_ms = 0;
        if (down) {
            if (cu.xnote[n])
                continue;                        /* (already down: a repeated note-on) */
            if (cs.notes_in == CF_NI_ROOTS && !sq_frame_trk(SQ_DRUMS)) {
                cu.xnote[n] = 0xFF;
                cu.xroots++;
                cf_post(CFE_ROOT, n, age <= 250u ? age : 0u, 1);
                continue;
            }
            for (k = n; k < 53u; k += 12u)
                ;
            while (k > 79u)
                k -= 12u;
            k -= 53u;
            cu.xnote[n] = (uint8_t)(k + 1u);
            if (cu.xcnt[k]++ == 0u) {
                cu.xkeys |= 1u << k;
                if (!((cu.pkeys >> k) & 1u))
                    ui_note_key_aged((int)k, 1, age <= 250u ? (int)age : 0);
            }
        } else {
            v = cu.xnote[n];
            cu.xnote[n] = 0;
            if (v == 0xFFu) {
                if (cu.xroots)
                    cu.xroots--;
                cf_post(CFE_ROOT, n, 0, 0);
            } else if (v) {
                k = v - 1u;
                if (cu.xcnt[k] && --cu.xcnt[k] == 0u) {
                    cu.xkeys &= ~(1u << k);
                    if (!((cu.pkeys >> k) & 1u))
                        ui_note_key((int)k, 0);
                }
            }
        }
    }
}

static void cf_ui_pump(void)
{
    /* tap: a key pressed and let go since the last read (the scan keeps its press, hal/fm1_input.h notes_pressed): a
     * frame that drew longer than the press lost it. Right after power-on (the intro, the first full redraw) a quick
     * tap on a chord button went that way, and the roots played single notes (the owner, 2026-10-21: "после включения
     * если выбрать тип аккорда и начать играть, то сначала играются по одной ноте") */
    uint32_t now = fm1_in.notes, tap = fm1_input_note_edges() & ~now & ~cu.pkeys & ((1u << CF_NKEYS) - 1u);
    uint32_t ch = ((now ^ cu.pkeys) | tap) & ~cu.xkeys, k, pass;   /* (a key held from outside too: no change) */
    if (!cu.booted)
        return;
    if (ch) {
        for (pass = 0; pass < 3u; pass++)
            for (k = 0; k < CF_NKEYS; k++)
                if ((ch >> k) & 1u) {
                    int down = (int)(((now | tap) >> k) & 1u), idx;
                    int btn = cf_key_role(cs.chord.algo, (int)k, &idx) == CF_ROLE_BUTTON;
                    if ((uint32_t)(down ? (btn ? 1 : 2) : 0) == pass)
                        ui_note_key((int)k, down);
                }
        for (k = 0; k < CF_NKEYS; k++)           /* (a tap's release, after the roots it went with) */
            if ((tap >> k) & 1u & ~(cu.xkeys >> k))
                ui_note_key((int)k, 0);
        cu.idle_ms = 0;
    }
    cu.pkeys = now;
    cf_xq_drain();
    cu.keys = cu.pkeys | cu.xkeys;
}

static void cf_ui_input(void)
{
    uint32_t rel = 0, pr = fm1_input_edges(&rel), i;
    int32_t d;
    sq_home_poll();                              /* (a take from HOME ended: its bars, before its undo is kept) */
    sq_snd_poll();                               /* (the current pattern's sounds: 0.5) */
    cf_glide_migrate();                          /* (the old GLIDE into HOME's sound, once) */
    sq_undo_poll();                              /* (before the input: the state an edit starts from) */
    cf_ui_pump();
    cf_save_poll();
    cf_reset_poll();
    cf_rec_poll();
    sq_glo_poll();
    sq_write_poll();
    sq_rec_poll();
    sq_sel_poll();
    sq_oct_poll();
    sq_edit_poll();
    sq_save_poll();
    for (i = 0; i < 14u; i++)
        if ((pr >> i) & 1u) {
            uint32_t b = panel_btn_of(i);
            if (b < NB)
                ui_button(b);
            cu.idle_ms = 0;
        }
    if ((d = panel_enc(EN_SELECT)) != 0)
        ui_select(d), cu.idle_ms = 0;
    if ((d = panel_enc(EN_PRESET)) != 0)
        ui_presets(d), cu.idle_ms = 0;
    if ((d = panel_enc(EN_ALGO)) != 0)
        ui_algo(d), cu.idle_ms = 0;
    for (i = 0; i < 4u; i++)
        if ((d = panel_enc(EN_K1 + i)) != 0)
            ui_knob_turn((int)i, d), cu.idle_ms = 0;
}

/* ------------------------------------------------------------ key caps --- */
static void ui_keycap(int key, cf_keycap_t *o)
{
    int idx, role = cf_key_role(cs.chord.algo, key, &idx), down = (cu.keys >> key) & 1u;
    const cf_chord_out_t *out = &cf_sn.out;
    if (sq_keycap(key, o))
        return;
    if (cu.scr == SCR_EDIT && cf_key_is_upper(key)) {
        int u = 0, k;
        for (k = 0; k < key; k++)
            u += cf_key_is_upper(k);
        if (u < CFE_NSEC) {
            str_cpy(o->top, cfe_key_name(cu.edit_part, (uint32_t)u), 8);
            o->style = u == cu.edit_sec ? KC_ON : KC_BTN;
        } else if (u >= 8 && u <= 10) {
            str_cpy(o->top, u == 8 ? "BAS" : u == 9 ? "CHD" : "LAY", 8);
            o->style = edit_key_part(u) == cu.edit_part ? KC_SOUNDING : KC_BTN;
        } else {
            o->style = KC_OFF;
        }
        return;
    }
    if (role == CF_ROLE_BUTTON) {
        const cf_form_t *f = cf_button_form(&cs.chord, idx);
        int hold = cf_is_off(&cs.chord, idx), on = hold ? out->hold : (out->buttons >> idx) & 1u;
        o->btn = 1;
        if (hold)
            str_cpy(o->top, "HOLD", 8);          /* (the Off form's key: HOLD since 0.5, lit while on) */
        else
            cf_form_label(f, o->top);
        /* held and in the chord: pressed; latched (Lock): lit; held but left out of the chord (a minor button with
         * a major one, cf_chord.c cfc_used): plain, so it plainly does not count */
        o->style = down && on ? KC_DOWN : on ? KC_ON : KC_BTN;
        if (cu.scr == SCR_SEL && idx == cu.sel_btn && !down)
            o->style = KC_SOUNDING;
        return;
    }
    {
        int n = cf_key_root_note(&cs.chord, key), pc = n % 12, i, sounding = 0;
        char nm[4];
        cf_pc_name(pc, &cu_spell, nm);
        str_cpy(o->top, nm, 8);
        if (cs.chord.quantize != CF_Q_OFF)
            cf_numeral(cs.chord.scale, cs.chord.tonic, pc, o->bot);
        if (o->bot[0]) {                         /* the degree as a digit, its flat or sharp kept (bVII -> b7) */
            int d = 0, j, v = 0, k = 0;
            char acc = o->bot[0] == 'b' || o->bot[0] == '#' ? o->bot[0] : 0;
            for (j = 0; o->bot[j]; j++) {
                char ch = o->bot[j];
                if (ch == 'I' || ch == 'i')
                    v++;
                else if (ch == 'V' || ch == 'v')
                    d = v ? 5 - v : 5, v = 0;
            }
            d += v;
            if (acc)
                o->bot[k++] = acc;
            o->bot[k++] = (char)('0' + (d >= 1 && d <= 7 ? d : 0));
            o->bot[k] = 0;
        }
        for (i = 0; i < cf_sn.n; i++)
            sounding |= cf_sn.note[i] % 12 == pc;
        o->style = down ? KC_DOWN : (out->sounding && out->root_pc == pc) ? KC_SOUNDING :
                   (cs.chord.quantize != CF_Q_OFF && pc == cs.chord.tonic) ? KC_SCALE : KC_ROOT;
        (void)sounding;
    }
}

/* ----------------------------------------------------------- the lights --- */
static uint8_t cu_led_pos[41];
static void cf_led_init(void)
{
    uint32_t id, p, r;
    for (id = 0; id < 41u; id++) {
        cu_led_pos[id] = 0xFF;
        for (p = 0; p < FM1_NCOL; p++)
            for (r = 1; r < 5u; r++)
                if (FM1_KEYMAP[r][p] == (int8_t)id)
                    cu_led_pos[id] = (uint8_t)((p << 3) | r);
    }
}
static void cf_led(uint8_t *m, uint32_t id, int on)
{
    uint8_t q = cu_led_pos[id % 41u];
    if (q != 0xFF && on)
        m[q >> 3] |= (uint8_t)(1u << (q & 7u));
}

/* the sequencer's lights. The buttons say what works here: EDIT blinks on the tracks (open one), SAVE (back) and SEL
 * (clear, when there is something to clear) are up in a track, REC goes dark (it does nothing here), PLAY is up when
 * there is something to play. The keys: DRUMS as a drum machine's (the lower keys the drum's steps in the bar, a
 * running light on the step that plays; the upper keys the drums, the one picked lit, those that strike in the bar
 * dim, each flashing as it sounds); CHORDS / BASS / LAYER light as on HOME (below). Returns 1 when the key is lit
 * here (the keys' rules below skip it) */
static int sq_key_led(uint32_t k, uint8_t *lit, uint8_t *mid, uint8_t *dim, uint8_t *faint)
{
    const sq_pat_t *p = sq_cur();
    uint32_t id = 14u + k;
    if (cu.scr == SCR_SQ && squ.pat == SQ_NPAT) {   /* the song: the patterns' keys (the frame's lit, the others with
                                                  * steps dim, the empty ones faint); the lower keys dark */
        int j, u = 0;
        if (!cf_key_is_upper((int)k))
            return 1;
        for (j = 0; j < (int)k; j++)
            u += cf_key_is_upper(j);
        if (u < (int)SQ_NPAT)
            cf_led((cu.keys >> k) & 1u || (sq_st.song.n && sq_st.song.pat[squ.slot] == u) ? lit :
                   sq_has((uint32_t)u) ? dim : faint, id, 1);
        return 1;
    }
    if (cu.scr == SCR_SQ && squ.pat < SQ_NPAT && squ.trk == SQ_DRUMS) {   /* the Drums row: the drums on the upper
                                                  * keys (the picked one lit, each flashing as it sounds), the lower
                                                  * keys dark */
        int j, i = 0;
        if (!cf_key_is_upper((int)k))
            return 1;
        for (j = 0; j < (int)k; j++)
            i += cf_key_is_upper(j);
        if (i < (int)SQ_NLANE)
            cf_led((cu.keys >> k) & 1u || i == squ.lane ||
                   ((int32_t)(cf_sn.now_ms - cf_sn.sq_lane_t[i]) < 110 && cf_sn.sq_lane_t[i]) ? lit : faint, id, 1);
        return 1;
    }
    if (cu.scr != SCR_SQED || (cu.keys >> k) & 1u)
        return 0;
    if (squ.trk == SQ_DRUMS) {
        int j, i = 0, st = SQ_ST(p);
        for (j = 0; j < (int)k; j++)
            i += cf_key_is_upper(j) == cf_key_is_upper((int)k);
        if (cf_key_is_upper((int)k)) {
            if (i >= (int)SQ_NLANE)
                return 1;
            if ((int32_t)(cf_sn.now_ms - cf_sn.sq_lane_t[i]) < 110 && cf_sn.sq_lane_t[i])
                cf_led(lit, id, 1);
            else
                cf_led(i == squ.lane ? lit : p->hit[i][squ.bar] ? dim : faint, id, 1);
            return 1;
        }
        if (i >= st)
            return 1;
        {                                        /* the picked drum's hits full, the beats lit less under them (the
                                                  * owner, 2026-10-20: "доли слегка подсвечиваются на клавишах, а
                                                  * поставленные на дорожку барабаны светятся во всю"); the running
                                                  * light full on an empty step and dark on a hit, so it shows there
                                                  * too */
            int hit = (p->hit[squ.lane][squ.bar] >> i) & 1u;
            if (cf_sn.sq_on && cf_sn.sq_pat == squ.pat &&   /* (the song's too) */
                sq_trk_bar(p, SQ_DRUMS, cf_sn.sq_n) == squ.bar && cf_sn.sq_bt &&
                (int)(cf_sn.sq_t * (uint32_t)st / cf_sn.sq_bt) == i)
                cf_led(lit, id, !hit);
            else if (hit)
                cf_led(lit, id, 1);
            else if ((i * cs.beats) % st == 0)  /* (the middle level: dim was hard to see in daylight, "ещё бы чуть
                                                  * поярче", the owner, 2026-10-20; nothing lies between dim and it) */
                cf_led(mid, id, 1);
        }
        return 1;
    }
    /* CHORDS / BASS / LAYER: the keys light as on HOME, as the screen's caps do: what they play and write (held, the
     * buttons latched in Lock, the scale). They showed the step under the frame (blinking) or the chord playing while
     * the caps showed the keys' own: a latched m9 on the screen, the step's sus2 sus4 on the keys (the owner,
     * 2026-10-19, a video; "как на HOME"). The step's chord is its name over the grid and its flower */
    return 0;
}

static void cf_leds(void)
{
    uint8_t lit[FM1_NCOL] = {0}, mid[FM1_NCOL] = {0}, dim[FM1_NCOL] = {0}, faint[FM1_NCOL] = {0};
    static const uint8_t SCR_BTN[SCR_N] = {B_HOME, B_SCL, B_ENV, B_LFO, B_EDIT, B_GLO, B_FX, B_ARP, B_SEQ, B_SAVE,
                                           B_SEQ, B_SEQ};
    const cf_chord_out_t *out = &cf_sn.out;
    uint32_t k, blink = (cu.now / 250u) & 1u, sq = sq_screen(cu.scr);
    /* buttons: the open screen lit, the others dim; PLAY with the beat. The sequencer: only what works there is up
     * (HOME, SEQ, GLO here; EDIT, SAVE, SEL, PLAY, REC below); the other screens' buttons do nothing there, dark */
    for (k = 0; k < SCR_N; k++)
        if (!sq || SCR_BTN[k] == B_HOME || SCR_BTN[k] == B_SEQ || SCR_BTN[k] == B_GLO)
            cf_led(k == cu.scr ? lit : dim, panel.btn[SCR_BTN[k]], 1);
    if (sq) {                                    /* the sequencer: what works here (sq_key_led's note) */
        const sq_pat_t *p = sq_cur();
        int has = squ.pat == SQ_NPAT ? 1 : !sq_empty(p) && (p->nev || sq_drums(p));
        if (cu.scr == SCR_SQ && squ.pat == SQ_NPAT) {   /* the song: EDIT and SEL work on the frame's pattern */
            has = sq_st.song.n != 0;
            cf_led(has ? mid : faint, panel.btn[B_EDIT], 1);
            cf_led(has ? mid : faint, panel.btn[B_SCL], 1);
        } else if (cu.scr == SCR_SQ) {           /* (SEL held clears the track: up when it has something) */
            cf_led(blink ? mid : dim, panel.btn[B_EDIT], 1);
            if (sq_track_has(p, squ.trk))
                cf_led(dim, panel.btn[B_SCL], 1);
        } else {
            int clear = squ.trk != SQ_DRUMS && !squ.nofr && sq_at(p, squ.trk, squ.bar, squ.step) >= 0;
            /* (DRUMS: SEL nothing) */
            cf_led(mid, panel.btn[B_SAVE], 1);
            if (squ.mv || clear)                 /* (EDIT held + SELECT moves the frame's chord: up on a chord) */
                cf_led(squ.mv ? lit : mid, panel.btn[B_EDIT], 1);
            if (squ.trk != SQ_DRUMS)
                cf_led(clear ? mid : faint, panel.btn[B_SCL], 1);
            if (squ.trk == SQ_CHORDS || squ.trk == SQ_LAYER)   /* (ARP: the track's own, lit while on) */
                cf_led((p->arp >> squ.trk) & 1u ? lit : dim, panel.btn[B_ARP], 1);
        }
        if (cf_sn.sq_on)
            cf_led((int32_t)(cf_sn.now_ms - cf_sn.beat_ms) < 120 ? lit : mid, panel.btn[B_PLAY], 1);
        else
            cf_led(has ? mid : faint, panel.btn[B_PLAY], 1);
        if (cf_sn.sq_rec != 0xFFu)               /* REC: a take lit, its count-in blinking; on a track: ready (dim) */
            cf_led(cf_sn.sq_cin ? (blink ? lit : faint) : lit, panel.btn[B_REC], 1);
        else if (squ.pat < SQ_NPAT)
            cf_led(dim, panel.btn[B_REC], 1);
    } else {
        if (sqk.on)                              /* REC: a take from here lit, its count-in blinking (as in a track) */
            cf_led(cf_sn.sq_cin || !sqk.seen ? (blink ? lit : faint) : lit, panel.btn[B_REC], 1);
        else
            cf_led(dim, panel.btn[B_REC], 1);
        if (cf_sn.running)                       /* PLAY: with the beat while it runs; a pattern to play: mid */
            cf_led((int32_t)(cf_sn.now_ms - cf_sn.beat_ms) < 120 ? lit : mid, panel.btn[B_PLAY], 1);
        else
            cf_led((sqk.song && sq_st.song.n) || (sqk.last < SQ_NPAT && sq_has(sqk.last)) ? mid : dim,
                   panel.btn[B_PLAY], 1);
        if (cf_sn.sq_on)                         /* the sequencer plays elsewhere: SEQ up, as ARP is */
            cf_led(mid, panel.btn[B_SEQ], 1);
    }
    if (cu.arp_on && !sq)                        /* the arpeggiator plays: ARP lit wherever it opens (not on the
                                                  * sequencer: it does nothing there) */
        cf_led(lit, panel.btn[B_ARP], 1);
    cf_led(cs.chord.octave < 0 ? (cs.chord.octave < -1 ? lit : mid) : faint, panel.btn[B_OCTDN], 1);
    cf_led(cs.chord.octave > 0 ? (cs.chord.octave > 1 ? lit : mid) : faint, panel.btn[B_OCTUP], 1);
    /* keys: held lit; the effective chord buttons lit, the others faint; the chord's root mid; the scale dim */
    for (k = 0; k < CF_NKEYS; k++) {
        int idx, role = cf_key_role(cs.chord.algo, (int)k, &idx), id = (int)(14u + k);
        if (sq_key_led(k, lit, mid, dim, faint))
            continue;
        if ((cu.keys >> k) & 1u) {
            cf_led(lit, (uint32_t)id, 1);
            continue;
        }
        if (cu.scr == SCR_EDIT && cf_key_is_upper((int)k)) {
            int u = 0, j;
            for (j = 0; j < (int)k; j++)
                u += cf_key_is_upper(j);
            if (u == cu.edit_sec || (u >= 8 && u <= 10 && edit_key_part(u) == cu.edit_part))
                cf_led(lit, (uint32_t)id, 1);
            else if (u < 11)
                cf_led(blink ? dim : faint, (uint32_t)id, 1);
            continue;
        }
        if (role == CF_ROLE_BUTTON) {
            if (cf_is_off(&cs.chord, idx) ? out->hold : (out->buttons >> idx) & 1u)   /* (HOLD: lit while on, as on the
                                                                                       * screen) */
                cf_led(lit, (uint32_t)id, 1);
            else if (cu.scr == SCR_SEL && idx == cu.sel_btn)
                cf_led(blink ? mid : dim, (uint32_t)id, 1);
            else
                cf_led(faint, (uint32_t)id, 1);
        } else if (role == CF_ROLE_ROOT) {
            int pc = cf_key_root_note(&cs.chord, (int)k) % 12;
            if (out->sounding && pc == out->root_pc)
                cf_led(mid, (uint32_t)id, 1);
            else if (cs.lights && cs.chord.quantize != CF_Q_OFF && pc == cs.chord.tonic)
                cf_led(dim, (uint32_t)id, 1);
        }
    }
    for (k = 0; k < FM1_NCOL; k++) {
        fm1_led_faint[k] = faint[k];
        fm1_led_dim[k] = dim[k];
        fm1_led_dim2[k] = mid[k];
        fm1_led[k] = lit[k];
    }
}

/* the GLIDE of 0.4.x / 0.5-rc1, one setting for every sound: HOME's chord sound takes it, as an edit of it, the
 * first time it is the one playing (not a pattern's own: 0.5, each sound its own, cf_play.c cfp_glide_g); the setting
 * goes to 0 for good. Every frame until then (cheap) */
static void cf_glide_migrate(void)
{
    uint32_t want, g = 0;
    if (!cs.glide_ms || cfk_own[CF_PART_CHORD] != 0u || cf_stage[CF_PART_CHORD].ready)
        return;
    want = (uint32_t)cs.glide_ms * FS / (1000u * CTL);
    while (g < 127u && 1u + g * g / 8u < want)
        g++;
    if (!trk[CF_PART_CHORD].p[P_GLIDE] && g) {
        fm1_irq_off();
        trk[CF_PART_CHORD].p[P_GLIDE] = (int16_t)g;
        fm1_irq_on();
        cf_edited(CF_PART_CHORD);
    }
    cs.glide_ms = 0;
}

/* ------------------------------------------------------------ the plant --- */
static uint32_t cu_last_serial;
static uint8_t cu_last_onset, cu_last_arp, cu_last_src;
/* a sequencer chord's flower quality from its pitch classes over the root (the keys' come from their type) */
static uint8_t cf_fl_quality(uint32_t mask, int single)
{
    if (single)
        return FL_Q_SINGLE;
    if (mask & 1u << 4)
        return (mask & 1u << 8) && !(mask & 1u << 7) ? FL_Q_AUG : FL_Q_MAJOR;
    if (mask & 1u << 3)
        return (mask & 1u << 6) && !(mask & 1u << 7) ? FL_Q_DIM : FL_Q_MINOR;
    return mask & 1u << 5 ? FL_Q_SUS4 : mask & 1u << 2 ? FL_Q_SUS2 : FL_Q_MAJOR;
}
static void cf_feed_plant(uint32_t dt)
{
    const cf_chord_out_t *o = &cf_sn.out;
    uint32_t i, lvl = 0, src, ser;
    /* the Top algorithms grow a plant, the Side ones a coral coloured by the key */
    flora_set_species(&flo, cs.chord.algo >= CF_ALG_SIDE ? FL_SP_CORAL : FL_SP_PLANT);
    flora_set_key(&flo, cs.chord.tonic);
    flo.backdrop = cs.backdrop;
    /* what the flower shows: the keys' chord while they sound, else the sequencer's CHORDS track's while it plays,
     * as if played by hand (0.5, the owner: "то, что играется в секвенсоре я бы хотел, чтобы на главной
     * отображалось на цветочке"), else nothing */
    src = o->sounding && o->n ? 1u : cf_sn.sq_on && cf_sn.sq_dn ? 2u : 0u;
    ser = src == 2u ? cf_sn.sq_dser : o->serial;
    if (src != cu_last_src || ser != cu_last_serial) {
        fl_chord_t c;
        int strike = src == 2u && (ser != cu_last_serial || cu_last_src != 2u);
        cu_last_src = (uint8_t)src;
        cu_last_serial = ser;
        if (src == 1u) {
            static const uint8_t QT[CF_NTYPES] = {FL_Q_SINGLE, FL_Q_MAJOR, FL_Q_MINOR, FL_Q_DIM, FL_Q_AUG,
                                                   FL_Q_SUS2, FL_Q_SUS4, FL_Q_MAJOR};
            c.n = (uint8_t)(o->n > 8 ? 8 : o->n);
            for (i = 0; i < c.n; i++)
                c.note[i] = o->note[i];
            c.root = o->root_pc;
            c.quality = o->single ? FL_Q_SINGLE : QT[o->type < CF_NTYPES ? o->type : 1];
            c.bass = (int8_t)(cf_sn.nb ? cf_sn.bass[cf_sn.nb - 1] : -1);
            flora_set_chord(&flo, &c);
        } else if (src == 2u) {
            c.n = cf_sn.sq_dn;
            for (i = 0; i < c.n; i++)
                c.note[i] = cf_sn.sq_dnote[i];
            c.root = (uint8_t)(cf_sn.sq_droot % 12u);
            c.quality = cf_fl_quality(cf_sn.sq_dmask, cf_sn.sq_dsingle);
            c.bass = (int8_t)(cf_sn.sq_bn ? cf_sn.sq_bass[cf_sn.sq_bn - 1] : -1);
            flora_set_chord(&flo, &c);
            if (strike)                          /* (each chord the sequencer strikes sparkles, as the keys' onset) */
                for (i = 0; i < c.n; i++)
                    flora_note_on(&flo, c.note[i], cs.vel);
        } else {
            c.n = 0;
            flora_set_chord(&flo, &c);
        }
    }
    if (cf_sn.onset != cu_last_onset) {
        cu_last_onset = cf_sn.onset;
        if (!cf_sn.arp_on)                       /* (the arpeggiator: a pulse a note, below) */
            for (i = 0; i < cf_sn.n; i++)
                flora_note_on(&flo, cf_sn.note[i], cs.vel);
    }
    if (cf_sn.arp_steps != cu_last_arp) {        /* each note the arpeggiator plays sparkles out of its branch */
        cu_last_arp = cf_sn.arp_steps;
        if (cf_sn.arp_on)
            flora_note_on(&flo, cf_sn.arp_note, cs.vel);
    }
    for (i = 0; i < NVOICE; i++) {               /* loudness: the chord part's strongest voice */
        int32_t e = trk[CF_PART_CHORD].v[i].active ? trk[CF_PART_CHORD].v[i].env_out : 0;
        if (e < 0)
            e = -e;
        if ((uint32_t)e > lvl)
            lvl = (uint32_t)e;
    }
    flora_set_level(&flo, (uint16_t)(lvl > 32767u ? 65535u : lvl * 2u));
    flora_tick(&flo, dt);
}

/* the sound editor's part (fm6_store.c: a DX7 voice received goes there), -1 when it is not open */
static int cf_editing_part(void) { return cu.scr == SCR_EDIT ? (int)cu.edit_part : -1; }
static void cf_edited(uint32_t part)
{
    if (part < 3u) {
        cu.edited[part] = 1;
        cfk_on[part] = 1;                        /* (kept over a power-off: cf_settings.c) */
        cfk_gen[part] = cf_snd_gen[part];
    }
}

/* ------------------------------------------------------------------ init --- */
static void cf_ui_init(void)
{
    if (!cfs_loaded)                             /* (cf_splash read the settings record: they stay) */
        cs_defaults(&cs);
    cu.scr = SCR_HOME;
    cu.sel_btn = sel_first();
    cf_led_init();
    cf_palette_build();
    cu_spell_update();
    cf_out_init();
    if (!cr_safe) {
        sq_load();                               /* the sequencer's patterns and song */
        cfps_load();                             /* .. their own sounds (0.5) */
        cfk_load();                              /* .. the sounds' tweaks (put on them as they load: cfk_take) */
        sq_from_loop();                          /* (a loop kept by 0.3.1 or before, or restored: a pattern now) */
        sq_resume();                             /* (what PLAY on HOME played before the power-off) */
    } else {
        sq_defaults();
    }
    if (cfs_loaded) {                            /* the sounds the parts had before the power-off */
        cf_snd_load_index(CF_PART_CHORD, cs.snd[0]);
        cf_snd_load_index(CF_PART_BASS, cs.snd[1]);
    } else {                                     /* a new unit: FM ORGAN for the chords (the owner's pick), a round bass */
        if (!cf_snd_find(CF_PART_CHORD, ENGI_FM6, "ORGAN"))
            cf_snd_load_index(CF_PART_CHORD, 1);
#if FELUCCA_VA
        if (!cf_snd_find(CF_PART_BASS, ENGI_VA, "DEEP SUB"))
#endif
            cf_snd_load_index(CF_PART_BASS, 0);
    }
    cs.snd[0] = (uint16_t)cf_snd_pos(CF_PART_CHORD);
    cs.snd[1] = (uint16_t)cf_snd_pos(CF_PART_BASS);
    if (cs.snd_layer != 0xFFFFu)                 /* the LAYER's sound; a new unit's: a soft lead, else the second sound */
        cf_snd_load_index(CF_PART_LAYER, cs.snd_layer);
#if FELUCCA_VA
    else if (!cf_snd_find(CF_PART_LAYER, ENGI_VA, "SOFT LEAD"))
#else
    else
#endif
        cf_snd_load_index(CF_PART_LAYER, 2);
    cs.snd_layer = (uint16_t)cf_snd_pos(CF_PART_LAYER);
    cf_drums_init();
    sq_home_poll();                              /* (the pattern PLAY plays, if none was kept) */
    sq_snd_force = 1;
    sq_snd_poll();                               /* (.. its own sounds, every sound's tweak on it: 0.5) */
    cf_glide_migrate();
    cu.env_tgt = cs.env_link ? 0u : 1u;
    cu.arp_on = cs.arp_keep;                     /* (the arpeggiator as it was at the power-off: cf_settings.c) */
    if (cs.env_link)
        cf_env_follow();
    cf_click_sound = cs.click;
    cf_send_config();
    cf_send_metro();
    if (!flo.nseg)
        flora_init(&flo, 0xC0A1F10Eu, FLW / 2, FLH + 1);   /* (the stem comes up from the bottom edge) */
    cu.idle_ms = 0x7FFFFFFF;                     /* (the intro runs until the first touch) */
    fb_full = 1;                                 /* (main.c cleared the panel after the splash) */
    cu.last_ms = fm1_ms;
    cu.booted = 1;
}

static void cf_ui_frame(void)                    /* ui_leds: the snapshot, the plant, the lights */
{
    uint32_t now = fm1_ms, dt = now - cu.last_ms;
    cu.last_ms = now;
    cu.frame_dt = dt;
    cu.now = now;
    cu.idle_ms += dt;
    cu.frames++;
    cf_snapshot();
    cf_feed_plant(dt);
    cf_leds();
    if (cu.pop && (int32_t)(cu.now - cu.pop_until) > 0)
        cu.pop = POP_NONE;
}
