/* SPDX-License-Identifier: GPL-3.0-only */
/* Exercise the production USB/TRS parsers and their shared queue, without hardware.
 * cc -std=c99 -O2 -Wall -Wextra -Wno-unused-function -Wno-int-to-pointer-cast \
 *    tests/midi_spp_test.c -o /tmp/midi_spp_test && /tmp/midi_spp_test
 * MIDI_SPP_EMU additionally tests the native CoreMIDI byte parser without opening
 * MIDI ports (macOS, SDL headers, -framework CoreMIDI -framework CoreFoundation).
 * MIDI 1.0: F2 carries LSB then MSB; system common cancels running status, while
 * real-time may interrupt any message. No transport behavior is tested here. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define RING_PUBLISH() ((void)0)
static void fm1_delay_ms(uint32_t ms) { (void)ms; }
#include "../firmware/src/usb.c"
#include "../firmware/src/midi_uart.c"
#ifdef MIDI_SPP_EMU
#include "../tools/emu/emu_midi.c"
#endif

static unsigned checks, failed;
static const char *scenario;
#define CHECK(condition, message) do { \
    checks++; \
    if (!(condition)) { \
        failed++; \
        fprintf(stderr, "FAIL %s:%d [%s]: %s\n", __FILE__, __LINE__, scenario, message); \
    } \
} while (0)

static void reset_input(const char *name)
{
    scenario = name;
    memset(&usb, 0, sizeof usb);
    memset(&um, 0, sizeof um);
    mi_w = mi_r = 0;
    midi_in_overflow = 0;
    fm1_ms = 0;
#ifdef MIDI_SPP_EMU
    rq_w = rq_r = 0;
    held = 0;
    run_st = need = have = sxn = in_sx = 0;
#endif
}

static void expect_event(uint32_t pkt, uint32_t source, uint32_t ms)
{
    CHECK(mi_w != mi_r, "expected event missing from midi_in_q");
    if (mi_w == mi_r)
        return;
    uint32_t at = mi_r++ % MQ;
    CHECK(midi_in_q[at] == pkt, "event has wrong CIN/status/data or order");
    CHECK(midi_in_source[at] == source, "event has wrong source");
    CHECK(midi_in_ms[at] == ms, "event has wrong receive timestamp");
}

static void expect_empty(void)
{
    CHECK(mi_w == mi_r, "unexpected/duplicate event in midi_in_q");
}

/* source 2 = physical TRS; source 1 = native emulator -> USB MIDI parser. */
static void feed(uint32_t source, const uint8_t *bytes, unsigned n)
{
    while (n--) {
        fm1_ms++;
        if (source == 2u)
            um_byte(*bytes++);
#ifdef MIDI_SPP_EMU
        else {
            uint32_t pkt;
            byte_in(*bytes++);
            while (emu_midi_take(&pkt))
                midi_in_event(pkt);
        }
#endif
    }
}
#define FEED(source, ...) do { \
    const uint8_t bytes[] = {__VA_ARGS__}; \
    feed(source, bytes, sizeof bytes); \
} while (0)

static void check_usb(void)
{
    reset_input("USB SPP + realtime order");
    fm1_ms = 123;
    midi_in_event(0x0000F80Fu);
    midi_in_event(0x4523F203u);
    midi_in_event(0x0000FB0Fu);
    expect_event(0x0000F80Fu, 1, 123);
    expect_event(0x4523F203u, 1, 123);
    expect_event(0x0000FB0Fu, 1, 123);
    expect_empty();

    reset_input("USB SPP boundaries and cable number");
    midi_in_event(0x0000F203u);
    midi_in_event(0x7F7FF213u);
    expect_event(0x0000F203u, 1, 0);
    expect_event(0x7F7FF213u, 1, 0);
    expect_empty();

    reset_input("USB invalid SPP");
    const uint32_t invalid[] = {0x0080F203u, 0x8000F203u, 0x0102F202u,
                               0x0102F20Fu, 0x0102F103u, 0x01029003u};
    for (unsigned i = 0; i < sizeof invalid / sizeof invalid[0]; i++)
        midi_in_event(invalid[i]);
    expect_empty();
}

static void check_byte_stream(uint32_t source)
{
    reset_input(source == 2 ? "TRS SPP realtime interleave" : "emulator SPP realtime interleave");
    FEED(source, 0xF2, 0x23);
    expect_empty();                                /* no partial SPP */
    FEED(source, 0xF8, 0xF9, 0xFD, 0xFE, 0xFF, 0x45);
    expect_event(0x0000F80Fu, source, 3);
    expect_event(0x4523F203u, source, 8);
    FEED(source, 1, 2);                            /* F2 must not become running status */
    expect_empty();

    reset_input(source == 2 ? "TRS channel running status" : "emulator channel running status");
    FEED(source, 0x92, 60, 100, 61, 0xF8, 101, 0xF2, 7, 1, 62, 102);
    expect_event(0x643C9209u, source, 3);
    expect_event(0x0000F80Fu, source, 5);
    expect_event(0x653D9209u, source, 6);
    expect_event(0x0107F203u, source, 9);
    expect_empty();                                /* SPP cancels the earlier 0x92 */
    FEED(source, 0xC1, 12, 13);                     /* one-data-byte running status still works */
    expect_event(0x000CC10Cu, source, 13);
    expect_event(0x000DC10Cu, source, 14);
    expect_empty();

    reset_input(source == 2 ? "TRS SPP interrupted/restarted" : "emulator SPP interrupted/restarted");
    FEED(source, 0xF2, 1, 0x90, 60, 100, 0xF2, 2, 0xF2, 3, 4);
    expect_event(0x643C9009u, source, 5);
    expect_event(0x0403F203u, source, 10);
    expect_empty();

    for (unsigned status = 0xF0; status < 0xF8; status++) {
        if (status == 0xF2)
            continue;
        reset_input(source == 2 ? "TRS system common/SysEx cancels partial SPP" :
                                 "emulator system common/SysEx cancels partial SPP");
        FEED(source, 0xF2, 1, status, 2, 3, 0xF7, 4, 5);
        expect_empty();
        FEED(source, 0xF2, 0x7F, 0x7F);
        expect_event(0x7F7FF203u, source, 11);
        expect_empty();
    }

    reset_input(source == 2 ? "TRS SPP terminates SysEx" : "emulator SPP terminates SysEx");
    FEED(source, 0xF0, 0x7D, 0xF2, 0, 0);
    expect_event(0x0000F203u, source, 5);
    expect_empty();
}

static void check_trs_overrun(void)
{
    reset_input("TRS DMA overrun discards partial SPP");
    FEED(2, 0xF2, 7);
    for (unsigned i = 0; i < UM_RING; i++)
        um_ring[i] = 1;
    uart_midi_take(UM_RING + 1u);
    CHECK(midi_in_overflow, "lost DMA bytes must flag a broken stream");
    expect_empty();
    midi_in_overflow = 0;                          /* audio consumer recovers the queue */
    FEED(2, 8, 9);
    expect_empty();
    FEED(2, 0xF2, 10, 11);
    expect_event(0x0B0AF203u, 2, 7);
    expect_empty();
}

int main(void)
{
    check_usb();
    check_byte_stream(2);
    check_trs_overrun();
#ifdef MIDI_SPP_EMU
    check_byte_stream(1);
    printf("MIDI SPP USB/TRS/native emulator: %u checks, %u failed\n", checks, failed);
#else
    printf("MIDI SPP USB/TRS: %u checks, %u failed\n", checks, failed);
#endif
    return failed ? 1 : 0;
}
