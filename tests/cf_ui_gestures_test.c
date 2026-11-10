/* SPDX-License-Identifier: GPL-3.0-only */
/* Exercise the sequencer through the browser emulator's button/key/encoder API.
 * Each named scenario runs in a fresh process. The firmware state is read only
 * for assertions; setup and actions use the same inputs as cf_headless.c.
 * See .github/workflows/check.yml for the build and scenario list. */
#include "../tools/emu/web/emu_web.c"

static unsigned test_checks, test_failed;
static uint32_t test_buttons;
#define CHECK(condition, message) do { \
    test_checks++; \
    if (!(condition)) { \
        test_failed++; \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, message); \
    } \
} while (0)

static void hold_button(unsigned button, int down)
{
    if (down)
        test_buttons |= 1u << button;
    else
        test_buttons &= ~(1u << button);
    web_buttons(test_buttons);
    web_step(20);
}

static void tap_button(unsigned button)
{
    hold_button(button, 1);
    web_step(40);
    hold_button(button, 0);
    web_step(40);
}

static void turn_knob(unsigned knob, int steps)
{
    web_enc((int)knob, steps);
    web_step(40);
}

static void tap_key(unsigned key)
{
    web_keys(1u << key);
    web_step(20);
    web_keys(0);
    web_step(20);
}

static void undo_or_redo(void)
{
    hold_button(EMU_B_REC, 1);
    web_step(1300);
    hold_button(EMU_B_REC, 0);
    web_step(200);
}

static void open_rhythm_page(void)
{
    tap_button(EMU_B_HOME);
    tap_button(EMU_B_SEQ);
    turn_knob(EMU_E_SELECT, 1);
    tap_button(EMU_B_SEQ);
    CHECK(cu.scr == SCR_DRUM, "rhythm page opened");
}

static void check_rhythm_copy(unsigned target, int cancel_button)
{
    sq_pat_t before[SQ_NPAT], copied;
    tap_button(EMU_B_SEQ);
    tap_button(EMU_B_SEQ);
    tap_button(EMU_B_EDIT);
    tap_key(7);                                  /* C4 */
    web_step(800);
    CHECK(sq_st.pat[0].nev == 1, "A contains the chord played in its editor");
    tap_button(EMU_B_SAVE);
    hold_button(EMU_B_OCTUP, 1);
    turn_knob(EMU_E_SELECT, 1);
    hold_button(EMU_B_OCTUP, 0);
    web_step(800);
    CHECK(squ.pat == 1 && sq_st.pat[1].nev == 1, "A copied to B, B selected");
    memcpy(before, sq_st.pat, sizeof before);

    open_rhythm_page();
    hold_button(EMU_B_OCTUP, 1);
    turn_knob(EMU_E_SELECT, (int)target + 1);
    CHECK(dmc.on && dmc.tgt == target, "rhythm copy chose the requested target");
    if (cancel_button >= 0)
        tap_button((unsigned)cancel_button);
    hold_button(EMU_B_OCTUP, 0);
    web_step(800);
    if (cancel_button >= 0) {
        CHECK(!memcmp(before, sq_st.pat, sizeof before), "cancelled copy preserves every pattern");
        undo_or_redo();
        CHECK(!sq_has(1) && !memcmp(&before[0], &sq_st.pat[0], sizeof before[0]),
              "cancelled copy preserves the preceding A-to-B undo action");
        return;
    }

    CHECK(sq_drums(&sq_st.pat[target]), "rhythm copied into the target");
    CHECK(sq_st.pat[target].nev == before[target].nev &&
          !memcmp(sq_st.pat[target].ev, before[target].ev, before[target].nev * sizeof before[target].ev[0]),
          "rhythm copy preserves the target's notes");
    for (unsigned i = 0; i < SQ_NPAT; i++)
        if (i != target)
            CHECK(!memcmp(&before[i], &sq_st.pat[i], sizeof before[i]), "copy preserves other patterns");
    copied = sq_st.pat[target];
    undo_or_redo();
    CHECK(!memcmp(before, sq_st.pat, sizeof before), "undo restores every pattern to before the rhythm copy");
    undo_or_redo();
    CHECK(!memcmp(&copied, &sq_st.pat[target], sizeof copied), "redo restores the copied rhythm exactly");
    for (unsigned i = 0; i < SQ_NPAT; i++)
        if (i != target)
            CHECK(!memcmp(&before[i], &sq_st.pat[i], sizeof before[i]), "undo/redo preserves other patterns");
}

enum { CHANGE_BARS, CHANGE_POSITION, CHANGE_DRUM };
static void check_clear_cue(unsigned change)
{
    sq_pat_t before, cleared;
    open_rhythm_page();
    tap_button(EMU_B_EDIT);
    CHECK(cu.scr == SCR_SQED && squ.trk == SQ_DRUMS && squ.lane == 0, "Kick editor opened");
    turn_knob(EMU_E_K3, 1);                        /* two active bars */
    hold_button(EMU_B_OCTUP, 1);
    turn_knob(EMU_E_SELECT, 1);                    /* copy the rhythm's bar into bar 2 */
    hold_button(EMU_B_OCTUP, 0);
    turn_knob(EMU_E_SELECT, -1);
    turn_knob(EMU_E_K3, -1);                       /* bar 2 stays stored outside the one-bar window */
    web_step(800);
    before = sq_st.pat[0];
    CHECK(before.hit[0][0] && before.hit[0][1] && before.hit[1][0] && before.hit[1][1],
          "both bars contain Kick and Snare hits before clearing");
    hold_button(EMU_B_SEL, 1);
    web_step(650);
    CHECK(cu.pop == POP_MSG && str_eq(cu.msg, "let go: clear Kick, bar 1\nturn [SELECT]: erase"),
          "initial cue names Kick in bar 1");

    unsigned lane = 0, first = 0, end = 1;
    const char *expected;
    if (change == CHANGE_BARS) {
        turn_knob(EMU_E_K3, 1);
        end = 2;
        expected = "let go: clear Kick, bars 1-2\nturn [SELECT]: erase";
    } else if (change == CHANGE_POSITION) {
        turn_knob(EMU_E_K4, 1);
        first = 1;
        end = 2;
        expected = "let go: clear Kick, bar 2\nturn [SELECT]: erase";
    } else {
        tap_key(3);                               /* G#3, the Snare key */
        lane = 1;
        expected = "let go: clear Snare, bar 1\nturn [SELECT]: erase";
    }
    web_step(150);
    CHECK(squ.lane == lane && sq_ts(sq_cur(), SQ_DRUMS) == first &&
          sq_tbars(sq_cur(), SQ_DRUMS) == end - first, "input changed the actual clear scope");
    CHECK(cu.pop == POP_MSG && str_eq(cu.msg, expected), "held SEL cue follows the new clear scope");
    CHECK(!memcmp(before.hit, sq_st.pat[0].hit, sizeof before.hit), "holding SEL has not cleared any hits");
    hold_button(EMU_B_SEL, 0);
    web_step(200);
    for (unsigned l = 0; l < SQ_NLANE; l++)
        for (unsigned b = 0; b < SQ_BARS; b++) {
            uint32_t want = l == lane && b >= first && b < end ? 0 : before.hit[l][b];
            CHECK(sq_st.pat[0].hit[l][b] == want, "release clears exactly the announced drum and bars");
        }
    cleared = sq_st.pat[0];
    undo_or_redo();
    CHECK(!memcmp(before.hit, sq_st.pat[0].hit, sizeof before.hit), "undo restores all drum hits");
    undo_or_redo();
    CHECK(!memcmp(&cleared, &sq_st.pat[0], sizeof cleared), "redo restores the clear and its range");
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "one gesture scenario is required (see check.yml)\n");
        return 2;
    }
    web_boot();
    web_step(1900);
    if (str_eq(argv[1], "copy-other")) check_rhythm_copy(0, -1);
    else if (str_eq(argv[1], "copy-same")) check_rhythm_copy(1, -1);
    else if (str_eq(argv[1], "copy-empty")) check_rhythm_copy(2, -1);
    else if (str_eq(argv[1], "copy-cancel-play")) check_rhythm_copy(0, EMU_B_PLAY);
    else if (str_eq(argv[1], "copy-cancel-rec")) check_rhythm_copy(0, EMU_B_REC);
    else if (str_eq(argv[1], "sel-bars")) check_clear_cue(CHANGE_BARS);
    else if (str_eq(argv[1], "sel-position")) check_clear_cue(CHANGE_POSITION);
    else if (str_eq(argv[1], "sel-drum")) check_clear_cue(CHANGE_DRUM);
    else {
        fprintf(stderr, "unknown gesture scenario: %s\n", argv[1]);
        return 2;
    }
    printf("%s: %u checks, %u failed\n", argv[1], test_checks, test_failed);
    return test_failed ? 1 : 0;
}
