/* SPDX-License-Identifier: GPL-3.0-only */
/* Active stream -> lost host / SIE handshakes, with Suspend still pending.
 * Oracle: the IRQ service returns; failed register reads are never published
 * as a frame; DMA-owned data survives; main can recover and reopen the stream.
 * A bounded probe escapes the OLD infinite loop as a test failure. No physical
 * unplug, target timing, or causal claim about the owner's hang is modeled. */
#include <setjmp.h>
#define main protocol_suite_main
#include "cf_usb_audio_protocol_test.c"
#undef main

enum { NORMAL, ROLLOVER, HIGH_FIRST, LOW, HIGH_SECOND, UNSTABLE,
       INDEX_WRITE, CSR_READ, ARM_WRITE, FAULT_COUNT };
static const char *const fault_names[] = {
    "pending suspend", "normal frame rollover", "first high read timeout",
    "low read timeout", "alternating high read success/timeout",
    "unstable frame high byte", "INDEX write timeout", "CSR read timeout", "packet arm timeout"
};
static unsigned fault, accesses, high_reads, injected;
static jmp_buf service_guard;

static void inject(int write, uint32_t reg, uint32_t value)
{
    if (++accesses > 64u) {
        CHECK(0, "USB service did not return: register-access budget exhausted before pending Suspend");
        longjmp(service_guard, 1);
    }
    cf_usb_mock_stuck = 0;
    if (!write && reg == S_FRAME2) {
        high_reads++;
        if (fault == ROLLOVER && high_reads >= 2u) {
            cf_usb_mock_reg_global[S_FRAME2] = 2;
            cf_usb_mock_reg_global[S_FRAME1] = 0;
        }
        if (fault == UNSTABLE)
            cf_usb_mock_reg_global[S_FRAME2] = (uint8_t)(high_reads & 1u);
    }
    if ((!write && reg == S_FRAME2 && fault == HIGH_FIRST && (high_reads & 1u)) ||
        (!write && reg == S_FRAME2 && fault == HIGH_SECOND && !(high_reads & 1u)) ||
        (!write && reg == S_FRAME1 && fault == LOW) ||
        (write && reg == S_INDEX && fault == INDEX_WRITE) ||
        (!write && reg == S_TXCSR1 && fault == CSR_READ) ||
        (write && reg == S_TXCSR1 && value == 1u && fault == ARM_WRITE)) {
        cf_usb_mock_stuck = 1;
        injected++;
    }
}

static void transition(unsigned rate, unsigned kind)
{
    cf_usb_mock_access_hook = 0;
    begin(fault_names[kind]); configure(); set_rate(rate); open_capture();
    take_capture(NULL); tick();
    uint32_t seq = 1; feed_frames(&seq, 256);
    for (unsigned i = 0; i < 510u; i++) {
        uint32_t n = take_capture(NULL) / (UA_CAP_CHANNELS * 2u);
        feed_frames(&seq, n); tick();
    }
    CHECK(usb.up && usb.config && ua.cap_alt && !usb.suspended && !ua.cap_hold,
          "fixture reached an active stream before disconnect");
    CHECK(le16(cf_usb_mock_reg_global + S_FRAME1) == 511u, "fixture reached frame511 before rollover");
    CHECK(cf_usb_mock_reg_ep[2][1] & 1u, "fixture has a DMA-owned packet");
    if (kind == ARM_WRITE) take_capture(NULL); /* the next packet's arm fails */
    else if (kind == INDEX_WRITE) cf_usb_mock_reg_index = 1; /* not already EP2 */
    uint32_t sofs = ua_dbg.sofs, old_frame = ua_frame, sends = cf_usb_mock_dma_sends[2];
    uint8_t old_packet[UA_PACKET];
    memcpy(old_packet, cf_usb_mock_dma_capture, cf_usb_mock_dma_size[2]);
    cf_usb_mock_reg_global[S_INTRUSB] = 1; /* bus-idle event not consumed yet */
    fault = kind; accesses = high_reads = injected = 0;
    cf_usb_mock_access_hook = inject;
    volatile int returned = 0;
    if (!setjmp(service_guard)) { ua_service(); returned = 1; }
    CHECK(returned, "service must return so TIMER5 can reach other work");
    CHECK(cf_usb_mock_reg_global[S_INTRUSB] == 1u, "fixture tests the service before Suspend is consumed");
    if (kind == NORMAL || kind == ROLLOVER) {
        CHECK(usb.up, "healthy frame read must keep USB available");
        if (kind == ROLLOVER) CHECK(ua_frame == 512u && high_reads >= 4u,
                                  "rollover retry must publish a coherent frame512");
    } else {
        CHECK(injected || kind == UNSTABLE, "requested SIE fault was actually injected");
        CHECK(!usb.up, "controller fault must release IRQ service to main recovery");
        CHECK(ua_dbg.sofs == sofs && ua_frame == old_frame,
              "failed frame read must not advance frame accounting");
        if (kind != ARM_WRITE) {
            CHECK(cf_usb_mock_dma_sends[2] == sends, "failed service must not replace pending DMA");
            CHECK(!memcmp(old_packet, cf_usb_mock_dma_tx[2], cf_usb_mock_dma_size[2]),
                  "pending DMA packet must survive failure unchanged");
        }
        /* The submitted snapshot is the oracle; inspect the live bytes even
         * when a timed-out arm leaves TxPktRdy/ownership ambiguous. */
        CHECK(!memcmp(cf_usb_mock_dma_tx[2], cf_usb_mock_dma_capture, cf_usb_mock_dma_size[2]),
              "live DMA bytes must match their submission before recovery");
        if (!usb.up) {
            unsigned before = accesses;
            for (unsigned i = 0; i < 100; i++) ua_service();
            CHECK(accesses == before, "subsequent IRQ services must not retry a dead SIE");
            CHECK(!memcmp(cf_usb_mock_dma_tx[2], cf_usb_mock_dma_capture, cf_usb_mock_dma_size[2]),
                  "subsequent service must preserve live DMA bytes after ambiguous arm");
        }
    }
    printf("DISCONNECT rate=%u case=%s returned=%d accesses=%u faults=%u up=%u\n",
           rate, fault_names[kind], returned, accesses, injected, usb.up);
    cf_usb_mock_access_hook = 0; cf_usb_mock_stuck = 0;
    if (!returned) return; /* already a reported nonreturn, not silent success */
    if (usb.up) {
        poll();
        CHECK(usb.suspended && ua_paused && ua.cap_hold, "pending Suspend must be serviced after return");
    } else {
        usb_retry(fm1_ms + 1001u);
        CHECK(usb.up && !ua.cap_alt, "main retry must reattach with capture stopped");
        cf_usb_mock_reg_global[S_INTRUSB] = 4; poll(); /* fresh host bus reset */
        configure(); set_rate(rate); open_capture();
        take_capture(NULL); tick(); feed_frames(&seq, 256);
        CHECK(usb.config && ua.cap_alt && !ua.cap_hold && (cf_usb_mock_reg_ep[2][1] & 1u),
              "capture can reopen after recovery");
        assert_dma();
    }
}

int main(void)
{
    for (unsigned rate = 44100; rate <= 48000; rate += 3900)
        for (unsigned kind = 0; kind < FAULT_COUNT; kind++) transition(rate, kind);
    printf("USB disconnect: %u checks, %u failed\n", checks, failed);
    return failed ? 1 : 0;
}
