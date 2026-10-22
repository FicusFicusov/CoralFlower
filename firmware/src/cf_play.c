/* SPDX-License-Identifier: GPL-3.0-only
 * CoralFlower: the chord player, in the audio ISR.
 *
 * The keys reach cf_chord (the chord model) here; what it asks for becomes notes on two parts:
 *   part 0 CHORD: the chord's notes (or the single notes of Off)
 *   part 1 BASS:  the chord's bass degree in the bass register, with the octaver an octave over it; monophonic (a
 *                 new bass note cuts what still rings of the last one)
 * A new root key restarts the chord; a chord button changed under a held root keeps the tones the two chords share.
 * With Glide on (or RETRIG Off), a change while the previous chord still sounds moves its voices to the new chord: the
 * voices are paired by pitch (cf_match: lowest with lowest, common tones stay), a chord that grows starts its extra
 * voices out of the nearest old note, one that shrinks lets the extra voices go. With RETRIG Wait a chord button under
 * held keys changes nothing that sounds (cf_chord_t.keep): the next root key takes it; Move: it moves the voices too
 * (the tones it adds start); Strike (Notes on the screen): the notes that change strike (with a Glide time: out of
 * the old ones); Chord: the whole new chord strikes, the shared tones too,
 * each note gliding from the last chord's.
 * Included after voice.c, fx.c, usb.c, cf_theory.c, cf_chord.c (coralflower.c, the emulator's unit). */

enum { CF_RT_WAIT = 0, CF_RT_STRIKE = 1, CF_RT_MOVE = 2, CF_RT_CHORD = 3, CF_NRT };   /* RETRIG (stored: 0 and 1
                                                                                    * were Off and On) */

typedef struct {                /* the arpeggiator's settings (ARP; cf_arp.c) */
    uint8_t mode;               /* CF_ARP_* */
    uint8_t oct;                /* 1..4 octaves */
    uint8_t rate;               /* a CF_ARP_UNITS index (1/1 .. 1/32) */
    uint8_t gate;               /* 20..100 % */
    uint8_t swing;              /* 50..75 % */
    uint8_t latch;
    uint8_t strum_ms;           /* Strum: 5..250 ms between the notes */
} cf_arp_t;
#define CF_ARP_DEFAULT {0, 1, 4, 70, 50, 0, 40}

typedef struct {                /* the looper's settings up to 0.3.1: unused since 0.4 has no looper, kept so the
                                 * settings record keeps its layout */
    uint8_t bars;               /* the take's length: 0 Free, else CF_LOOP_BARS[bars] bars after a count-in */
    uint8_t mode;               /* REC over a loop: 0 Overwrite (a new take), 1 Overdub (a layer on top) */
    uint8_t quant;              /* 0 off, 1 a 1/8, 2 a 1/16: the chords' starts as they are recorded */
    uint8_t count_in;           /* a bar counted in before a take of bars */
} cf_loop_t;
#define CF_LOOP_DEFAULT {0, 0, 0, 1}

/* a note the keys' player sounds, a note the sequencer sounds (each leaves such a note sounding when it lets it go:
 * the last of the two lets it go) */
static int cfp_holds(uint32_t part, uint32_t note);   /* (below: a note the keys' player sounds) */
static int sqp_holds(uint32_t part, uint32_t note);   /* (cf_seqp.c: a note the sequencer sounds) */

enum { CF_NI_KEYS = 0, CF_NI_ROOTS, CF_NI_DIRECT, CF_NI_N };   /* NOTES IN (0.5): the FM-1's keys, roots, as before */
typedef struct {
    uint16_t glide_ms;          /* (unused since 0.5: the chord sound's own P_GLIDE, cfp_glide_g) */
    uint8_t glide_always;       /* 1 Always: a chord after a release glides from the last one; 0 Legato: only held */
    uint8_t vel;                /* the keys have no velocity: this one */
    uint8_t bass_on;
    int8_t bass_oct;            /* -2 .. +2 octaves; 0: an octave under the root as played */
    uint8_t octaver;            /* a second bass note an octave higher */
    uint8_t midi_out;           /* notes out on USB MIDI */
    uint8_t ch_chord, ch_bass;  /* 0..15 */
    uint8_t retrig;             /* CF_RT_*: WAIT and MOVE move the sounding voices under held keys, no new attack */
    uint8_t arp_on;             /* the arpeggiator plays part 0 (the ARP button) */
    cf_arp_t arp;
    uint16_t bpm;               /* the tempo (the metronome's): the arpeggiator's steps */
    cf_loop_t loop;
    uint8_t clock_in;           /* follow MIDI clock in (GLO > MIDI > CLOCK In): its pulses run the beat */
    uint8_t clock_out;          /* send MIDI clock (GLO > MIDI > CLOCK Out): 24 pulses a beat while the beat runs,
                                 * start and stop with it */
    uint8_t cpart;              /* the part the keys' chords play: 0, or 2 while the sequencer's LAYER is open (its
                                 * own sound; the bass is silent then) */
    uint8_t ch_layer, ch_drums; /* the sequencer's LAYER (part 2) and DRUMS (part 3) out on MIDI, 0..15 */
    uint8_t bass_only;          /* the keys play the bass alone (the sequencer's BASS in the frame: its row, its track,
                                 * its take), HOME's bass switch or not; no chord */
    uint8_t notes_in;           /* GLO > MIDI > NOTES IN (CF_NI_*): notes in play the parts directly
                                 * (Direct), else they go to the UI as the FM-1's keys or as roots (cf_out.c cf_xq) */
} cf_play_cfg_t;

/* the beat grid the arpeggiator locks to while the metronome's clock runs (cf_out.c cfm_tick writes it): on, where it
 * is in 1/24 beats (a MIDI clock's pulses), gen: + 1 when it starts again from beat 1 */
static struct { uint8_t on; uint32_t g24, gen; } cf_grid;

static uint32_t cf_now_ms, cf_ms_rem;                /* the engine clock: audio samples -> ms (cf_out.c) */
static cf_chord_t cfc;                               /* the chord model: the ISR's */
static cf_chord_cfg_t cfc_shadow;                    /* the UI's copy; CFE_CONFIG takes it (IRQ off while written) */
static cf_play_cfg_t cfp_cfg = {0, 0, 100, 1, 0, 0, 1, 0, 1, 0, 0, CF_ARP_DEFAULT, 120, CF_LOOP_DEFAULT, 0};
static uint8_t cfp_cp;                               /* the part the keys' chords sound on now (cfp_cfg.cpart, taken
                                                     * at a CFE_CONFIG with the notes let go) */
static uint8_t cfp_bo;                               /* .. the bass alone (cfp_cfg.bass_only, taken likewise) */

static struct {
    uint8_t n, note[CF_MAX_NOTES];                   /* what part 0 plays now, low to high */
    uint8_t nb, bass[2];                             /* what part 1 plays now */
    uint8_t nl, last[CF_MAX_NOTES], nlb, lastb[2]; /* the chord and bass that sounded last (Always glides from them) */
    uint32_t last_ms;                                /* when they were let go (cf_now_ms) */
    uint32_t serial;                                 /* the chord model's serial they follow */
    uint8_t onset;                                   /* bumped at every chord onset (the screen's plant) */
    uint32_t root_ms;                                /* when a root key last went down (cf_now_ms) */
    uint8_t slast;                                   /* single notes: the last one struck (a new one slides from it with a
                                                     * Glide time; 0: none, or a chord sounded since) */
    uint8_t sfrom;                                   /* .. the note the last slide left (its tail fades if it is let go */
    uint32_t sms;                                    /* .. while the slide runs), and when it began (cf_now_ms) */
} cfp;

/* the Glide (0.5, the owner: "glide работает как обычно, но влияет на аккорды из секвенсора ещё. Если переключается
 * инструмент, глайд переключается на тот что был у другого инструмента. Басс по-умолчанию подвязывается, леер нет"):
 * the chord sound's own, its P_GLIDE (HOME K4, voice.c glide_set's TIME: 1 + g^2 / 8 control blocks), so it comes and
 * goes with the sound (a pattern's own, PRESETS, a tweak, SAVE); the bass the keys play follows it. Keys on the LAYER:
 * the LAYER's sound glides by itself (voice.c TRK_GLIDE), none here */
static uint32_t cfp_glide_g(void)
{
    int32_t g = trk[0].p[P_GLIDE];
    return cfp_cp == 2u || g <= 0 ? 0u : g > 127 ? 127u : (uint32_t)g;
}
static uint32_t cfp_ticks(void)                      /* Glide in control blocks (CTL samples) */
{
    uint32_t g = cfp_glide_g();
    return g ? 1u + g * g / 8u : 0u;
}
static uint32_t cfp_glide_ms(void)
{
    return cfp_ticks() * CTL * 1000u / FS;
}

/* a part's MIDI out channel: the chord's, the bass's, the sequencer's LAYER's and DRUMS' (GLO > Channels; the
 * LAYER's notes first went out on the bass's channel) */
static uint32_t cfp_ch(uint32_t part)
{
    return part == 0u ? cfp_cfg.ch_chord : part == 1u ? cfp_cfg.ch_bass : part == 2u ? cfp_cfg.ch_layer :
           part == 3u ? cfp_cfg.ch_drums : 0xFFu;
}
static void cfp_midi(uint32_t st, uint32_t ch, uint32_t d1, uint32_t d2)
{
    if (ch > 15u)
        return;
    if (cfp_cfg.midi_out)
        midi_out_event((st >> 4) | (st | (ch & 15u)) << 8 | (d1 & 0x7Fu) << 16 | (d2 & 0x7Fu) << 24);
}
static void cfp_on(uint32_t part, uint32_t note, uint32_t vel)
{
    trk_note_on(&trk[part], note, vel);
    cfp_midi(0x90u, cfp_ch(part), note, vel);
}
static void cfp_off(uint32_t part, uint32_t note)
{
    if (sqp_holds(part, note))                   /* (the sequencer sounds it too: it goes when it lets go) */
        return;
    trk_note_off(&trk[part], note);
    cfp_midi(0x80u, cfp_ch(part), note, 0);
}
static void cfp_on_from(uint32_t part, uint32_t note, uint32_t vel, uint32_t from, uint32_t ticks)
{
    trk_note_on_from(&trk[part], note, vel, from, ticks);
    cfp_midi(0x90u, cfp_ch(part), note, vel);
}
/* a held voice moved to another note: MIDI says it as an off and an on */
static void cfp_move(uint32_t part, uint32_t from, uint32_t to, uint32_t ticks)
{
    voice_t *v = trk_voice_of(&trk[part], from);
    uint32_t ch = cfp_ch(part);
    if (from == to)
        return;
    if (v) {
        trk_retune(&trk[part], v, to, ticks);
    } else {                                         /* (no held voice: stolen, or its note still waits to start) */
        trk_note_off(&trk[part], from);
        trk_note_on(&trk[part], to, cfp_cfg.vel);
    }
    cfp_midi(0x80u, ch, from, 0);
    cfp_midi(0x90u, ch, to, cfp_cfg.vel);
}

static int has(const uint8_t *a, int n, uint8_t v)
{
    int i;
    for (i = 0; i < n; i++)
        if (a[i] == v)
            return 1;
    return 0;
}

/* the bass is monophonic: when its new notes come, what still rings of the old ones (their release tails) fades out
 * over the next block, as a stolen voice does (voice.c stage 4), so a long release never stacks two bass notes */
static void cfp_bass_mono(void)
{
    uint32_t i;
    for (i = 0; i < NVOICE; i++) {
        voice_t *v = &trk[1].v[i];                 /* (part 1: the bass) */
        if (v->active && !v->gate && v->stage != 4u)
            v->stage = 4;
    }
}

/* RETRIG Chord / Notes: the notes a strike replaces fade over the next block (as a stolen voice, voice.c stage 4)
 * instead of ringing out their release under the new ones: a tail at the old pitch hid the glide (the owner,
 * 2026-10-15: "the glide does not work in this mode") */
static void cfp_fade_released(uint32_t part)
{
    uint32_t i;
    for (i = 0; i < NVOICE; i++) {
        voice_t *v = &trk[part].v[i];
        if (v->active && !v->gate && v->stage != 4u)
            v->stage = 4;
    }
}
static void cfp_fade_note(uint32_t part, uint32_t note)   /* .. one note's, let go */
{
    uint32_t i;
    for (i = 0; i < NVOICE; i++) {
        voice_t *v = &trk[part].v[i];
        if (v->active && !v->gate && v->note == note && v->stage != 4u)
            v->stage = 4;
    }
}

/* part's notes old[n] -> new[m]: restart, keep the shared ones, or glide */
static void cfp_change(uint32_t part, uint8_t *old, uint8_t *pn, const uint8_t *nw, int m, int mode)
{
    int n = *pn, i, bass = part == 1u;           /* (the bass part: part 2 is the LAYER, a chord sound) */
    uint32_t vel = cfp_cfg.vel, ticks = cfp_ticks();
    if (bass && m)                                   /* the tails of bass notes let go before */
        cfp_bass_mono();
    if (mode == 6) {                                 /* single notes, a Glide time: the held ones stay where they are, a
                                                      * new one slides from the last note struck */
        int adds = 0;
        for (i = 0; i < m; i++)
            adds |= !has(old, n, nw[i]);
        for (i = 0; i < n; i++)
            if (!has(nw, m, old[i])) {
                cfp_off(part, old[i]);
                if (old[i] == cfp.sfrom && cf_now_ms - cfp.sms < cfp_glide_ms())
                    cfp_fade_note(part, old[i]);     /* (the note a slide is leaving: its tail at the old pitch hid the
                                                      * slide; one of two held notes let go later rings out) */
            }
        if (adds) {
            cfp_fade_note(part, cfp.slast);          /* (Always: the last note's tail, let go before) */
            cfp.sfrom = cfp.slast;
            cfp.sms = cf_now_ms;
        }
        for (i = 0; i < m; i++)
            if (!has(old, n, nw[i]))
                cfp_on_from(part, nw[i], vel, cfp.slast, ticks);
    } else if (!m || mode == 0 || mode == 3 || mode == 4) {   /* silence, or a fresh strike: everything restarts */
        uint8_t *prev = bass ? cfp.lastb : cfp.last, was[CF_MAX_NOTES];
        int np = bass ? cfp.nlb : cfp.nl;
        if (mode == 4) {                             /* RETRIG Chord: the whole chord strikes, out of the one sounding */
            for (i = 0; i < n; i++)
                was[i] = old[i];
            prev = was;
            np = n;
        }
        for (i = 0; i < n; i++)
            cfp_off(part, old[i]);
        if ((bass && m) || (mode == 4 && m && ticks))   /* the old notes just let go: they too (Chord: gliding) */
            cfp_fade_released(part);
        if (m && (mode == 3 || mode == 4) && np && ticks) {   /* Always, Chord: the new notes come out of the last chord's */
            uint8_t pi[24], qj[24], done[CF_MAX_NOTES] = {0};
            int k = cf_match(prev, np, nw, m, pi, qj), x;
            for (x = 0; x < k; x++)
                if (!done[qj[x]]) {
                    done[qj[x]] = 1;
                    if (prev[pi[x]] == nw[qj[x]])
                        cfp_on(part, nw[qj[x]], vel);
                    else
                        cfp_on_from(part, nw[qj[x]], vel, prev[pi[x]], ticks);
                }
            for (i = 0; i < m; i++)
                if (!done[i])
                    cfp_on(part, nw[i], vel);
        } else {
            for (i = 0; i < m; i++)
                cfp_on(part, nw[i], vel);
        }
    } else if ((mode == 2 || mode == 5) && n) {     /* glide: the voices move (no Glide time: at once, RETRIG Off);
                                                      * 5 (RETRIG Notes): the notes that change strike, gliding */
        uint8_t pi[24], qj[24], used_p[CF_MAX_NOTES] = {0}, used_q[CF_MAX_NOTES] = {0};
        uint8_t from[CF_MAX_NOTES];
        int k = cf_match(old, n, nw, m, pi, qj), x;
        /* every voice that moves is found before any moves (two voices may pass through the same note) */
        voice_t *vv[CF_MAX_NOTES];
        for (i = 0; i < n; i++) {
            vv[i] = trk_voice_of(&trk[part], old[i]);
            from[i] = old[i];
        }
        for (x = 0; x < k; x++) {
            int a = pi[x], b = qj[x];
            if (!used_p[a] && !used_q[b]) {          /* the first pairing of this old voice: it moves */
                used_p[a] = used_q[b] = 1;
                if (from[a] != nw[b] && mode == 5) {    /* the old note fades, the new one strikes out of it */
                    if (vv[a])
                        vv[a]->gate = 0, vv[a]->stage = 4;
                    else
                        trk_note_off(&trk[part], from[a]);
                    cfp_midi(0x80u, cfp_ch(part), from[a], 0);
                    cfp_on_from(part, nw[b], vel, from[a], ticks);
                } else if (from[a] != nw[b]) {
                    if (vv[a]) {
                        trk_retune(&trk[part], vv[a], nw[b], ticks);
                    } else {
                        /* no held voice for the old note: its note-on may still wait to start (voice.c vsq: a
                         * stolen voice's fade, a block; xp: an engine switch). Without this it would start a moment
                         * later and never be let go: the stuck note of a fast chord change (the owner, 2026-10-15) */
                        trk_note_off(&trk[part], from[a]);
                        trk_note_on(&trk[part], nw[b], vel);
                    }
                    cfp_midi(0x80u, cfp_ch(part), from[a], 0);
                    cfp_midi(0x90u, cfp_ch(part), nw[b], vel);
                }
            } else if (used_p[a] && !used_q[b]) {    /* a split: a new voice out of this old note */
                used_q[b] = 1;
                cfp_on_from(part, nw[b], vel, from[a], ticks);
            } else if (!used_p[a] && used_q[b]) {    /* a merge: this old voice goes into a note another one reaches.
                                                      * It slides there and lets go on the way (RETRIG Notes: fades);
                                                      * let go where it was, its tail stood at the old pitch: a chord
                                                      * moving down kept its top note (the owner, 2026-10-19: "глайд
                                                      * вниз хуже, чем вверх") */
                used_p[a] = 1;
                if (vv[a] && !bass && mode != 5) {
                    if (from[a] != nw[b])
                        trk_retune(&trk[part], vv[a], nw[b], ticks);
                    vv[a]->gate = 0, vv[a]->stage = 3;
                } else if (vv[a]) {
                    vv[a]->gate = 0, vv[a]->stage = 4;
                } else {
                    trk_note_off(&trk[part], from[a]);
                }
                cfp_midi(0x80u, cfp_ch(part), from[a], 0);
            }
        }
        for (i = 0; i < n; i++)                      /* merged away: the extra old voices let go */
            if (!used_p[i]) {
                if (vv[i])
                    vv[i]->gate = 0, vv[i]->stage = bass ? 4 : 3;   /* (the bass: at once, it is monophonic) */
                else
                    trk_note_off(&trk[part], from[i]);  /* (still waiting to start: it must not start now) */
                cfp_midi(0x80u, cfp_ch(part), from[i], 0);
            }
        for (i = 0; i < m; i++)                      /* (a target nobody reached: plainly) */
            if (!used_q[i])
                cfp_on(part, nw[i], vel);
    } else {                                         /* re-voiced under a held root: the shared tones hold */
        for (i = 0; i < n; i++)
            if (!has(nw, m, old[i]))
                cfp_off(part, old[i]);
        if (bass && m)
            cfp_bass_mono();
        for (i = 0; i < m; i++)
            if (!has(old, n, nw[i]))
                cfp_on(part, nw[i], vel);
    }
    if (n && !m) {                                   /* let go: remember what sounded, for Always */
        uint8_t *prev = bass ? cfp.lastb : cfp.last;
        int lim = bass ? 2 : CF_MAX_NOTES;
        for (i = 0; i < n && i < lim; i++)
            prev[i] = old[i];
        if (bass)
            cfp.nlb = (uint8_t)(n < 2 ? n : 2);
        else
            cfp.nl = (uint8_t)n;
        cfp.last_ms = cf_now_ms;
    }
    for (i = 0; i < m; i++)
        old[i] = nw[i];
    *pn = (uint8_t)m;
}

#include "cf_arp.c"                                  /* (the arpeggiator: cfa_chord, cfa_tick, cfa_config) */

/* the bass notes of a chord (the keys' model's, or a sequencer step's); _calc: whatever HOME's bass switch says */
static int cfp_bass_calc(const cf_chord_out_t *o, uint8_t out[2])
{
    int n = 0, b;
    if (!o->sounding || o->single || o->bass_pc < 0)
        return 0;
    /* an octave under the root as played (the owner's 0), so the bass goes the way the keys go (it used to sit in
     * C2 .. B2 by pitch class: B up to C fell eleven semitones); a bass degree other than the root: its nearest note */
    b = (int)o->root_note - 12 + 12 * cfp_cfg.bass_oct;
    b += ((o->bass_pc - (b % 12 + 12) % 12 + 18) % 12) - 6;
    while (b < 12)                                   /* (only the MIDI range folds it: a fold is a jump) */
        b += 12;
    while (b > 96)
        b -= 12;
    out[n++] = (uint8_t)b;
    if (cfp_cfg.octaver && b + 12 <= 120)       /* the octaver: the same note an octave up */
        out[n++] = (uint8_t)(b + 12);
    return n;
}
static int cfp_bass_notes_of(const cf_chord_out_t *o, uint8_t out[2])
{
    return cfp_cfg.bass_on ? cfp_bass_calc(o, out) : 0;
}
static int cfp_bass_notes(uint8_t out[2]) { return cfp_bass_notes_of(&cfc.out, out); }
/* the keys on the sequencer's BASS: its bass alone; a single note (no chord): that note, as the track writes it */
static int cfp_bass_alone(uint8_t out[2])
{
    const cf_chord_out_t *o = &cfc.out;
    int n = cfp_bass_calc(o, out);
    if (!n && o->sounding && o->n) {
        out[0] = o->note[0];
        n = 1;
    }
    return n;
}

/* a note the keys' player sounds (the sequencer lets such a note go only when the keys let it go) */
static int cfp_holds(uint32_t part, uint32_t note)
{
    uint32_t i;
    if (part != cfp_cp) {
        if (part != 1u)
            return 0;
        for (i = 0; i < cfp.nb; i++)
            if (cfp.bass[i] == note)
                return 1;
        return 0;
    }
    if (cfa_k.on) {                                  /* (the keys' arpeggiator's notes: the ones that sound; cfa_k by
                                                      * name: a sequencer's may be the one in hand) */
        if (cfa_k.cur == note)
            return 1;
        for (i = 0; i < cfa_k.sn; i++)
            if (cfa_k.son[i] && cfa_k.snote[i] == note)
                return 1;
        return 0;
    }
    for (i = 0; i < cfp.n; i++)
        if (cfp.note[i] == note)
            return 1;
    return 0;
}

/* the same notes (a key that changed nothing that sounds: a Lock button let go): no new strike */
static int cfp_same(const uint8_t *a, int n, const uint8_t *b, int m)
{
    int i;
    if (n != m)
        return 0;
    for (i = 0; i < n; i++)
        if (!has(b, m, a[i]))
            return 0;
    return 1;
}

/* the parts follow the chord model. how: 0 a fresh strike (a root key went down), 1 a change under held keys */
static void cfp_sync(int how)
{
    const cf_chord_out_t *o = &cfc.out;
    uint8_t nb[2];
    int m = o->sounding && !cfp_bo ? o->n : 0;   /* (the bass alone: no chord) */
    int mb = cfp_cp ? 0 : cfp_bo ? cfp_bass_alone(nb) : cfp_bass_notes(nb);   /* (the LAYER: no bass) */
    /* RETRIG Off: a chord changed while the last one still sounds under held keys (another chord button, another
     * root) moves its voices there with no new attack, at the Glide time (0: at once) */
    int glide = (cfp_glide_g() || cfp_cfg.retrig != CF_RT_STRIKE) && cfp.n && m;
    /* Always: a chord struck after a release glides from the last one, if that was within a few seconds */
    int always = cfp_glide_g() && cfp_cfg.glide_always && !cfp.n && m && cfp.nl &&
                 (uint32_t)(cf_now_ms - cfp.last_ms) < 4000u;
    /* RETRIG Chord: a chord changed while one sounds strikes all its notes again, the shared ones too (the owner,
     * 2026-10-15: the whole chord, not just the tones that change), each sliding from the last chord's at the Glide time */
    int strike = cfp_cfg.retrig == CF_RT_CHORD && !cfp_same(cfp.note, cfp.n, o->note, m);
    int notes = cfp_cfg.retrig == CF_RT_STRIKE ? 5 : 2;   /* RETRIG Notes: with a Glide time, the changed notes strike */
    /* single notes (no chord button, or Off) with a Glide time: a new note slides from the last one struck, while one
     * is held (Legato) or a few seconds after (Always); the held ones stay (the owner, 2026-10-19: they glided not at
     * all, the chords only) */
    int sglide = o->single && cfp_glide_g() && m && cfp.slast &&
                 (cfp.n || (cfp_cfg.glide_always && (uint32_t)(cf_now_ms - cfp.last_ms) < 4000u));
    /* the voices are moved only on a polyphonic sound: a mono, legato or unison one (the LAYER's Glide page) keeps its
     * notes in voice.c's stack, which moving them went round: notes stuck (a review, 2026-10-19); it glides itself */
    int mv = trk[cfp_cp].p[P_VOICE] == V_POLY;
    int mode = sglide && mv ? 6 : strike && cfp.n && m && !o->single && mv ? 4 : glide && !o->single && mv ? notes :
               always && !o->single && mv ? 3 : how == 0 ? 0 : 1;
    int bmode = strike && cfp.nb && mb ? 4 : glide && cfp.nb && mb ? notes : always && !cfp.nb && mb && cfp.nlb ? 3 :
                how == 0 ? 0 : 1;
    if (m && (!cfp.n || how == 0))
        cfp.onset++;
#ifdef CF_TRACE                                      /* (the headless runner, -DCF_TRACE: every change) */
    {
        int q;
        fprintf(stderr, "%6u sync how %d mode %d:", (unsigned)cf_now_ms, how, mode);
        for (q = 0; q < m; q++)
            fprintf(stderr, " %u", o->note[q]);
        fprintf(stderr, "\n");
    }
#endif
    if (cfa_chord(o, m)) {                    /* ARP: the arpeggiator plays part 0; its notes are the chord */
        int i;
        for (i = 0; i < m; i++)
            cfp.note[i] = o->note[i];
        cfp.n = (uint8_t)m;
    } else {
        uint8_t was[CF_MAX_NOTES], nw = cfp.n;
        int i, j;
        for (i = 0; i < nw; i++)
            was[i] = cfp.note[i];
        cfp_change(cfp_cp, cfp.note, &cfp.n, o->note, m, mode);
        if (!o->single && m)                         /* (a chord: single notes after it start afresh) */
            cfp.slast = 0;
        for (i = 0; o->single && i < m; i++) {       /* (the note struck now: the next one slides from it) */
            for (j = 0; j < nw && was[j] != o->note[i]; j++)
                ;
            if (j == nw)
                cfp.slast = o->note[i];
        }
    }
    if (!(cfa.latched && !mb))                       /* (LATCH: the bass held under the arpeggio too) */
        cfp_change(1, cfp.bass, &cfp.nb, nb, mb, bmode);
    cfp.serial = o->serial;
}

#define CFP_GATHER_MS 50u                            /* a button this soon after a root is still that chord's */
static void cfp_key(int key, int down)
{
    int idx, role = key >= CF_KEY_EXT ? CF_ROLE_ROOT : cf_key_role(cfc.cfg.algo, key, &idx);   /* (an outside root) */
    /* RETRIG Wait: a button under held keys waits for the next root; but one pressed or let go just after a root went
     * down (fingers are never quite together) still makes that root's chord */
    cfc.keep = cfp_cfg.retrig == CF_RT_WAIT &&
               (role != CF_ROLE_BUTTON || (uint32_t)(cf_now_ms - cfp.root_ms) >= CFP_GATHER_MS);
    if (role == CF_ROLE_ROOT && down)
        cfp.root_ms = cf_now_ms;
    if (!cf_chord_key(&cfc, key, down))
        return;
    cfp_sync(role == CF_ROLE_ROOT && down ? 0 : 1);
}

static void cfp_config(void)                         /* CFE_CONFIG: the UI's settings */
{
    uint8_t was = cfa.on, latched = cfa.latched, i, moved = 0;
    if (cfp_cfg.cpart != cfp_cp || cfp_cfg.bass_only != cfp_bo) {   /* the keys move to another part: what they
                                                     * sound there goes */
        moved = 1;
        cfa_stop();
        for (i = 0; i < cfp.n; i++)
            cfp_off(cfp_cp, cfp.note[i]);
        for (i = 0; i < cfp.nb; i++)
            cfp_off(1, cfp.bass[i]);
        cfp.n = cfp.nb = 0;
        cfp.slast = 0;                               /* (another part: no slide from the old one's note) */
        cfp_cp = (uint8_t)(cfp_cfg.cpart == 2u ? 2u : 0u);
        cfp_bo = cfp_cfg.bass_only;
    }
    cfc.keep = cfp_cfg.retrig == CF_RT_WAIT;
    cf_chord_config(&cfc, &cfc_shadow);
    cfa_config(cfc.out.sounding);
    if (cfa.on != was) {                             /* ARP switched: part 0's notes go to the arpeggiator, or back */
        if (cfa.on)
            for (i = 0; i < cfp.n; i++)
                cfp_off(cfp_cp, cfp.note[i]);
        cfp.n = 0;                                   /* (switched off: cfa_config let its notes go) */
        cfp_sync(1);
    } else if (moved || cfc.out.serial != cfp.serial || latched != cfa.latched) {
        cfp_sync(1);                                 /* (moved: the chord still held, by HOLD or fingers, sounds on the
                                                      * keys' new part; it went silent while HOLD stayed lit) */
    }
}

static void cfp_panic(void)
{
    uint32_t i;
    cfa_stop();
    cf_chord_reset(&cfc);
    cf_chord_config(&cfc, &cfc.cfg);
    for (i = 0; i < cfp.n; i++)
        cfp_off(cfp_cp, cfp.note[i]);
    for (i = 0; i < cfp.nb; i++)
        cfp_off(1, cfp.bass[i]);
    cfp.n = cfp.nb = 0;
    trk_all_off(&trk[0]);
    trk_all_off(&trk[1]);
    trk_all_off(&trk[2]);
    trk_all_off(&trk[3]);
}

static void cfp_init(void)
{
    cf_chord_defaults(&cfc_shadow);
    cf_chord_init(&cfc, &cfc_shadow);
    cfp.n = cfp.nb = 0;
}
