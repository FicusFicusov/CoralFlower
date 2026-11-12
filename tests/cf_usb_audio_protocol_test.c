/* SPDX-License-Identifier: GPL-3.0-only */
/* Production usb.c, including its EP0, MIDI and UAC service, with a register/DMA
 * model in place of the USB HAL. The model cannot establish hardware timing.
 * cc -std=c11 -O2 -Wall -Wextra -Wno-unused-function -Wno-unused-variable \
 *   -Wno-missing-field-initializers -Wno-int-to-pointer-cast \
 *   tests/cf_usb_audio_protocol_test.c -o /tmp/cf_usb_audio_protocol_test
 * /tmp/cf_usb_audio_protocol_test
 * Add -fsanitize=address,undefined -fno-omit-frame-pointer for bounds checks.
 * CF_TEST_CHANNELS=6 is used only to demonstrate the pre-stereo descriptor
 * assertions fail on the inherited implementation, before its stereo FIR builds. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks, failed;
static const char *scenario;
#define CHECK(condition, message) do { \
    checks++; \
    if (!(condition)) { \
        failed++; \
        fprintf(stderr, "FAIL %s:%d [%s]: %s\n", __FILE__, __LINE__, scenario, message); \
    } \
} while (0)

#define CF_USB_MOCK_CHECK(condition, message) CHECK(condition, message)
#include "cf_usb_mock.h"
static uint32_t fake_ticks;
static int blocked;
static uint32_t fm1_ticks(void) { return fake_ticks; }
static void fm1_delay_ms(uint32_t ms) { fake_ticks += ms * 1000u; }
static uint32_t ota_now_ms(void) { return fake_ticks / 1000u; }
static void ota_idle(void) { fake_ticks += 1000u; }

#define FELUCCA_UAC 1
#define FELUCCA_CDC 1
#define FELUCCA_OTA 1
#ifndef CF_TEST_CHANNELS
#define CF_TEST_CHANNELS 2u
#endif
#define UA_CAP_CHANNELS CF_TEST_CHANNELS
#define FM1_TICKS_PER_US 1u
#define UAC_BLOCKED() blocked
#define RING_PUBLISH() ((void)0)
#include "../firmware/src/usb.c"

static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t le24(const uint8_t *p) { return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16; }
static void assert_dma(void)
{
    if (cf_usb_mock_reg_ep[2][1] & 1u)
        CHECK(memcmp(cf_usb_mock_dma_tx[2], cf_usb_mock_dma_capture, cf_usb_mock_dma_size[2]) == 0, "CPU overwrote the packet still owned by DMA");
}
static void poll(void) { usb_poll(); ua_service(); assert_dma(); }
static void begin(const char *name)
{
    scenario = name;
    memset(&usb, 0, sizeof usb); memset(&ua, 0, sizeof ua); memset(&ua_dbg, 0, sizeof ua_dbg);
    memset(cf_usb_mock_dma_sends, 0, sizeof cf_usb_mock_dma_sends); memset(cf_usb_mock_dma_size, 0, sizeof cf_usb_mock_dma_size);
    memset(cf_usb_mock_dma_tx, 0, sizeof cf_usb_mock_dma_tx); memset(cf_usb_mock_dma_rx, 0, sizeof cf_usb_mock_dma_rx);
    memset(cf_usb_mock_reg_global, 0, sizeof cf_usb_mock_reg_global); memset(cf_usb_mock_reg_ep, 0, sizeof cf_usb_mock_reg_ep);
    mi_w = mi_r = mo_w = mo_r = 0; midi_in_overflow = 0;
    so_w = so_r = so_gone = 0; sx_ready = sx_collect = sx_busy = sx_watch = 0;
    ci_w = ci_r = co_w = co_r = 0; cdc.e0_rx = cdc.dtr = cdc.rx_pend = 0;
    ua_off = ua_off_want = 0; ua_armed = ua_tx_bytes = ua_tx_slot = 0;
    ua_rate_pending = ua_frame_valid = ua_paused = 0;
    fake_ticks = 1000; fm1_ms = 1; cf_usb_mock_reg_index = 0; blocked = 0;
    cf_usb_mock_control_stalled = 0; cf_usb_mock_control_size = 0; cf_usb_mock_stuck = 0;
    usb_start();
    CHECK(usb.up && cf_usb_mock_hal_on, "USB failed to attach in model");
}
static void control_rx(const uint8_t *p, uint32_t n, int setup)
{
    CHECK(n <= sizeof ep0buf, "control fixture exceeds EP0 buffer");
    if (n > sizeof ep0buf) return;
    memcpy(ep0buf, p, n);
    cf_usb_mock_reg_ep[0][6] = (uint8_t)n;
    cf_usb_mock_reg_ep[0][1] = (uint8_t)(1u | (setup ? 0x10u : 0u));
    cf_usb_mock_reg_global[S_INTRTX1] |= 1u;
    poll();
}
static int request(uint8_t type, uint8_t req, uint16_t value, uint16_t index, uint16_t length)
{
    uint8_t p[8] = {type, req, (uint8_t)value, (uint8_t)(value >> 8), (uint8_t)index,
                    (uint8_t)(index >> 8), (uint8_t)length, (uint8_t)(length >> 8)};
    cf_usb_mock_control_stalled = 0; cf_usb_mock_control_size = 0;
    control_rx(p, 8, 1);
    for (unsigned i = 0; usb.e0_tx && i < 32u; i++) {
        cf_usb_mock_reg_ep[0][1] = 0; cf_usb_mock_reg_global[S_INTRTX1] |= 1u; poll();
    }
    CHECK(!usb.e0_tx, "EP0 transfer did not finish within bound");
    cf_usb_mock_reg_ep[0][1] = 0;
    return !cf_usb_mock_control_stalled;
}
static void configure(void) { CHECK(request(0, 9, 1, 0, 0), "SET_CONFIGURATION(1) rejected"); }
static void open_capture(void) { CHECK(request(1, 11, 1, 3, 0), "capture alt1 rejected"); }
static void set_rate(uint32_t hz)
{
    uint8_t p[3] = {(uint8_t)hz, (uint8_t)(hz >> 8), (uint8_t)(hz >> 16)};
    CHECK(request(0x22, 1, 0x100, 0x82, 3), "SET_CUR setup rejected");
    control_rx(p, 3, 0);
    CHECK(!cf_usb_mock_control_stalled && !ua_rate_pending, "SET_CUR data rejected or still pending");
}
static void tick(void)
{
    uint16_t f = (uint16_t)((le16(cf_usb_mock_reg_global + S_FRAME1) + 1u) & 2047u);
    cf_usb_mock_reg_global[S_FRAME1] = (uint8_t)f; cf_usb_mock_reg_global[S_FRAME2] = (uint8_t)(f >> 8);
    fm1_ms++; fake_ticks += 1000u; poll();
}
static uint32_t take_capture(uint8_t *out)
{
    CHECK(cf_usb_mock_reg_ep[2][1] & 1u, "capture has no packet armed for host");
    assert_dma();
    uint32_t n = cf_usb_mock_dma_size[2];
    if (out) memcpy(out, cf_usb_mock_dma_tx[2], n);
    cf_usb_mock_reg_ep[2][1] &= (uint8_t)~1u;
    return n;
}

static void descriptors(void)
{
    begin("stereo descriptors through EP0");
    CHECK(request(0x80, 6, 0x100, 0, 18), "device descriptor rejected");
    CHECK(cf_usb_mock_control_size == 18 && le16(cf_usb_mock_control_reply + 12) == 0x0400u, "stereo dual-rate bcdDevice must be 0x0400");
    CHECK(request(0x80, 6, 0x200, 0, 255), "configuration descriptor rejected");
    CHECK(cf_usb_mock_control_size == 211 && le16(cf_usb_mock_control_reply + 2) == cf_usb_mock_control_size, "configuration length differs from data");
    CHECK(cf_usb_mock_control_reply[4] == 4u, "MIDI + capture must expose four interfaces");
    unsigned terminals = 0, formats = 0, capture_eps = 0, midi_eps = 0, iface = 255;
    uint8_t channel_string = 0;
    for (uint32_t i = 0; i < cf_usb_mock_control_size;) {
        const uint8_t *d = cf_usb_mock_control_reply + i;
        CHECK(d[0] >= 2u && i + d[0] <= cf_usb_mock_control_size, "malformed descriptor length");
        if (d[0] < 2u || i + d[0] > cf_usb_mock_control_size) break;
        if (d[1] == 4u) iface = d[2];
        if (iface == 2u && d[1] == 0x24u && d[2] == 2u) {
            terminals++;
            CHECK(d[7] == 2 && le16(d + 8) == 3, "input terminal must be stereo front-left/front-right");
            channel_string = d[10];
        }
        if (iface == 3u && d[1] == 0x24u && d[2] == 2u) {
            formats++;
            CHECK(d[0] == 14 && d[4] == 2 && d[5] == 2 && d[6] == 16 && d[7] == 2,
                  "format must be two-channel PCM16 with two rates");
            CHECK(le24(d + 8) == 44100 && le24(d + 11) == 48000, "format rates differ from 44.1/48 kHz");
        }
        if (d[1] == 5u) {
            if (iface == 3u) {
                capture_eps++;
                CHECK(d[2] == 0x82 && d[3] == 5 && d[6] == 1, "capture must have only asynchronous IN endpoint");
                CHECK(le16(d + 4) >= 196u && le16(d + 4) <= 232u, "stereo reservation must be 196..232 bytes");
            } else if (iface == 1u) midi_eps++;
        }
        i += d[0];
    }
    CHECK(terminals == 1 && formats == 1 && capture_eps == 1 && midi_eps == 2,
          "capture/MIDI endpoint or format count incorrect");
    CHECK(channel_string == 4u, "stereo channel name base must be string4");
    CHECK(request(0x80, 6, (uint16_t)(0x300u + channel_string), 0x409, 255), "left channel string missing");
    CHECK(cf_usb_mock_control_size == 18 && cf_usb_mock_control_reply[2] == 'M' && cf_usb_mock_control_reply[16] == 'L', "left channel name is not Master L");
    CHECK(request(0x80, 6, (uint16_t)(0x301u + channel_string), 0x409, 255), "right channel string missing");
    CHECK(cf_usb_mock_control_size == 18 && cf_usb_mock_control_reply[16] == 'R', "right channel name is not Master R");
    CHECK(!request(0x80, 6, 0x306, 0x409, 255), "stereo device must not expose phantom stem strings");

    ua_off = UA_OFF_48; usb_start();
    CHECK(request(0x80, 6, 0x100, 0, 18), "single-rate descriptor rejected");
    CHECK(le16(cf_usb_mock_control_reply + 12) == 0x0401u, "single-rate stereo bcdDevice must be distinct");
    CHECK(request(0x80, 6, 0x200, 0, 255), "single-rate configuration rejected");
    CHECK(cf_usb_mock_control_size == 208, "single-rate configuration length incorrect");
    configure(); set_rate(48000);
    CHECK(!ua.r48, "single-rate mode selected forbidden 48kHz");
    CHECK(request(0xA2, 0x83, 0x100, 0x82, 3) && le24(cf_usb_mock_control_reply) == 44100,
          "single-rate GET_MAX must return44.1kHz");
}

static void controls(void)
{
    begin("control bounds and rate transitions");
    CHECK(!request(1, 11, 1, 3, 0), "alt1 accepted before configuration");
    configure();
    CHECK(request(0xA2, 0x81, 0x100, 0x82, 3) && cf_usb_mock_control_size == 3 && le24(cf_usb_mock_control_reply) == 44100, "initial GET_CUR wrong");
    CHECK(request(0xA2, 0x82, 0x100, 0x82, 3) && le24(cf_usb_mock_control_reply) == 44100, "GET_MIN wrong");
    CHECK(request(0xA2, 0x83, 0x100, 0x82, 3) && le24(cf_usb_mock_control_reply) == 48000, "GET_MAX wrong");
    CHECK(request(0xA2, 0x84, 0x100, 0x82, 3) && le24(cf_usb_mock_control_reply) == 0, "GET_RES wrong");
    open_capture();
    CHECK(request(0x81, 10, 0, 3, 1) && cf_usb_mock_control_reply[0] == 1, "GET_INTERFACE did not return alt1");
    const uint16_t bad_alt[] = {2, 256, 65535};
    for (unsigned i = 0; i < sizeof bad_alt / sizeof bad_alt[0]; i++)
        CHECK(!request(1, 11, bad_alt[i], 3, 0) && ua.cap_alt == 1, "unsupported alt accepted or changed capture");
    CHECK(!request(1, 11, 1, 4, 0), "absent interface accepted");
    CHECK(!request(1, 11, 1, 0x103, 0), "high interface byte ignored");
    CHECK(!request(1, 11, 1, 3, 1), "SET_INTERFACE data length accepted");
    CHECK(!request(0x81, 10, 0, 3, 2), "GET_INTERFACE wrong length accepted");
    CHECK(!request(0x22, 1, 0x100, 0x02, 3), "capture OUT frequency control accepted");
    CHECK(!request(0x22, 1, 0x200, 0x82, 3), "unknown selector accepted");
    CHECK(!request(0x22, 1, 0x100, 0x182, 3), "high endpoint byte ignored");
    CHECK(!request(0x22, 1, 0x100, 0x82, 2), "short frequency request accepted");
    CHECK(!request(0xA2, 0x85, 0x100, 0x82, 3), "unknown UAC control accepted");
    CHECK(!request(0, 9, 0x101, 0, 0) && usb.config == 1, "high configuration byte ignored");
    CHECK(!request(0, 9, 1, 1, 0) && usb.config == 1, "SET_CONFIGURATION bad index accepted");
    CHECK(!request(0, 9, 1, 0, 1) && usb.config == 1, "SET_CONFIGURATION bad length accepted");
    CHECK(request(0x22, 1, 0x100, 0x82, 3), "SET_CUR setup failed");
    const uint8_t short_rate[2] = {0x80, 0xbb};
    control_rx(short_rate, 2, 0);
    CHECK(cf_usb_mock_control_stalled && !ua_rate_pending && !ua.r48, "truncated rate stage accepted");
    CHECK(request(0x22, 1, 0x100, 0x82, 3), "abandoned SET_CUR setup failed");
    CHECK(request(0xA2, 0x81, 0x100, 0x82, 3) && !ua_rate_pending && le24(cf_usb_mock_control_reply) == 44100,
          "new SETUP did not cancel pending rate OUT stage");
    set_rate(48000);
    CHECK(ua.r48 && request(0xA2, 0x81, 0x100, 0x82, 3) && le24(cf_usb_mock_control_reply) == 48000, "48kHz did not become current");
    uint32_t changes = ua.rate_changes;
    set_rate(48000);
    CHECK(ua.rate_changes == changes, "same-rate request needlessly resets stream");
    set_rate(44100); CHECK(!ua.r48, "44.1kHz selection failed");
    set_rate(47000); CHECK(ua.r48, "unsupported rate did not round to nearer discrete48kHz");
    ua.rs_on = 1;
    open_capture(); CHECK(!ua.rs_on, "repeated alt1 reused resampler history from the previous open");
    CHECK(request(1, 11, 0, 3, 0) && !ua.cap_alt && !(cf_usb_mock_reg_ep[2][1] & 1u), "alt0 did not stop capture DMA");
    CHECK(request(0, 9, 0, 0, 0) && !usb.config && !ua.cap_alt && !ua.r48, "unconfigure did not reset capture");
}

static void feed_frames(uint32_t *sequence, uint32_t frames)
{
    int16_t stage[32u * UA_CAP_CHANNELS];
    while (frames) {
        uint32_t n = frames > 32u ? 32u : frames;
        for (uint32_t i = 0; i < n; i++) {
            stage[i * UA_CAP_CHANNELS] = (int16_t)(*sequence + i);
            stage[i * UA_CAP_CHANNELS + 1u] = (int16_t)(-((int32_t)*sequence + (int32_t)i));
            for (uint32_t ch = 2; ch < UA_CAP_CHANNELS; ch++) stage[i * UA_CAP_CHANNELS + ch] = 0;
        }
        ua_feed(stage, n, ua.r48); *sequence += n; frames -= n;
    }
}
static void capture_continuity(uint32_t rate)
{
    begin(rate == 44100 ? "44.1kHz capture continuity and DMA ownership" : "48kHz capture continuity and DMA ownership");
    configure(); set_rate(rate); open_capture();
    CHECK(ua.cap_hold, "opening capture must hold producer until first host take");
    uint32_t seq = 1, expected = 1, seen = 0, audio_packets = 0;
    feed_frames(&seq, 32); CHECK(ua.cw == 0, "host-not-reading capture accepted producer data"); seq = 1;
    take_capture(NULL); tick();
    CHECK(!ua.cap_hold, "first host take did not release producer");
    feed_frames(&seq, 256);
    for (unsigned packet = 0; packet < 100; packet++) {
        uint8_t p[1024]; uint32_t bytes = take_capture(p), frames = bytes / (2u * UA_CAP_CHANNELS);
        CHECK(bytes % 4u == 0 && bytes <= (rate == 44100 ? 180u : 196u), "stereo packet byte count out of bounds");
        CHECK(rate == 44100 ? frames >= 44u && frames <= 45u : frames >= 47u && frames <= 49u, "capture packet frame count out of bounds");
        int audible = le16(p) != 0;
        if (audible) {
            seen = 1; audio_packets++;
            for (uint32_t i = 0; i < frames; i++) {
                CHECK((int16_t)le16(p + i * 2u * UA_CAP_CHANNELS) == (int16_t)expected, "left frame missing, duplicated or reordered");
                CHECK((int16_t)le16(p + i * 2u * UA_CAP_CHANNELS + 2) == (int16_t)-(int32_t)expected, "right frame changed or channels crossed");
                expected++;
            }
            feed_frames(&seq, frames);
        } else CHECK(!seen, "silence returned after stream had primed");
        tick();
        uint32_t sends = cf_usb_mock_dma_sends[2];
        poll(); poll();
        CHECK(cf_usb_mock_dma_sends[2] == sends, "same pending packet armed more than once");
    }
    CHECK(audio_packets > 90 && ua.cap_underruns == 0 && ua.cap_overruns == 0, "steady capture lost audio or ring bounds");
    CHECK(ua_dbg.pk_other == 0, "normal stereo packets classified as anomalous six-channel sizes");
    uint32_t sends = cf_usb_mock_dma_sends[2];
    cf_usb_mock_stuck = 1; ua_service(); cf_usb_mock_stuck = 0;
    CHECK(cf_usb_mock_dma_sends[2] == sends, "SIE timeout was mistaken for a free DMA slot");
    assert_dma();
}

static void lifecycle(void)
{
    begin("suspend, stopped host, reset and reopen"); configure(); open_capture();
    take_capture(NULL); tick();
    uint32_t seq = 1; feed_frames(&seq, 256); tick();
    cf_usb_mock_reg_global[S_INTRUSB] = 1; poll();
    CHECK(usb.suspended && ua_paused && ua.cap_hold && !ua.cap_ready, "suspend did not pause and reset producer");
    uint32_t writes = ua.cw, sends = cf_usb_mock_dma_sends[2]; feed_frames(&seq, 32); tick();
    CHECK(ua.cw == writes && cf_usb_mock_dma_sends[2] == sends, "suspended capture accepted/armed new audio");
    cf_usb_mock_reg_global[S_INTRUSB] = 2; poll();
    CHECK(!usb.suspended && !ua_paused && ua.cap_hold, "resume must wait for a fresh host take");
    take_capture(NULL); tick(); CHECK(!ua.cap_hold, "resumed host take did not restart producer");
    feed_frames(&seq, 256);
    take_capture(NULL); tick(); take_capture(NULL); tick();
    CHECK(le16(ua_tx[ua_tx_slot]) != 0, "hold fixture did not prepare a non-silent cached packet");
    for (unsigned i = 0; i < 25; i++) tick();
    CHECK(ua.cap_hold && !ua.cap_ready && ua.cw == ua.cr, "non-reading host did not enter hold after20ms");
    take_capture(NULL); tick(); CHECK(!ua.cap_hold, "held stream did not resume when host consumed packet");
    for (uint32_t i = 0; i < cf_usb_mock_dma_size[2]; i++)
        CHECK(cf_usb_mock_dma_tx[2][i] == 0, "resuming a held stream emitted a stale prepared packet");
    set_rate(48000);
    cf_usb_mock_reg_global[S_INTRUSB] = 4; poll();
    CHECK(!usb.config && !ua.cap_alt && !ua.r48 && !ua_rate_pending && !(cf_usb_mock_reg_ep[2][1] & 1u), "bus reset retained stream state");
    configure(); open_capture();
    CHECK(ua.cap_hold && !ua.r48, "reopened stream did not return to default rate/hold");
    usb_detach();
    CHECK(!usb.up && !cf_usb_mock_hal_on && !usb.config && !ua.cap_alt && !ua_rate_pending, "detach left logical capture active");
    uint32_t retries = usb.retries; usb_retry(fm1_ms + 5000);
    CHECK(usb.retries == retries, "explicit detach unexpectedly reattached");

    begin("frozen host frame detects disconnect"); configure(); open_capture();
    take_capture(NULL); tick();
    for (unsigned i = 0; i < 240; i++) { fake_ticks += 500u; fm1_ms = fake_ticks / 1000u; poll(); }
    CHECK(usb.suspended && ua_paused && ua.cap_hold, "frozen host frame did not suspend capture");
    for (unsigned i = 0; i < 20; i++) tick();
    CHECK(!usb.suspended && !ua_paused && ua.cap_hold, "advancing host frame did not restart at producer hold");

    begin("SAFE MODE uses legacy CDC and rejects UAC"); blocked = 1; usb_start(); configure();
    CHECK(USB_CDC_ON && ua_if_cap == UA_NO_IF, "SAFE MODE still exposes capture");
    CHECK(!request(1, 11, 1, 3, 0) && !ua.cap_alt, "SAFE MODE accepted capture alternate");
    CHECK(!request(0xA2, 0x81, 0x100, 0x82, 3), "SAFE MODE accepted UAC sampling request");
    CHECK(request(0x80, 6, 0x200, 0, 255) && cf_usb_mock_control_size == 175, "SAFE MODE lost legacy CDC configuration");
    CHECK(request(0xA1, 0x21, 0, 2, 7) && cf_usb_mock_control_size == 7, "SAFE MODE lost CDC line coding");
}

static void midi_receive(const uint8_t *p, uint32_t n)
{
    memcpy(ep1rx, p, n); cf_usb_mock_reg_ep[1][6] = (uint8_t)n; cf_usb_mock_reg_ep[1][7] = 0;
    cf_usb_mock_reg_ep[1][4] = 1; cf_usb_mock_reg_global[S_INTRRX1] = 2; poll();
}
static void midi_sysex(void)
{
    begin("MIDI clock/SPP and SysEx while capture runs"); configure(); open_capture();
    const uint8_t p[] = {0x0f,0xf8,0,0, 0x03,0xf2,0x23,0x45, 0x0f,0xfa,0,0,
                         0x04,0xf0,0x7d,1, 0x0f,0xf8,0,0, 0x0f,2,0,0, 0x06,3,0xf7,0,
                         0x0f,0xfb,0,0, 0x0f,0xfc,0,0, 0x09,0x90,60,100};
    midi_receive(p, sizeof p);
    const uint32_t expected[] = {0x0000f80f,0x4523f203,0x0000fa0f,0x0000f80f,0x0000fb0f,0x0000fc0f,0x643c9009};
    CHECK(mi_w - mi_r == sizeof expected / sizeof expected[0], "MIDI events lost or SysEx leaked into MIDI ring");
    for (unsigned i = 0; i < sizeof expected / sizeof expected[0]; i++) {
        CHECK(midi_in_q[i] == expected[i] && midi_in_source[i] == 1 && midi_in_ms[i] == fm1_ms,
              "USB MIDI event order/source/timestamp changed during audio capture");
    }
    const uint8_t *sx; uint32_t n;
    CHECK(ota_frame_get(&sx, &n), "SysEx frame not preserved for command consumer");
    CHECK(n == 4 && sx[0] == 0x7d && sx[1] == 1 && sx[2] == 2 && sx[3] == 3,
          "SysEx payload changed across realtime/single-byte packets");
    ota_frame_done();
    const uint8_t reply[] = {0xf0,0x7d,0x10,0x11,0xf7};
    CHECK(ota_wire_send(reply, sizeof reply) == 0, "SysEx reply enqueue failed");
    midi_out_event(0x643c9009); poll();
    CHECK(cf_usb_mock_dma_size[1] == 12 && cf_usb_mock_dma_tx[1][0] == 4 && cf_usb_mock_dma_tx[1][4] == 6 && cf_usb_mock_dma_tx[1][8] == 9,
          "SysEx reply packets or priority over MIDI notes changed");
    CHECK(ua.cap_alt && (cf_usb_mock_reg_ep[2][1] & 1u), "MIDI/SysEx service disrupted capture");
    const uint8_t key[] = {4,0xf0,0x22,0x24, 7,0x35,0x7d,0xf7};
    midi_receive(key, sizeof key); CHECK(usb.uboot_req, "stock bootloader SysEx soft key was lost");
}

static void stereo_resampler(void)
{
    begin("stereo FIR bounds, channel isolation and reset");
    int16_t in[32u * UA_CAP_CHANNELS];
    uint32_t made = 0;
    for (unsigned block = 0; block < 147; block++) {
        for (unsigned i = 0; i < 32u * UA_CAP_CHANNELS; i++) in[i] = i % UA_CAP_CHANNELS == 0 ? 12000 : 0;
        uint32_t n = ua_resample(in, 32); made += n;
        CHECK(n >= 34 && n <= 35, "stereo FIR output count out of block bounds");
        for (unsigned i = 0; i < n; i++) {
            CHECK(ua.rs_out[i * UA_CAP_CHANNELS + 1u] == 0, "left signal leaked into right resampler channel");
            if (block) CHECK(abs(ua.rs_out[i * UA_CAP_CHANNELS] - 12000) <= 2, "FIR DC gain wrong after history prime");
        }
    }
    CHECK(made == 5120u, "147 blocks did not produce exact160/147 frame ratio");
    ua.rs_on = 0; memset(in, 0, sizeof in); uint32_t n = ua_resample(in, 32);
    for (unsigned i = 0; i < n * UA_CAP_CHANNELS; i++) CHECK(ua.rs_out[i] == 0, "reset leaked prior stream into FIR output");
}

/* Model the documented IRQ-off flash windows. The bus keeps taking packets while
 * neither producer nor service can run. This proves a limitation, not flash timing. */
static void flash_blackout(unsigned blocked_ms)
{
    begin("IRQ-off flash window limitation");configure();open_capture();
    take_capture(NULL);tick();uint32_t seq=1;feed_frames(&seq,256);
    unsigned absent=0,after=0;
    for(unsigned ms=1;ms<=160;ms++) {
        fm1_ms++;fake_ticks+=1000u;
        cf_usb_mock_reg_global[S_FRAME1]=ms&255u;
        cf_usb_mock_reg_global[S_FRAME2]=(ms>>8)&7u;
        if(cf_usb_mock_reg_ep[2][1]&1u) { take_capture(NULL);if(ms>50+blocked_ms)after++; }
        else absent++;
        if(ms>=50 && ms<50+blocked_ms)continue;
        feed_frames(&seq,44u+(ms%10u==0));poll();
    }
    CHECK(absent==blocked_ms,"one armed packet cannot cover an IRQ-off flash window");
    CHECK(ua.missed_frames>=blocked_ms,"missed bus frames must be visible in diagnostics");
    CHECK(after>40,"stream resumes after flash service returns");
    printf("FLASH LIMIT model IRQ-off=%u ms: absent packets=%u, missed frames=%u, resumed packets=%u\n",
           blocked_ms,absent,ua.missed_frames,after);
}

/* Ten minutes per case, with producer and USB clocks independent. Unlike the
 * packet-order test above, production does not depend on how much was consumed. */
static void clock_drift(unsigned rate,int ppm)
{
    begin("long independent-clock drift");ua_rate_set(rate);ua.cap_alt=1;ua.cap_hold=0;
    uint64_t phase=0,produced=0,consumed=0;uint32_t seq=1,expected=1;
    uint8_t packet[UA_PACKET];int16_t in[100];
    for(unsigned ms=0;ms<600000;ms++) {
        phase+=(uint64_t)rate*(1000000+ppm);
        unsigned n=(unsigned)(phase/1000000000u);phase%=1000000000u;
        CHECK(n<=50,"producer fixture block bound");
        for(unsigned i=0;i<n;i++,seq++) {in[2*i]=(int16_t)seq;in[2*i+1]=(int16_t)(0u-seq);}
        ua_feed(in,n,rate==48000);produced+=n;
        unsigned bytes=ua_transmit(packet),frames=bytes/4;
        CHECK(bytes<= (rate==48000?196u:180u),"drift packet stays within descriptor");
        if(ua.cap_ready) {
            CHECK((int16_t)le16(packet)==(int16_t)expected,"drift lost or repeated a frame at packet boundary");
            CHECK((int16_t)le16(packet+(frames-1)*4+2)==(int16_t)(0u-(expected+frames-1)),"drift crossed channels or skipped tail");
            expected+=frames;consumed+=frames;
        }
    }
    CHECK(!ua.cap_underruns&&!ua.cap_overruns,"long drift has no ring loss");
    CHECK(produced-consumed==ua.cw-ua.cr,"all generated frames accounted for");
    CHECK(ppm>0?ua.cap_nudges_up>0:ua.cap_nudges_down>0,"drift correction actually engaged");
    printf("DRIFT rate=%u ppm=%d seconds=600 produced=%llu consumed=%llu ring=%u underrun=%u overrun=%u\n",
        rate,ppm,(unsigned long long)produced,(unsigned long long)consumed,ua.cw-ua.cr,ua.cap_underruns,ua.cap_overruns);
}

int main(void)
{
    descriptors(); controls(); capture_continuity(44100); capture_continuity(48000);
    lifecycle(); midi_sysex(); stereo_resampler();flash_blackout(3);flash_blackout(45);
    clock_drift(44100,400);clock_drift(44100,-1000);clock_drift(44100,1000);
    clock_drift(48000,400);clock_drift(48000,-1000);clock_drift(48000,1000);
    printf("CoralFlower USB audio protocol: %u checks, %u failed\n", checks, failed);
    return failed ? 1 : 0;
}
