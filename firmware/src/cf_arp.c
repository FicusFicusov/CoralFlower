/* SPDX-License-Identifier: GPL-3.0-only
 * CoralFlower: the arpeggiator, in the audio ISR (the chord player's: included by cf_play.c, before cfp_sync).
 *
 * The stock FM-1's arpeggiator, played on the chord: while ARP is on, part 0 does not hold the chord, it plays its
 * notes one at a time at the tempo (the metronome's), RATE a note, GATE of each step sounding, SWING the second of
 * each pair late. MODE: Up, Down, Inclusive (up and down, the ends twice), Exclusive (up and down, the ends once),
 * Random, Order (the chord's tones as it is built: root, third, fifth ..; single notes: as the keys went down),
 * Repeat (each note twice). OCTAVE 1-4: the pattern goes on that many octaves up. And the Orchid's and ChoralRoot's
 * Strum, up or down: every new chord is played once, its notes RATE ms apart, held until the chord ends.
 * LATCH: the notes go on when the keys are let go; new keys change them. The bass part is left as it is (held under
 * the arpeggio), latched with it.
 * A chord that changes while the pattern runs takes its place in it (the step count goes on); one that starts from
 * silence starts the pattern on its first note, at once. Step times are exact (1/24-beat units against samples x
 * BPM, a 64-bit count: no drift). */

enum { CF_ARP_UP, CF_ARP_DOWN, CF_ARP_INC, CF_ARP_EXC, CF_ARP_RAND, CF_ARP_ORDER, CF_ARP_REPEAT, CF_ARP_STRUM_UP,
       CF_ARP_STRUM_DOWN, CF_ARP_NMODES };
#define CF_ARP_STRUMMING(m) ((m) >= CF_ARP_STRUM_UP)
/* RATE: a step, in 1/24 beat (a quarter note: 24) */
static const uint8_t CF_ARP_UNITS[] = {96, 48, 24, 16, 12, 8, 6, 4, 3};
#define CF_ARP_NRATES ((uint32_t)sizeof CF_ARP_UNITS)
#define CF_ARP_MAX 48u                               /* notes in the pattern: 12 tones x 4 octaves */

typedef struct {
    uint8_t part;                                    /* the part it plays (0xFF: the keys' part, cfp_cp) */
    uint8_t on;                                      /* the player's view of cfp_cfg.arp_on (cfp_config moves it) */
    uint8_t mode;                                    /* the mode the pattern was started in */
    uint8_t n, note[CF_MAX_NOTES], order[CF_MAX_NOTES];   /* the chord: low to high, and in its tones' order */
    uint8_t latched;                                 /* the keys are up, LATCH keeps the chord */
    uint32_t idx;                                    /* steps played since the pattern started */
    uint8_t cur;                                     /* the note sounding (0: none) */
    uint32_t gate_left;                              /* samples until it is let go */
    uint64_t acc;                                    /* samples x BPM x 2400 since the last step */
    uint8_t rnd_last;
    uint32_t rng;
    /* Strum */
    uint8_t sn, si, snote[CF_ARP_MAX], son[CF_ARP_MAX];   /* the notes, how many have started, which sound */
    uint32_t sdue;                                   /* cf_now_ms of the next one */
    /* for the screen */
    uint8_t steps;                                   /* bumped at every note played */
    uint8_t last_note;                               /* the last note played */
    /* locked to the beat (cf_grid on): the next step's place on the grid (1/24 beats), the grid's gen it is on */
    uint32_t next24, gen;
    uint8_t locked;
} cfa_t;
/* the keys' arpeggiator, and the sequencer's CHORDS and LAYER tracks' (ARP in a track's editor: the owner,
 * 2026-10-19, "ARP у дорожки"; the same settings, the ARP page's). The functions below work on cfa, the one cfa_at
 * points to: the keys' but while the sequencer runs one of its own (cf_seqp.c sqp_arp_*), then back at once */
static cfa_t cfa_k = {.part = 0xFF}, cfa_sq[2] = {{.part = 0}, {.part = 2}};
static cfa_t *cfa_at = &cfa_k;
#define cfa (*cfa_at)
static void sqp_off(uint32_t part, uint32_t note);   /* (cf_seqp.c: a note of the sequencer's let go, unless the keys
                                                     * sound it) */
static uint32_t cfa_part(void) { return cfa.part == 0xFFu ? cfp_cp : cfa.part; }

static uint32_t cfa_bpm(void) { return cfp_cfg.bpm >= 30u && cfp_cfg.bpm <= 300u ? cfp_cfg.bpm : 120u; }
static uint32_t cfa_oct(void) { return cfp_cfg.arp.oct >= 1u && cfp_cfg.arp.oct <= 4u ? cfp_cfg.arp.oct : 1u; }

/* the step about to come, in samples x BPM x 2400: the RATE's length, a pair's first stretched by SWING */
static uint64_t cfa_step_len(uint32_t idx)
{
    uint32_t u = CF_ARP_UNITS[cfp_cfg.arp.rate < CF_ARP_NRATES ? cfp_cfg.arp.rate : 4u];
    uint32_t sw = cfp_cfg.arp.swing >= 50u && cfp_cfg.arp.swing <= 75u ? cfp_cfg.arp.swing : 50u;
    uint32_t share = (idx & 1u) ? 2u * (100u - sw) : 2u * sw;   /* of 200 */
    return (uint64_t)FS * 60u * u * share;
}

/* the pattern's n-th note: the chord (low to high, or in Order its tones' order) over OCTAVE octaves, walked as
 * the mode walks it */
static int cfa_pick(uint32_t i)
{
    uint32_t n = cfa.n, oc = cfa_oct(), len = n * oc, k, j;
    const uint8_t *src = cfp_cfg.arp.mode == CF_ARP_ORDER ? cfa.order : cfa.note;
    int nt;
    if (!n)
        return -1;
    switch (cfp_cfg.arp.mode) {
    case CF_ARP_DOWN: k = len - 1u - i % len; break;
    case CF_ARP_INC: k = i % (2u * len); k = k < len ? k : 2u * len - 1u - k; break;
    case CF_ARP_EXC:
        if (len < 3u) {
            k = i % len;
        } else {
            k = i % (2u * len - 2u);
            k = k < len ? k : 2u * len - 2u - k;
        }
        break;
    case CF_ARP_RAND:                                /* (never the same note twice running, when there is a choice) */
        cfa.rng = cfa.rng * 1664525u + 1013904223u;
        k = (cfa.rng >> 16) % len;
        if (len > 1u && k == cfa.rnd_last)
            k = (k + 1u + (cfa.rng >> 8) % (len - 1u)) % len;
        cfa.rnd_last = (uint8_t)k;
        break;
    case CF_ARP_REPEAT: k = (i / 2u) % len; break;
    default: k = i % len; break;
    }
    j = k % n;
    nt = src[j] + 12 * (int)(k / n);
    return nt > 127 ? src[j] : nt;
}

static void cfa_note_off(void)
{
    if (cfa.cur && cfa_at != &cfa_k)
        sqp_off(cfa_part(), cfa.cur);
    else if (cfa.cur)
        cfp_off(cfa_part(), cfa.cur);
    cfa.cur = 0;
}
static void cfa_strum_off(void)
{
    uint32_t i;
    for (i = 0; i < cfa.sn; i++)
        if (cfa.son[i]) {
            if (cfa_at != &cfa_k)
                sqp_off(cfa_part(), cfa.snote[i]);
            else
                cfp_off(cfa_part(), cfa.snote[i]);
            cfa.son[i] = 0;
        }
    cfa.sn = cfa.si = 0;
}
static void cfa_stop(void)                           /* everything the arpeggiator plays lets go */
{
    cfa_note_off();
    cfa_strum_off();
    cfa.n = 0;
    cfa.latched = 0;
}

static void cfa_play(uint32_t note)
{
#ifdef CF_TRACE
    fprintf(stderr, "%6u arp %u\n", (unsigned)cf_now_ms, (unsigned)note);
#endif
    if (cfa_at == &cfa_k || !cfp_holds(cfa_part(), note))
        cfp_on(cfa_part(), note, cfp_cfg.vel);       /* (the sequencer's: not over a note the keys hold) */
    cfa.steps++;
    cfa.last_note = (uint8_t)note;
}
/* the grid's step places: pairs of 2 RATE units, the second SWING late (s: where it falls in the pair) */
static uint32_t cfa_pair(void) { return 2u * CF_ARP_UNITS[cfp_cfg.arp.rate < CF_ARP_NRATES ? cfp_cfg.arp.rate : 4u]; }
static uint32_t cfa_second(void)
{
    uint32_t sw = cfp_cfg.arp.swing >= 50u && cfp_cfg.arp.swing <= 75u ? cfp_cfg.arp.swing : 50u;
    return (cfa_pair() * sw + 50u) / 100u;
}
static uint32_t cfa_next_place(uint32_t g)           /* the first step place after g */
{
    uint32_t p = cfa_pair(), s = cfa_second(), m = g / p, r = g % p;
    return r < s ? m * p + s : (m + 1u) * p;
}

/* one step of the clocked modes; len: its length (samples x BPM x 2400), for its gate */
/* n / d with a 32-bit divisor, without a 64-bit division: the device's toolchain (pi32v2) has no __udivdi3 (nor
 * __divdi3), so no code here may divide a 64-bit value at run time. Long division bit by bit; a quotient that does not
 * fit 32 bits saturates */
static uint32_t cfa_div64(uint64_t n, uint32_t d)
{
    uint32_t hi = (uint32_t)(n >> 32), lo = (uint32_t)n, q = 0, r = hi;
    int i;
    if (!d || hi >= d)
        return 0xFFFFFFFFu;
    for (i = 31; i >= 0; i--) {
        uint32_t top = r >> 31;                  /* (the remainder's 33rd bit: then it is past d for sure) */
        r = r << 1 | (lo >> i & 1u);
        if (top || r >= d) {
            r -= d;
            q |= 1u << i;
        }
    }
    return q;
}

static void cfa_step_n(uint64_t len)
{
    int nt = cfa_pick(cfa.idx);
    uint32_t g = cfp_cfg.arp.gate >= 20u && cfp_cfg.arp.gate <= 100u ? cfp_cfg.arp.gate : 70u;
    cfa_note_off();
    cfa.idx++;
    if (nt < 0)
        return;
    cfa.cur = (uint8_t)nt;
    cfa_play((uint32_t)nt);
    /* the gate in samples: the step's (len / (BPM x 2400)) x GATE % (a full gate lets go just as the next comes) */
    {
        uint32_t st = cfa_div64(len, cfa_bpm() * 2400u);   /* (the step in samples; x GATE % without overflow) */
        cfa.gate_left = st / 100u * g + st % 100u * g / 100u;
    }
    if (g >= 100u)
        cfa.gate_left += CTL;
}
static void cfa_step(void) { cfa_step_n(cfa_step_len(cfa.idx)); }
/* locked: the step at the grid's place now; the next place after it (one closer than a third of a step is joined
 * to this one: a chord a little early is not played twice) */
static void cfa_lock_from(uint32_t g)
{
    uint32_t nx = cfa_next_place(g), u = cfa_pair() / 2u;
    if (nx - g < (u + 2u) / 3u)
        nx = cfa_next_place(nx);
    cfa.next24 = nx;
    cfa.gen = cf_grid.gen;
    cfa.locked = 1;
}

/* a new chord for Strum: its notes low to high (or high to low) over OCTAVE octaves, the first at once */
static void cfa_strum_start(void)
{
    uint32_t oc = cfa_oct(), o, i, k = 0;
    cfa_strum_off();
    for (o = 0; o < oc; o++)
        for (i = 0; i < cfa.n && k < CF_ARP_MAX; i++)
            if (cfa.note[i] + 12u * o <= 127u)
                cfa.snote[k++] = (uint8_t)(cfa.note[i] + 12u * o);
    if (cfp_cfg.arp.mode == CF_ARP_STRUM_DOWN)
        for (i = 0; i < k / 2u; i++) {
            uint8_t t = cfa.snote[i];
            cfa.snote[i] = cfa.snote[k - 1u - i];
            cfa.snote[k - 1u - i] = t;
        }
    cfa.sn = (uint8_t)k;
    cfa.si = 0;
    cfa.sdue = cf_now_ms;
}

/* the chord the keys ask for now (cfp_sync): m notes, or none (the keys let go). Returns 1 when the arpeggiator
 * took it (part 0's own notes are then not played) */
static int cfa_chord(const cf_chord_out_t *o, int m)
{
    int i, j, fresh;
    if (!cfa.on)
        return 0;
    if (!m) {
        if (cfp_cfg.arp.latch && cfa.n) {            /* LATCH: the chord goes on */
            cfa.latched = 1;
            return 1;
        }
        cfa_stop();
        return 1;
    }
    fresh = !cfa.n || cfa.latched;
    cfa.latched = 0;
    cfa.n = (uint8_t)(m > CF_MAX_NOTES ? CF_MAX_NOTES : m);
    for (i = 0; i < cfa.n; i++)
        cfa.note[i] = o->note[i];
    if (o->single) {                                 /* Order: single notes as the keys went down */
        int k = 0;
        for (i = 0; i < cfc.nroots && k < cfa.n; i++) {
            int nt = cf_key_root_note(&cfc.cfg, cfc.root_key[i]);
            for (j = 0; j < cfa.n; j++)
                if (cfa.note[j] == nt)
                    break;
            if (nt >= 0 && j < cfa.n)
                cfa.order[k++] = (uint8_t)nt;
        }
        for (; k < cfa.n; k++)
            cfa.order[k] = cfa.note[k];
    } else {                                         /* Order: the chord's tones from its root up (root, 3rd, 5th ..) */
        for (i = 0; i < cfa.n; i++)
            cfa.order[i] = cfa.note[i];
        for (i = 1; i < cfa.n; i++) {
            uint8_t v = cfa.order[i];
            int kv = (v - o->root_pc + 120) % 12;
            for (j = i - 1; j >= 0 && (cfa.order[j] - o->root_pc + 120) % 12 > kv; j--)
                cfa.order[j + 1] = cfa.order[j];
            cfa.order[j + 1] = v;
        }
    }
    if (CF_ARP_STRUMMING(cfp_cfg.arp.mode)) {
        cfa_strum_start();                           /* every new chord strummed afresh */
    } else if (fresh) {                              /* from silence: its first note now (locked: then on the beat) */
        cfa.idx = 0;
        cfa.acc = 0;
        if (cf_grid.on) {
            cfa_lock_from(cf_grid.g24);
            cfa_step_n((uint64_t)(cfa.next24 - cf_grid.g24) * FS * 60u * 100u);
        } else {
            cfa.locked = 0;
            cfa_step();
        }
    }
    cfa.mode = cfp_cfg.arp.mode;
    return 1;
}

/* every audio block: the clocked steps and their gates, the strum's next notes */
static void cfa_tick(uint32_t n)
{
    if (!cfa.on || !cfa.n)
        return;
    if (CF_ARP_STRUMMING(cfp_cfg.arp.mode)) {
        uint32_t gap = cfp_cfg.arp.strum_ms >= 5u ? cfp_cfg.arp.strum_ms : 40u;
        while (cfa.si < cfa.sn && (int32_t)(cf_now_ms - cfa.sdue) >= 0) {
            cfa.son[cfa.si] = 1;
            cfa_play(cfa.snote[cfa.si]);
            cfa.si++;
            cfa.sdue += gap;
        }
        return;
    }
    if (cfa.cur) {
        if (cfa.gate_left <= n)
            cfa_note_off();
        else
            cfa.gate_left -= n;
    }
    if (cf_grid.on) {                                /* locked to the beat: the steps on the grid's places */
        uint32_t g = cf_grid.g24;
        if (!cfa.locked || cfa.gen != cf_grid.gen || cfa.next24 > g + 4u * cfa_pair()) {
            cfa_lock_from(g);                        /* (the grid started again, or the lock is new: its next place) */
            return;
        }
        if (g >= cfa.next24) {
            uint32_t nx = cfa_next_place(g);
            cfa_step_n((uint64_t)(nx - g) * FS * 60u * 100u);
            cfa.next24 = nx;
        }
        return;
    }
    if (cfa.locked) {                                /* (the clock stopped: free from here, a step's wait) */
        cfa.locked = 0;
        cfa.acc = 0;
    }
    cfa.acc += (uint64_t)n * cfa_bpm() * 2400u;
    /* the wait is the length of the step just played (idx - 1: a pair's first is the long one with SWING); at most
     * one step a block, a step being longer than a block */
    if (cfa.idx && cfa.acc >= cfa_step_len(cfa.idx - 1u)) {
        cfa.acc -= cfa_step_len(cfa.idx - 1u);
        cfa_step();
    }
}

/* the settings changed (cfp_config): ARP switched, the mode moved between Strum and the clocked ones, LATCH off
 * with the keys up */
static void cfa_config(int keys_down)
{
    if (cfp_cfg.arp_on != cfa.on) {
        cfa_stop();
        cfa.on = cfp_cfg.arp_on;
        cfa.rng = 0x2545F491u ^ cf_now_ms;
        return;                                      /* (cfp_config re-plays the chord the keys hold) */
    }
    if (!cfa.on)
        return;
    if (CF_ARP_STRUMMING(cfp_cfg.arp.mode) != CF_ARP_STRUMMING(cfa.mode)) {
        uint8_t n = cfa.n, latched = cfa.latched;
        cfa_note_off();
        cfa_strum_off();
        cfa.n = n;
        cfa.latched = latched;
        cfa.mode = cfp_cfg.arp.mode;
        if (cfa.n && CF_ARP_STRUMMING(cfa.mode))
            cfa_strum_start();
        else if (cfa.n) {
            cfa.idx = 0;
            cfa.acc = 0;
            cfa_step();
        }
    }
    if (!cfp_cfg.arp.latch && cfa.latched && !keys_down)
        cfa_stop();
}
